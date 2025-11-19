#pragma once

#include "scheduler.h"

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
   * Validation phase - currently delegates to parent SchedulerOcc::DoPrepare()
   *
   * Future Implementation:
   * - Enqueue transaction to ValidationQueue instead of immediate validation
   * - Background thread will process batches
   * - Use BatchValidator for parallel validation
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
  // TODO: Add BatchValidator* batch_validator_ field
  // TODO: Add EarlyAbortDetector* early_abort_detector_ field
  // TODO: Add ValidationQueue validation_queue_ field
  // TODO: Add std::thread validation_thread_ for batch processing
};

} // namespace janus
