#include "conflict_graph.h"
#include <queue>
#include <algorithm>

namespace deptran {

ConflictGraph::ConflictGraph() = default;

ConflictGraph::~ConflictGraph() = default;

void ConflictGraph::add_transaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Initialize empty adjacency lists if not present
    if (outgoing_edges_.find(txn_id) == outgoing_edges_.end()) {
        outgoing_edges_[txn_id] = std::unordered_set<txn_id_t>();
        incoming_edges_[txn_id] = std::unordered_set<txn_id_t>();
    }
}

void ConflictGraph::add_conflict(txn_id_t from, txn_id_t to) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Ensure both nodes exist
    if (outgoing_edges_.find(from) == outgoing_edges_.end()) {
        outgoing_edges_[from] = std::unordered_set<txn_id_t>();
        incoming_edges_[from] = std::unordered_set<txn_id_t>();
    }
    if (outgoing_edges_.find(to) == outgoing_edges_.end()) {
        outgoing_edges_[to] = std::unordered_set<txn_id_t>();
        incoming_edges_[to] = std::unordered_set<txn_id_t>();
    }
    
    // Add the edge
    outgoing_edges_[from].insert(to);
    incoming_edges_[to].insert(from);
}

void ConflictGraph::remove_transaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove all outgoing edges
    if (outgoing_edges_.find(txn_id) != outgoing_edges_.end()) {
        for (txn_id_t neighbor : outgoing_edges_[txn_id]) {
            incoming_edges_[neighbor].erase(txn_id);
        }
        outgoing_edges_.erase(txn_id);
    }
    
    // Remove all incoming edges
    if (incoming_edges_.find(txn_id) != incoming_edges_.end()) {
        for (txn_id_t neighbor : incoming_edges_[txn_id]) {
            outgoing_edges_[neighbor].erase(txn_id);
        }
        incoming_edges_.erase(txn_id);
    }
}

bool ConflictGraph::would_create_cycle(txn_id_t from, txn_id_t to) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Adding edge from -> to creates a cycle if there's already a path from to -> from
    return has_path(to, from);
}

bool ConflictGraph::has_cycle() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::unordered_set<txn_id_t> visited;
    std::unordered_set<txn_id_t> rec_stack;
    
    // Check each connected component
    for (const auto& pair : outgoing_edges_) {
        txn_id_t node = pair.first;
        if (visited.find(node) == visited.end()) {
            if (has_cycle_util(node, visited, rec_stack)) {
                return true;
            }
        }
    }
    
    return false;
}

bool ConflictGraph::has_cycle_involving(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if txn_id exists in the graph
    if (outgoing_edges_.find(txn_id) == outgoing_edges_.end()) {
        return false;
    }
    
    std::unordered_set<txn_id_t> visited;
    std::unordered_set<txn_id_t> rec_stack;
    
    // Check if there's a cycle starting from this transaction
    return has_cycle_involving_util(txn_id, txn_id, visited, rec_stack);
}

std::unordered_set<txn_id_t> ConflictGraph::get_conflicts(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = outgoing_edges_.find(txn_id);
    if (it != outgoing_edges_.end()) {
        return it->second;
    }
    return std::unordered_set<txn_id_t>();
}

void ConflictGraph::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    outgoing_edges_.clear();
    incoming_edges_.clear();
}

size_t ConflictGraph::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outgoing_edges_.size();
}

bool ConflictGraph::has_cycle_util(txn_id_t node,
                                   std::unordered_set<txn_id_t>& visited,
                                   std::unordered_set<txn_id_t>& rec_stack) const {
    visited.insert(node);
    rec_stack.insert(node);
    
    // Check all neighbors
    auto it = outgoing_edges_.find(node);
    if (it != outgoing_edges_.end()) {
        for (txn_id_t neighbor : it->second) {
            // If neighbor not visited, recurse
            if (visited.find(neighbor) == visited.end()) {
                if (has_cycle_util(neighbor, visited, rec_stack)) {
                    return true;
                }
            }
            // If neighbor is in recursion stack, we found a cycle
            else if (rec_stack.find(neighbor) != rec_stack.end()) {
                return true;
            }
        }
    }
    
    rec_stack.erase(node);
    return false;
}

bool ConflictGraph::has_cycle_involving_util(txn_id_t node,
                                              txn_id_t target,
                                              std::unordered_set<txn_id_t>& visited,
                                              std::unordered_set<txn_id_t>& rec_stack) const {
    visited.insert(node);
    rec_stack.insert(node);
    
    // Check all neighbors
    auto it = outgoing_edges_.find(node);
    if (it != outgoing_edges_.end()) {
        for (txn_id_t neighbor : it->second) {
            // If we reach the target node and it's in the recursion stack,
            // we found a cycle involving the target
            if (neighbor == target && rec_stack.find(target) != rec_stack.end()) {
                return true;
            }
            
            // If neighbor not visited, recurse
            if (visited.find(neighbor) == visited.end()) {
                if (has_cycle_involving_util(neighbor, target, visited, rec_stack)) {
                    return true;
                }
            }
        }
    }
    
    rec_stack.erase(node);
    return false;
}

bool ConflictGraph::has_path(txn_id_t from, txn_id_t to) const {
    // BFS to find if there's a path from 'from' to 'to'
    if (from == to) {
        return true;
    }
    
    std::unordered_set<txn_id_t> visited;
    std::queue<txn_id_t> queue;
    
    queue.push(from);
    visited.insert(from);
    
    while (!queue.empty()) {
        txn_id_t current = queue.front();
        queue.pop();
        
        auto it = outgoing_edges_.find(current);
        if (it != outgoing_edges_.end()) {
            for (txn_id_t neighbor : it->second) {
                if (neighbor == to) {
                    return true;
                }
                
                if (visited.find(neighbor) == visited.end()) {
                    visited.insert(neighbor);
                    queue.push(neighbor);
                }
            }
        }
    }
    
    return false;
}

} // namespace deptran
