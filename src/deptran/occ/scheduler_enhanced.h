#pragma once

#include "batch_validator.h"
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
 *
 * Current Implementation: Skeleton class that behaves identically to baseline
 * OCC Future Enhancements: Will integrate BatchValidator and EarlyAbortDetector
 */
class SchedulerOccEnhanced : public SchedulerOcc {
public:
  SchedulerOccEnhanced();
  virtual ~SchedulerOccEnhanced();

  /**
   * Validation phase - enqueue transaction to validation queue
   *
   * Step 2 Implementation:
   * - Enqueue transaction to ValidationQueue
   * - Wait for background thread to validate batch
   * - Return result from batch validation
   *
   * @param tx_id Transaction ID to validate
   * @return true if validation succeeds (locks acquired), false if aborted
   */
  virtual bool DoPrepare(txnid_t tx_id) override;

  /**
   * Commit phase - currently delegates to parent SchedulerOcc::DoCommit()
   *
   * Future Implementation:
   * - After committing and incrementing versions, notify EarlyAbortDetector
   * - Detector will mark conflicting active transactions for early abort
   *
   * @param tx Transaction to commit
   */
  virtual void DoCommit(Tx &tx) override;

private:
  /**
   * Background thread that processes validation batches
   */
  void ValidationLoop();

  // Batch validation components
  ValidationQueue validation_queue_;
  std::unique_ptr<BatchValidator> batch_validator_;

  // Background thread for batch processing
  std::thread validation_thread_;
  std::atomic<bool> running_{false};

  // Configuration
  size_t batch_size_ = 32;                       // Max transactions per batch
  std::chrono::microseconds batch_timeout_{100}; // Max wait time for batch

  // TODO (Step 4): Add EarlyAbortDetector
  // std::unique_ptr<EarlyAbortDetector> early_abort_detector_;
};

} // namespace janus
