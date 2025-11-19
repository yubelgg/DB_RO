#pragma once

#include "tx_enhanced.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

namespace janus {

/**
 * Thread-safe queue for transactions awaiting batch validation
 *
 * Supports both size-based and timeout-based batch triggering:
 * - Size-based: Dequeue when queue has enough transactions
 * - Timeout-based: Dequeue after timeout even if batch incomplete
 *
 * Thread-safe for concurrent enqueue/dequeue operations.
 */
class ValidationQueue {
public:
  ValidationQueue() = default;
  ~ValidationQueue() = default;

  /**
   * Add transaction to queue (thread-safe)
   *
   * @param tx Transaction to enqueue
   */
  void Enqueue(TxOccEnhanced *tx);

  /**
   * Dequeue batch of transactions (blocking with timeout)
   *
   * Blocks until one of the following:
   * - Queue has at least max_size transactions
   * - Timeout expires
   * - Queue is not empty and we've waited some time
   *
   * @param max_size Maximum batch size (returns early if reached)
   * @param timeout Maximum time to wait for batch
   * @return Vector of transactions (may be less than max_size)
   */
  std::vector<TxOccEnhanced *> DequeueBatch(size_t max_size,
                                            std::chrono::microseconds timeout);

  /**
   * Check if queue has enough transactions for a batch (non-blocking)
   *
   * @param min_size Minimum batch size
   * @return true if queue size >= min_size
   */
  bool HasBatch(size_t min_size) const;

  /**
   * Get current queue size (thread-safe)
   *
   * @return Number of transactions in queue
   */
  size_t Size() const;

  /**
   * Check if queue is empty (thread-safe)
   *
   * @return true if queue is empty
   */
  bool Empty() const;

  /**
   * Clear all transactions from queue (thread-safe)
   * Used for cleanup/shutdown
   */
  void Clear();

private:
  mutable std::mutex mutex_;          // Protects queue access
  std::condition_variable cv_;        // For blocking/waking threads
  std::deque<TxOccEnhanced *> queue_; // Internal transaction queue
};

} // namespace janus
