#include "scheduler_enhanced.h"
#include "tx_enhanced.h"
#include "../memdb/txn_occ.h"
#include "../memdb/row.h"
#include "../config.h"
#include "../rcc_rpc.h"
#include "rrr/reactor/event.h"
#include "rrr/reactor/reactor.h"
#include "base/all.hpp"
#include <signal.h>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace janus {

// Global pointer to current scheduler instance (for early abort detector access)
// Defined here (not static), declared as extern in scheduler_enhanced.h
SchedulerOccEnhanced* g_scheduler_enhanced = nullptr;

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

    // Export results to CSV (signal handler context - limited but should work for simple file I/O)
    g_scheduler_enhanced->ExportResultsToCSV();
  }
  exit(0);
}

SchedulerOccEnhanced::SchedulerOccEnhanced()
    : SchedulerOcc(),
      start_time_(std::chrono::steady_clock::now()) {
  // Set global pointer for early abort detector access from transactions
  g_scheduler_enhanced = this;

  // NOTE: Abort reason counters are initialized by parent SchedulerOcc constructor

  // Create early abort detector (if enabled in config)
  if (Config::GetConfig()->get_early_abort_enabled()) {
    early_abort_detector_ = std::make_unique<EarlyAbortDetector>();
    Log_info("SchedulerOccEnhanced: Early abort detection ENABLED (check_interval=%d)",
             Config::GetConfig()->get_check_interval());
  } else {
    Log_info("SchedulerOccEnhanced: Early abort detection DISABLED");
  }

  // DISABLE HotKeyTracker - global mutex bottleneck causes 4-24× slowdown
  // hot_key_tracker_ = std::make_unique<HotKeyTracker>();

  // ASYNC BATCH VALIDATION (Option A: Coroutine-friendly)
  // Uses BoxEvent instead of promise/future for non-blocking validation
  // BoxEvent::Wait() yields the coroutine, allowing other transactions to proceed
  // This enables true batching even with coroutine-based execution
  if (Config::GetConfig()->get_batch_validation_enabled()) {
    batch_size_ = Config::GetConfig()->get_batch_size();
    batch_timeout_ = std::chrono::microseconds(Config::GetConfig()->get_batch_timeout_us());
    batch_validator_ = std::make_unique<BatchValidator>(batch_size_, Config::GetConfig()->get_num_workers());
    running_ = true;
    validation_thread_ = std::thread(&SchedulerOccEnhanced::ValidationLoop, this);
    Log_info("SchedulerOccEnhanced: Async batch validation ENABLED (batch_size=%zu, workers=%d)",
             batch_size_, Config::GetConfig()->get_num_workers());
  } else {
    Log_info("SchedulerOccEnhanced: Using inline validation (batch validation disabled)");
  }

  // Register signal handler to print metrics on SIGTERM/SIGINT
  signal(SIGTERM, sigterm_handler_enhanced);
  signal(SIGINT, sigterm_handler_enhanced);

  Log_info("SchedulerOccEnhanced: initialized");
}

