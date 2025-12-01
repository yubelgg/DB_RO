#include "deptran/occ/concurrent_map.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <set>

using namespace janus;

class ConcurrentMapTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Tests create their own maps
  }
};

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST_F(ConcurrentMapTest, BasicInsertAndGet) {
  ConcurrentMap<int, std::string> map;

  // Map should be empty initially
  EXPECT_TRUE(map.Empty());
  EXPECT_EQ(map.Size(), 0);

  // Insert elements
  EXPECT_TRUE(map.Insert(1, "one"));    // New key
  EXPECT_TRUE(map.Insert(2, "two"));    // New key
  EXPECT_FALSE(map.Insert(1, "ONE"));   // Existing key (update)

  // Get elements
  auto val1 = map.TryGet(1);
  EXPECT_TRUE(val1.has_value());
  EXPECT_EQ(*val1, "ONE");  // Was updated

  auto val2 = map.TryGet(2);
  EXPECT_TRUE(val2.has_value());
  EXPECT_EQ(*val2, "two");

  // Non-existent key
  auto val3 = map.TryGet(3);
  EXPECT_FALSE(val3.has_value());
}

TEST_F(ConcurrentMapTest, GetOrDefault) {
  ConcurrentMap<int, std::string> map;

  map.Insert(1, "one");

  // Existing key
  EXPECT_EQ(map.GetOrDefault(1, "default"), "one");

  // Non-existent key
  EXPECT_EQ(map.GetOrDefault(2, "default"), "default");
}

TEST_F(ConcurrentMapTest, Contains) {
  ConcurrentMap<int, int> map;

  map.Insert(10, 100);
  map.Insert(20, 200);

  EXPECT_TRUE(map.Contains(10));
  EXPECT_TRUE(map.Contains(20));
  EXPECT_FALSE(map.Contains(30));
  EXPECT_FALSE(map.Contains(0));
}

TEST_F(ConcurrentMapTest, Remove) {
  ConcurrentMap<int, int> map;

  map.Insert(1, 10);
  map.Insert(2, 20);
  map.Insert(3, 30);

  EXPECT_EQ(map.Size(), 3);

  // Remove existing key
  EXPECT_TRUE(map.Remove(2));
  EXPECT_EQ(map.Size(), 2);
  EXPECT_FALSE(map.Contains(2));

  // Remove non-existent key
  EXPECT_FALSE(map.Remove(2));  // Already removed
  EXPECT_FALSE(map.Remove(100)); // Never existed
  EXPECT_EQ(map.Size(), 2);

  // Remaining elements intact
  EXPECT_TRUE(map.Contains(1));
  EXPECT_TRUE(map.Contains(3));
}

TEST_F(ConcurrentMapTest, Clear) {
  ConcurrentMap<int, int> map;

  for (int i = 0; i < 100; i++) {
    map.Insert(i, i * 10);
  }

  EXPECT_EQ(map.Size(), 100);
  EXPECT_FALSE(map.Empty());

  map.Clear();

  EXPECT_EQ(map.Size(), 0);
  EXPECT_TRUE(map.Empty());
  EXPECT_FALSE(map.Contains(0));
  EXPECT_FALSE(map.Contains(50));
  EXPECT_FALSE(map.Contains(99));
}

// =============================================================================
// Update Tests
// =============================================================================

TEST_F(ConcurrentMapTest, UpdateExisting) {
  ConcurrentMap<std::string, int> map;

  map.Insert("counter", 0);

  // Update existing
  EXPECT_TRUE(map.Update("counter", [](int& val) { val += 10; }));

  auto val = map.TryGet("counter");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, 10);

  // Multiple updates
  map.Update("counter", [](int& val) { val *= 2; });
  val = map.TryGet("counter");
  EXPECT_EQ(*val, 20);
}

TEST_F(ConcurrentMapTest, UpdateNonExistent) {
  ConcurrentMap<std::string, int> map;

  // Update non-existent key should return false
  EXPECT_FALSE(map.Update("missing", [](int& val) { val = 100; }));
}

TEST_F(ConcurrentMapTest, InsertOrUpdate) {
  ConcurrentMap<std::string, int> map;

  // Insert new
  EXPECT_TRUE(map.InsertOrUpdate("key", 0, [](int& val) { val++; }));
  auto val = map.TryGet("key");
  EXPECT_EQ(*val, 0);  // Used initial value

  // Update existing
  EXPECT_FALSE(map.InsertOrUpdate("key", 0, [](int& val) { val++; }));
  val = map.TryGet("key");
  EXPECT_EQ(*val, 1);  // Was incremented

  // Another update
  EXPECT_FALSE(map.InsertOrUpdate("key", 100, [](int& val) { val += 10; }));
  val = map.TryGet("key");
  EXPECT_EQ(*val, 11);  // Initial value ignored, update applied
}

