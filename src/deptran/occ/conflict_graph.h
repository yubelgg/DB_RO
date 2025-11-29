#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <cstdint>

namespace deptran {

using txn_id_t = uint64_t;

/**
 * ConflictGraph - Tracks dependencies and conflicts between transactions
 * Used for cycle detection in optimistic concurrency control
 * 
 * Thread-safe for concurrent access from multiple validation threads
 */
class ConflictGraph {
public:
    ConflictGraph();
    ~ConflictGraph();

    /**
     * Add a new transaction node to the graph
     * @param txn_id Transaction identifier
     */
    void add_transaction(txn_id_t txn_id);

    /**
     * Add a directed edge representing a conflict
     * Edge from -> to means "from conflicts with to" or "from must commit before to"
     * @param from Source transaction
     * @param to Destination transaction
     */
    void add_conflict(txn_id_t from, txn_id_t to);

    /**
     * Remove a transaction and all its associated edges
     * @param txn_id Transaction to remove
     */
    void remove_transaction(txn_id_t txn_id);

    /**
     * Check if adding an edge would create a cycle
     * Used for early abort detection
     * @param from Source transaction
     * @param to Destination transaction
     * @return true if adding this edge would create a cycle
     */
    bool would_create_cycle(txn_id_t from, txn_id_t to);

    /**
     * Check if the graph contains a cycle
     * @return true if any cycle exists
     */
    bool has_cycle();

    /**
     * Get all transactions that conflict with the given transaction
     * @param txn_id Transaction identifier
     * @return Set of conflicting transaction IDs
     */
    std::unordered_set<txn_id_t> get_conflicts(txn_id_t txn_id) const;

    /**
     * Clear all transactions and edges
     */
    void clear();

    /**
     * Get number of transactions in the graph
     */
    size_t size() const;

    // ================================================================
    // Convenience aliases for integration tests
    // ================================================================
    
    /**
     * Alias for add_transaction() - adds a vertex to the graph
     * @param txn_id Transaction identifier (vertex ID)
     */
    inline void add_vertex(txn_id_t txn_id) {
        add_transaction(txn_id);
    }

    /**
     * Alias for add_conflict() - adds a directed edge
     * @param from Source vertex
     * @param to Destination vertex
     */
    inline void add_edge(txn_id_t from, txn_id_t to) {
        add_conflict(from, to);
    }

    /**
     * Alias for remove_transaction() - removes a vertex
     * @param txn_id Vertex to remove
     */
    inline void remove_vertex(txn_id_t txn_id) {
        remove_transaction(txn_id);
    }

    /**
     * Check if a specific transaction is involved in a cycle
     * @param txn_id Transaction to check
     * @return true if this transaction is part of any cycle
     */
    bool has_cycle_involving(txn_id_t txn_id);

private:
    // Adjacency list: txn_id -> set of transactions it conflicts with
    std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> outgoing_edges_;
    
    // Reverse adjacency list for efficient lookup
    std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> incoming_edges_;

    // Mutex for thread-safe access
    mutable std::mutex mutex_;

    /**
     * DFS-based cycle detection helper
     * @param node Current node being visited
     * @param visited Set of all visited nodes
     * @param rec_stack Recursion stack for current path
     * @return true if cycle detected
     */
    bool has_cycle_util(txn_id_t node, 
                       std::unordered_set<txn_id_t>& visited,
                       std::unordered_set<txn_id_t>& rec_stack) const;

    /**
     * Check if there's a path from 'from' to 'to'
     * Used in would_create_cycle check
     */
    bool has_path(txn_id_t from, txn_id_t to) const;

    /**
     * DFS helper for has_cycle_involving
     * @param node Starting node
     * @param target Target node to find in cycle
     * @param visited Set of visited nodes
     * @param rec_stack Recursion stack
     * @return true if found cycle involving target
     */
    bool has_cycle_involving_util(txn_id_t node,
                                   txn_id_t target,
                                   std::unordered_set<txn_id_t>& visited,
                                   std::unordered_set<txn_id_t>& rec_stack) const;
};

} // namespace deptran
