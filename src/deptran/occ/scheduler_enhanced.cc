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

  // INLINE BATCH VALIDATION (runs on reactor thread, no cross-thread issues)
  // Events are signaled on the same thread that calls Wait(), avoiding race conditions.
  // Batches are processed inline by the coroutine that triggers batch_size threshold.
  if (Config::GetConfig()->get_batch_validation_enabled()) {
    batch_size_ = Config::GetConfig()->get_batch_size();
    batch_timeout_ = std::chrono::microseconds(Config::GetConfig()->get_batch_timeout_us());
    batch_validator_ = std::make_unique<BatchValidator>(batch_size_, Config::GetConfig()->get_num_workers());
    running_ = true;
    // NOTE: No separate thread! Validation happens inline on reactor thread.
    Log_info("SchedulerOccEnhanced: Inline batch validation ENABLED (batch_size=%zu)",
             batch_size_);
  } else {
    Log_info("SchedulerOccEnhanced: Using inline validation (batch validation disabled)");
  }

  // Initialize execution threading (Phase 2)
  if (Config::GetConfig()->get_execution_threading_enabled()) {
    int num_workers = Config::GetConfig()->get_num_exec_workers();
    tx_executor_ = std::make_unique<TxExecutor>(num_workers);
    execution_threading_enabled_ = true;
    Log_info("SchedulerOccEnhanced: Execution threading ENABLED (workers=%d)", num_workers);
  } else {
    Log_info("SchedulerOccEnhanced: Execution threading DISABLED (using coroutines)");
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

    // Step 1: Signal shutdown - this prevents new enqueues
    running_ = false;

    // Step 2: STOP BATCH VALIDATOR WORKERS FIRST
    // This ensures no workers are racing with our event signaling below
    if (batch_validator_) {
      batch_validator_->SignalShutdown();
      batch_validator_.reset();  // ~BatchValidator joins worker threads
      Log_info("SchedulerOccEnhanced: batch validator destroyed");
    }

    // Step 3: Wait for validation thread to exit (if using threaded mode)
    if (validation_thread_.joinable()) {
      validation_thread_.join();
      Log_info("SchedulerOccEnhanced: validation thread stopped");
    }

    // Step 4: NOW drain pending transactions and signal their events as failed
    // Workers are stopped, so no race conditions here
    while (!validation_queue_.Empty()) {
      auto batch = validation_queue_.DequeueBatch(batch_size_, std::chrono::microseconds(0));
      for (auto* tx : batch) {
        // Skip null entries (defensive check)
        if (!tx) continue;

        // Signal via promise if set (threaded mode), else BoxEvent (coroutine mode)
        auto& metadata = tx->GetBatchMetadata();
        if (metadata.validation_promise) {
          metadata.validation_promise->set_value(false);
          metadata.validation_promise.reset();  // Prevent double-signal
          Log_debug("Shutdown: signaled failure via promise for pending tx %" PRIx64, tx->tid_);
        } else if (metadata.validation_event) {
          metadata.validation_event->Set(false);
          metadata.validation_event.reset();  // Prevent double-signal
          Log_debug("Shutdown: signaled failure via event for pending tx %" PRIx64, tx->tid_);
        }
      }
    }

    // Step 5: Clear the queue entirely
    validation_queue_.Clear();
  } else {
    // Even if not running, still clean up batch_validator
    if (batch_validator_) {
      batch_validator_.reset();
      Log_info("SchedulerOccEnhanced: batch validator destroyed");
    }
  }

  // Shutdown TxExecutor (Phase 2)
  if (tx_executor_) {
    tx_executor_->Shutdown();
    tx_executor_.reset();
    Log_info("SchedulerOccEnhanced: tx_executor destroyed");
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
    tx_box->GetBatchMetadata().enqueue_time = std::chrono::steady_clock::now();

    // Double-check running_ before enqueuing - shutdown might have started
    if (!running_.load()) {
      // Shutdown started - fall through to inline validation instead
      use_batch_validation = false;
    } else {
      bool passed = false;

      if (execution_threading_enabled_) {
        // Phase 2: Threaded mode - use promise/future for blocking wait
        // Worker threads can block on future.get() without affecting reactor
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        tx_box->GetBatchMetadata().validation_promise = promise;

        // Enqueue transaction for batch validation
        validation_queue_.Enqueue(tx_box.get());

        // Block on future - OK since we're on a worker thread
        passed = future.get();
      } else {
        // Coroutine mode with BATCH LEADER pattern
        // The first transaction to enqueue becomes the "batch leader".
        // The batch leader waits for a timeout to let other transactions accumulate,
        // then processes the entire batch. Other transactions just wait for their event.
        // This achieves true batching while staying on the reactor thread!
        auto event = rrr::Reactor::CreateSpEvent<rrr::BoxEvent<bool>>();
        tx_box->GetBatchMetadata().validation_event = event;

        // Enqueue and check if we're the batch leader (queue was empty)
        bool am_batch_leader = validation_queue_.Enqueue(tx_box.get());

        if (am_batch_leader) {
          // We're the batch leader - wait for timeout to let batch accumulate
          // TimeoutEvent::Wait() yields the coroutine, allowing other RPCs to be processed
          // and other transactions to arrive and enqueue!
          // MUST use CreateSpEvent to properly initialize __debug_creator field!
          auto timeout_event = rrr::Reactor::CreateSpEvent<rrr::TimeoutEvent>(batch_timeout_.count());
          timeout_event->Wait();  // Yields! Other transactions can arrive!

          // After timeout, drain ALL accumulated transactions (may be more than one batch)
          // This ensures no transactions are orphaned waiting for a leader
          while (ProcessValidationQueue()) {
            // Keep processing until queue is empty
          }
        }

        // Wait for our validation result
        // If we were the batch leader and processed, our event is already set
        // If we're not the leader, wait for the leader to process our batch
        event->Wait();
        passed = event->Get();
      }

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
  // Prevent double export (signal handler + destructor/ServerWorker)
  bool expected = false;
  if (!results_exported_.compare_exchange_strong(expected, true)) {
    Log_info("ExportResultsToCSV: Already exported, skipping duplicate");
    return;
  }

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

  // Determine mode based on enabled features
  const char* mode;
  bool early_abort_on = Config::GetConfig()->get_early_abort_enabled();
  bool batch_on = Config::GetConfig()->get_batch_validation_enabled();

  if (early_abort_on && batch_on) {
    mode = "occ_both";
  } else if (early_abort_on) {
    mode = "occ_early_abort";
  } else if (batch_on) {
    mode = "occ_batch";
  } else {
    mode = "occ_enhanced";
  }

  fprintf(fp, "%s,%s,%u,%lu,%lu,%lu,%.4f,%.2f,%lu,%lu,%lu\n",
          timestamp, mode, duration, attempted, committed, aborted, abort_rate, tps,
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
    // signal failure to all transactions so coroutines/workers can wake up and complete
    if (!running_) {
      Log_info("ValidationLoop: shutdown requested, signaling failure for batch of %zu transactions", batch.size());
      for (auto* tx : batch) {
        // Phase 2: Signal via promise if set (threaded mode), else BoxEvent (coroutine mode)
        if (tx->GetBatchMetadata().validation_promise) {
          tx->GetBatchMetadata().validation_promise->set_value(false);
        } else if (tx->GetBatchMetadata().validation_event) {
          tx->GetBatchMetadata().validation_event->Set(false);
        }
      }
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

bool SchedulerOccEnhanced::ProcessValidationQueue() {
  // Process validation queue on the reactor thread.
  // This is called inline after enqueueing a transaction.
  // Running on the same thread as Wait() avoids all cross-thread race conditions.

  if (!batch_validator_ || !running_.load()) {
    return false;
  }

  // Non-blocking dequeue - return immediately if batch not ready
  auto batch = validation_queue_.DequeueBatch(batch_size_, std::chrono::microseconds(0));

  if (batch.empty()) {
    return false;
  }

  Log_debug("ProcessValidationQueue: processing batch of %zu transactions", batch.size());

  // Validate the batch using BatchValidator
  // This runs on the reactor thread, so Set() calls are single-threaded
  auto result = batch_validator_->ValidateBatch(batch);

  // Results are already delivered via BoxEvent::Set() in BatchValidator
  // Since we're on the same thread as Wait(), no race conditions!
  return true;
}

} // namespace janus
