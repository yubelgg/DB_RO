#include "scheduler_enhanced.h"
#include "tx_enhanced.h"
#include "../memdb/txn_occ.h"
#include "../memdb/row.h"
#include "../config.h"
#include "../rcc_rpc.h"
#include "base/all.hpp"

namespace janus {

SchedulerOccEnhanced::SchedulerOccEnhanced()
    : SchedulerOcc(),
      batch_size_(Config::GetConfig()->get_batch_size()),
      batch_timeout_(std::chrono::microseconds(
          Config::GetConfig()->get_batch_timeout_us())),
      start_time_(std::chrono::steady_clock::now()) {
  // Initialize abort reason counters
  aborts_by_reason_[AbortReason::EARLY] = 0;
  aborts_by_reason_[AbortReason::VERSION_MISMATCH] = 0;
  aborts_by_reason_[AbortReason::LOCK_CONFLICT] = 0;
  aborts_by_reason_[AbortReason::UNKNOWN] = 0;

  // Create early abort detector
  early_abort_detector_ = std::make_unique<EarlyAbortDetector>();

  // Create batch validator
  batch_validator_ =
      std::make_unique<BatchValidator>(batch_size_,
                                       Config::GetConfig()->get_num_workers());

  // Start background validation thread
  running_ = true;
  validation_thread_ = std::thread(&SchedulerOccEnhanced::ValidationLoop, this);

  Log_info(
      "SchedulerOccEnhanced: initialized with batch_size=%zu, timeout=%ldus, num_workers=%d",
      batch_size_, batch_timeout_.count(), Config::GetConfig()->get_num_workers());
}

SchedulerOccEnhanced::~SchedulerOccEnhanced() {
  // Stop background thread
  running_ = false;

  // Wake up thread if it's waiting
  validation_queue_.Enqueue(nullptr); // Sentinel value to wake thread

  // Wait for thread to finish
  if (validation_thread_.joinable()) {
    validation_thread_.join();
  }

  // Log final statistics
  Log_info("SchedulerOccEnhanced: Transaction metrics:");
  Log_info("  Total attempted: %llu", num_transactions_attempted_.load());
  Log_info("  Total committed: %llu", num_transactions_committed_.load());
  Log_info("  Total aborted: %llu", num_transactions_aborted_.load());
  Log_info("  Abort rate: %.2f%%", GetAbortRate() * 100.0);
  Log_info("  Throughput: %.2f TPS", GetThroughput());

  Log_info("SchedulerOccEnhanced: Abort breakdown:");
  Log_info("  Early aborts: %llu", GetAbortCount(AbortReason::EARLY));
  Log_info("  Version mismatch: %llu", GetAbortCount(AbortReason::VERSION_MISMATCH));
  Log_info("  Lock conflicts: %llu", GetAbortCount(AbortReason::LOCK_CONFLICT));
  Log_info("  Unknown: %llu", GetAbortCount(AbortReason::UNKNOWN));

  if (early_abort_detector_) {
    const auto& stats = early_abort_detector_->GetStats();
    Log_info("SchedulerOccEnhanced: Early abort detector stats - "
             "reads=%llu, writes=%llu, early_aborts=%llu, version_changes=%llu",
             stats.total_reads.load(),
             stats.total_writes.load(),
             stats.early_aborts_detected.load(),
             stats.version_changes_processed.load());
  }

  Log_info("SchedulerOccEnhanced: shut down");
}

bool SchedulerOccEnhanced::DoPrepare(txnid_t tx_id) {
  // Increment attempted counter
  num_transactions_attempted_++;

  // Get enhanced transaction
  auto tx_box = std::dynamic_pointer_cast<TxOccEnhanced>(GetOrCreateTx(tx_id));
  verify(tx_box != nullptr);

  // Set execution start time for latency tracking
  tx_box->SetExecutionStartTime();

  // Check if transaction was marked for early abort
  if (tx_box->IsEarlyAborted()) {
    tx_box->SetExecutionEndTime(); // Mark end time even on early abort
    Log_debug("DoPrepare: tx %" PRIx64 " was marked for early abort", tx_id);
    RecordAbort(AbortReason::EARLY);
    return false;
  }

  // Create promise/future for waiting on validation result
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  // Store promise in batch metadata
  tx_box->GetBatchMetadata().validation_promise = promise;

  // Enqueue transaction for batch validation
  validation_queue_.Enqueue(tx_box.get());

  Log_debug("DoPrepare: enqueued tx %" PRIx64 " for batch validation", tx_id);

  // Wait for validation result from background thread
  bool validation_passed = future.get();

  if (!validation_passed) {
    tx_box->SetExecutionEndTime(); // Mark end time on validation failure
    // Categorize abort reason based on validation failure
    // TODO: Distinguish between VERSION_MISMATCH and LOCK_CONFLICT
    // For now, assume version mismatch is most common OCC abort cause
    RecordAbort(AbortReason::VERSION_MISMATCH);
  }

  Log_debug("DoPrepare: tx %" PRIx64 " validation result: %s", tx_id,
            validation_passed ? "PASSED" : "FAILED");

  return validation_passed;
}

void SchedulerOccEnhanced::DoCommit(Tx &tx) {
  // Increment committed counter
  num_transactions_committed_++;

  // Mark execution end time for latency tracking
  auto* tx_enhanced = dynamic_cast<TxOccEnhanced*>(&tx);
  if (tx_enhanced) {
    tx_enhanced->SetExecutionEndTime();
  }

  // First, perform the commit using parent implementation
  // This applies writes and increments versions
  SchedulerOcc::DoCommit(tx);

  // After commit, notify early abort detector of version changes
  if (early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    auto* tx_enhanced = dynamic_cast<TxOccEnhanced*>(&tx);
    if (tx_enhanced) {
      auto* mdb_txn = dynamic_cast<mdb::TxnOCC*>(tx_enhanced->mdb_txn());
      if (mdb_txn) {
        // Notify detector about all columns that were written
        // This triggers early abort detection for conflicting transactions
        for (auto& it : mdb_txn->ver_check_write_) {
          Row* row = it.first.row;
          mdb::colid_t col_id = it.first.col_id;
          auto* v_row = dynamic_cast<VersionedRow*>(row);

          if (v_row) {
            // Get the new version (already incremented by DoCommit)
            i64 new_version = v_row->get_column_ver(col_id);

            // Notify detector - this will mark conflicting txs for abort
            early_abort_detector_->NotifyVersionChange(row, col_id, new_version);

            Log_debug("Notified version change: row=%p col=%d new_ver=%" PRIx64,
                      row, col_id, new_version);
          }
        }
      }

      // Clean up this transaction's tracking in the detector
      early_abort_detector_->RemoveTransaction(tx_enhanced->tid_);
    }
  }
}

void SchedulerOccEnhanced::ValidationLoop() {
  Log_info("ValidationLoop: background thread started");

  while (running_) {
    // Dequeue batch with timeout
    auto batch = validation_queue_.DequeueBatch(batch_size_, batch_timeout_);

    // Check for shutdown sentinel
    if (!batch.empty() && batch[0] == nullptr) {
      Log_info("ValidationLoop: received shutdown signal");
      break;
    }

    // Skip empty batches
    if (batch.empty()) {
      continue;
    }

    Log_debug("ValidationLoop: processing batch of size %zu", batch.size());

    // Validate batch (uses parallel validation if batch is large enough)
    auto result = batch_validator_->ValidateBatch(batch);

    Log_debug("ValidationLoop: batch %zu validation complete, "
              "%zu passed, %zu failed, time=%ldus",
              result.batch_id,
              std::count(result.passed.begin(), result.passed.end(), true),
              std::count(result.passed.begin(), result.passed.end(), false),
              result.total_time.count());
  }

  Log_info("ValidationLoop: background thread exiting");
}

bool SchedulerOccEnhanced::Dispatch(cmdid_t cmd_id,
                                    shared_ptr<Marshallable> cmd,
                                    TxnOutput& ret_output) {
  // Create dummy DepId (required by SchedulerClassic::Dispatch signature)
  DepId dep_id;
  dep_id.str = "dep";
  dep_id.id = 0;

  // Call parent SchedulerClassic::Dispatch with 4 parameters
  // This will eventually call our overridden DoPrepare() for validation
  return SchedulerClassic::Dispatch(cmd_id, dep_id, cmd, ret_output);
}

} // namespace janus
