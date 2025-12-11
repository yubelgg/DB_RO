#include "hot_key_tracker.h"

namespace janus {

HotKeyTracker::HotKeyTracker(uint32_t hot_threshold)
    : hot_threshold_(hot_threshold),
      last_cleanup_(std::chrono::steady_clock::now()) {
  Log_info("HotKeyTracker initialized with threshold=%u", hot_threshold_);
}

HotKeyTracker::~HotKeyTracker() {
  Log_info("HotKeyTracker shutdown - Stats: total_accesses=%llu, hot_key_accesses=%llu, unique_keys=%llu",
           stats_.total_accesses.load(),
           stats_.hot_key_accesses.load(),
           stats_.unique_keys_tracked.load());
}

void HotKeyTracker::RecordAccess(Row* row) {
  if (!row) return;
  
  stats_.total_accesses.fetch_add(1, std::memory_order_relaxed);
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = access_counts_.find(row);
  if (it == access_counts_.end()) {
    // New key
    access_counts_[row].store(1, std::memory_order_relaxed);
    stats_.unique_keys_tracked.fetch_add(1, std::memory_order_relaxed);
  } else {
    uint32_t new_count = it->second.fetch_add(1, std::memory_order_relaxed) + 1;
    
    // Check if this access made the key "hot"
    if (new_count == hot_threshold_) {
      stats_.hot_key_accesses.fetch_add(1, std::memory_order_relaxed);
      Log_debug("Key %p became hot (count=%u)", row, new_count);
    }
  }
  
  // Periodic cleanup to avoid unbounded growth
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup_).count();
  if (elapsed >= WINDOW_SECONDS * 10) {
    // Release lock before cleanup (will reacquire)
    // Note: This is a simple heuristic, could be improved
    last_cleanup_ = now;
    // Don't actually cleanup here to avoid deadlock, just reset the timer
    // Cleanup should be called explicitly by the scheduler periodically
  }
}

bool HotKeyTracker::IsHotKey(Row* row) const {
  if (!row) return false;
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = access_counts_.find(row);
  if (it == access_counts_.end()) {
    return false;
  }
  
  return it->second.load(std::memory_order_relaxed) >= hot_threshold_;
}

uint32_t HotKeyTracker::GetAccessCount(Row* row) const {
  if (!row) return 0;
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = access_counts_.find(row);
  if (it == access_counts_.end()) {
    return 0;
  }
  
  return it->second.load(std::memory_order_relaxed);
}

void HotKeyTracker::Cleanup() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Remove keys with low access counts
  size_t removed = 0;
  for (auto it = access_counts_.begin(); it != access_counts_.end(); ) {
    if (it->second.load(std::memory_order_relaxed) < hot_threshold_ / 2) {
      it = access_counts_.erase(it);
      removed++;
    } else {
      // Decay counts for remaining keys
      uint32_t old_count = it->second.load(std::memory_order_relaxed);
      it->second.store(old_count / 2, std::memory_order_relaxed);
      ++it;
    }
  }
  
  if (removed > 0) {
    Log_debug("HotKeyTracker cleanup: removed %zu cold keys", removed);
  }
  
  last_cleanup_ = std::chrono::steady_clock::now();
}

void HotKeyTracker::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  access_counts_.clear();
  stats_.total_accesses.store(0, std::memory_order_relaxed);
  stats_.hot_key_accesses.store(0, std::memory_order_relaxed);
  stats_.unique_keys_tracked.store(0, std::memory_order_relaxed);
  last_cleanup_ = std::chrono::steady_clock::now();
  
  Log_info("HotKeyTracker reset");
}

} // namespace janus
