#pragma once

#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include "../memdb/row.h"
#include "base/all.hpp"

using mdb::Row;

namespace janus {

/**
 * HotKeyTracker - Tracks frequently accessed keys to identify contention hotspots
 * 
 * Strategy: Keys with high access counts in a short window are "hot" and
 * transactions accessing them may benefit from special handling:
 * - Longer lock timeouts
 * - Queuing/serialization
 * - Priority scheduling
 */
class HotKeyTracker {
public:
  // Default threshold: a key is "hot" if accessed more than this many times
  static constexpr uint32_t DEFAULT_HOT_THRESHOLD = 10;
  // Window for counting accesses (in seconds)
  static constexpr int WINDOW_SECONDS = 1;

  HotKeyTracker(uint32_t hot_threshold = DEFAULT_HOT_THRESHOLD);
  ~HotKeyTracker();

  /**
   * Record an access to a row
   * Thread-safe
   */
  void RecordAccess(Row* row);

  /**
   * Check if a row is a hot key
   * Thread-safe
   */
  bool IsHotKey(Row* row) const;

  /**
   * Get access count for a row
   */
  uint32_t GetAccessCount(Row* row) const;

  /**
   * Get the hot threshold
   */
  uint32_t GetHotThreshold() const { return hot_threshold_; }

  /**
   * Get statistics
   */
  struct Stats {
    std::atomic<uint64_t> total_accesses{0};
    std::atomic<uint64_t> hot_key_accesses{0};
    std::atomic<uint64_t> unique_keys_tracked{0};
  };
  
  const Stats& GetStats() const { return stats_; }

  /**
   * Reset counts (call periodically to avoid unbounded growth)
   * Clears counts for keys below threshold
   */
  void Cleanup();

  /**
   * Reset all tracking
   */
  void Reset();

private:
  // Access counts per row
  mutable std::mutex mutex_;
  std::unordered_map<Row*, std::atomic<uint32_t>> access_counts_;
  
  // Threshold for considering a key "hot"
  uint32_t hot_threshold_;
  
  // Statistics
  Stats stats_;
  
  // Last cleanup time
  std::chrono::steady_clock::time_point last_cleanup_;
};

} // namespace janus
