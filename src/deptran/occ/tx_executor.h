#pragma once

/**
 * Transaction Executor Thread Pool for Enhanced OCC
 *
 * Executes transactions on worker threads instead of coroutines,
 * enabling true parallel transaction execution for batch validation.
 *
 * Phase 2 of Enhanced OCC implementation.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "../constants.h"

namespace janus {

/**
 * Work item submitted to executor
 */
struct TxWorkItem {
  // Transaction execution function (captures RPC params)
  std::function<void()> execute_fn;

  // Promise to signal completion to caller
  std::promise<bool> completion_promise;

  // Transaction ID for logging
  txnid_t tx_id{0};

  // Timestamp for metrics
  std::chrono::steady_clock::time_point submit_time;

  TxWorkItem() = default;
  TxWorkItem(TxWorkItem&&) = default;
  TxWorkItem& operator=(TxWorkItem&&) = default;

  // Delete copy (promise is move-only)
  TxWorkItem(const TxWorkItem&) = delete;
  TxWorkItem& operator=(const TxWorkItem&) = delete;
};

/**
 * Thread pool for transaction execution
 *
 * Reuses the pattern from BatchValidator:
 * - std::vector<std::thread> for worker pool
 * - std::queue + mutex + condition_variable for work queue
 * - std::promise/future for completion signaling
 * - Proper shutdown with flag check and join()
 */
class TxExecutor {
public:
  /**
   * Constructor
   *
   * @param num_workers Number of worker threads to create
   */
  explicit TxExecutor(size_t num_workers);

  /**
   * Destructor - calls Shutdown() if not already done
   */
  ~TxExecutor();

  // Non-copyable, non-movable
  TxExecutor(const TxExecutor&) = delete;
  TxExecutor& operator=(const TxExecutor&) = delete;
  TxExecutor(TxExecutor&&) = delete;
  TxExecutor& operator=(TxExecutor&&) = delete;

  /**
   * Submit transaction for execution on worker thread
   *
   * @param execute_fn Function to execute (should contain transaction logic)
   * @param tx_id Transaction ID for logging
   * @return Future that completes when execution is done
   */
  std::future<bool> Submit(std::function<void()> execute_fn, txnid_t tx_id);

  /**
   * Shutdown executor and wait for workers to finish
   *
   * Safe to call multiple times.
   * After shutdown, Submit() returns immediately with false future.
   */
  void Shutdown();

  /**
   * Check if shutdown was initiated
   */
  bool IsShuttingDown() const { return shutdown_.load(); }

  /**
   * Get number of pending transactions in queue
   */
  size_t QueueSize() const;

  /**
   * Get number of worker threads
   */
  size_t NumWorkers() const { return num_workers_; }

  // Statistics
  uint64_t GetTotalSubmitted() const { return total_submitted_.load(); }
  uint64_t GetTotalCompleted() const { return total_completed_.load(); }
  uint64_t GetTotalFailed() const { return total_failed_.load(); }

private:
  /**
   * Worker thread loop
   *
   * Waits for work items, executes them, signals completion.
   * Exits when shutdown_ is true and queue is empty.
   *
   * @param worker_id Worker ID for logging
   */
  void WorkerLoop(int worker_id);

  // Configuration
  size_t num_workers_;

  // Worker threads
  std::vector<std::thread> workers_;

  // Work queue
  std::queue<TxWorkItem> work_queue_;
  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Shutdown flag
  std::atomic<bool> shutdown_{false};

  // Statistics
  std::atomic<uint64_t> total_submitted_{0};
  std::atomic<uint64_t> total_completed_{0};
  std::atomic<uint64_t> total_failed_{0};
};

} // namespace janus
