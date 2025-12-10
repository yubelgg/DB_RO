#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace janus {

// Forward declaration
enum class AbortReason;

/**
 * Aggregated metrics for Enhanced OCC evaluation
 *
 * This class collects and aggregates transaction-level metrics including:
 * - Throughput and abort rates
 * - Latency percentiles (P50, P95, P99)
 * - Abort reason breakdown
 * - Batch validation statistics
 *
 * Used for performance evaluation and comparison with baseline OCC.
 */
class EnhancedOccMetrics {
public:
  /**
   * Single transaction record for detailed analysis
   */
  struct TransactionRecord {
    uint64_t txn_id;
    bool committed;  // true = committed, false = aborted
    AbortReason abort_reason;  // Valid only if committed = false
    int64_t duration_us;  // Execution time in microseconds
    size_t batch_id;      // Which batch validated this transaction
    size_t batch_size;    // Size of the batch
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
  };

  /**
   * Aggregate statistics calculated from transaction records
   */
  struct AggregateStats {
    // Counts
    uint64_t total_attempted = 0;
    uint64_t total_committed = 0;
    uint64_t total_aborted = 0;

    // Rates
    double abort_rate = 0.0;  // Fraction (0.0 to 1.0)
    double throughput_tps = 0.0;  // Transactions per second

    // Latency (microseconds)
    int64_t min_latency_us = 0;
    int64_t max_latency_us = 0;
    int64_t avg_latency_us = 0;
    int64_t p50_latency_us = 0;  // Median
    int64_t p95_latency_us = 0;
    int64_t p99_latency_us = 0;

    // Abort breakdown
    uint64_t early_aborts = 0;
    uint64_t version_mismatch_aborts = 0;
    uint64_t lock_conflict_aborts = 0;
    uint64_t unknown_aborts = 0;

    // Batch statistics
    double avg_batch_size = 0.0;
    size_t min_batch_size = 0;
    size_t max_batch_size = 0;
    uint64_t total_batches = 0;

    // Timing
    int64_t total_duration_us = 0;  // Wall clock time
  };

  /**
   * Constructor
   */
  EnhancedOccMetrics() = default;

  /**
   * Add a transaction record
   * @param record Transaction execution details
   */
  void AddTransaction(const TransactionRecord& record);

  /**
   * Calculate aggregate statistics from collected records
   * @return Aggregated statistics
   */
  AggregateStats CalculateStats() const;

  /**
   * Export transaction records to CSV file
   * @param filename Output file path
   * @return true if successful, false on error
   */
  bool ExportCSV(const std::string& filename) const;

  /**
   * Generate human-readable text report
   * @return Formatted report string
   */
  std::string GenerateReport() const;

  /**
   * Print report to stdout
   */
  void PrintReport() const;

  /**
   * Clear all collected data
   */
  void Reset();

  /**
   * Get number of transaction records collected
   */
  size_t GetRecordCount() const { return records_.size(); }

private:
  // Collected transaction records
  std::vector<TransactionRecord> records_;

  // Calculate percentile from sorted latency vector
  static int64_t CalculatePercentile(const std::vector<int64_t>& sorted_latencies,
                                       double percentile);
};

} // namespace janus
