#pragma once

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include "../memdb/row.h"
#include "base/all.hpp"

using rrr::i64;
using mdb::Row;

namespace janus {

/**
 * RowColumnKey - Identifies a specific column in a specific row
 * Used as key for tracking reads/writes
 */
struct RowColumnKey {
  Row* row;
  mdb::colid_t column_id;
  
  RowColumnKey(Row* r, mdb::colid_t col) : row(r), column_id(col) {}
  
  bool operator==(const RowColumnKey& other) const {
    return row == other.row && column_id == other.column_id;
  }
};

// Hash function for RowColumnKey
struct RowColumnKeyHash {
  size_t operator()(const RowColumnKey& key) const {
    return std::hash<void*>()(key.row) ^ std::hash<int>()(key.column_id);
  }
};

/**
 * ReadRecord - Tracks a specific read operation
 */
struct ReadRecord {
  i64 tx_id;
  i64 version;  // Version number that was read
  
  ReadRecord(i64 tid, i64 ver) : tx_id(tid), version(ver) {}
  
  bool operator==(const ReadRecord& other) const {
    return tx_id == other.tx_id && version == other.version;
  }
};

struct ReadRecordHash {
  size_t operator()(const ReadRecord& record) const {
    return std::hash<i64>()(record.tx_id) ^ std::hash<i64>()(record.version);
  }
};

/**
 * EarlyAbortDetector - Detects conflicts during transaction execution
 * 
 * Problem: In baseline OCC, transactions execute fully even if they'll 
 * fail validation later, wasting CPU cycles.
 * 
 * Solution: Track all active reads/writes. When a transaction commits and
 * increments versions, immediately detect transactions reading old versions
 * and mark them for abort.
 * 
 * Thread-safety: All methods are thread-safe using fine-grained locking
 */
class EarlyAbortDetector {
public:
  EarlyAbortDetector();
  ~EarlyAbortDetector();
  
  /**
   * Register a read operation
   * Called during transaction execution when reading a column
   */
  void RegisterRead(i64 tx_id, Row* row, mdb::colid_t column_id, i64 version);
  
  /**
   * Register a write operation
   * Called during transaction execution when writing a column
   */
  void RegisterWrite(i64 tx_id, Row* row, mdb::colid_t column_id);
  
  /**
   * Notify detector that a column's version has changed
   * Called when a transaction commits and increments version
   * Triggers early abort for transactions reading old versions
   */
  void NotifyVersionChange(Row* row, mdb::colid_t column_id, i64 new_version);
  
  /**
   * Check if a transaction should abort
   * Transactions call this periodically during execution
   */
  bool ShouldAbort(i64 tx_id);
  
  /**
   * Mark a transaction as aborted
   * Can be called externally or internally by detector
   */
  void MarkAborted(i64 tx_id);
  
  /**
   * Clear abort flag for a transaction (when retrying)
   */
  void ClearAbortFlag(i64 tx_id);
  
  /**
   * Remove all tracking for a transaction (when it completes)
   */
  void RemoveTransaction(i64 tx_id);
  
  /**
   * Get statistics
   */
  struct Stats {
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> total_writes{0};
    std::atomic<uint64_t> early_aborts_detected{0};
    std::atomic<uint64_t> version_changes_processed{0};
  };
  
  const Stats& GetStats() const { return stats_; }
  void ResetStats();
  
  /**
   * Enable/disable early abort detection
   */
  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool IsEnabled() const { return enabled_; }

private:
  // Thread-safe tracking of active reads
  // Key: (row, column) -> Set of (tx_id, version) pairs
  std::unordered_map<RowColumnKey, std::unordered_set<ReadRecord, ReadRecordHash>, RowColumnKeyHash> active_reads_;
  std::mutex reads_mutex_;
  
  // Thread-safe tracking of active writes
  // Key: (row, column) -> Set of tx_ids
  std::unordered_map<RowColumnKey, std::unordered_set<i64>, RowColumnKeyHash> active_writes_;
  std::mutex writes_mutex_;
  
  // Thread-safe set of transactions marked for abort
  std::unordered_set<i64> aborted_txs_;
  std::mutex abort_mutex_;
  
  // Statistics
  Stats stats_;
  
  // Enable/disable flag
  std::atomic<bool> enabled_;
  
  /**
   * Internal: Find transactions reading a specific version and mark for abort
   */
  void DetectAndMarkConflicts(const RowColumnKey& key, i64 new_version);
  
  /**
   * Internal: Remove read tracking for a specific transaction and key
   */
  void RemoveRead(i64 tx_id, const RowColumnKey& key);
  
  /**
   * Internal: Remove write tracking for a specific transaction and key
   */
  void RemoveWrite(i64 tx_id, const RowColumnKey& key);
};

} // namespace janus
