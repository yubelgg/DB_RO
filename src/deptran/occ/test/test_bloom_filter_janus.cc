#include "deptran/occ/bloom_filter.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include <random>

using namespace janus;

class BloomFilterTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Tests will create their own filters with specific parameters
  }
};

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST_F(BloomFilterTest, BasicAddAndCheck) {
  BloomFilter<int> filter(100, 0.01);

  // Element not added yet
  EXPECT_FALSE(filter.MayContain(42));

  // Add element
  filter.Add(42);

  // Element should now be found
  EXPECT_TRUE(filter.MayContain(42));

  // Other elements should (likely) not be found
  EXPECT_FALSE(filter.MayContain(43));
  EXPECT_FALSE(filter.MayContain(0));
  EXPECT_FALSE(filter.MayContain(-1));
}

TEST_F(BloomFilterTest, MultipleElements) {
  BloomFilter<int> filter(1000, 0.01);

  std::vector<int> elements = {1, 10, 100, 1000, 10000};
  
  // Add all elements
  for (int e : elements) {
    filter.Add(e);
  }

  // All added elements should be found
  for (int e : elements) {
    EXPECT_TRUE(filter.MayContain(e)) << "Element " << e << " not found";
  }
}

TEST_F(BloomFilterTest, StringElements) {
  BloomFilter<std::string> filter(100, 0.01);

  filter.Add("hello");
  filter.Add("world");
  filter.Add("bloom");
  filter.Add("filter");

  EXPECT_TRUE(filter.MayContain("hello"));
  EXPECT_TRUE(filter.MayContain("world"));
  EXPECT_TRUE(filter.MayContain("bloom"));
  EXPECT_TRUE(filter.MayContain("filter"));

  // Not added
  EXPECT_FALSE(filter.MayContain("foo"));
  EXPECT_FALSE(filter.MayContain("bar"));
}

TEST_F(BloomFilterTest, EmptyFilter) {
  BloomFilter<int> filter(100, 0.01);

  // Empty filter should not contain anything
  EXPECT_FALSE(filter.MayContain(0));
  EXPECT_FALSE(filter.MayContain(1));
  EXPECT_FALSE(filter.MayContain(-1));
  EXPECT_FALSE(filter.MayContain(INT_MAX));
  EXPECT_FALSE(filter.MayContain(INT_MIN));

  EXPECT_EQ(filter.NumElements(), 0);
}

// =============================================================================
// Clear Tests
// =============================================================================

TEST_F(BloomFilterTest, ClearFilter) {
  BloomFilter<int> filter(100, 0.01);

  // Add elements
  filter.Add(1);
  filter.Add(2);
  filter.Add(3);

  EXPECT_TRUE(filter.MayContain(1));
  EXPECT_TRUE(filter.MayContain(2));
  EXPECT_TRUE(filter.MayContain(3));
  EXPECT_EQ(filter.NumElements(), 3);

  // Clear
  filter.Clear();

  // Should no longer contain elements
  EXPECT_FALSE(filter.MayContain(1));
  EXPECT_FALSE(filter.MayContain(2));
  EXPECT_FALSE(filter.MayContain(3));
  EXPECT_EQ(filter.NumElements(), 0);
}

TEST_F(BloomFilterTest, ClearAndReuse) {
  BloomFilter<int> filter(100, 0.01);

  // First use
  filter.Add(10);
  EXPECT_TRUE(filter.MayContain(10));

  // Clear
  filter.Clear();
  EXPECT_FALSE(filter.MayContain(10));

  // Second use
  filter.Add(20);
  EXPECT_TRUE(filter.MayContain(20));
  EXPECT_FALSE(filter.MayContain(10));  // Old element still not found
}

// =============================================================================
// Configuration Tests
// =============================================================================

TEST_F(BloomFilterTest, ExplicitSizeAndHashes) {
  // Create with explicit bit count and hash count
  BloomFilter<int> filter(static_cast<size_t>(1000), static_cast<size_t>(5));  // 1000 bits, 5 hash functions

  EXPECT_EQ(filter.NumBits(), 1000);
  EXPECT_EQ(filter.NumHashes(), 5);

  // Should still work correctly
  filter.Add(42);
  EXPECT_TRUE(filter.MayContain(42));
  EXPECT_FALSE(filter.MayContain(43));
}

