#include "deptran/occ/validation_queue.h"
#include "deptran/occ/tx_enhanced.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace janus;

class ValidationQueueTest : public ::testing::Test {
protected:
  void SetUp() override {
    // No setup needed - we'll use dummy transaction pointers
  }

  void TearDown() override {
    // No teardown needed
  }

  TxOccEnhanced* CreateMockTx(txnid_t tid) {
    // Use a dummy pointer - ValidationQueue just stores pointers
    // In real usage, these would be actual TxOccEnhanced objects
    // but for queue testing, we just need unique addresses
    return reinterpret_cast<TxOccEnhanced*>(tid);
  }
};

TEST_F(ValidationQueueTest, BasicEnqueueDequeue) {
  ValidationQueue queue;

  auto tx1 = CreateMockTx(1);
  auto tx2 = CreateMockTx(2);

  // Enqueue two transactions
  queue.Enqueue(tx1);
  queue.Enqueue(tx2);

  // Verify queue size
  EXPECT_EQ(queue.Size(), 2);

  // Dequeue batch
  auto batch = queue.DequeueBatch(10, std::chrono::microseconds(100));

  // Verify we got both transactions
  EXPECT_EQ(batch.size(), 2);
  EXPECT_EQ(batch[0], tx1);
  EXPECT_EQ(batch[1], tx2);

  // Queue should be empty now
  EXPECT_TRUE(queue.Empty());

  // No cleanup needed for dummy pointers
}

TEST_F(ValidationQueueTest, SizeBatching) {
  ValidationQueue queue;

  std::vector<TxOccEnhanced*> txns;
  for (int i = 0; i < 5; i++) {
    auto tx = CreateMockTx(i);
    txns.push_back(tx);
    queue.Enqueue(tx);
  }

  // Request batch of max size 3 - should return immediately
  auto batch = queue.DequeueBatch(3, std::chrono::seconds(10));

  // Should get exactly 3 transactions (size limit)
  EXPECT_EQ(batch.size(), 3);
  EXPECT_EQ(queue.Size(), 2);  // 2 remaining

  // No cleanup needed for dummy pointers
}

TEST_F(ValidationQueueTest, TimeoutBatching) {
  ValidationQueue queue;

  auto tx = CreateMockTx(1);
  queue.Enqueue(tx);

  // Request batch of size 10, but we only have 1 transaction
  // Should timeout and return the 1 transaction we have
  auto start = std::chrono::steady_clock::now();
  auto batch = queue.DequeueBatch(10, std::chrono::milliseconds(50));
  auto elapsed = std::chrono::steady_clock::now() - start;

  // Should have returned 1 transaction
  EXPECT_EQ(batch.size(), 1);

  // Should have waited approximately 50ms
  EXPECT_GE(elapsed, std::chrono::milliseconds(40));  // Allow some slack
  EXPECT_LE(elapsed, std::chrono::milliseconds(100));

  // No cleanup needed for dummy pointers
}

TEST_F(ValidationQueueTest, ThreadSafety) {
  ValidationQueue queue;
  const int NUM_PRODUCERS = 4;
  const int TXS_PER_PRODUCER = 10;

  std::vector<std::thread> producers;

  // Create producer threads
  for (int p = 0; p < NUM_PRODUCERS; p++) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < TXS_PER_PRODUCER; i++) {
        auto tx = CreateMockTx(p * TXS_PER_PRODUCER + i);
        queue.Enqueue(tx);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    });
  }

  // Wait for all producers to finish
  for (auto& t : producers) t.join();

  // Verify all transactions are in queue
  EXPECT_EQ(queue.Size(), NUM_PRODUCERS * TXS_PER_PRODUCER);

  // Dequeue all
  auto batch = queue.DequeueBatch(1000, std::chrono::milliseconds(10));
  EXPECT_EQ(batch.size(), NUM_PRODUCERS * TXS_PER_PRODUCER);

  // No cleanup needed for dummy pointers
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