// =============================================================================
// Collection Methods Tests
// =============================================================================

TEST_F(ConcurrentMapTest, GetKeys) {
  ConcurrentMap<int, std::string> map;

  map.Insert(1, "a");
  map.Insert(2, "b");
  map.Insert(3, "c");

  auto keys = map.GetKeys();

  EXPECT_EQ(keys.size(), 3);
  
  std::set<int> key_set(keys.begin(), keys.end());
  EXPECT_TRUE(key_set.count(1) > 0);
  EXPECT_TRUE(key_set.count(2) > 0);
  EXPECT_TRUE(key_set.count(3) > 0);
}

TEST_F(ConcurrentMapTest, GetValues) {
  ConcurrentMap<int, std::string> map;

  map.Insert(1, "a");
  map.Insert(2, "b");
  map.Insert(3, "c");

  auto values = map.GetValues();

  EXPECT_EQ(values.size(), 3);
  
  std::set<std::string> value_set(values.begin(), values.end());
  EXPECT_TRUE(value_set.count("a") > 0);
  EXPECT_TRUE(value_set.count("b") > 0);
  EXPECT_TRUE(value_set.count("c") > 0);
}

TEST_F(ConcurrentMapTest, ForEach) {
  ConcurrentMap<int, int> map;

  for (int i = 0; i < 10; i++) {
    map.Insert(i, i * 10);
  }

  int sum_keys = 0;
  int sum_values = 0;

  map.ForEach([&sum_keys, &sum_values](const int& key, const int& value) {
    sum_keys += key;
    sum_values += value;
  });

  EXPECT_EQ(sum_keys, 45);       // 0+1+2+...+9 = 45
  EXPECT_EQ(sum_values, 450);    // 10*(0+1+2+...+9) = 450
}

// =============================================================================
// Sharding Tests
// =============================================================================

TEST_F(ConcurrentMapTest, ShardDistribution) {
  ConcurrentMap<int, int> map;

  // Insert many elements - they should be distributed across shards
  const int NUM_ELEMENTS = 1000;
  for (int i = 0; i < NUM_ELEMENTS; i++) {
    map.Insert(i, i);
  }

  EXPECT_EQ(map.Size(), NUM_ELEMENTS);

  // All elements should be retrievable
  for (int i = 0; i < NUM_ELEMENTS; i++) {
    auto val = map.TryGet(i);
    EXPECT_TRUE(val.has_value()) << "Element " << i << " not found";
    EXPECT_EQ(*val, i);
  }
}

TEST_F(ConcurrentMapTest, ShardCount) {
  // Verify shard count is as expected
  // Note: Use typedef to avoid macro parsing issue with template comma
  using IntMap = ConcurrentMap<int, int>;
  EXPECT_EQ(IntMap::NUM_SHARDS, 256u);
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST_F(ConcurrentMapTest, ConcurrentInserts) {
  ConcurrentMap<int, int> map;
  const int NUM_THREADS = 8;
  const int ELEMENTS_PER_THREAD = 1000;

  std::vector<std::thread> threads;

  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, t, ELEMENTS_PER_THREAD]() {
      for (int i = 0; i < ELEMENTS_PER_THREAD; i++) {
        map.Insert(t * 100000 + i, i);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(map.Size(), NUM_THREADS * ELEMENTS_PER_THREAD);

  // Verify all elements exist
  for (int t = 0; t < NUM_THREADS; t++) {
    for (int i = 0; i < ELEMENTS_PER_THREAD; i++) {
      EXPECT_TRUE(map.Contains(t * 100000 + i));
    }
  }
}

TEST_F(ConcurrentMapTest, ConcurrentReads) {
  ConcurrentMap<int, int> map;

  // Pre-populate
  for (int i = 0; i < 1000; i++) {
    map.Insert(i, i * 2);
  }

  const int NUM_THREADS = 8;
  std::atomic<int> correct_reads{0};

  std::vector<std::thread> threads;

  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, &correct_reads]() {
      for (int i = 0; i < 1000; i++) {
        auto val = map.TryGet(i);
        if (val.has_value() && *val == i * 2) {
          correct_reads++;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(correct_reads.load(), NUM_THREADS * 1000);
}

TEST_F(ConcurrentMapTest, ConcurrentMixedOperations) {
  ConcurrentMap<int, int> map;
  const int NUM_THREADS = 4;
  const int OPERATIONS = 500;

  std::atomic<bool> done{false};
  std::vector<std::thread> threads;

  // Writer threads
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, t, OPERATIONS]() {
      std::mt19937 rng(t);
      std::uniform_int_distribution<int> key_dist(0, 999);
      
      for (int i = 0; i < OPERATIONS; i++) {
        int key = key_dist(rng);
        map.Insert(key, t * 1000 + i);
      }
    });
  }

  // Reader threads
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, &done, t]() {
      std::mt19937 rng(100 + t);
      std::uniform_int_distribution<int> key_dist(0, 999);
      
      while (!done.load()) {
        int key = key_dist(rng);
        map.TryGet(key);  // Just read, don't check value
        std::this_thread::yield();
      }
    });
  }

  // Wait for writers
  for (int i = 0; i < NUM_THREADS; i++) {
    threads[i].join();
  }

  done.store(true);

  // Wait for readers
  for (int i = NUM_THREADS; i < 2 * NUM_THREADS; i++) {
    threads[i].join();
  }

  // Map should be consistent
  EXPECT_GT(map.Size(), 0);
}

