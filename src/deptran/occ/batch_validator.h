#pragma once

#include "batch_metadata.h"
#include "tx_enhanced.h"
#include <chrono>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <unordered_set>

namespace janus {

// Forward declaration
class ConflictGraph;

/**
 * Batch Validator for Enhanced OCC
 *
 * Step 3 Implementation: Parallel validation with conflict graph
 * - Collects transactions into batches
 * - Builds conflict graph to identify independent transactions
 * - Validates independent sets in parallel using worker threads
 * - Falls back to serial validation for small batches
 */
class BatchValidator {
public:
  /**
   * Constructor
   *
   * @param batch_size Maximum transactions per batch
   * @param num_workers Number of validation worker threads
   */
  BatchValidator(size_t batch_size, int num_workers = 1);

  ~BatchValidator();

  /**
   * Validate a batch of transactions
   *
   * Implementation:
   * - For large batches: builds conflict graph and validates in parallel
   * - For small batches: validates serially for efficiency
   * - Uses existing version_check() from baseline OCC
   * - Acquires locks for transactions that pass validation
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

  /**
   * Validate batch serially (for small batches)
   */
  void ValidateBatchSerial(const std::vector<TxOccEnhanced*>& batch,
                           BatchValidationResult& result);

  /**
   * Validate batch in parallel using conflict graph
   */
  void ValidateBatchParallel(const std::vector<TxOccEnhanced*>& batch,
                             BatchValidationResult& result);

  /**
   * Validate an independent set of transactions in parallel
   */
  void ValidateIndependentSet(const std::vector<TxOccEnhanced*>& batch,
                              const std::vector<size_t>& indices,
                              BatchValidationResult& result);

  /**
   * Worker thread function
   */
  void WorkerThread(int worker_id);

  /**
   * Helper: Extract read/write sets from transaction
   */
  void GetTransactionAccessSets(TxOccEnhanced* tx,
                                std::unordered_set<Row*>& read_set,
                                std::unordered_set<Row*>& write_set);

  // Configuration
  size_t batch_size_;    // Maximum batch size
  int num_workers_;      // Number of validation threads
  size_t batch_counter_; // Batch ID counter

  // Worker thread pool
  std::vector<std::thread> workers_;

  // Work queue for parallel validation
  struct WorkItem {
    TxOccEnhanced* tx;
    size_t tx_index;
    std::promise<bool> result;
  };

  std::queue<WorkItem> work_queue_;
  std::mutex work_queue_mutex_;
  std::condition_variable work_queue_cv_;
  std::atomic<bool> shutdown_;
};

} // namespace janus
