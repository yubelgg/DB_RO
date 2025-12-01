#include "deptran/occ/batch_validator.h"
#include "deptran/occ/batch_metadata.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace janus;

/**
 * BatchValidator Unit Tests
 *
 * Note: Full validation testing requires real TxOccEnhanced transactions
 * with valid mdb::TxnOCC internals, which is complex to set up.
 * Integration tests will cover the full validation flow.
 *
 * These unit tests focus on:
 * 1. Construction and destruction (thread pool lifecycle)
 * 2. Configuration accessors
 * 3. Empty batch handling
 * 4. BatchMetadata and BatchValidationResult structures
 */

class BatchValidatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Tests create their own validators
  }
};

// =============================================================================
// Construction/Destruction Tests
// =============================================================================

TEST_F(BatchValidatorTest, BasicConstruction) {
  // Create validator with default settings
  BatchValidator validator(32, 4);

  EXPECT_EQ(validator.GetBatchSize(), 32);
  EXPECT_EQ(validator.GetNumWorkers(), 4);
}

TEST_F(BatchValidatorTest, SingleWorker) {
  BatchValidator validator(16, 1);

  EXPECT_EQ(validator.GetBatchSize(), 16);
  EXPECT_EQ(validator.GetNumWorkers(), 1);
}

TEST_F(BatchValidatorTest, ZeroWorkers) {
  // Zero workers means serial validation only
  BatchValidator validator(32, 0);

  EXPECT_EQ(validator.GetBatchSize(), 32);
  EXPECT_EQ(validator.GetNumWorkers(), 0);
}

TEST_F(BatchValidatorTest, LargeBatchSize) {
  BatchValidator validator(1024, 8);

  EXPECT_EQ(validator.GetBatchSize(), 1024);
  EXPECT_EQ(validator.GetNumWorkers(), 8);
}

