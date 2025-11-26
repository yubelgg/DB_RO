#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "tx_enhanced.h"
#include "batch_metadata.h"

namespace janus {

/**
 * ConflictGraph - Analyzes dependencies between transactions in a batch
 * 
 * Builds a graph where:
 * - Nodes = transactions (represented by indices into the batch)
 * - Edges = conflicts (two transactions access same row, at least one writes)
 * 
 * Provides algorithms for:
 * - Finding independent sets (transactions that can validate in parallel)
 * - Topological sort (safe commit order)
 */
class ConflictGraph {
public:
  ConflictGraph();
  ~ConflictGraph();

  /**
   * Build conflict graph from a batch of transactions
   * Analyzes read/write sets to determine which transactions conflict
   */
  void Build(const std::vector<TxOccEnhanced*>& transactions);

  /**
   * Find sets of transactions that don't conflict with each other
   * Uses graph coloring to partition transactions into independent sets
   * 
   * Returns: Vector of independent sets, where each set is a vector of 
   *          transaction indices that can be validated in parallel
   */
  std::vector<std::vector<size_t>> FindIndependentSets();

  /**
   * Get a safe commit order using topological sort
   * Ensures transactions commit in dependency order
   * 
   * Returns: Vector of transaction indices in commit order
   */
  std::vector<size_t> TopologicalSort();

  /**
   * Check if two transaction indices conflict
   */
  bool HasConflict(size_t tx1_idx, size_t tx2_idx) const;

  /**
   * Get all transactions that conflict with given transaction
   */
  std::unordered_set<size_t> GetConflicts(size_t tx_idx) const;

  /**
   * Get number of nodes (transactions) in the graph
   */
  size_t NumNodes() const { return num_nodes_; }

  /**
   * Get number of edges (conflicts) in the graph
   */
  size_t NumEdges() const { return num_edges_; }

  /**
   * Clear the graph
   */
  void Clear();

private:
  // Number of nodes (transactions)
  size_t num_nodes_;
  
  // Number of edges (conflicts)
  size_t num_edges_;
  
  // Adjacency list: tx_index -> set of conflicting tx indices
  std::unordered_map<size_t, std::unordered_set<size_t>> adj_list_;
  
  // Transaction batch (stored for access set analysis)
  std::vector<TxOccEnhanced*> transactions_;
  
  // Cache of read/write sets for each transaction
  struct AccessSets {
    std::unordered_set<Row*> read_set;
    std::unordered_set<Row*> write_set;
  };
  std::vector<AccessSets> access_sets_;

  /**
   * Extract read and write sets from a transaction
   */
  void ExtractAccessSets(TxOccEnhanced* tx, AccessSets& sets);

  /**
   * Determine conflict type between two transactions
   */
  ConflictType CheckConflictType(size_t tx1_idx, size_t tx2_idx);

  /**
   * Add an edge to the graph (bidirectional)
   */
  void AddEdge(size_t tx1_idx, size_t tx2_idx);

  /**
   * Greedy graph coloring for finding independent sets
   * Assigns each node a color such that no two adjacent nodes have same color
   * Each color class is an independent set
   */
  std::vector<std::vector<size_t>> GreedyColoring();

  /**
   * DFS helper for topological sort
   */
  void TopologicalSortDFS(size_t node, 
                          std::unordered_set<size_t>& visited,
                          std::vector<size_t>& stack);
};

} // namespace janus
