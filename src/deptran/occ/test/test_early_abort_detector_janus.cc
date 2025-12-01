#include "deptran/occ/early_abort_detector.h"
#include <gtest/gtest.h>

using namespace janus;

class EarlyAbortDetectorTest : public ::testing::Test {
protected:
  void SetUp() override {
    detector = new EarlyAbortDetector();
    detector->SetEnabled(true);

    // Create dummy row pointers for testing
    row_a = reinterpret_cast<Row*>(0x1000);
    row_b = reinterpret_cast<Row*>(0x2000);
  }

  void TearDown() override {
    delete detector;
  }

  EarlyAbortDetector* detector;
  Row* row_a;
  Row* row_b;
};

TEST_F(EarlyAbortDetectorTest, RegisterReadAndDetectConflict) {
  // Transaction 1 reads row_a column 0 at version 10
  detector->RegisterRead(1, row_a, 0, 10);

  // Transaction 1 should not be marked for abort yet
  EXPECT_FALSE(detector->ShouldAbort(1));

  // Transaction 2 commits and increments version to 11
  detector->NotifyVersionChange(row_a, 0, 11);

  // Transaction 1 should now be marked for early abort (reading stale version)
  EXPECT_TRUE(detector->ShouldAbort(1));

  // Transaction 2 should not be affected
  EXPECT_FALSE(detector->ShouldAbort(2));

  // Stats should reflect one early abort detected
  EXPECT_EQ(detector->GetStats().early_aborts_detected.load(), 1);
}

TEST_F(EarlyAbortDetectorTest, RegisterWriteAndDetectConflict) {
  // Transaction 1 writes to row_a column 0
  detector->RegisterWrite(1, row_a, 0);

  // Transaction 2 reads row_a column 0 at version 5
  detector->RegisterRead(2, row_a, 0, 5);

  // Neither should be aborted yet
  EXPECT_FALSE(detector->ShouldAbort(1));
  EXPECT_FALSE(detector->ShouldAbort(2));

  // Transaction 1 commits (version change)
  detector->NotifyVersionChange(row_a, 0, 6);

  // Transaction 2 should be aborted (reading stale data from before Tx1's write)
  EXPECT_TRUE(detector->ShouldAbort(2));
  EXPECT_FALSE(detector->ShouldAbort(1));
}

TEST_F(EarlyAbortDetectorTest, NoFalsePositives) {
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

TEST_F(EarlyAbortDetectorTest, MultipleTransactionsConflict) {
  // Multiple transactions reading the same row+column
  detector->RegisterRead(1, row_a, 0, 5);
  detector->RegisterRead(2, row_a, 0, 5);
  detector->RegisterRead(3, row_a, 0, 5);
  detector->RegisterRead(4, row_b, 0, 10);  // Different row

  // Version change on row_a column 0
  detector->NotifyVersionChange(row_a, 0, 6);

  // Transactions 1, 2, 3 should all be aborted
  EXPECT_TRUE(detector->ShouldAbort(1));
  EXPECT_TRUE(detector->ShouldAbort(2));
  EXPECT_TRUE(detector->ShouldAbort(3));

  // Transaction 4 should not be affected (different row)
  EXPECT_FALSE(detector->ShouldAbort(4));

  // Should detect 3 early aborts
  EXPECT_EQ(detector->GetStats().early_aborts_detected.load(), 3);
}

TEST_F(EarlyAbortDetectorTest, RemoveTransactionClearsTracking) {
  // Transaction 1 reads and writes
  detector->RegisterRead(1, row_a, 0, 10);
  detector->RegisterWrite(1, row_a, 1);

  // Mark as aborted
  detector->MarkAborted(1);
  EXPECT_TRUE(detector->ShouldAbort(1));

  // Remove transaction
  detector->RemoveTransaction(1);

  // Should no longer be marked for abort
  EXPECT_FALSE(detector->ShouldAbort(1));

  // Version changes should not affect removed transaction
  detector->NotifyVersionChange(row_a, 0, 11);
  EXPECT_FALSE(detector->ShouldAbort(1));
}

TEST_F(EarlyAbortDetectorTest, ClearAbortFlag) {
  // Transaction is marked for abort
  detector->MarkAborted(5);
  EXPECT_TRUE(detector->ShouldAbort(5));

  // Clear the abort flag (e.g., for retry)
  detector->ClearAbortFlag(5);
  EXPECT_FALSE(detector->ShouldAbort(5));
}

TEST_F(EarlyAbortDetectorTest, DisabledDetector) {
  // Disable detector
  detector->SetEnabled(false);
  EXPECT_FALSE(detector->IsEnabled());

  // Register read
  detector->RegisterRead(1, row_a, 0, 10);

  // Notify version change
  detector->NotifyVersionChange(row_a, 0, 11);

  // When disabled, should NOT detect conflicts
  EXPECT_FALSE(detector->ShouldAbort(1));

  // Re-enable
  detector->SetEnabled(true);
  EXPECT_TRUE(detector->IsEnabled());
}

TEST_F(EarlyAbortDetectorTest, StatsTracking) {
  // Reset stats
  detector->ResetStats();

  // Register operations
  detector->RegisterRead(1, row_a, 0, 10);
  detector->RegisterRead(2, row_a, 0, 10);
  detector->RegisterWrite(3, row_b, 1);

  // Check stats
  EXPECT_EQ(detector->GetStats().total_reads.load(), 2);
  EXPECT_EQ(detector->GetStats().total_writes.load(), 1);

  // Trigger version change
  detector->NotifyVersionChange(row_a, 0, 11);

  // Check version change and early abort stats
  EXPECT_EQ(detector->GetStats().version_changes_processed.load(), 1);
  EXPECT_EQ(detector->GetStats().early_aborts_detected.load(), 2);  // Both Tx1 and Tx2 aborted
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
