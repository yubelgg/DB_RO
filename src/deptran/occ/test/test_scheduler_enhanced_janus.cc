#include "deptran/occ/scheduler_enhanced.h"
#include "deptran/occ/early_abort_detector.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace janus;

/**
 * SchedulerOccEnhanced Unit Tests
 *
 * Note: Full SchedulerOccEnhanced testing requires the Config singleton
 * to be initialized, which requires a complex setup. These tests focus on
 * the EarlyAbortDetector component which can be tested independently.
 *
 * Integration tests (run with labtest) will cover the full scheduler flow.
 */

class SchedulerEnhancedTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create a standalone detector for testing
    detector = new EarlyAbortDetector();
    detector->SetEnabled(true);
  }

  void TearDown() override {
    delete detector;
  }

  EarlyAbortDetector* detector;
};

// =============================================================================
// EarlyAbortDetector Tests (component of SchedulerEnhanced)
// =============================================================================

TEST_F(SchedulerEnhancedTest, DetectorInitialState) {
  EXPECT_TRUE(detector->IsEnabled());
  
  const auto& stats = detector->GetStats();
  EXPECT_EQ(stats.total_reads.load(), 0);
  EXPECT_EQ(stats.total_writes.load(), 0);
  EXPECT_EQ(stats.early_aborts_detected.load(), 0);
  EXPECT_EQ(stats.version_changes_processed.load(), 0);
}

TEST_F(SchedulerEnhancedTest, DetectorEnableDisable) {
  EXPECT_TRUE(detector->IsEnabled());

  detector->SetEnabled(false);
  EXPECT_FALSE(detector->IsEnabled());

  detector->SetEnabled(true);
  EXPECT_TRUE(detector->IsEnabled());
}

TEST_F(SchedulerEnhancedTest, DetectorStatsTracking) {
  Row* row1 = reinterpret_cast<Row*>(0x1000);
  Row* row2 = reinterpret_cast<Row*>(0x2000);

  // Register operations
  detector->RegisterRead(1, row1, 0, 10);
  detector->RegisterRead(2, row1, 0, 10);
  detector->RegisterWrite(3, row2, 1);

  const auto& stats = detector->GetStats();
  EXPECT_EQ(stats.total_reads.load(), 2);
  EXPECT_EQ(stats.total_writes.load(), 1);
}

TEST_F(SchedulerEnhancedTest, DetectorConflictDetection) {
  Row* row = reinterpret_cast<Row*>(0x1000);

  // Transaction 1 reads row at version 10
  detector->RegisterRead(1, row, 0, 10);
  EXPECT_FALSE(detector->ShouldAbort(1));

  // Simulate commit: version changes to 11
  detector->NotifyVersionChange(row, 0, 11);

  // Transaction 1 should now be marked for abort
  EXPECT_TRUE(detector->ShouldAbort(1));

  const auto& stats = detector->GetStats();
  EXPECT_EQ(stats.early_aborts_detected.load(), 1);
  EXPECT_EQ(stats.version_changes_processed.load(), 1);
}

TEST_F(SchedulerEnhancedTest, DetectorMultipleConflicts) {
  Row* row = reinterpret_cast<Row*>(0x1000);

  // Multiple transactions read same row
  detector->RegisterRead(1, row, 0, 5);
  detector->RegisterRead(2, row, 0, 5);
  detector->RegisterRead(3, row, 0, 5);

  EXPECT_FALSE(detector->ShouldAbort(1));
  EXPECT_FALSE(detector->ShouldAbort(2));
  EXPECT_FALSE(detector->ShouldAbort(3));

  // Commit invalidates all reads
  detector->NotifyVersionChange(row, 0, 6);

  // All three transactions should be marked for abort
  EXPECT_TRUE(detector->ShouldAbort(1));
  EXPECT_TRUE(detector->ShouldAbort(2));
  EXPECT_TRUE(detector->ShouldAbort(3));

  const auto& stats = detector->GetStats();
  EXPECT_EQ(stats.early_aborts_detected.load(), 3);
}

