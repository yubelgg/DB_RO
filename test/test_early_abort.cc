#include "early_abort_detector.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

// Avoid conflict with system key_t - don't import it, use fully qualified deptran::key_t
using deptran::EarlyAbortDetector;
using deptran::txn_id_t;

// Simple test framework
#define TEST(name) void name()
#define EXPECT_TRUE(expr) assert(expr)
#define EXPECT_FALSE(expr) assert(!(expr))
#define EXPECT_EQ(a, b) assert((a) == (b))
#define EXPECT_GT(a, b) assert((a) > (b))
#define RUN_TEST(test) do { \
    std::cout << "Running " << #test << "..."; \
    test(); \
    std::cout << " PASSED" << std::endl; \
} while(0)

TEST(BasicRegistration) {
    EarlyAbortDetector detector;
    
    detector.register_transaction(1);
    detector.register_transaction(2);
    
    // Should not abort initially
    EXPECT_FALSE(detector.should_abort_early(1));
    EXPECT_FALSE(detector.should_abort_early(2));
    
    detector.remove_transaction(1);
    detector.remove_transaction(2);
}

TEST(SimpleConflict) {
    EarlyAbortDetector detector;
    
    txn_id_t t1 = 1, t2 = 2;
    deptran::key_t key = 100;
    
    detector.register_transaction(t1);
    detector.register_transaction(t2);
    
    // T1 writes key
    detector.record_write(t1, key);
    
    // T2 reads same key - should create conflict
    detector.record_read(t2, key);
    
    // At this point, there's a conflict but no cycle
    EXPECT_FALSE(detector.should_abort_early(t1));
    EXPECT_FALSE(detector.should_abort_early(t2));
    
    detector.remove_transaction(t1);
    detector.remove_transaction(t2);
}

TEST(CycleDetection) {
    EarlyAbortDetector::Config config;
    config.enable_cycle_detection = true;
    config.enable_conflict_counting = false;
    EarlyAbortDetector detector(config);
    
    txn_id_t t1 = 1, t2 = 2, t3 = 3;
    deptran::key_t k1 = 100, k2 = 200, k3 = 300;
    
    detector.register_transaction(t1);
    detector.register_transaction(t2);
    detector.register_transaction(t3);
    
    // Create a cycle: T1 -> T2 -> T3 -> T1
    // T1 writes k1, T2 reads k1 (T1 -> T2)
    detector.record_write(t1, k1);
    detector.record_read(t2, k1);
    
    // T2 writes k2, T3 reads k2 (T2 -> T3)
    detector.record_write(t2, k2);
    detector.record_read(t3, k2);
    
    // T3 writes k3, T1 reads k3 (T3 -> T1) - Creates cycle!
    detector.record_write(t3, k3);
    detector.record_read(t1, k3);
    
    // At least one transaction should be detected for early abort
    bool any_abort = detector.should_abort_early(t1) || 
                     detector.should_abort_early(t2) || 
                     detector.should_abort_early(t3);
    EXPECT_TRUE(any_abort);
    
    auto stats = detector.get_statistics();
    EXPECT_GT(stats.cycle_aborts, 0u);
    
    detector.remove_transaction(t1);
    detector.remove_transaction(t2);
    detector.remove_transaction(t3);
}

TEST(ExcessiveConflicts) {
    EarlyAbortDetector::Config config;
    config.enable_cycle_detection = false;
    config.enable_conflict_counting = true;
    config.max_conflicts = 3;
    EarlyAbortDetector detector(config);
    
    txn_id_t victim = 1;
    detector.register_transaction(victim);
    
    // Create many conflicting transactions
    for (int i = 2; i <= 6; i++) {
        txn_id_t t = i;
        detector.register_transaction(t);
        
        // Each transaction writes a key that victim reads
        detector.record_write(t, 100 + i);
        detector.record_read(victim, 100 + i);
    }
    
    // Victim should abort due to too many conflicts
    EXPECT_TRUE(detector.should_abort_early(victim));
    
    auto stats = detector.get_statistics();
    EXPECT_GT(stats.conflict_aborts, 0u);
    
    // Cleanup
    for (int i = 1; i <= 6; i++) {
        detector.remove_transaction(i);
    }
}

TEST(WouldAbortOnRead) {
    EarlyAbortDetector::Config config;
    config.enable_conflict_counting = true;
    config.max_conflicts = 2;
    EarlyAbortDetector detector(config);
    
    txn_id_t t1 = 1;
    detector.register_transaction(t1);
    
    // Create writers for keys
    for (int i = 2; i <= 4; i++) {
        txn_id_t t = i;
        detector.register_transaction(t);
        detector.record_write(t, 100 + i);
    }
    
    // Reading first two keys should be ok
    EXPECT_FALSE(detector.would_abort_on_read(t1, 102));
    detector.record_read(t1, 102);
    
    EXPECT_FALSE(detector.would_abort_on_read(t1, 103));
    detector.record_read(t1, 103);
    
    // Reading third key would exceed max conflicts
    EXPECT_TRUE(detector.would_abort_on_read(t1, 104));
    
    // Cleanup
    for (int i = 1; i <= 4; i++) {
        detector.remove_transaction(i);
    }
}

