#include "early_abort_detector.h"
#include <algorithm>

using rrr::i64;

namespace janus {

EarlyAbortDetector::EarlyAbortDetector() 
    : enabled_(true) {
  Log_info("EarlyAbortDetector initialized");
}

EarlyAbortDetector::~EarlyAbortDetector() {
  Log_info("EarlyAbortDetector shutdown - Stats: reads=%llu, writes=%llu, early_aborts=%llu",
           stats_.total_reads.load(),
           stats_.total_writes.load(),
           stats_.early_aborts_detected.load());
}

void EarlyAbortDetector::RegisterRead(i64 tx_id, Row* row, 
                                       mdb::colid_t column_id, i64 version) {
  if (!enabled_) return;
  
  RowColumnKey key(row, column_id);
  ReadRecord record(tx_id, version);
  
  {
    std::lock_guard<std::mutex> lock(reads_mutex_);
    active_reads_[key].insert(record);
  }
  
  stats_.total_reads.fetch_add(1, std::memory_order_relaxed);
  
  Log_debug("RegisterRead: tx=%" PRIx64 " row=%p col=%d ver=%" PRIx64, 
            tx_id, row, column_id, version);
}

void EarlyAbortDetector::RegisterWrite(i64 tx_id, Row* row, 
                                        mdb::colid_t column_id) {
  if (!enabled_) return;
  
  RowColumnKey key(row, column_id);
  
  {
    std::lock_guard<std::mutex> lock(writes_mutex_);
    active_writes_[key].insert(tx_id);
  }
  
  stats_.total_writes.fetch_add(1, std::memory_order_relaxed);
  
  Log_debug("RegisterWrite: tx=%" PRIx64 " row=%p col=%d", 
            tx_id, row, column_id);
}

void EarlyAbortDetector::NotifyVersionChange(Row* row, 
                                              mdb::colid_t column_id, 
                                              i64 new_version) {
  if (!enabled_) return;
  
  RowColumnKey key(row, column_id);
  
  // Find all transactions reading old versions of this column
  DetectAndMarkConflicts(key, new_version);
  
  stats_.version_changes_processed.fetch_add(1, std::memory_order_relaxed);
  
  Log_debug("NotifyVersionChange: row=%p col=%d new_ver=%" PRIx64, 
            row, column_id, new_version);
}

void EarlyAbortDetector::DetectAndMarkConflicts(const RowColumnKey& key, 
                                                 i64 new_version) {
  std::vector<i64> txs_to_abort;
  
  // Find transactions reading old versions
  {
    std::lock_guard<std::mutex> lock(reads_mutex_);
    
    auto it = active_reads_.find(key);
    if (it != active_reads_.end()) {
      for (const ReadRecord& record : it->second) {
        // If transaction read an older version, it will fail validation
        if (record.version < new_version) {
          txs_to_abort.push_back(record.tx_id);
          Log_debug("Detected conflict: tx=%" PRIx64 " read ver=%" PRIx64 
                    " but current ver=%" PRIx64,
                    record.tx_id, record.version, new_version);
        }
      }
    }
  }
  
  // Mark conflicting transactions for abort
  if (!txs_to_abort.empty()) {
    std::lock_guard<std::mutex> lock(abort_mutex_);
    for (i64 tx_id : txs_to_abort) {
      aborted_txs_.insert(tx_id);
      stats_.early_aborts_detected.fetch_add(1, std::memory_order_relaxed);
      Log_debug("Marked tx %" PRIx64 " for early abort", tx_id);
    }
  }
}

bool EarlyAbortDetector::ShouldAbort(i64 tx_id) {
  if (!enabled_) return false;
  
  std::lock_guard<std::mutex> lock(abort_mutex_);
  return aborted_txs_.count(tx_id) > 0;
}

void EarlyAbortDetector::MarkAborted(i64 tx_id) {
  if (!enabled_) return;
  
  std::lock_guard<std::mutex> lock(abort_mutex_);
  aborted_txs_.insert(tx_id);
  
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
  
  // Remove from active writes
  {
    std::lock_guard<std::mutex> lock(writes_mutex_);
    
    for (auto it = active_writes_.begin(); it != active_writes_.end(); ) {
      auto& write_set = it->second;
      write_set.erase(tx_id);
      
      // Remove key if no writes remain
      if (write_set.empty()) {
        it = active_writes_.erase(it);
      } else {
        ++it;
      }
    }
  }
  
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

void EarlyAbortDetector::RemoveWrite(i64 tx_id, const RowColumnKey& key) {
  std::lock_guard<std::mutex> lock(writes_mutex_);
  
  auto it = active_writes_.find(key);
  if (it != active_writes_.end()) {
    it->second.erase(tx_id);
    
    // Clean up empty entries
    if (it->second.empty()) {
      active_writes_.erase(it);
    }
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
