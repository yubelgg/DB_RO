#pragma once

#include "../tx.h"
#include "rrr/reactor/event.h"
#include <chrono>
#include <memory>
#include <vector>

namespace janus {

/**
 * Enum representing types of conflicts between transactions
 */
enum class ConflictType {
  NONE,        // No conflict
  READ_WRITE,  // tx1 reads, tx2 writes same location
  WRITE_READ,  // tx1 writes, tx2 reads same location
  WRITE_WRITE  // Both write same location
};

/**
 * Metadata for a transaction within a batch
 *
 * Tracks batch information, timestamps, and validation results
 * for a single transaction being processed in batch validation.
 */
struct BatchMetadata {
  // Batch identification
  size_t batch_id = 0;          // Unique ID for this batch
  size_t position_in_batch = 0; // Position within batch (0-indexed)

  // Timestamps
  std::chrono::steady_clock::time_point enqueue_time; // When added to queue
  std::chrono::steady_clock::time_point
      validation_start; // When validation began
  std::chrono::steady_clock::time_point
      validation_end; // When validation completed

  // Validation results
  bool validated = false; // Whether validation has completed
  bool passed = false;    // Whether validation succeeded

  // Dependency information (for Step 3: parallel validation)
  std::vector<txnid_t> depends_on; // Transactions this depends on
  std::vector<txnid_t> blocks;     // Transactions blocked by this

  // Synchronization: DoPrepare waits for validation result
  // Uses BoxEvent instead of promise/future for coroutine-friendly async
  // BoxEvent::Wait() yields the coroutine, BoxEvent::Set() wakes it up
  std::shared_ptr<rrr::BoxEvent<bool>> validation_event;

  // Reset metadata for reuse
  void Reset() {
    batch_id = 0;
    position_in_batch = 0;
    validated = false;
    passed = false;
    depends_on.clear();
    blocks.clear();
    validation_event.reset();
  }
};

/**
 * Result of batch validation
 *
 * Contains validation results for all transactions in a batch.
 */
struct BatchValidationResult {
  size_t batch_id = 0;
  size_t batch_size = 0;
  std::vector<bool> passed; // passed[i] = true if tx i validated successfully
  std::chrono::microseconds total_time{0}; // Total validation time

  BatchValidationResult() = default;

  explicit BatchValidationResult(size_t size)
      : batch_size(size), passed(size, false) {}
};

} // namespace janus
