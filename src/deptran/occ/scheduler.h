#pragma once

#include "deptran/classic/scheduler.h"
#include <atomic>
#include <chrono>
#include <map>

namespace janus {

// Forward declaration from scheduler_enhanced.h
enum class AbortReason;

class SchedulerOcc: public SchedulerClassic {
 public:
  SchedulerOcc();
  virtual ~SchedulerOcc();

  virtual mdb::Txn *get_mdb_txn(const i64 tid);

  virtual bool HandleConflicts(Tx& dtxn,
                               innid_t inn_id,
                               vector<string>& conflicts) override {
    verify(0);
  };
  virtual bool DispatchPiece(Tx& tx,
                             TxPieceData& cmd,
                             TxnOutput& ret_output) override {
    SchedulerClassic::DispatchPiece(tx, cmd, ret_output);
    ExecutePiece(tx, cmd, ret_output);
    return true;
  }
  virtual bool Guard(Tx &tx_box, Row *row, int col_id, bool write) override {
    // TODO? read write guard?
//    Log_fatal("before access not implemented for occ");
    return false;
  };
  virtual bool DoPrepare(txnid_t tx_id) override;
  virtual void DoCommit(Tx& tx) override;

  /**
   * Get abort rate (percentage of transactions aborted)
   * @return Abort rate as a fraction (0.0 to 1.0)
   */
  double GetAbortRate() const {
    uint64_t attempted = num_transactions_attempted_.load();
    if (attempted == 0) return 0.0;
    return static_cast<double>(num_transactions_aborted_.load()) / attempted;
  }

  /**
   * Get throughput in transactions per second
   * @return Throughput (committed transactions / elapsed seconds)
   */
  double GetThroughput() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time_).count();
    if (elapsed == 0) return 0.0;
    double elapsed_seconds = elapsed / 1000000.0;
    return static_cast<double>(num_transactions_committed_.load()) / elapsed_seconds;
  }

  /**
   * Get abort count for a specific reason
   */
  uint64_t GetAbortCount(AbortReason reason) const;

  /**
   * Get total transaction counts
   */
  uint64_t GetAttemptedCount() const { return num_transactions_attempted_.load(); }
  uint64_t GetCommittedCount() const { return num_transactions_committed_.load(); }
  uint64_t GetAbortedCount() const { return num_transactions_aborted_.load(); }

  /**
   * Reset all metrics (for testing or between experiments)
   */
  void ResetMetrics() {
    num_transactions_attempted_ = 0;
    num_transactions_committed_ = 0;
    num_transactions_aborted_ = 0;
    for (auto& pair : aborts_by_reason_) {
      pair.second = 0;
    }
    start_time_ = std::chrono::steady_clock::now();
  }

 protected:
  // Metrics - Global transaction counters
  std::atomic<uint64_t> num_transactions_attempted_{0};
  std::atomic<uint64_t> num_transactions_committed_{0};
  std::atomic<uint64_t> num_transactions_aborted_{0};

  // Abort reason tracking
  mutable std::map<AbortReason, std::atomic<uint64_t>> aborts_by_reason_;

  // Timing for throughput calculation
  std::chrono::steady_clock::time_point start_time_;

  /**
   * Record an abort with categorization
   * @param reason The reason for the abort
   */
  void RecordAbort(AbortReason reason) {
    num_transactions_aborted_++;
    aborts_by_reason_[reason]++;
  }
};

} // namespace janus
