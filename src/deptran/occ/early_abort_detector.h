#pragma once

#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include "../memdb/row.h"
#include "base/all.hpp"

using rrr::i64;
using mdb::Row;

namespace janus {

/**
 * RowColumnKey - Identifies a specific column in a specific row
 * Used as key for tracking reads/writes
 *
 * IMPORTANT: Uses uintptr_t instead of Row* to avoid dangling pointer issues.
 * The row address is used as an identity/key only - never dereferenced.
 * This prevents segfaults during shutdown when Rows may be freed while
 * entries still exist in the tracking maps.
 */
struct RowColumnKey {
  uintptr_t row_id;  // Row address as identity (never dereferenced)
  mdb::colid_t column_id;

  RowColumnKey(Row* r, mdb::colid_t col)
    : row_id(reinterpret_cast<uintptr_t>(r)), column_id(col) {}

  bool operator==(const RowColumnKey& other) const {
    return row_id == other.row_id && column_id == other.column_id;
  }
};

// Hash function for RowColumnKey
struct RowColumnKeyHash {
  size_t operator()(const RowColumnKey& key) const {
    return std::hash<uintptr_t>()(key.row_id) ^ std::hash<int>()(key.column_id);
  }
};

/**
 * ReadRecord - Tracks a specific read operation with timestamp for GC
 */
struct ReadRecord {
  i64 tx_id;
  i64 version;  // Version number that was read
  std::chrono::steady_clock::time_point timestamp;  // For GC expiration

  ReadRecord(i64 tid, i64 ver)
    : tx_id(tid), version(ver), timestamp(std::chrono::steady_clock::now()) {}

  // Comparison ignores timestamp (for duplicate detection)
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
  
  // NOTE: Write tracking removed - was dead code (never used for conflict detection)
  // Write-write conflicts are handled by OCC lock acquisition during validation
  
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
  
  // NOTE: active_writes_ removed - was never used for conflict detection
  
  // Thread-safe map of transactions marked for abort with timestamps
  // Key: tx_id, Value: timestamp when abort was marked (for TTL-based GC)
  std::unordered_map<i64, std::chrono::steady_clock::time_point> aborted_txs_;
  std::mutex abort_mutex_;
  
  // Statistics
  Stats stats_;
  
  // Enable/disable flag
  std::atomic<bool> enabled_;

  // Garbage collection thread and settings (Fix 5)
  std::thread gc_thread_;
  std::atomic<bool> gc_running_{false};
  std::mutex gc_mutex_;                   // Protects gc_cv_
  std::condition_variable gc_cv_;         // For immediate shutdown wakeup
  static constexpr std::chrono::milliseconds gc_interval_{100};  // Clean every 100ms
  static constexpr std::chrono::milliseconds entry_ttl_{500};    // Entries expire after 500ms

  /**
   * Internal: GC background thread loop
   */
  void GarbageCollectionLoop();

  /**
   * Internal: Clean expired entries from tracking maps
   */
  void CleanExpiredEntries();

  /**
   * Internal: Find transactions reading a specific version and mark for abort
   */
  void DetectAndMarkConflicts(const RowColumnKey& key, i64 new_version);

  /**
   * Internal: Remove read tracking for a specific transaction and key
   */
  void RemoveRead(i64 tx_id, const RowColumnKey& key);

  // NOTE: RemoveWrite removed - write tracking no longer exists
};


} // namespace janus
