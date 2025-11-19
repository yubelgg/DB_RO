#pragma once

#include "batch_metadata.h"
#include "tx_enhanced.h"
#include <chrono>
#include <vector>

namespace janus {

/**
 * Batch Validator for Enhanced OCC
 *
 * Step 2 Implementation: Serial validation of batches
 * - Collects transactions into batches
 * - Validates them serially (one by one)
 * - Returns validation results
 *
 * Step 3 Enhancement (TODO): Parallel validation
 * - Build conflict graph
 * - Partition independent transactions
 * - Validate in parallel using worker threads
 */
class BatchValidator {
public:
  /**
   * Constructor
   *
   * @param batch_size Maximum transactions per batch
   * @param num_workers Number of validation worker threads (unused in Step 2)
   */
  BatchValidator(size_t batch_size, int num_workers = 1);

  ~BatchValidator();

  /**
   * Validate a batch of transactions (Step 2: Serial validation)
   *
   * Current implementation:
   * - Validates each transaction sequentially
   * - Uses existing version_check() from baseline OCC
   * - Acquires locks for transactions that pass validation
   *
   * Future (Step 3):
   * - Build conflict graph
   * - Partition into independent sets
   * - Validate partitions in parallel
   *
   * @param batch Vector of transactions to validate
   * @return BatchValidationResult with per-transaction results
   */
  BatchValidationResult
  ValidateBatch(const std::vector<TxOccEnhanced *> &batch);

  /**
   * Get batch size configuration
   */
  size_t GetBatchSize() const { return batch_size_; }

  /**
   * Get number of worker threads
   */
  int GetNumWorkers() const { return num_workers_; }

private:
  /**
   * Validate single transaction (helper method)
   *
   * Performs version check and acquires locks if successful.
   * Reuses logic from SchedulerOcc::DoPrepare().
   *
   * @param tx Transaction to validate
   * @return true if validation passed and locks acquired
   */
  bool ValidateSingle(TxOccEnhanced *tx);

  size_t batch_size_;    // Maximum batch size
  int num_workers_;      // Number of validation threads (for Step 3)
  size_t batch_counter_; // Batch ID counter

  // TODO (Step 3): Add worker thread pool
  // TODO (Step 3): Add conflict graph builder
};

} // namespace janus
