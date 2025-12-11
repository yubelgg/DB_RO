#include "scheduler_enhanced.h"
#include "tx_enhanced.h"
#include "../memdb/txn_occ.h"
#include "../memdb/row.h"
#include "../config.h"
#include "../rcc_rpc.h"
#include "base/all.hpp"
#include <signal.h>

namespace janus {

// Global pointer for signal handler (only one scheduler instance per process)
static SchedulerOccEnhanced* g_scheduler_enhanced = nullptr;

// Signal handler for SIGTERM/SIGINT - print metrics before exit
void sigterm_handler_enhanced(int signum) {
  if (g_scheduler_enhanced) {
    Log_info("SchedulerOccEnhanced: Caught signal %d, printing metrics:", signum);
    Log_info("  Total attempted: %llu", g_scheduler_enhanced->num_transactions_attempted_.load());
    Log_info("  Total committed: %llu", g_scheduler_enhanced->num_transactions_committed_.load());
    Log_info("  Total aborted: %llu", g_scheduler_enhanced->num_transactions_aborted_.load());
    Log_info("  Abort rate: %.2f%%", g_scheduler_enhanced->GetAbortRate() * 100.0);
    Log_info("  Throughput: %.2f TPS", g_scheduler_enhanced->GetThroughput());

    Log_info("SchedulerOccEnhanced: Abort breakdown:");
    Log_info("  Early aborts: %llu", g_scheduler_enhanced->GetAbortCount(AbortReason::EARLY));
    Log_info("  Version mismatch: %llu", g_scheduler_enhanced->GetAbortCount(AbortReason::VERSION_MISMATCH));
    Log_info("  Lock conflicts: %llu", g_scheduler_enhanced->GetAbortCount(AbortReason::LOCK_CONFLICT));
    Log_info("  Unknown: %llu", g_scheduler_enhanced->GetAbortCount(AbortReason::UNKNOWN));

    if (g_scheduler_enhanced->early_abort_detector_) {
      const auto& stats = g_scheduler_enhanced->early_abort_detector_->GetStats();
      Log_info("SchedulerOccEnhanced: Early abort detector stats - "
               "reads=%llu, writes=%llu, early_aborts=%llu, version_changes=%llu",
               stats.total_reads.load(),
               stats.total_writes.load(),
               stats.early_aborts_detected.load(),
               stats.version_changes_processed.load());
    }

    if (g_scheduler_enhanced->hot_key_tracker_) {
      const auto& stats = g_scheduler_enhanced->hot_key_tracker_->GetStats();
      Log_info("SchedulerOccEnhanced: Hot key tracker stats - "
               "total_accesses=%llu, hot_key_accesses=%llu, unique_keys=%llu",
               stats.total_accesses.load(),
               stats.hot_key_accesses.load(),
               stats.unique_keys_tracked.load());
    }
  }
  exit(0);
}

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

  // Create hot key tracker
  hot_key_tracker_ = std::make_unique<HotKeyTracker>();

  // Create batch validator
  batch_validator_ =
      std::make_unique<BatchValidator>(batch_size_,
                                       Config::GetConfig()->get_num_workers());

  // Start background validation thread
  running_ = true;
  validation_thread_ = std::thread(&SchedulerOccEnhanced::ValidationLoop, this);

  // Register signal handler to print metrics on SIGTERM/SIGINT
  g_scheduler_enhanced = this;
  signal(SIGTERM, sigterm_handler_enhanced);
  signal(SIGINT, sigterm_handler_enhanced);

  Log_info(
      "SchedulerOccEnhanced: initialized with batch_size=%zu, timeout=%ldus, num_workers=%d",
      batch_size_, batch_timeout_.count(), Config::GetConfig()->get_num_workers());
}

SchedulerOccEnhanced::~SchedulerOccEnhanced() {
  // Clear global pointer
  g_scheduler_enhanced = nullptr;

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

  // CRITICAL FIX: Collect write info BEFORE calling parent DoCommit
  // Parent's DoCommit calls RemoveMTxn() which removes the mdb transaction,
  // making it inaccessible afterwards
  std::vector<std::pair<Row*, mdb::colid_t>> written_columns;
  txnid_t tx_tid = 0;
  
  if (early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    if (tx_enhanced) {
      tx_tid = tx_enhanced->tid_;
      auto* mdb_txn = dynamic_cast<mdb::TxnOCC*>(tx_enhanced->mdb_txn());
      if (mdb_txn) {
        // Collect all written columns BEFORE parent removes them
        for (auto& it : mdb_txn->ver_check_write_) {
          written_columns.push_back({it.first.row, it.first.col_id});
        }
        // Also collect from updates_ in case ver_check_write_ is empty
        for (auto& it : mdb_txn->updates_) {
          Row* row = it.first;
          // updates_ doesn't have column info directly, iterate columns
          // For simplicity, assume all columns are written (column 0)
          // This is a conservative approach
          bool already_added = false;
          for (auto& wc : written_columns) {
            if (wc.first == row) {
              already_added = true;
              break;
            }
          }
          if (!already_added) {
            written_columns.push_back({row, 0});
          }
        }
        Log_debug("DoCommit: collected %zu written columns for tx %" PRIx64,
                  written_columns.size(), tx_tid);
      }
    }
  }

  // Now call parent's DoCommit (this applies writes, increments versions, and removes mdb_txn)
  SchedulerOcc::DoCommit(tx);

  // After commit, notify early abort detector of version changes
  // Use the pre-collected write info since mdb_txn is now removed
  if (!written_columns.empty()) {
    for (auto& wc : written_columns) {
      Row* row = wc.first;
      mdb::colid_t col_id = wc.second;
      
      // Track hot keys
      if (hot_key_tracker_) {
        hot_key_tracker_->RecordAccess(row);
      }
      
      // Notify early abort detector if enabled
      if (early_abort_detector_ && early_abort_detector_->IsEnabled()) {
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
    if (early_abort_detector_ && tx_tid != 0) {
      early_abort_detector_->RemoveTransaction(tx_tid);
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
