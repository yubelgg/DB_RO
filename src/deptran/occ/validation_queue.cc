#include "validation_queue.h"

namespace janus {

void ValidationQueue::Enqueue(TxOccEnhanced *tx) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(tx);

  // Notify one waiting thread that a transaction is available
  cv_.notify_one();
}

std::vector<TxOccEnhanced *>
ValidationQueue::DequeueBatch(size_t max_size,
                              std::chrono::microseconds timeout) {

  std::unique_lock<std::mutex> lock(mutex_);

  // Calculate absolute timeout point
  auto deadline = std::chrono::steady_clock::now() + timeout;

  // Wait until we have at least one transaction or timeout
  // This handles the case where queue might be empty
  if (queue_.empty()) {
    if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      // Timeout and still empty - return empty batch
      return std::vector<TxOccEnhanced *>();
    }
  }

  // Now queue has at least one transaction
  // Wait for more transactions until we reach max_size OR timeout
  while (queue_.size() < max_size) {
    // If we've already waited and have some transactions, return what we have
    auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break; // Timeout - return current batch
    }

    // Wait for more transactions with remaining time
    auto wait_result = cv_.wait_until(lock, deadline);
    if (wait_result == std::cv_status::timeout) {
      break; // Timeout - return current batch
    }

    // New transaction arrived, loop continues to check if we have enough
  }

  // Dequeue up to max_size transactions
  size_t batch_size = std::min(max_size, queue_.size());
  std::vector<TxOccEnhanced *> batch;
  batch.reserve(batch_size);

  for (size_t i = 0; i < batch_size; i++) {
    batch.push_back(queue_.front());
    queue_.pop_front();
  }

  return batch;
}

bool ValidationQueue::HasBatch(size_t min_size) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size() >= min_size;
}

size_t ValidationQueue::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

bool ValidationQueue::Empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

void ValidationQueue::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.clear();
  // Notify waiting threads so they can check shutdown condition
  cv_.notify_all();
}

} // namespace janus
