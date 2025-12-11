#pragma once

#include "batch_validator.h"
#include "early_abort_detector.h"
#include "hot_key_tracker.h"
#include "scheduler.h"
#include "validation_queue.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <thread>

namespace janus {

/**
 * Abort reason categorization for metrics tracking
 */
enum class AbortReason {
  EARLY = 0,           // Early abort detected during execution
  VERSION_MISMATCH = 1, // OCC validation failed due to version changes
  LOCK_CONFLICT = 2,    // Could not acquire locks
  UNKNOWN = 3           // Other/unspecified reasons
};

// Forward declaration for signal handler friend function
void sigterm_handler_enhanced(int);

/**
 * Enhanced OCC Scheduler with Parallel Batch Validation and Early Abort
 * Detection
 *
 * This scheduler extends the baseline OCC scheduler to support:
 * 1. Parallel Batch Validation: Collect transactions into batches and validate
 *    them concurrently to improve throughput
 * 2. Early Abort Detection: Detect conflicts during execution phase to reduce
 *    wasted work on transactions that will eventually abort
 */
class SchedulerOccEnhanced : public SchedulerOcc {
  // Friend declaration for signal handler to access protected members
  friend void sigterm_handler_enhanced(int);

public:
  SchedulerOccEnhanced();
  virtual ~SchedulerOccEnhanced();

  /**
   * Validation phase - enqueue transaction to validation queue
   *
   * Implementation:
   * - Enqueue transaction to ValidationQueue
   * - Wait for background thread to validate batch
   * - Return result from batch validation
   *
   * @param tx_id Transaction ID to validate
   * @return true if validation succeeds (locks acquired), false if aborted
   */
  virtual bool DoPrepare(txnid_t tx_id) override;

  /**
   * Commit phase - apply writes and notify early abort detector
   *
   * Implementation:
   * - Delegate to parent SchedulerOcc::DoCommit() to apply writes
   * - After committing and incrementing versions, notify EarlyAbortDetector
   * - Detector will mark conflicting active transactions for early abort
   *
   * @param tx Transaction to commit
   */
  virtual void DoCommit(Tx &tx) override;

  /**
   * RPC Dispatch method - called by RPC service layer
   *
   * Wraps base class 3-parameter signature to call parent's 4-parameter version.
   * Creates dummy DepId to match SchedulerClassic::Dispatch signature.
   *
   * @param cmd_id Transaction/command ID
   * @param cmd Command data (marshallable)
   * @param ret_output Output to populate
   * @return true if dispatch succeeds
   */
  virtual bool Dispatch(cmdid_t cmd_id,
                        shared_ptr<Marshallable> cmd,
                        TxnOutput& ret_output) override;

  /**
   * Get the early abort detector instance
   * Used by TxOccEnhanced to register reads/writes
   */
  EarlyAbortDetector* GetEarlyAbortDetector() const {
    return early_abort_detector_.get();
  }

  /**
   * Get the hot key tracker instance
   * Used to track and identify frequently accessed keys
   */
  HotKeyTracker* GetHotKeyTracker() const {
    return hot_key_tracker_.get();
  }

  /**
   * Enable/disable early abort detection
   */
  void SetEarlyAbortEnabled(bool enabled) {
    if (early_abort_detector_) {
      early_abort_detector_->SetEnabled(enabled);
    }
  }

  /**
   * Get early abort statistics
   */
  const EarlyAbortDetector::Stats& GetEarlyAbortStats() const {
    static EarlyAbortDetector::Stats empty_stats;
    return early_abort_detector_ ? early_abort_detector_->GetStats() : empty_stats;
  }

  /**
   * Get abort rate (percentage of transactions aborted)
   * @return Abort rate as a fraction (0.0 to 1.0)
   */
  double GetAbortRate() const {
    uint64_t attempted = num_transactions_attempted_.load();
    if (attempted == 0) return 0.0;
    return static_cast<double>(num_transactions_aborted_.load()) / attempted;
  }

  /**
   * Get throughput in transactions per second
   * @return Throughput (committed transactions / elapsed seconds)
   */
  double GetThroughput() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time_).count();
    if (elapsed == 0) return 0.0;
    double elapsed_seconds = elapsed / 1000000.0;
    return static_cast<double>(num_transactions_committed_.load()) / elapsed_seconds;
  }

  /**
   * Get abort count for a specific reason
   * @param reason The abort reason category
   * @return Number of aborts for that reason
   */
  uint64_t GetAbortCount(AbortReason reason) const {
    auto it = aborts_by_reason_.find(reason);
    return (it != aborts_by_reason_.end()) ? it->second.load() : 0;
  }

  /**
   * Get total transaction counts
   */
  uint64_t GetAttemptedCount() const { return num_transactions_attempted_.load(); }
  uint64_t GetCommittedCount() const { return num_transactions_committed_.load(); }
  uint64_t GetAbortedCount() const { return num_transactions_aborted_.load(); }

  /**
   * Reset all metrics (for testing or between experiments)
   */
  void ResetMetrics() {
    num_transactions_attempted_ = 0;
    num_transactions_committed_ = 0;
    num_transactions_aborted_ = 0;
    for (auto& pair : aborts_by_reason_) {
      pair.second = 0;
    }
    start_time_ = std::chrono::steady_clock::now();
  }

private:
  /**
   * Background thread that processes validation batches
   */
  void ValidationLoop();

  // Batch validation components
  ValidationQueue validation_queue_;
  std::unique_ptr<BatchValidator> batch_validator_;

  // Early abort detection
  std::unique_ptr<EarlyAbortDetector> early_abort_detector_;

  // Hot key tracking
  std::unique_ptr<HotKeyTracker> hot_key_tracker_;

  // Background thread for batch processing
  std::thread validation_thread_;
  std::atomic<bool> running_{false};

  // Configuration (initialized from Config in constructor)
  size_t batch_size_;                       // Max transactions per batch
  std::chrono::microseconds batch_timeout_; // Max wait time for batch

  // Metrics - Global transaction counters
  std::atomic<uint64_t> num_transactions_attempted_{0};
  std::atomic<uint64_t> num_transactions_committed_{0};
  std::atomic<uint64_t> num_transactions_aborted_{0};

  // Abort reason tracking
  mutable std::map<AbortReason, std::atomic<uint64_t>> aborts_by_reason_;

  // Timing for throughput calculation
  std::chrono::steady_clock::time_point start_time_;

  /**
   * Record an abort with categorization
   * @param reason The reason for the abort
   */
  void RecordAbort(AbortReason reason) {
    num_transactions_aborted_++;
    aborts_by_reason_[reason]++;
  }
};

} // namespace janus