TEST_F(BatchValidatorTest, DestructorCleansUpThreads) {
  // Create and immediately destroy - should not hang or crash
  {
    BatchValidator validator(32, 4);
    // Let threads start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // Destructor should have joined all threads
  SUCCEED();
}

TEST_F(BatchValidatorTest, MultipleValidatorsCanExist) {
  // Multiple validators should work independently
  BatchValidator validator1(32, 2);
  BatchValidator validator2(64, 4);

  EXPECT_EQ(validator1.GetBatchSize(), 32);
  EXPECT_EQ(validator1.GetNumWorkers(), 2);

  EXPECT_EQ(validator2.GetBatchSize(), 64);
  EXPECT_EQ(validator2.GetNumWorkers(), 4);
}

// =============================================================================
// Empty Batch Tests
// =============================================================================

TEST_F(BatchValidatorTest, EmptyBatchReturnsEmptyResult) {
  BatchValidator validator(32, 4);

  std::vector<TxOccEnhanced*> empty_batch;
  auto result = validator.ValidateBatch(empty_batch);

  EXPECT_EQ(result.batch_size, 0);
  EXPECT_TRUE(result.passed.empty());
}

TEST_F(BatchValidatorTest, EmptyBatchValidationIsFast) {
  BatchValidator validator(32, 4);

  std::vector<TxOccEnhanced*> empty_batch;
  
  auto start = std::chrono::steady_clock::now();
  auto result = validator.ValidateBatch(empty_batch);
  auto end = std::chrono::steady_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  
  // Empty batch should return almost immediately (< 10ms)
  EXPECT_LT(duration.count(), 10);
}

// =============================================================================
// BatchMetadata Tests
// =============================================================================

TEST_F(BatchValidatorTest, BatchMetadataDefaultValues) {
  BatchMetadata meta;

  EXPECT_EQ(meta.batch_id, 0);
  EXPECT_EQ(meta.position_in_batch, 0);
  EXPECT_FALSE(meta.validated);
  EXPECT_FALSE(meta.passed);
  EXPECT_TRUE(meta.depends_on.empty());
  EXPECT_TRUE(meta.blocks.empty());
  EXPECT_EQ(meta.validation_promise, nullptr);
}

TEST_F(BatchValidatorTest, BatchMetadataReset) {
  BatchMetadata meta;
  
  // Set some values
  meta.batch_id = 42;
  meta.position_in_batch = 5;
  meta.validated = true;
  meta.passed = true;
  meta.depends_on.push_back(1);
  meta.depends_on.push_back(2);
  meta.blocks.push_back(3);
  meta.validation_promise = std::make_shared<std::promise<bool>>();

  // Verify values are set
  EXPECT_EQ(meta.batch_id, 42);
  EXPECT_EQ(meta.position_in_batch, 5);
  EXPECT_TRUE(meta.validated);
  EXPECT_TRUE(meta.passed);
  EXPECT_EQ(meta.depends_on.size(), 2);
  EXPECT_EQ(meta.blocks.size(), 1);
  EXPECT_NE(meta.validation_promise, nullptr);

  // Reset
  meta.Reset();

  // Verify reset to defaults
  EXPECT_EQ(meta.batch_id, 0);
  EXPECT_EQ(meta.position_in_batch, 0);
  EXPECT_FALSE(meta.validated);
  EXPECT_FALSE(meta.passed);
  EXPECT_TRUE(meta.depends_on.empty());
  EXPECT_TRUE(meta.blocks.empty());
  EXPECT_EQ(meta.validation_promise, nullptr);
}

TEST_F(BatchValidatorTest, BatchMetadataPromise) {
  BatchMetadata meta;
  
  // Create promise
  meta.validation_promise = std::make_shared<std::promise<bool>>();
  auto future = meta.validation_promise->get_future();

  // Set result from another "thread"
  meta.validation_promise->set_value(true);

  // Get result
  EXPECT_TRUE(future.get());
}

TEST_F(BatchValidatorTest, BatchMetadataTimestamps) {
  BatchMetadata meta;
  
  // Set timestamps
  meta.enqueue_time = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  meta.validation_start = std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  meta.validation_end = std::chrono::steady_clock::now();

  // Timestamps should be in order
  EXPECT_LT(meta.enqueue_time, meta.validation_start);
  EXPECT_LT(meta.validation_start, meta.validation_end);
}

// =============================================================================
// BatchValidationResult Tests
// =============================================================================

TEST_F(BatchValidatorTest, BatchValidationResultDefaultConstructor) {
  BatchValidationResult result;

  EXPECT_EQ(result.batch_id, 0);
  EXPECT_EQ(result.batch_size, 0);
  EXPECT_TRUE(result.passed.empty());
  EXPECT_EQ(result.total_time.count(), 0);
}

TEST_F(BatchValidatorTest, BatchValidationResultSizeConstructor) {
  BatchValidationResult result(10);

  EXPECT_EQ(result.batch_size, 10);
  EXPECT_EQ(result.passed.size(), 10);

  // All should be initialized to false
  for (bool p : result.passed) {
    EXPECT_FALSE(p);
  }
}

TEST_F(BatchValidatorTest, BatchValidationResultModification) {
  BatchValidationResult result(5);

  // Modify some results
  result.batch_id = 42;
  result.passed[0] = true;
  result.passed[2] = true;
  result.passed[4] = true;
  result.total_time = std::chrono::microseconds(1234);

  EXPECT_EQ(result.batch_id, 42);
  EXPECT_TRUE(result.passed[0]);
  EXPECT_FALSE(result.passed[1]);
  EXPECT_TRUE(result.passed[2]);
  EXPECT_FALSE(result.passed[3]);
  EXPECT_TRUE(result.passed[4]);
  EXPECT_EQ(result.total_time.count(), 1234);
}

// =============================================================================
// ConflictType Tests
// =============================================================================

TEST_F(BatchValidatorTest, ConflictTypeEnumValues) {
  // Verify enum values exist and are distinct
  EXPECT_NE(static_cast<int>(ConflictType::NONE), 
            static_cast<int>(ConflictType::READ_WRITE));
  EXPECT_NE(static_cast<int>(ConflictType::NONE), 
            static_cast<int>(ConflictType::WRITE_READ));
  EXPECT_NE(static_cast<int>(ConflictType::NONE), 
            static_cast<int>(ConflictType::WRITE_WRITE));
  EXPECT_NE(static_cast<int>(ConflictType::READ_WRITE), 
            static_cast<int>(ConflictType::WRITE_READ));
  EXPECT_NE(static_cast<int>(ConflictType::READ_WRITE), 
            static_cast<int>(ConflictType::WRITE_WRITE));
  EXPECT_NE(static_cast<int>(ConflictType::WRITE_READ), 
            static_cast<int>(ConflictType::WRITE_WRITE));
}

// =============================================================================
// Thread Pool Lifecycle Tests
// =============================================================================

TEST_F(BatchValidatorTest, ThreadPoolStartsWithConstruction) {
  // Create validator with workers
  BatchValidator validator(32, 4);

  // Give threads time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify workers exist (can only verify indirectly through operation)
  EXPECT_EQ(validator.GetNumWorkers(), 4);
}

TEST_F(BatchValidatorTest, RepeatedConstruction) {
  // Create and destroy validators repeatedly
  for (int i = 0; i < 5; i++) {
    BatchValidator validator(32, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  SUCCEED();  // No deadlocks or crashes
}

TEST_F(BatchValidatorTest, HighWorkerCount) {
  // Test with high worker count
  BatchValidator validator(64, 16);

  EXPECT_EQ(validator.GetNumWorkers(), 16);

  // Give threads time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Should complete destruction cleanly
}

// =============================================================================
// Configuration Tests
// =============================================================================

TEST_F(BatchValidatorTest, VariousBatchSizes) {
  std::vector<size_t> sizes = {1, 8, 16, 32, 64, 128, 256};

  for (size_t size : sizes) {
    BatchValidator validator(size, 1);
    EXPECT_EQ(validator.GetBatchSize(), size);
  }
}

TEST_F(BatchValidatorTest, VariousWorkerCounts) {
  std::vector<int> worker_counts = {0, 1, 2, 4, 8, 16};

  for (int workers : worker_counts) {
    BatchValidator validator(32, workers);
    EXPECT_EQ(validator.GetNumWorkers(), workers);
  }
}

// =============================================================================
// Integration with ValidationQueue (structure only)
// =============================================================================

TEST_F(BatchValidatorTest, BatchValidationResultCanBeReturned) {
  BatchValidator validator(32, 4);

  // Empty batch returns valid result
  std::vector<TxOccEnhanced*> batch;
  BatchValidationResult result = validator.ValidateBatch(batch);

  // Result is a valid object
  EXPECT_EQ(result.batch_size, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