SchedulerOccEnhanced::~SchedulerOccEnhanced() {
  // CRITICAL: Reset signal handlers FIRST to prevent race conditions
  // If a signal arrives during destruction, accessing partially destroyed
  // objects would cause a crash
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);

  // Clear global pointer to prevent new transactions from registering
  // and to prevent signal handler from accessing us during destruction
  g_scheduler_enhanced = nullptr;

  // Stop validation thread if running
  if (running_.load()) {
    Log_info("SchedulerOccEnhanced: initiating shutdown...");

    // Step 1: Signal shutdown to batch validator FIRST
    // This prevents Set() calls on events which could crash if coroutines are gone
    if (batch_validator_) {
      batch_validator_->SignalShutdown();
    }

    // Step 2: Signal shutdown - this prevents new enqueues
    running_ = false;

    // Step 3: Wake up the validation thread if it's waiting
    // Clear() empties the queue and notifies waiting threads
    validation_queue_.Clear();

    // Step 4: Wait for validation thread to exit
    // The thread will check running_ and exit its loop
    if (validation_thread_.joinable()) {
      validation_thread_.join();
      Log_info("SchedulerOccEnhanced: validation thread stopped");
    }
  }

  // Destroy batch_validator explicitly before any other cleanup
  // to ensure worker threads are stopped before we continue
  if (batch_validator_) {
    batch_validator_.reset();
    Log_info("SchedulerOccEnhanced: batch validator destroyed");
  }

  // Export results to CSV before logging
  ExportResultsToCSV();

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
    // Explicitly destroy to ensure GC thread is stopped
    early_abort_detector_.reset();
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

  // Check if transaction was marked for early abort during execution
  if (early_abort_detector_ && early_abort_detector_->IsEnabled() &&
      tx_box->IsEarlyAborted()) {
    tx_box->SetExecutionEndTime(); // Mark end time even on early abort
    Log_debug("DoPrepare: tx %" PRIx64 " was marked for early abort", tx_id);
    RecordAbort(AbortReason::EARLY);
    return false;
  }

  // AGGRESSIVE EARLY ABORT CHECK #2: Final check before validation
  // This catches conflicts that happened while transaction was executing
  // but before it reached validation (since transactions are very fast - 0.2ms)
  if (early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    if (early_abort_detector_->ShouldAbort(tx_id)) {
      tx_box->SetExecutionEndTime();
      tx_box->MarkEarlyAborted();
      RecordAbort(AbortReason::EARLY);
      Log_info("Early abort at validation: tx %" PRIx64 " (caught by final check)", tx_id);
      return false;
    }
  }

  // Decide whether to use batch validation or inline validation
  // Check running_ to avoid enqueuing during shutdown
  bool use_batch_validation = batch_validator_ && running_.load();

  if (use_batch_validation) {
    // Create BoxEvent for async validation result
    // BoxEvent::Wait() yields the coroutine (doesn't block thread!)
    // This allows other transactions to enqueue while we wait
    // MUST use CreateSpEvent to set __debug_creator (required by Event::Test)
    auto event = rrr::Reactor::CreateSpEvent<rrr::BoxEvent<bool>>();

    // Set up batch metadata with event
    tx_box->GetBatchMetadata().validation_event = event;
    tx_box->GetBatchMetadata().enqueue_time = std::chrono::steady_clock::now();

    // Double-check running_ before enqueuing - shutdown might have started
    if (!running_.load()) {
      // Shutdown started - fall through to inline validation instead
      use_batch_validation = false;
    } else {
      // Enqueue transaction for batch validation
      validation_queue_.Enqueue(tx_box.get());

      // Yield coroutine until validation completes
      // This is the key difference from future.get() - it doesn't block the thread!
      event->Wait();

      // Get validation result
      bool passed = event->Get();

      if (!passed) {
        tx_box->SetExecutionEndTime();
        RecordAbort(AbortReason::VERSION_MISMATCH);  // Could be version or lock conflict
        return false;
      }
    }
  }

  // Inline validation - used when batch validation is disabled or during shutdown
  if (!use_batch_validation) {
    // Direct inline validation - no queue, no promises, no background thread
    // This is simpler and faster than batch validation for short transactions
    auto txn = (mdb::TxnOCC*) get_mdb_txn(tx_id);
    verify(txn != nullptr);
    verify(txn->outcome_ == symbol_t::NONE);
    verify(!txn->verified_);

    // Version check (only on leader)
    if (tx_box->is_leader_hint_ && !txn->version_check()) {
      tx_box->SetExecutionEndTime();
      Log_debug("DoPrepare: tx %" PRIx64 " version check failed", tx_id);
      txn->__debug_abort_ = 1;
      RecordAbort(AbortReason::VERSION_MISMATCH);
      return false;
    }

    // Acquire read locks
    for (auto &it : txn->ver_check_read_) {
      Row *row = it.first.row;
      auto *v_row = (VersionedRow *) row;
      if (!v_row->rlock_row_by(txn->id())) {
        // Lock acquisition failed - unlock everything and abort
        for (auto &lit : txn->locks_) {
          Row* r = lit.first;
          auto vr = (VersionedRow *) r;
          vr->unlock_row_by(txn->id());
        }
        txn->locks_.clear();
        tx_box->SetExecutionEndTime();
        Log_debug("DoPrepare: tx %" PRIx64 " read lock failed", tx_id);
        txn->__debug_abort_ = 1;
        RecordAbort(AbortReason::LOCK_CONFLICT);
        return false;
      }
      insert_into_map(txn->locks_, row, -1);
    }

    // Acquire write locks
    for (auto &it : txn->updates_) {
      Row *row = it.first;
      auto *v_row = (VersionedRow *) row;
      if (!v_row->wlock_row_by(txn->id())) {
        // Lock acquisition failed - unlock everything and abort
        for (auto &lit : txn->locks_) {
          Row* r = lit.first;
          auto vr = (VersionedRow *) r;
          vr->unlock_row_by(txn->id());
        }
        txn->locks_.clear();
        tx_box->SetExecutionEndTime();
        Log_debug("DoPrepare: tx %" PRIx64 " write lock failed", tx_id);
        txn->__debug_abort_ = 1;
        RecordAbort(AbortReason::LOCK_CONFLICT);
        return false;
      }
      insert_into_map(txn->locks_, row, -1);
    }

    // Validation passed!
    txn->__debug_abort_ = 0;
    txn->verified_ = true;
    Log_debug("DoPrepare: tx %" PRIx64 " validation PASSED (inline)", tx_id);
  }

  // Don't set end time yet - will be set in DoCommit
  return true;
}

// NOTE: Batch validation logic removed in favor of inline validation
// Previous approach added too much overhead (promises/futures/queuing) for
// short-lived transactions (0.2ms execution time). Inline validation is simpler
// and faster for our workload.

