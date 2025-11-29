#include "early_abort_detector.h"
#include <chrono>
#include <algorithm>
#include <unordered_set>  // ✅ ADDED: For cycle detection DFS visited tracking

namespace deptran {

EarlyAbortDetector::EarlyAbortDetector() 
    : EarlyAbortDetector(Config()) {
}


EarlyAbortDetector::EarlyAbortDetector(const Config& config) 
    : config_(config) {
}


EarlyAbortDetector::~EarlyAbortDetector() = default;

void EarlyAbortDetector::register_transaction(txn_id_t txn_id) {
    auto txn_info = std::make_shared<TxnInfo>(txn_id);
    txn_info->start_time = get_current_time_ms();
    
    active_txns_.insert(txn_id, txn_info);
    conflict_graph_.add_transaction(txn_id);
    conflict_counts_.insert(txn_id, 0);
}

void EarlyAbortDetector::record_read(txn_id_t txn_id, key_t key) {
    std::shared_ptr<TxnInfo> txn_info;
    if (active_txns_.get(txn_id, txn_info)) {
        txn_info->read_set.insert(key);
        
        // Check for conflicts with active writers
        auto conflicts = detect_conflicts(txn_id, key, false);
        for (txn_id_t conflicting_txn : conflicts) {
            record_conflict(conflicting_txn, txn_id);
        }
    }
}

void EarlyAbortDetector::record_write(txn_id_t txn_id, key_t key) {
    std::shared_ptr<TxnInfo> txn_info;
    if (active_txns_.get(txn_id, txn_info)) {
        txn_info->write_set.insert(key);
        
        // Check for conflicts with active readers and writers
        auto conflicts = detect_conflicts(txn_id, key, true);
        for (txn_id_t conflicting_txn : conflicts) {
            record_conflict(conflicting_txn, txn_id);
        }
    }
}

void EarlyAbortDetector::record_conflict(txn_id_t from_txn, txn_id_t to_txn) {
    // Add edge to conflict graph
    conflict_graph_.add_conflict(from_txn, to_txn);
    
    // Increment conflict count for both transactions
    conflict_counts_.update(from_txn, [](size_t count) { return count + 1; });
    conflict_counts_.update(to_txn, [](size_t count) { return count + 1; });
}

bool EarlyAbortDetector::should_abort_early(txn_id_t txn_id) {
    total_checks_.fetch_add(1, std::memory_order_relaxed);
    
    // Check various abort conditions
    bool abort = false;
    
    // 1. Cycle detection
    if (config_.enable_cycle_detection && should_abort_cycle(txn_id)) {
        cycle_aborts_.fetch_add(1, std::memory_order_relaxed);
        abort = true;
    }
    
    // 2. Excessive conflicts
    if (!abort && config_.enable_conflict_counting && should_abort_conflicts(txn_id)) {
        conflict_aborts_.fetch_add(1, std::memory_order_relaxed);
        abort = true;
    }
    
    // 3. Age-based abort
    if (!abort && config_.enable_age_based_abort && should_abort_age(txn_id)) {
        age_aborts_.fetch_add(1, std::memory_order_relaxed);
        abort = true;
    }
    
    if (abort) {
        total_early_aborts_.fetch_add(1, std::memory_order_relaxed);
    }
    
    return abort;
}

bool EarlyAbortDetector::would_abort_on_read(txn_id_t txn_id, key_t key) {
    // Check if this read would create conflicts leading to abort
    auto conflicts = detect_conflicts(txn_id, key, false);
    
    if (conflicts.empty()) {
        return false;
    }
    
    // Simulate adding conflicts and check if it would cause abort
    size_t current_conflicts = 0;
    conflict_counts_.get(txn_id, current_conflicts);
    
    if (config_.enable_conflict_counting && 
        current_conflicts + conflicts.size() > config_.max_conflicts) {
        return true;
    }
    
    // Check if any conflict would create a cycle
    if (config_.enable_cycle_detection) {
        for (txn_id_t conflicting_txn : conflicts) {
            if (conflict_graph_.would_create_cycle(conflicting_txn, txn_id)) {
                return true;
            }
        }
    }
    
    return false;
}

bool EarlyAbortDetector::would_abort_on_write(txn_id_t txn_id, key_t key) {
    // Check if this write would create conflicts leading to abort
    auto conflicts = detect_conflicts(txn_id, key, true);
    
    if (conflicts.empty()) {
        return false;
    }
    
    // Simulate adding conflicts and check if it would cause abort
    size_t current_conflicts = 0;
    conflict_counts_.get(txn_id, current_conflicts);
    
    if (config_.enable_conflict_counting && 
        current_conflicts + conflicts.size() > config_.max_conflicts) {
        return true;
    }
    
    // Check if any conflict would create a cycle
    if (config_.enable_cycle_detection) {
        for (txn_id_t conflicting_txn : conflicts) {
            if (conflict_graph_.would_create_cycle(conflicting_txn, txn_id)) {
                return true;
            }
        }
    }
    
    return false;
}

void EarlyAbortDetector::mark_committed(txn_id_t txn_id) {
    std::shared_ptr<TxnInfo> txn_info;
    if (active_txns_.get(txn_id, txn_info)) {
        txn_info->state = TxnState::COMMITTED;
    }
}

void EarlyAbortDetector::mark_aborted(txn_id_t txn_id) {
    std::shared_ptr<TxnInfo> txn_info;
    if (active_txns_.get(txn_id, txn_info)) {
        txn_info->state = TxnState::ABORTED;
    }
}

void EarlyAbortDetector::remove_transaction(txn_id_t txn_id) {
    active_txns_.erase(txn_id);
    conflict_graph_.remove_transaction(txn_id);
    conflict_counts_.erase(txn_id);
}

EarlyAbortDetector::Statistics EarlyAbortDetector::get_statistics() const {
    Statistics stats;
    stats.total_checks = total_checks_.load(std::memory_order_relaxed);
    stats.cycle_aborts = cycle_aborts_.load(std::memory_order_relaxed);
    stats.conflict_aborts = conflict_aborts_.load(std::memory_order_relaxed);
    stats.age_aborts = age_aborts_.load(std::memory_order_relaxed);
    stats.total_early_aborts = total_early_aborts_.load(std::memory_order_relaxed);
    return stats;
}

void EarlyAbortDetector::reset_statistics() {
    total_checks_.store(0, std::memory_order_relaxed);
    cycle_aborts_.store(0, std::memory_order_relaxed);
    conflict_aborts_.store(0, std::memory_order_relaxed);
    age_aborts_.store(0, std::memory_order_relaxed);
    total_early_aborts_.store(0, std::memory_order_relaxed);
}

void EarlyAbortDetector::clear() {
    active_txns_.clear();
    conflict_graph_.clear();
    conflict_counts_.clear();
    reset_statistics();
}

// ============================================================================
// ✅ FIXED: Cycle detection logic - checks for EXISTING cycles
// ============================================================================
bool EarlyAbortDetector::should_abort_cycle(txn_id_t txn_id) {
    // Check if this transaction is part of an EXISTING cycle in the conflict graph
    // A cycle exists if: txn_id -> A -> ... -> txn_id (path back to itself)
    
    auto conflicts = conflict_graph_.get_conflicts(txn_id);
    
    // ✅ ADDED: Early return if no outgoing edges (can't be in a cycle)
    if (conflicts.empty()) {
        return false;
    }
    
    // For each transaction we point to, check if there's a path back to us
    for (txn_id_t target : conflicts) {
        // ✅ FIXED: Properly declare visited set for each DFS search
        std::unordered_set<txn_id_t> visited;
        
        // ✅ FIXED: Check if target can reach back to txn_id (existing cycle)
        // NOT checking if adding reverse edge would create cycle (that was the bug)
        if (dfs_has_path(target, txn_id, visited, 20)) {
            return true;  // Found cycle: txn_id -> ... -> target -> ... -> txn_id
        }
    }
    
    return false;
}
// ============================================================================

// ============================================================================
// ✅ ADDED: DFS helper method to find path in conflict graph
// ============================================================================
bool EarlyAbortDetector::dfs_has_path(
    txn_id_t from, 
    txn_id_t to, 
    std::unordered_set<txn_id_t>& visited,
    int max_depth) {
    
    // Depth limit to prevent infinite loops in degenerate cases
    if (max_depth <= 0) {
        return false;
    }
    
    // Already visited this node in current search (avoid infinite loops)
    if (visited.count(from)) {
        return false;
    }
    
    // Mark this node as visited
    visited.insert(from);
    
    // Get all transactions that 'from' points to (outgoing edges)
    auto conflicts = conflict_graph_.get_conflicts(from);
    
    for (txn_id_t next : conflicts) {
        // Found direct path to target!
        if (next == to) {
            return true;
        }
        
        // Recursively search from the next node
        if (dfs_has_path(next, to, visited, max_depth - 1)) {
            return true;
        }
    }
    
    return false;
}
// ============================================================================

bool EarlyAbortDetector::should_abort_conflicts(txn_id_t txn_id) {
    size_t num_conflicts = 0;
    if (conflict_counts_.get(txn_id, num_conflicts)) {
        return num_conflicts > config_.max_conflicts;
    }
    return false;
}

bool EarlyAbortDetector::should_abort_age(txn_id_t txn_id) {
    std::shared_ptr<TxnInfo> txn_info;
    if (active_txns_.get(txn_id, txn_info)) {
        uint64_t age_ms = get_current_time_ms() - txn_info->start_time;
        return age_ms > config_.max_age_ms;
    }
    return false;
}

std::vector<txn_id_t> EarlyAbortDetector::detect_conflicts(
    txn_id_t txn_id, key_t key, bool is_write) {
    
    std::vector<txn_id_t> conflicts;
    
    // Get all active transactions
    auto all_txns = active_txns_.snapshot();
    
    for (const auto& pair : all_txns) {
        txn_id_t other_txn = pair.first;
        auto other_info = pair.second;
        
        // Skip self
        if (other_txn == txn_id) {
            continue;
        }
        
        // Skip non-active transactions
        if (other_info->state != TxnState::ACTIVE) {
            continue;
        }
        
        bool has_conflict = false;
        
        if (is_write) {
            // Write operation: conflicts with both reads and writes
            if (other_info->read_set.contains(key) || 
                other_info->write_set.contains(key)) {
                has_conflict = true;
            }
        } else {
            // Read operation: conflicts only with writes
            if (other_info->write_set.contains(key)) {
                has_conflict = true;
            }
        }
        
        if (has_conflict) {
            conflicts.push_back(other_txn);
        }
    }
    
    return conflicts;
}

uint64_t EarlyAbortDetector::get_current_time_ms() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

} // namespace deptran
