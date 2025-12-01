#include "deptran/occ/conflict_graph.h"
#include <gtest/gtest.h>
#include <algorithm>

using namespace janus;

/**
 * ConflictGraph Unit Tests
 *
 * Note: Testing Build() requires real TxOccEnhanced objects with valid
 * mdb::TxnOCC internals, which is complex to set up in unit tests.
 * The integration tests will cover the full Build() flow.
 *
 * These unit tests focus on:
 * 1. Empty graph behavior
 * 2. Clear() functionality
 * 3. Graph algorithm verification through a test helper subclass
 */

// Test helper subclass to access protected/private members for testing
class TestableConflictGraph : public ConflictGraph {
public:
  // Expose internal methods for testing graph algorithms
  void SetupTestGraph(size_t num_nodes,
                      const std::vector<std::pair<size_t, size_t>>& edges) {
    Clear();
    num_nodes_ = num_nodes;
    
    // Add all edges
    for (const auto& edge : edges) {
      if (edge.first < num_nodes && edge.second < num_nodes) {
        adj_list_[edge.first].insert(edge.second);
        adj_list_[edge.second].insert(edge.first);
        num_edges_++;
      }
    }
  }
  
  // Access to check internal state
  size_t GetNumNodes() const { return num_nodes_; }
  size_t GetNumEdges() const { return num_edges_; }
};

class ConflictGraphTest : public ::testing::Test {
protected:
  void SetUp() override {
    graph = new TestableConflictGraph();
  }

  void TearDown() override {
    delete graph;
  }

  TestableConflictGraph* graph;
};

// =============================================================================
// Empty Graph Tests
// =============================================================================

TEST_F(ConflictGraphTest, EmptyGraph) {
  // Empty graph
  EXPECT_EQ(graph->NumNodes(), 0);
  EXPECT_EQ(graph->NumEdges(), 0);

  // FindIndependentSets on empty graph should return empty vector
  auto sets = graph->FindIndependentSets();
  EXPECT_TRUE(sets.empty());

  // TopologicalSort on empty graph should return empty vector
  auto order = graph->TopologicalSort();
  EXPECT_TRUE(order.empty());
}

TEST_F(ConflictGraphTest, ClearGraph) {
  // Setup a simple graph
  graph->SetupTestGraph(3, {{0, 1}, {1, 2}});
  EXPECT_EQ(graph->NumNodes(), 3);
  EXPECT_EQ(graph->NumEdges(), 2);

  // Clear the graph
  graph->Clear();
  EXPECT_EQ(graph->NumNodes(), 0);
  EXPECT_EQ(graph->NumEdges(), 0);

  // Operations on cleared graph should work
  auto sets = graph->FindIndependentSets();
  EXPECT_TRUE(sets.empty());

  auto order = graph->TopologicalSort();
  EXPECT_TRUE(order.empty());
}

// =============================================================================
// HasConflict and GetConflicts Tests
// =============================================================================

TEST_F(ConflictGraphTest, HasConflictSimple) {
  // Graph: 0 -- 1 -- 2
  graph->SetupTestGraph(3, {{0, 1}, {1, 2}});

  // Direct edges
  EXPECT_TRUE(graph->HasConflict(0, 1));
  EXPECT_TRUE(graph->HasConflict(1, 0));  // Symmetric
  EXPECT_TRUE(graph->HasConflict(1, 2));
  EXPECT_TRUE(graph->HasConflict(2, 1));  // Symmetric

  // No direct edge between 0 and 2
  EXPECT_FALSE(graph->HasConflict(0, 2));
  EXPECT_FALSE(graph->HasConflict(2, 0));

  // Non-existent node
  EXPECT_FALSE(graph->HasConflict(0, 10));
  EXPECT_FALSE(graph->HasConflict(10, 0));
}