TEST_F(SchedulerEnhancedTest, DetectorNoFalsePositives) {
  Row* row_a = reinterpret_cast<Row*>(0x1000);
  Row* row_b = reinterpret_cast<Row*>(0x2000);

  // Transaction 1 reads row_a column 0
  detector->RegisterRead(1, row_a, 0, 10);

  // Transaction 2 reads row_a column 1 (different column)
  detector->RegisterRead(2, row_a, 1, 20);

  // Transaction 3 reads row_b column 0 (different row)
  detector->RegisterRead(3, row_b, 0, 30);

  // Update row_a column 2 (different from all reads)
  detector->NotifyVersionChange(row_a, 2, 100);

  // None should be aborted (no overlapping row+column)
  EXPECT_FALSE(detector->ShouldAbort(1));
  EXPECT_FALSE(detector->ShouldAbort(2));
  EXPECT_FALSE(detector->ShouldAbort(3));

  // Now update row_a column 0 (only Tx1 should abort)
  detector->NotifyVersionChange(row_a, 0, 11);

  EXPECT_TRUE(detector->ShouldAbort(1));   // Tx1 was reading row_a col 0
  EXPECT_FALSE(detector->ShouldAbort(2));  // Tx2 reading different column
  EXPECT_FALSE(detector->ShouldAbort(3));  // Tx3 reading different row
}

TEST_F(SchedulerEnhancedTest, DetectorRemoveTransaction) {
  Row* row = reinterpret_cast<Row*>(0x1000);

  // Transaction 1 reads and writes
  detector->RegisterRead(1, row, 0, 10);
  detector->RegisterWrite(1, row, 1);

  // Mark as aborted
  detector->MarkAborted(1);
  EXPECT_TRUE(detector->ShouldAbort(1));

  // Remove transaction
  detector->RemoveTransaction(1);

  // Should no longer be marked for abort
  EXPECT_FALSE(detector->ShouldAbort(1));

  // Version changes should not affect removed transaction
  detector->NotifyVersionChange(row, 0, 11);
  EXPECT_FALSE(detector->ShouldAbort(1));
}

TEST_F(SchedulerEnhancedTest, DetectorClearAbortFlag) {
  // Transaction is marked for abort
  detector->MarkAborted(5);
  EXPECT_TRUE(detector->ShouldAbort(5));

  // Clear the abort flag (e.g., for retry)
  detector->ClearAbortFlag(5);
  EXPECT_FALSE(detector->ShouldAbort(5));
}

TEST_F(SchedulerEnhancedTest, DetectorDisabledNoConflicts) {
  Row* row = reinterpret_cast<Row*>(0x1000);

  // Disable detector
  detector->SetEnabled(false);
  EXPECT_FALSE(detector->IsEnabled());

  // Register read
  detector->RegisterRead(1, row, 0, 10);

  // Notify version change
  detector->NotifyVersionChange(row, 0, 11);

  // When disabled, should NOT detect conflict
  EXPECT_FALSE(detector->ShouldAbort(1));
}

TEST_F(SchedulerEnhancedTest, DetectorStatsReset) {
  Row* row = reinterpret_cast<Row*>(0x1000);

  // Generate some stats
  detector->RegisterRead(1, row, 0, 10);
  detector->RegisterWrite(2, row, 1);

  const auto& stats = detector->GetStats();
  EXPECT_GT(stats.total_reads.load(), 0);
  EXPECT_GT(stats.total_writes.load(), 0);

  // Reset stats
  detector->ResetStats();

  // Stats should be reset
  EXPECT_EQ(stats.total_reads.load(), 0);
  EXPECT_EQ(stats.total_writes.load(), 0);
}

// =============================================================================
// EarlyAbortDetector::Stats Tests
// =============================================================================

TEST_F(SchedulerEnhancedTest, StatsStructure) {
  EarlyAbortDetector::Stats stats;
  
  // Default values should be 0
  EXPECT_EQ(stats.total_reads.load(), 0);
  EXPECT_EQ(stats.total_writes.load(), 0);
  EXPECT_EQ(stats.early_aborts_detected.load(), 0);
  EXPECT_EQ(stats.version_changes_processed.load(), 0);
}

TEST_F(SchedulerEnhancedTest, StatsThreadSafety) {
  EarlyAbortDetector::Stats stats;
  
  const int NUM_THREADS = 4;
  const int INCREMENTS = 1000;
  
  std::vector<std::thread> threads;
  
  for (int t = 0; t < NUM_THREADS; t++) {
    threads.emplace_back([&stats, INCREMENTS]() {
      for (int i = 0; i < INCREMENTS; i++) {
        stats.total_reads.fetch_add(1, std::memory_order_relaxed);
        stats.total_writes.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  EXPECT_EQ(stats.total_reads.load(), NUM_THREADS * INCREMENTS);
  EXPECT_EQ(stats.total_writes.load(), NUM_THREADS * INCREMENTS);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
