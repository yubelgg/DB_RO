#pragma once

#include "batch_validator.h"
#include "early_abort_detector.h"
#include "scheduler.h"
#include "validation_queue.h"
#include <atomic>
#include <condition_variable>
#include <thread>

namespace janus {

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
   * Get the early abort detector instance
   * Used by TxOccEnhanced to register reads/writes
   */
  EarlyAbortDetector* GetEarlyAbortDetector() const {
    return early_abort_detector_.get();
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

  // Background thread for batch processing
  std::thread validation_thread_;
  std::atomic<bool> running_{false};

  // Configuration
  size_t batch_size_ = 32;                       // Max transactions per batch
  std::chrono::microseconds batch_timeout_{100}; // Max wait time for batch
};

} // namespace janus