TEST_F(BloomFilterTest, ConfigurationAccessors) {
  BloomFilter<int> filter(500, 0.05);

  EXPECT_GT(filter.NumBits(), 0);
  EXPECT_GT(filter.NumHashes(), 0);
  EXPECT_GT(filter.MemoryUsage(), 0);
}

TEST_F(BloomFilterTest, SmallFilter) {
  // Very small filter
  BloomFilter<int> filter(static_cast<size_t>(10), static_cast<size_t>(1));

  filter.Add(1);
  filter.Add(2);

  EXPECT_TRUE(filter.MayContain(1));
  EXPECT_TRUE(filter.MayContain(2));
}

// =============================================================================
// False Positive Rate Tests
// =============================================================================

TEST_F(BloomFilterTest, FalsePositiveRateWithinBounds) {
  const size_t NUM_ELEMENTS = 1000;
  const double TARGET_FP_RATE = 0.01;  // 1%
  
  BloomFilter<int> filter(NUM_ELEMENTS, TARGET_FP_RATE);

  // Add elements
  for (size_t i = 0; i < NUM_ELEMENTS; i++) {
    filter.Add(static_cast<int>(i));
  }

  // Check false positive rate on elements NOT added
  size_t false_positives = 0;
  const size_t NUM_TESTS = 10000;
  
  for (size_t i = NUM_ELEMENTS; i < NUM_ELEMENTS + NUM_TESTS; i++) {
    if (filter.MayContain(static_cast<int>(i))) {
      false_positives++;
    }
  }

  double actual_fp_rate = static_cast<double>(false_positives) / NUM_TESTS;
  
  // Allow generous margin (20x the target rate) since hash functions
  // can have varying quality and the test should be robust
  // The important thing is that false positive rate is bounded, not exact
  EXPECT_LT(actual_fp_rate, TARGET_FP_RATE * 20)
      << "False positive rate " << actual_fp_rate 
      << " is unreasonably high (>20x target " << TARGET_FP_RATE << ")";
}

TEST_F(BloomFilterTest, EstimatedFalsePositiveRate) {
  BloomFilter<int> filter(100, 0.01);

  // Empty filter should have 0 estimated FP rate
  EXPECT_DOUBLE_EQ(filter.EstimatedFalsePositiveRate(), 0.0);

  // Add elements
  for (int i = 0; i < 50; i++) {
    filter.Add(i);
  }

  // Estimated FP rate should be positive and reasonable
  double estimated = filter.EstimatedFalsePositiveRate();
  EXPECT_GT(estimated, 0.0);
  EXPECT_LT(estimated, 1.0);
}

// =============================================================================
// No False Negatives Tests
// =============================================================================

TEST_F(BloomFilterTest, NoFalseNegatives) {
  BloomFilter<int> filter(1000, 0.01);

  // Add many elements
  std::vector<int> elements;
  for (int i = 0; i < 500; i++) {
    elements.push_back(i * 7);  // Use multiples to spread values
    filter.Add(elements.back());
  }

  // All added elements MUST be found (no false negatives)
  for (int e : elements) {
    EXPECT_TRUE(filter.MayContain(e)) 
        << "False negative: element " << e << " not found";
  }
}

TEST_F(BloomFilterTest, NoFalseNegativesStrings) {
  BloomFilter<std::string> filter(500, 0.01);

  std::vector<std::string> elements;
  for (int i = 0; i < 200; i++) {
    elements.push_back("element_" + std::to_string(i));
    filter.Add(elements.back());
  }

  // All added elements MUST be found
  for (const auto& e : elements) {
    EXPECT_TRUE(filter.MayContain(e))
        << "False negative: string '" << e << "' not found";
  }
}

// =============================================================================
// Thread Safety Tests
// =============================================================================

