#include "tx_executor.h"
#include "base/all.hpp"

namespace janus {

TxExecutor::TxExecutor(size_t num_workers) : num_workers_(num_workers) {
  // Start worker threads
  for (size_t i = 0; i < num_workers_; i++) {
    workers_.emplace_back(&TxExecutor::WorkerLoop, this, static_cast<int>(i));
  }

  Log_info("TxExecutor initialized with %zu workers", num_workers_);
}

TxExecutor::~TxExecutor() {
  Shutdown();
}

std::future<bool> TxExecutor::Submit(std::function<void()> execute_fn,
                                     txnid_t tx_id) {
  TxWorkItem item;
  item.execute_fn = std::move(execute_fn);
  item.tx_id = tx_id;
  item.submit_time = std::chrono::steady_clock::now();

  auto future = item.completion_promise.get_future();

  {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    // If shutdown, immediately return false
    if (shutdown_.load()) {
      item.completion_promise.set_value(false);
      return future;
    }

    work_queue_.push(std::move(item));
    total_submitted_++;
  }

  queue_cv_.notify_one();
  return future;
}

void TxExecutor::Shutdown() {
  // Use exchange to ensure we only shutdown once
  if (shutdown_.exchange(true)) {
    return; // Already shutdown
  }

  Log_info("TxExecutor shutdown initiated (submitted=%llu, completed=%llu, "
           "failed=%llu)",
           total_submitted_.load(), total_completed_.load(),
           total_failed_.load());

  // Wake up all workers
  queue_cv_.notify_all();

  // Wait for all workers to finish
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }

  Log_info("TxExecutor shutdown complete");
}

size_t TxExecutor::QueueSize() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return work_queue_.size();
}

void TxExecutor::WorkerLoop(int worker_id) {
  Log_debug("TxExecutor worker %d started", worker_id);

  while (true) {
    TxWorkItem item;

    // Wait for work or shutdown
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock,
                     [this] { return shutdown_.load() || !work_queue_.empty(); });

      // Exit if shutdown and queue is empty
      if (shutdown_.load() && work_queue_.empty()) {
        break;
      }

      // Get work item if available
      if (!work_queue_.empty()) {
        item = std::move(work_queue_.front());
        work_queue_.pop();
      } else {
        continue;
      }
    }

    // Execute transaction outside the lock
    Log_debug("TxExecutor worker %d: executing tx %" PRIx64, worker_id,
              item.tx_id);

    auto start_time = std::chrono::steady_clock::now();
    bool success = false;

    try {
      // Execute the transaction function
      item.execute_fn();
      success = true;
      total_completed_++;
    } catch (const std::exception& e) {
      Log_error("TxExecutor worker %d: tx %" PRIx64 " threw exception: %s",
                worker_id, item.tx_id, e.what());
      success = false;
      total_failed_++;
    } catch (...) {
      Log_error("TxExecutor worker %d: tx %" PRIx64 " threw unknown exception",
                worker_id, item.tx_id);
      success = false;
      total_failed_++;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto exec_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            end_time - start_time)
                            .count();

    Log_debug("TxExecutor worker %d: tx %" PRIx64 " %s (exec_time=%lld us)",
              worker_id, item.tx_id, success ? "SUCCESS" : "FAILED",
              exec_time_us);

    // Signal completion (always, even on failure)
    item.completion_promise.set_value(success);
  }

  Log_debug("TxExecutor worker %d stopped", worker_id);
}

} // namespace janus
