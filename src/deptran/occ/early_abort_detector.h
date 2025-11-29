#pragma once

#include "conflict_graph.h"
#include "bloom_filter.h"
#include "concurrent_map.h"
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>
#include <unordered_set>  // ✅ ADDED: For cycle detection DFS visited tracking

namespace deptran {

using txn_id_t = uint64_t;
using key_t = uint64_t;

/**
 * Transaction state tracked by early abort detector
 */
enum class TxnState {
    ACTIVE,          // Transaction is executing
    VALIDATING,      // Transaction is being validated
    COMMITTED,       // Transaction committed successfully
    ABORTED          // Transaction was aborted
};

/**
 * Information about a transaction's read/write sets
 */
struct TxnInfo {
    txn_id_t txn_id;
    TxnState state;
    BloomFilter<key_t> read_set;   // Items read by transaction
    BloomFilter<key_t> write_set;  // Items written by transaction
    uint64_t start_time;           // Timestamp when transaction started
    
    TxnInfo(txn_id_t id) 
        : txn_id(id), 
          state(TxnState::ACTIVE),
          read_set(1000, 0.01),    // Expected 1000 items, 1% FPP
          write_set(1000, 0.01),
          start_time(0) {}
};

/**
 * EarlyAbortDetector - Detects transactions that should abort early
 * 
 * Uses conflict graph analysis and bloom filters to identify transactions
 * that are likely to fail validation, allowing them to abort early and
 * avoid wasted work.
 * 
 * Detection strategies:
 * 1. Cycle detection: If a transaction creates a cycle in the conflict graph
 * 2. Write-after-read conflicts: If a new write conflicts with active reads
 * 3. Read-after-write conflicts: If a new read conflicts with active writes
 * 4. Excessive conflicts: If a transaction has too many conflicts
 * 
 * Thread-safe for concurrent access from multiple scheduler threads.
 */
class EarlyAbortDetector {
public:
    /**
     * Configuration parameters
     */
    struct Config {
        bool enable_cycle_detection = true;      // Detect cycles in conflict graph
        bool enable_conflict_counting = true;    // Count conflicts per transaction
        size_t max_conflicts = 10;               // Abort if conflicts exceed this
        bool enable_age_based_abort = true;      // Consider transaction age
        uint64_t max_age_ms = 5000;              // Abort if transaction older than this
        
        Config() = default;
    };

    EarlyAbortDetector(); 
    /**
     * Constructor
     * @param config Configuration parameters
     */
    explicit EarlyAbortDetector(const Config& config);

    ~EarlyAbortDetector();

    /**
     * Register a new transaction
     * Must be called when transaction starts
     * @param txn_id Transaction identifier
     */
    void register_transaction(txn_id_t txn_id);

    /**
     * Record a read operation
     * @param txn_id Transaction identifier
     * @param key Key that was read
     */
    void record_read(txn_id_t txn_id, key_t key);

    /**
     * Record a write operation
     * @param txn_id Transaction identifier
     * @param key Key that was written
     */
    void record_write(txn_id_t txn_id, key_t key);

    /**
     * Record a conflict between two transactions
     * @param from_txn Transaction that conflicts
     * @param to_txn Transaction it conflicts with
     */
    void record_conflict(txn_id_t from_txn, txn_id_t to_txn);

    /**
     * Check if a transaction should abort early
     * @param txn_id Transaction to check
     * @return true if transaction should abort early
     */
    bool should_abort_early(txn_id_t txn_id);

    /**
     * Check if adding a read would cause early abort
     * @param txn_id Transaction identifier
     * @param key Key to read
     * @return true if this read would trigger early abort
     */
    bool would_abort_on_read(txn_id_t txn_id, key_t key);

    /**
     * Check if adding a write would cause early abort
     * @param txn_id Transaction identifier
     * @param key Key to write
     * @return true if this write would trigger early abort
     */
    bool would_abort_on_write(txn_id_t txn_id, key_t key);

    /**
     * Mark transaction as committed
     * @param txn_id Transaction identifier
     */
    void mark_committed(txn_id_t txn_id);

    /**
     * Mark transaction as aborted
     * @param txn_id Transaction identifier
     */
    void mark_aborted(txn_id_t txn_id);

    /**
     * Remove transaction from tracking
     * Should be called after commit/abort
     * @param txn_id Transaction identifier
     */
    void remove_transaction(txn_id_t txn_id);

    /**
     * Get statistics about early aborts
     */
    struct Statistics {
        uint64_t total_checks = 0;              // Total abort checks
        uint64_t cycle_aborts = 0;              // Aborts due to cycles
        uint64_t conflict_aborts = 0;           // Aborts due to too many conflicts
        uint64_t age_aborts = 0;                // Aborts due to old age
        uint64_t total_early_aborts = 0;        // Total early aborts
        
        double early_abort_rate() const {
            return total_checks > 0 ? 
                   static_cast<double>(total_early_aborts) / total_checks : 0.0;
        }
    };

    /**
     * Get current statistics
     */
    Statistics get_statistics() const;

    /**
     * Reset statistics
     */
    void reset_statistics();

    /**
     * Clear all state (for testing)
     */
    void clear();

private:
    Config config_;
    
    // Conflict tracking
    ConflictGraph conflict_graph_;
    
    // Transaction information
    ConcurrentMap<txn_id_t, std::shared_ptr<TxnInfo>> active_txns_;
    
    // Conflict counters
    ConcurrentMap<txn_id_t, size_t> conflict_counts_;
    
    // Statistics
    mutable std::atomic<uint64_t> total_checks_{0};
    mutable std::atomic<uint64_t> cycle_aborts_{0};
    mutable std::atomic<uint64_t> conflict_aborts_{0};
    mutable std::atomic<uint64_t> age_aborts_{0};
    mutable std::atomic<uint64_t> total_early_aborts_{0};

    /**
     * Check for cycle detection abort
     */
    bool should_abort_cycle(txn_id_t txn_id);

    /**
     * Check for excessive conflicts abort
     */
    bool should_abort_conflicts(txn_id_t txn_id);

    /**
     * Check for age-based abort
     */
    bool should_abort_age(txn_id_t txn_id);

    // ========================================================================
    // ✅ ADDED: Helper method for proper cycle detection
    // ========================================================================
    /**
     * DFS helper to check if there's a path from 'from' to 'to' in conflict graph
     * Used by should_abort_cycle() to detect EXISTING cycles
     * 
     * @param from Starting transaction ID
     * @param to Target transaction ID to reach
     * @param visited Set of already visited nodes (prevents infinite loops)
     * @param max_depth Maximum search depth (prevents stack overflow)
     * @return true if path exists from 'from' to 'to'
     */
    bool dfs_has_path(
        txn_id_t from, 
        txn_id_t to, 
        std::unordered_set<txn_id_t>& visited,
        int max_depth);
    // ========================================================================

    /**
     * Detect conflicts with active transactions
     */
    std::vector<txn_id_t> detect_conflicts(txn_id_t txn_id, key_t key, bool is_write);

    /**
     * Get current timestamp in milliseconds
     */
    uint64_t get_current_time_ms() const;
};

} // namespace deptran