TEST_F(ConflictGraphTest, GetConflicts) {
  // Graph: 0 -- 1, 0 -- 2, 1 -- 2 (triangle)
  graph->SetupTestGraph(3, {{0, 1}, {0, 2}, {1, 2}});

  auto conflicts0 = graph->GetConflicts(0);
  EXPECT_EQ(conflicts0.size(), 2);
  EXPECT_TRUE(conflicts0.count(1) > 0);
  EXPECT_TRUE(conflicts0.count(2) > 0);

  auto conflicts1 = graph->GetConflicts(1);
  EXPECT_EQ(conflicts1.size(), 2);
  EXPECT_TRUE(conflicts1.count(0) > 0);
  EXPECT_TRUE(conflicts1.count(2) > 0);

  // Non-existent node returns empty set
  auto conflicts10 = graph->GetConflicts(10);
  EXPECT_TRUE(conflicts10.empty());
}

// =============================================================================
// Independent Sets Tests
// =============================================================================

TEST_F(ConflictGraphTest, SingleNodeIndependentSet) {
  // Single node graph
  graph->SetupTestGraph(1, {});

  auto sets = graph->FindIndependentSets();
  EXPECT_EQ(sets.size(), 1);
  EXPECT_EQ(sets[0].size(), 1);
  EXPECT_EQ(sets[0][0], 0);
}

TEST_F(ConflictGraphTest, TwoIndependentNodes) {
  // Two nodes, no edges
  graph->SetupTestGraph(2, {});

  auto sets = graph->FindIndependentSets();
  // Both nodes can be in the same independent set (same color)
  EXPECT_EQ(sets.size(), 1);
  EXPECT_EQ(sets[0].size(), 2);
}

TEST_F(ConflictGraphTest, TwoConnectedNodes) {
  // Two nodes with an edge
  graph->SetupTestGraph(2, {{0, 1}});

  auto sets = graph->FindIndependentSets();
  // Need two independent sets (different colors)
  EXPECT_EQ(sets.size(), 2);
  EXPECT_EQ(sets[0].size(), 1);
  EXPECT_EQ(sets[1].size(), 1);
}

TEST_F(ConflictGraphTest, LinearGraph) {
  // Linear graph: 0 -- 1 -- 2 -- 3
  graph->SetupTestGraph(4, {{0, 1}, {1, 2}, {2, 3}});

  auto sets = graph->FindIndependentSets();
  // Graph is bipartite: needs 2 colors
  EXPECT_EQ(sets.size(), 2);

  // Verify each set is actually independent
  for (const auto& set : sets) {
    for (size_t i = 0; i < set.size(); i++) {
      for (size_t j = i + 1; j < set.size(); j++) {
        EXPECT_FALSE(graph->HasConflict(set[i], set[j]))
            << "Nodes " << set[i] << " and " << set[j]
            << " in same independent set but have conflict";
      }
    }
  }
}

TEST_F(ConflictGraphTest, TriangleGraph) {
  // Triangle: 0 -- 1, 0 -- 2, 1 -- 2
  graph->SetupTestGraph(3, {{0, 1}, {0, 2}, {1, 2}});

  auto sets = graph->FindIndependentSets();
  // Triangle needs 3 colors (chromatic number = 3)
  EXPECT_EQ(sets.size(), 3);

  // Each set should have exactly 1 node
  for (const auto& set : sets) {
    EXPECT_EQ(set.size(), 1);
  }
}

TEST_F(ConflictGraphTest, StarGraph) {
  // Star graph: 0 connected to 1, 2, 3, 4 (center is 0)
  graph->SetupTestGraph(5, {{0, 1}, {0, 2}, {0, 3}, {0, 4}});

  auto sets = graph->FindIndependentSets();
  // Star is bipartite: center vs leaves
  EXPECT_EQ(sets.size(), 2);

  // One set should have 4 nodes (leaves), other should have 1 (center)
  size_t sizes_sum = 0;
  for (const auto& set : sets) {
    sizes_sum += set.size();
  }
  EXPECT_EQ(sizes_sum, 5);
}