TEST(WouldAbortOnWrite) {
    EarlyAbortDetector::Config config;
    config.enable_conflict_counting = true;
    config.max_conflicts = 2;
    EarlyAbortDetector detector(config);
    
    txn_id_t t1 = 1;
    detector.register_transaction(t1);
    
    // Create readers for keys
    for (int i = 2; i <= 4; i++) {
        txn_id_t t = i;
        detector.register_transaction(t);
        detector.record_read(t, 100 + i);
    }
    
    // Writing first two keys should be ok
    EXPECT_FALSE(detector.would_abort_on_write(t1, 102));
    detector.record_write(t1, 102);
    
    EXPECT_FALSE(detector.would_abort_on_write(t1, 103));
    detector.record_write(t1, 103);
    
    // Writing third key would exceed max conflicts
    EXPECT_TRUE(detector.would_abort_on_write(t1, 104));
    
    // Cleanup
    for (int i = 1; i <= 4; i++) {
        detector.remove_transaction(i);
    }
}

TEST(StatisticsTracking) {
    EarlyAbortDetector detector;
    
    auto stats = detector.get_statistics();
    EXPECT_EQ(stats.total_checks, 0u);
    EXPECT_EQ(stats.total_early_aborts, 0u);
    
    txn_id_t t1 = 1;
    detector.register_transaction(t1);
    
    // Multiple checks
    detector.should_abort_early(t1);
    detector.should_abort_early(t1);
    detector.should_abort_early(t1);
    
    stats = detector.get_statistics();
    EXPECT_EQ(stats.total_checks, 3u);
    
    // Reset
    detector.reset_statistics();
    stats = detector.get_statistics();
    EXPECT_EQ(stats.total_checks, 0u);
    
    detector.remove_transaction(t1);
}

TEST(ConcurrentAccess) {
    EarlyAbortDetector detector;
    
    const int NUM_THREADS = 4;
    const int TXNS_PER_THREAD = 100;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&detector, t]() {
            for (int i = 0; i < TXNS_PER_THREAD; i++) {
                txn_id_t txn_id = t * TXNS_PER_THREAD + i;
                
                detector.register_transaction(txn_id);
                detector.record_read(txn_id, txn_id % 50);
                detector.record_write(txn_id, txn_id % 50 + 100);
                detector.should_abort_early(txn_id);
                detector.remove_transaction(txn_id);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto stats = detector.get_statistics();
    EXPECT_EQ(stats.total_checks, NUM_THREADS * TXNS_PER_THREAD);
}

TEST(MarkCommittedAborted) {
    EarlyAbortDetector detector;
    
    txn_id_t t1 = 1, t2 = 2;
    
    detector.register_transaction(t1);
    detector.register_transaction(t2);
    
    detector.mark_committed(t1);
    detector.mark_aborted(t2);
    
    // Transactions should still be tracked
    EXPECT_FALSE(detector.should_abort_early(t1));
    
    detector.remove_transaction(t1);
    detector.remove_transaction(t2);
}

TEST(ClearState) {
    EarlyAbortDetector detector;
    
    // Register some transactions
    for (int i = 1; i <= 5; i++) {
        detector.register_transaction(i);
        detector.record_write(i, i * 100);
    }
    
    // Check state exists
    detector.should_abort_early(1);
    auto stats = detector.get_statistics();
    EXPECT_GT(stats.total_checks, 0u);
    
    // Clear everything
    detector.clear();
    
    // Statistics should be reset
    stats = detector.get_statistics();
    EXPECT_EQ(stats.total_checks, 0u);
}

int main() {
    std::cout << "Running Early Abort Detector Tests\n";
    std::cout << "===================================\n\n";
    
    RUN_TEST(BasicRegistration);
    RUN_TEST(SimpleConflict);
    RUN_TEST(CycleDetection);
    RUN_TEST(ExcessiveConflicts);
    RUN_TEST(WouldAbortOnRead);
    RUN_TEST(WouldAbortOnWrite);
    RUN_TEST(StatisticsTracking);
    RUN_TEST(ConcurrentAccess);
    RUN_TEST(MarkCommittedAborted);
    RUN_TEST(ClearState);
    
    std::cout << "\n===================================\n";
    std::cout << "All tests passed!\n";
    
    return 0;
}
