#include "early_abort_detector.h"
#include <algorithm>

using rrr::i64;

namespace janus {

// Static member definitions for GC timing
constexpr std::chrono::milliseconds EarlyAbortDetector::gc_interval_;
constexpr std::chrono::milliseconds EarlyAbortDetector::entry_ttl_;

EarlyAbortDetector::EarlyAbortDetector()
    : enabled_(true) {
  // Start GC thread (Fix 5)
  gc_running_ = true;
  gc_thread_ = std::thread(&EarlyAbortDetector::GarbageCollectionLoop, this);
  Log_info("EarlyAbortDetector initialized with GC (interval=%ldms, ttl=%ldms)",
           gc_interval_.count(), entry_ttl_.count());
}

EarlyAbortDetector::~EarlyAbortDetector() {
  // Stop GC thread (Fix 5)
  gc_running_ = false;
  if (gc_thread_.joinable()) {
    gc_thread_.join();
  }

  Log_info("EarlyAbortDetector shutdown - Stats: reads=%llu, early_aborts=%llu",
           stats_.total_reads.load(),
           stats_.early_aborts_detected.load());
}

void EarlyAbortDetector::RegisterRead(i64 tx_id, Row* row,
                                       mdb::colid_t column_id, i64 version) {
  if (!enabled_) return;

  RowColumnKey key(row, column_id);
  ReadRecord record(tx_id, version);

  // FIX: Use try_lock to avoid blocking - if contended, skip registration
  // Transaction will still be caught by version check at validation time
  {
    std::unique_lock<std::mutex> lock(reads_mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
      active_reads_[key].insert(record);
      stats_.total_reads.fetch_add(1, std::memory_order_relaxed);
    }
    // If we couldn't get the lock, skip this read registration
    // This is safe because validation will still catch the conflict
  }
}

// NOTE: RegisterWrite removed - was dead code (never used for conflict detection)

void EarlyAbortDetector::NotifyVersionChange(Row* row,
                                              mdb::colid_t column_id,
                                              i64 new_version) {
  if (!enabled_) return;

  RowColumnKey key(row, column_id);

  // DISABLED: Expensive logging causes 60% performance overhead
  // {
  //   std::lock_guard<std::mutex> lock(reads_mutex_);
  //   Log_info("NotifyVersionChange: row=%p col=%d new_ver=%" PRIx64 " | active_reads_ has %zu keys total",
  //            row, column_id, new_version, active_reads_.size());
  //
  //   auto it = active_reads_.find(key);
  //   if (it != active_reads_.end()) {
  //     Log_info("  -> Found %zu active readers for THIS key", it->second.size());
  //   } else {
  //     Log_info("  -> Key NOT FOUND in active_reads_");
  //   }
  // }

  // Find all transactions reading old versions of this column
  DetectAndMarkConflicts(key, new_version);

  stats_.version_changes_processed.fetch_add(1, std::memory_order_relaxed);
}

void EarlyAbortDetector::DetectAndMarkConflicts(const RowColumnKey& key,
                                                 i64 new_version) {
  std::vector<i64> txs_to_abort;

  // Find transactions reading old versions
  // FIX: Acquire abort_mutex first to check if already aborted (avoid repeated detections)
  {
    std::lock_guard<std::mutex> abort_lock(abort_mutex_);
    std::lock_guard<std::mutex> reads_lock(reads_mutex_);

    auto it = active_reads_.find(key);
    if (it != active_reads_.end()) {
      for (const ReadRecord& record : it->second) {
        // If transaction read an older version, it will fail validation
        // FIX: Skip if already marked for abort (prevents 200x repeated detections!)
        if (record.version < new_version && aborted_txs_.count(record.tx_id) == 0) {
          txs_to_abort.push_back(record.tx_id);
        }
      }
    }

    // Mark conflicting transactions for abort (inside same lock scope)
    if (!txs_to_abort.empty()) {
      auto now = std::chrono::steady_clock::now();
      for (i64 tx_id : txs_to_abort) {
        aborted_txs_[tx_id] = now;
        stats_.early_aborts_detected.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }
}

bool EarlyAbortDetector::ShouldAbort(i64 tx_id) {
  if (!enabled_) return false;

  // FIX: Use try_lock to avoid blocking on contended mutex
  // If we can't get the lock, return false (will check again later)
  std::unique_lock<std::mutex> lock(abort_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;  // Couldn't get lock, assume not aborted for now
  }
  return aborted_txs_.count(tx_id) > 0;
}

void EarlyAbortDetector::MarkAborted(i64 tx_id) {
  if (!enabled_) return;

  std::lock_guard<std::mutex> lock(abort_mutex_);
  aborted_txs_[tx_id] = std::chrono::steady_clock::now();  // Store with timestamp

  Log_debug("Manually marked tx %" PRIx64 " for abort", tx_id);
}

void EarlyAbortDetector::ClearAbortFlag(i64 tx_id) {
  if (!enabled_) return;
  
  std::lock_guard<std::mutex> lock(abort_mutex_);
  aborted_txs_.erase(tx_id);
  
  Log_debug("Cleared abort flag for tx %" PRIx64, tx_id);
}

void EarlyAbortDetector::RemoveTransaction(i64 tx_id) {
  if (!enabled_) return;

  Log_debug("RemoveTransaction: cleaning up tx %" PRIx64 " from tracking", tx_id);

  // Remove from active reads
  {
    std::lock_guard<std::mutex> lock(reads_mutex_);
    
    // Need to iterate through all keys to find this transaction's reads
    for (auto it = active_reads_.begin(); it != active_reads_.end(); ) {
      auto& read_set = it->second;
      
      // Remove all reads from this transaction
      for (auto read_it = read_set.begin(); read_it != read_set.end(); ) {
        if (read_it->tx_id == tx_id) {
          read_it = read_set.erase(read_it);
        } else {
          ++read_it;
        }
      }
      
      // Remove key if no reads remain
      if (read_set.empty()) {
        it = active_reads_.erase(it);
      } else {
        ++it;
      }
    }
  }
  
  // NOTE: active_writes_ cleanup removed - write tracking no longer exists

  // Remove from abort set
  {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    aborted_txs_.erase(tx_id);
  }
  
  Log_debug("Removed all tracking for tx %" PRIx64, tx_id);
}

void EarlyAbortDetector::RemoveRead(i64 tx_id, const RowColumnKey& key) {
  std::lock_guard<std::mutex> lock(reads_mutex_);
  
  auto it = active_reads_.find(key);
  if (it != active_reads_.end()) {
    auto& read_set = it->second;
    for (auto read_it = read_set.begin(); read_it != read_set.end(); ) {
      if (read_it->tx_id == tx_id) {
        read_it = read_set.erase(read_it);
      } else {
        ++read_it;
      }
    }
    
    // Clean up empty entries
    if (read_set.empty()) {
      active_reads_.erase(it);
    }
  }
}

// NOTE: RemoveWrite removed - write tracking no longer exists

// GC methods (Fix 5)
void EarlyAbortDetector::GarbageCollectionLoop() {
  Log_debug("EarlyAbortDetector GC thread started");
  while (gc_running_) {
    std::this_thread::sleep_for(gc_interval_);
    if (gc_running_) {  // Check again after sleep
      CleanExpiredEntries();
    }
  }
  Log_debug("EarlyAbortDetector GC thread stopped");
}

void EarlyAbortDetector::CleanExpiredEntries() {
  auto now = std::chrono::steady_clock::now();
  size_t reads_cleaned = 0;
  size_t aborts_cleaned = 0;

  // Clean expired read records
  {
    std::lock_guard<std::mutex> lock(reads_mutex_);
    for (auto it = active_reads_.begin(); it != active_reads_.end(); ) {
      auto& read_set = it->second;
      for (auto read_it = read_set.begin(); read_it != read_set.end(); ) {
        if (now - read_it->timestamp > entry_ttl_) {
          read_it = read_set.erase(read_it);
          reads_cleaned++;
        } else {
          ++read_it;
        }
      }
      // Remove key if no reads remain
      if (read_set.empty()) {
        it = active_reads_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Clean old abort flags using TTL-based expiration
  // Each abort flag now has a timestamp, so we can properly expire old ones
  {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    for (auto it = aborted_txs_.begin(); it != aborted_txs_.end(); ) {
      if (now - it->second > entry_ttl_) {
        it = aborted_txs_.erase(it);
        aborts_cleaned++;
      } else {
        ++it;
      }
    }
  }

  if (reads_cleaned > 0 || aborts_cleaned > 0) {
    Log_debug("GC: cleaned %zu expired read records, %zu expired abort flags",
              reads_cleaned, aborts_cleaned);
  }
}

void EarlyAbortDetector::ResetStats() {
  stats_.total_reads.store(0, std::memory_order_relaxed);
  stats_.total_writes.store(0, std::memory_order_relaxed);
  stats_.early_aborts_detected.store(0, std::memory_order_relaxed);
  stats_.version_changes_processed.store(0, std::memory_order_relaxed);
  
  Log_info("EarlyAbortDetector stats reset");
}

} // namespace janus