TEST_F(ConflictGraphTest, DisconnectedGraph) {
  // Two separate edges: 0 -- 1, 2 -- 3
  graph->SetupTestGraph(4, {{0, 1}, {2, 3}});

  auto sets = graph->FindIndependentSets();
  // Two colors suffice for two disconnected edges
  EXPECT_EQ(sets.size(), 2);

  // Total nodes should be 4
  size_t total_nodes = 0;
  for (const auto& set : sets) {
    total_nodes += set.size();
  }
  EXPECT_EQ(total_nodes, 4);
}

// =============================================================================
// Topological Sort Tests
// =============================================================================

TEST_F(ConflictGraphTest, TopologicalSortSingleNode) {
  graph->SetupTestGraph(1, {});

  auto order = graph->TopologicalSort();
  EXPECT_EQ(order.size(), 1);
  EXPECT_EQ(order[0], 0);
}

TEST_F(ConflictGraphTest, TopologicalSortLinear) {
  // Linear graph: 0 -- 1 -- 2
  graph->SetupTestGraph(3, {{0, 1}, {1, 2}});

  auto order = graph->TopologicalSort();
  EXPECT_EQ(order.size(), 3);

  // All nodes should be present
  std::unordered_set<size_t> node_set(order.begin(), order.end());
  EXPECT_TRUE(node_set.count(0) > 0);
  EXPECT_TRUE(node_set.count(1) > 0);
  EXPECT_TRUE(node_set.count(2) > 0);
}

TEST_F(ConflictGraphTest, TopologicalSortDisconnected) {
  // Two separate components
  graph->SetupTestGraph(4, {{0, 1}, {2, 3}});

  auto order = graph->TopologicalSort();
  EXPECT_EQ(order.size(), 4);

  // All nodes should be present exactly once
  std::unordered_set<size_t> node_set(order.begin(), order.end());
  EXPECT_EQ(node_set.size(), 4);
}

TEST_F(ConflictGraphTest, TopologicalSortAllNodesVisited) {
  // Complete graph K4
  graph->SetupTestGraph(4, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}});

  auto order = graph->TopologicalSort();
  EXPECT_EQ(order.size(), 4);

  // All nodes present
  std::unordered_set<size_t> node_set(order.begin(), order.end());
  for (size_t i = 0; i < 4; i++) {
    EXPECT_TRUE(node_set.count(i) > 0) << "Node " << i << " missing from order";
  }
}

// =============================================================================
// Combined Tests
// =============================================================================

TEST_F(ConflictGraphTest, IndependentSetsCoversAllNodes) {
  // Complex graph
  graph->SetupTestGraph(6, {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}});

  auto sets = graph->FindIndependentSets();

  // Count total nodes in all sets
  std::unordered_set<size_t> all_nodes;
  for (const auto& set : sets) {
    for (size_t node : set) {
      all_nodes.insert(node);
    }
  }

  EXPECT_EQ(all_nodes.size(), 6) << "Not all nodes covered by independent sets";
}

TEST_F(ConflictGraphTest, ValidIndependentSetsNoConflicts) {
  // Larger test: 8 nodes with various edges
  graph->SetupTestGraph(8, {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {4, 5}, {5, 6}, {6, 7}, {3, 4}});

  auto sets = graph->FindIndependentSets();

  // Verify each set is truly independent
  for (size_t setIdx = 0; setIdx < sets.size(); setIdx++) {
    const auto& set = sets[setIdx];
    for (size_t i = 0; i < set.size(); i++) {
      for (size_t j = i + 1; j < set.size(); j++) {
        EXPECT_FALSE(graph->HasConflict(set[i], set[j]))
            << "Set " << setIdx << ": Nodes " << set[i] << " and " << set[j]
            << " have conflict but are in same independent set";
      }
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