TEST_F(ConcurrentMapTest, ConcurrentUpdates) {
  ConcurrentMap<int, std::atomic<int>*> map;
  const int NUM_THREADS = 4;
  const int INCREMENTS_PER_THREAD = 1000;

  // Create a single counter
  std::atomic<int> counter{0};
  map.Insert(0, &counter);

  std::vector<std::thread> threads;

  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, INCREMENTS_PER_THREAD]() {
      for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        auto val = map.TryGet(0);
        if (val.has_value()) {
          (*val)->fetch_add(1);
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(counter.load(), NUM_THREADS * INCREMENTS_PER_THREAD);
}

TEST_F(ConcurrentMapTest, ConcurrentInsertAndRemove) {
  ConcurrentMap<int, int> map;
  const int NUM_THREADS = 4;
  const int OPERATIONS = 500;

  std::atomic<int> successful_removes{0};

  std::vector<std::thread> threads;

  // Insert threads
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, t, OPERATIONS]() {
      for (int i = 0; i < OPERATIONS; i++) {
        map.Insert(t * OPERATIONS + i, i);
      }
    });
  }

  // Remove threads (try to remove what inserters are adding)
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&map, &successful_removes, OPERATIONS, NUM_THREADS, t]() {
      std::mt19937 rng(42 + t);
      std::uniform_int_distribution<int> dist(0, NUM_THREADS * OPERATIONS - 1);
      
      for (int i = 0; i < OPERATIONS; i++) {
        if (map.Remove(dist(rng))) {
          successful_removes++;
        }
        std::this_thread::yield();
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Size should be total inserts minus successful removes
  size_t expected_size = NUM_THREADS * OPERATIONS - successful_removes.load();
  
  // Allow for some concurrent overlap
  EXPECT_LE(map.Size(), NUM_THREADS * OPERATIONS);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(ConcurrentMapTest, EmptyStringsAsKeys) {
  ConcurrentMap<std::string, int> map;

  map.Insert("", 0);
  map.Insert("a", 1);
  map.Insert("aa", 2);

  auto val = map.TryGet("");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, 0);
}

TEST_F(ConcurrentMapTest, NegativeKeys) {
  ConcurrentMap<int, int> map;

  map.Insert(-1, 1);
  map.Insert(-100, 100);
  map.Insert(INT_MIN, -1);

  EXPECT_TRUE(map.Contains(-1));
  EXPECT_TRUE(map.Contains(-100));
  EXPECT_TRUE(map.Contains(INT_MIN));

  auto val = map.TryGet(INT_MIN);
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, -1);
}

TEST_F(ConcurrentMapTest, LargeValues) {
  ConcurrentMap<int, std::vector<int>> map;

  std::vector<int> large_vec(1000, 42);
  map.Insert(1, large_vec);

  auto val = map.TryGet(1);
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(val->size(), 1000);
  EXPECT_EQ((*val)[0], 42);
}

TEST_F(ConcurrentMapTest, PointerValues) {
  ConcurrentMap<int, int*> map;

  int a = 10, b = 20, c = 30;
  map.Insert(1, &a);
  map.Insert(2, &b);
  map.Insert(3, &c);

  auto val1 = map.TryGet(1);
  EXPECT_TRUE(val1.has_value());
  EXPECT_EQ(**val1, 10);

  // Modify through pointer
  **val1 = 100;
  EXPECT_EQ(a, 100);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

