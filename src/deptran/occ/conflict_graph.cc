#include "conflict_graph.h"
#include "../memdb/txn_occ.h"
#include "../memdb/row.h"
#include "base/all.hpp"
#include <algorithm>
#include <queue>

namespace janus {

ConflictGraph::ConflictGraph() 
    : num_nodes_(0), num_edges_(0) {
}

ConflictGraph::~ConflictGraph() {
  Clear();
}

void ConflictGraph::Build(const std::vector<TxOccEnhanced*>& transactions) {
  Clear();
  
  if (transactions.empty()) {
    return;
  }
  
  transactions_ = transactions;
  num_nodes_ = transactions.size();
  
  // Extract access sets for all transactions
  access_sets_.resize(num_nodes_);
  for (size_t i = 0; i < num_nodes_; i++) {
    ExtractAccessSets(transactions_[i], access_sets_[i]);
  }
  
  // Build edges by checking conflicts between all pairs
  for (size_t i = 0; i < num_nodes_; i++) {
    for (size_t j = i + 1; j < num_nodes_; j++) {
      ConflictType conflict = CheckConflictType(i, j);
      if (conflict != ConflictType::NONE) {
        AddEdge(i, j);
      }
    }
  }
  
  Log_debug("ConflictGraph built: %zu nodes, %zu edges", num_nodes_, num_edges_);
}

void ConflictGraph::ExtractAccessSets(TxOccEnhanced* tx, AccessSets& sets) {
  auto txn = dynamic_cast<mdb::TxnOCC*>(tx->mdb_txn());
  if (!txn) {
    Log_warn("Failed to cast to TxnOCC in ConflictGraph::ExtractAccessSets");
    return;
  }
  
  // Collect reads
  for (auto& it : txn->ver_check_read_) {
    sets.read_set.insert(it.first.row);
  }
  
  // Collect writes
  for (auto& it : txn->updates_) {
    sets.write_set.insert(it.first);
  }
  
  Log_debug("Extracted access sets for tx %" PRIx64 ": %zu reads, %zu writes",
            tx->tid_, sets.read_set.size(), sets.write_set.size());
}

ConflictType ConflictGraph::CheckConflictType(size_t tx1_idx, size_t tx2_idx) {
  if (tx1_idx >= access_sets_.size() || tx2_idx >= access_sets_.size()) {
    return ConflictType::NONE;
  }
  
  const AccessSets& sets1 = access_sets_[tx1_idx];
  const AccessSets& sets2 = access_sets_[tx2_idx];
  
  // Check for write-write conflicts (highest priority)
  for (Row* row : sets1.write_set) {
    if (sets2.write_set.count(row) > 0) {
      return ConflictType::WRITE_WRITE;
    }
  }
  
  // Check for write-read conflicts (T1 writes, T2 reads)
  for (Row* row : sets1.write_set) {
    if (sets2.read_set.count(row) > 0) {
      return ConflictType::WRITE_READ;
    }
  }
  
  // Check for read-write conflicts (T1 reads, T2 writes)
  for (Row* row : sets1.read_set) {
    if (sets2.write_set.count(row) > 0) {
      return ConflictType::READ_WRITE;
    }
  }
  
  return ConflictType::NONE;
}

void ConflictGraph::AddEdge(size_t tx1_idx, size_t tx2_idx) {
  // Add bidirectional edge
  adj_list_[tx1_idx].insert(tx2_idx);
  adj_list_[tx2_idx].insert(tx1_idx);
  num_edges_++;
  
  Log_debug("Added conflict edge: %zu <-> %zu", tx1_idx, tx2_idx);
}

bool ConflictGraph::HasConflict(size_t tx1_idx, size_t tx2_idx) const {
  auto it = adj_list_.find(tx1_idx);
  if (it == adj_list_.end()) {
    return false;
  }
  return it->second.count(tx2_idx) > 0;
}

std::unordered_set<size_t> ConflictGraph::GetConflicts(size_t tx_idx) const {
  auto it = adj_list_.find(tx_idx);
  if (it == adj_list_.end()) {
    return std::unordered_set<size_t>();
  }
  return it->second;
}

std::vector<std::vector<size_t>> ConflictGraph::FindIndependentSets() {
  if (num_nodes_ == 0) {
    return {};
  }
  
  // Use greedy graph coloring to find independent sets
  return GreedyColoring();
}

std::vector<std::vector<size_t>> ConflictGraph::GreedyColoring() {
  // Color assignment for each node
  std::vector<int> colors(num_nodes_, -1);
  
  // Color nodes greedily
  for (size_t node = 0; node < num_nodes_; node++) {
    // Find colors used by neighbors
    std::unordered_set<int> neighbor_colors;
    auto it = adj_list_.find(node);
    if (it != adj_list_.end()) {
      for (size_t neighbor : it->second) {
        if (colors[neighbor] != -1) {
          neighbor_colors.insert(colors[neighbor]);
        }
      }
    }
    
    // Assign smallest available color
    int color = 0;
    while (neighbor_colors.count(color) > 0) {
      color++;
    }
    colors[node] = color;
  }
  
  // Group nodes by color (each color is an independent set)
  std::unordered_map<int, std::vector<size_t>> color_groups;
  for (size_t node = 0; node < num_nodes_; node++) {
    color_groups[colors[node]].push_back(node);
  }
  
  // Convert to vector of independent sets
  std::vector<std::vector<size_t>> independent_sets;
  for (auto& pair : color_groups) {
    independent_sets.push_back(std::move(pair.second));
  }
  
  // Sort by size (largest first) for better load balancing
  std::sort(independent_sets.begin(), independent_sets.end(),
            [](const std::vector<size_t>& a, const std::vector<size_t>& b) {
              return a.size() > b.size();
            });
  
  Log_debug("GreedyColoring found %zu independent sets", independent_sets.size());
  for (size_t i = 0; i < independent_sets.size(); i++) {
    Log_debug("  Set %zu: %zu transactions", i, independent_sets[i].size());
  }
  
  return independent_sets;
}

std::vector<size_t> ConflictGraph::TopologicalSort() {
  std::vector<size_t> result;
  std::unordered_set<size_t> visited;
  
  // DFS from each unvisited node
  for (size_t node = 0; node < num_nodes_; node++) {
    if (visited.count(node) == 0) {
      TopologicalSortDFS(node, visited, result);
    }
  }
  
  // Reverse to get correct topological order
  std::reverse(result.begin(), result.end());
  
  Log_debug("TopologicalSort produced order of %zu nodes", result.size());
  
  return result;
}

void ConflictGraph::TopologicalSortDFS(size_t node,
                                        std::unordered_set<size_t>& visited,
                                        std::vector<size_t>& stack) {
  visited.insert(node);
  
  // Visit all neighbors
  auto it = adj_list_.find(node);
  if (it != adj_list_.end()) {
    for (size_t neighbor : it->second) {
      if (visited.count(neighbor) == 0) {
        TopologicalSortDFS(neighbor, visited, stack);
      }
    }
  }
  
  // Push node to stack after visiting all neighbors
  stack.push_back(node);
}

void ConflictGraph::Clear() {
  num_nodes_ = 0;
  num_edges_ = 0;
  adj_list_.clear();
  transactions_.clear();
  access_sets_.clear();
}

} // namespace janus