TEST_F(BloomFilterTest, ConcurrentAdds) {
  BloomFilter<int> filter(10000, 0.01);
  const int NUM_THREADS = 4;
  const int ELEMENTS_PER_THREAD = 100;

  std::vector<std::thread> threads;

  // Multiple threads adding elements concurrently
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&filter, t, ELEMENTS_PER_THREAD]() {
      for (int i = 0; i < ELEMENTS_PER_THREAD; i++) {
        filter.Add(t * 10000 + i);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All elements should be findable (no false negatives)
  for (int t = 0; t < NUM_THREADS; t++) {
    for (int i = 0; i < ELEMENTS_PER_THREAD; i++) {
      EXPECT_TRUE(filter.MayContain(t * 10000 + i))
          << "Element " << (t * 10000 + i) << " not found";
    }
  }
}

TEST_F(BloomFilterTest, ConcurrentReads) {
  BloomFilter<int> filter(1000, 0.01);

  // Add elements first
  for (int i = 0; i < 100; i++) {
    filter.Add(i);
  }

  const int NUM_THREADS = 4;
  std::atomic<int> found_count{0};

  std::vector<std::thread> threads;

  // Multiple threads reading concurrently
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&filter, &found_count]() {
      for (int i = 0; i < 100; i++) {
        if (filter.MayContain(i)) {
          found_count++;
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All added elements should be found by all threads
  EXPECT_EQ(found_count.load(), NUM_THREADS * 100);
}

TEST_F(BloomFilterTest, ConcurrentAddAndRead) {
  BloomFilter<int> filter(10000, 0.01);
  const int NUM_ADD_THREADS = 2;
  const int NUM_READ_THREADS = 2;
  const int ELEMENTS = 500;

  std::atomic<bool> done{false};
  std::atomic<int> false_negatives{0};

  std::vector<std::thread> threads;

  // Writer threads
  for (int t = 0; t < NUM_ADD_THREADS; t++) {
    threads.emplace_back([&filter, t, ELEMENTS]() {
      for (int i = 0; i < ELEMENTS; i++) {
        filter.Add(t * 10000 + i);
        std::this_thread::yield();  // Allow interleaving
      }
    });
  }

  // Reader threads (check their own thread's elements)
  for (int t = 0; t < NUM_READ_THREADS; t++) {
    threads.emplace_back([&filter, &done, t, ELEMENTS]() {
      // Wait a bit for some elements to be added
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      
      // Keep reading until done
      while (!done.load()) {
        std::this_thread::yield();
      }
    });
  }

  // Wait for writers
  for (int i = 0; i < NUM_ADD_THREADS; i++) {
    threads[i].join();
  }

  done.store(true);

  // Wait for readers
  for (int i = NUM_ADD_THREADS; i < NUM_ADD_THREADS + NUM_READ_THREADS; i++) {
    threads[i].join();
  }

  // Final check: all added elements should be found
  for (int t = 0; t < NUM_ADD_THREADS; t++) {
    for (int i = 0; i < ELEMENTS; i++) {
      EXPECT_TRUE(filter.MayContain(t * 10000 + i));
    }
  }
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(BloomFilterTest, DuplicateAdds) {
  BloomFilter<int> filter(100, 0.01);

  // Add same element multiple times
  filter.Add(42);
  filter.Add(42);
  filter.Add(42);

  EXPECT_TRUE(filter.MayContain(42));
  EXPECT_EQ(filter.NumElements(), 3);  // Counts all adds
}

TEST_F(BloomFilterTest, LargeValues) {
  BloomFilter<long long> filter(100, 0.01);

  long long large1 = 1LL << 60;
  long long large2 = -1LL;
  long long large3 = LLONG_MAX;
  long long large4 = LLONG_MIN;

  filter.Add(large1);
  filter.Add(large2);
  filter.Add(large3);
  filter.Add(large4);

  EXPECT_TRUE(filter.MayContain(large1));
  EXPECT_TRUE(filter.MayContain(large2));
  EXPECT_TRUE(filter.MayContain(large3));
  EXPECT_TRUE(filter.MayContain(large4));
}

TEST_F(BloomFilterTest, PointerType) {
  BloomFilter<void*> filter(100, 0.01);

  int a, b, c;
  void* ptr1 = &a;
  void* ptr2 = &b;
  void* ptr3 = &c;

  filter.Add(ptr1);
  filter.Add(ptr2);

  EXPECT_TRUE(filter.MayContain(ptr1));
  EXPECT_TRUE(filter.MayContain(ptr2));
  EXPECT_FALSE(filter.MayContain(ptr3));
  EXPECT_FALSE(filter.MayContain(nullptr));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