void SchedulerOccEnhanced::DoCommit(Tx &tx) {
  // NOTE: Commit counter moved to after parent DoCommit (Fix 7)

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
        // NOTE: Only use updates_ (not ver_check_write_) to avoid duplicate notifications
        // ver_check_write_ and updates_ can contain the same (row, col) pairs
        // updates_ is std::multimap<Row*, std::pair<colid_t, Value>>
        for (auto& it : mdb_txn->updates_) {
          Row* row = it.first;
          mdb::colid_t col_id = it.second.first;  // Extract actual column ID!

          // Add each (row, column) pair that was written
          written_columns.push_back({row, col_id});

          Log_debug("DoCommit: collected write row=%p col=%d for tx %" PRIx64,
                    row, col_id, tx_tid);
        }
        Log_debug("DoCommit: collected %zu written columns for tx %" PRIx64,
                  written_columns.size(), tx_tid);
      }
    }
  }

  // Now call parent's DoCommit (this applies writes, increments versions, and removes mdb_txn)
  // NOTE: Parent DoCommit already increments num_transactions_committed_, don't double-count!
  SchedulerOcc::DoCommit(tx);

  // After commit, notify early abort detector of version changes
  // Use the pre-collected write info since mdb_txn is now removed
  if (!written_columns.empty()) {
    for (auto& wc : written_columns) {
      Row* row = wc.first;
      mdb::colid_t col_id = wc.second;

      // Track hot keys
      // DISABLED: HotKeyTracker has global mutex bottleneck causing 4-24× slowdown
      // if (hot_key_tracker_) {
      //   hot_key_tracker_->RecordAccess(row);
      // }

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

    // TIMING FIX: Don't remove transaction from tracking here
    // Keep it in active_reads_ longer so other committing transactions can detect conflicts
    // The transaction will be cleaned up by its destructor instead
    //
    // Previous code (removed for better conflict detection):
    // if (early_abort_detector_ && tx_tid != 0) {
    //   early_abort_detector_->RemoveTransaction(tx_tid);
    // }
  }
}

// ValidationLoop() method removed - batch validation no longer used

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

void SchedulerOccEnhanced::ExportResultsToCSV() {
  // Generate timestamped filename
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm tm_now;
  localtime_r(&time_t_now, &tm_now);

  char filename[256];
  snprintf(filename, sizeof(filename), "results_%04d%02d%02d_%02d%02d%02d.csv",
           tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

  // Check if file exists (append) or new (write header)
  bool write_header = (access(filename, F_OK) == -1);

  FILE* fp = fopen(filename, "a");
  if (!fp) {
    Log_warn("Failed to open CSV file: %s", filename);
    return;
  }

  // Enhanced header with early abort stats
  if (write_header) {
    fprintf(fp, "timestamp,mode,duration,attempted,committed,aborted,abort_rate,tps,"
                "early_aborts,version_changes,reads_tracked\n");
  }

  // Get metrics
  uint64_t attempted = num_transactions_attempted_.load();
  uint64_t committed = num_transactions_committed_.load();
  uint64_t aborted = num_transactions_aborted_.load();
  double abort_rate = attempted > 0 ? (double)aborted / attempted : 0.0;
  uint32_t duration = Config::GetConfig()->duration_;
  double tps = duration > 0 ? (double)committed / duration : 0.0;

  // Get early abort detector stats
  uint64_t early_aborts = 0;
  uint64_t version_changes = 0;
  uint64_t reads_tracked = 0;
  if (early_abort_detector_) {
    const auto& stats = early_abort_detector_->GetStats();
    early_aborts = stats.early_aborts_detected.load();
    version_changes = stats.version_changes_processed.load();
    reads_tracked = stats.total_reads.load();
  }

  // Write ISO timestamp
  char timestamp[64];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);

  fprintf(fp, "%s,occ_enhanced,%u,%lu,%lu,%lu,%.4f,%.2f,%lu,%lu,%lu\n",
          timestamp, duration, attempted, committed, aborted, abort_rate, tps,
          early_aborts, version_changes, reads_tracked);

  fclose(fp);
  Log_info("Results exported to %s", filename);
}

void SchedulerOccEnhanced::ValidationLoop() {
  Log_info("ValidationLoop: started (batch_size=%zu, timeout=%lld us)",
           batch_size_, batch_timeout_.count());

  while (running_) {
    // Dequeue a batch of transactions (blocks until batch ready or timeout)
    auto batch = validation_queue_.DequeueBatch(batch_size_, batch_timeout_);

    if (batch.empty()) {
      // Timeout with no transactions - check if we should stop
      continue;
    }

    // SHUTDOWN CHECK: If running_ was set to false while we were waiting,
    // don't process the batch - the coroutines might be getting destroyed
    if (!running_) {
      Log_info("ValidationLoop: shutdown requested, aborting batch of %zu transactions", batch.size());
      // Mark all transactions as failed without calling Set()
      // The coroutines will be cleaned up during shutdown
      break;
    }

    Log_info("ValidationLoop: processing batch of %zu transactions", batch.size());

    // Validate the batch using BatchValidator
    auto result = batch_validator_->ValidateBatch(batch);

    // Results are already delivered via BoxEvent::Set() in BatchValidator
    // which wakes up the yielded coroutines
  }

  Log_info("ValidationLoop: stopped");
}

} // namespace janus
