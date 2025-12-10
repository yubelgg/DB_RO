#include "metrics.h"
#include "scheduler_enhanced.h"  // For AbortReason enum
#include "base/all.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace janus {

void EnhancedOccMetrics::AddTransaction(const TransactionRecord& record) {
  records_.push_back(record);
}

EnhancedOccMetrics::AggregateStats EnhancedOccMetrics::CalculateStats() const {
  AggregateStats stats;

  if (records_.empty()) {
    return stats;
  }

  // Collect latencies and batch sizes for percentile calculation
  std::vector<int64_t> latencies;
  std::map<size_t, uint64_t> batch_id_to_size;

  // Find earliest and latest timestamps
  auto earliest = records_[0].start_time;
  auto latest = records_[0].end_time;

  // First pass: collect data
  for (const auto& record : records_) {
    stats.total_attempted++;

    if (record.committed) {
      stats.total_committed++;
      latencies.push_back(record.duration_us);
    } else {
      stats.total_aborted++;

      // Categorize abort reasons
      switch (record.abort_reason) {
        case AbortReason::EARLY:
          stats.early_aborts++;
          break;
        case AbortReason::VERSION_MISMATCH:
          stats.version_mismatch_aborts++;
          break;
        case AbortReason::LOCK_CONFLICT:
          stats.lock_conflict_aborts++;
          break;
        case AbortReason::UNKNOWN:
        default:
          stats.unknown_aborts++;
          break;
      }
    }

    // Track batch sizes
    batch_id_to_size[record.batch_id] = record.batch_size;

    // Update timestamp range
    if (record.start_time < earliest) {
      earliest = record.start_time;
    }
    if (record.end_time > latest) {
      latest = record.end_time;
    }
  }

  // Calculate abort rate
  stats.abort_rate = stats.total_attempted > 0
                         ? static_cast<double>(stats.total_aborted) / stats.total_attempted
                         : 0.0;

  // Calculate wall clock duration
  stats.total_duration_us =
      std::chrono::duration_cast<std::chrono::microseconds>(latest - earliest).count();

  // Calculate throughput
  double duration_seconds = stats.total_duration_us / 1000000.0;
  stats.throughput_tps = duration_seconds > 0.0
                             ? static_cast<double>(stats.total_committed) / duration_seconds
                             : 0.0;

  // Calculate latency statistics (only for committed transactions)
  if (!latencies.empty()) {
    // Sort for percentile calculation
    std::sort(latencies.begin(), latencies.end());

    stats.min_latency_us = latencies.front();
    stats.max_latency_us = latencies.back();

    // Average
    int64_t sum = 0;
    for (auto lat : latencies) {
      sum += lat;
    }
    stats.avg_latency_us = sum / latencies.size();

    // Percentiles
    stats.p50_latency_us = CalculatePercentile(latencies, 0.50);
    stats.p95_latency_us = CalculatePercentile(latencies, 0.95);
    stats.p99_latency_us = CalculatePercentile(latencies, 0.99);
  }

  // Calculate batch statistics
  if (!batch_id_to_size.empty()) {
    stats.total_batches = batch_id_to_size.size();

    size_t sum = 0;
    stats.min_batch_size = SIZE_MAX;
    stats.max_batch_size = 0;

    for (const auto& pair : batch_id_to_size) {
      size_t batch_size = pair.second;
      sum += batch_size;
      if (batch_size < stats.min_batch_size) {
        stats.min_batch_size = batch_size;
      }
      if (batch_size > stats.max_batch_size) {
        stats.max_batch_size = batch_size;
      }
    }

    stats.avg_batch_size = static_cast<double>(sum) / batch_id_to_size.size();
  }

  return stats;
}

bool EnhancedOccMetrics::ExportCSV(const std::string& filename) const {
  std::ofstream file(filename);
  if (!file.is_open()) {
    Log_error("Failed to open CSV file for writing: %s", filename.c_str());
    return false;
  }

  // Write CSV header
  file << "txn_id,committed,abort_reason,duration_us,batch_id,batch_size\n";

  // Write transaction records
  for (const auto& record : records_) {
    file << record.txn_id << ","
         << (record.committed ? "true" : "false") << ",";

    // Abort reason (only meaningful if aborted)
    if (!record.committed) {
      switch (record.abort_reason) {
        case AbortReason::EARLY:
          file << "EARLY";
          break;
        case AbortReason::VERSION_MISMATCH:
          file << "VERSION_MISMATCH";
          break;
        case AbortReason::LOCK_CONFLICT:
          file << "LOCK_CONFLICT";
          break;
        case AbortReason::UNKNOWN:
        default:
          file << "UNKNOWN";
          break;
      }
    } else {
      file << "N/A";  // Committed transactions have no abort reason
    }

    file << "," << record.duration_us
         << "," << record.batch_id
         << "," << record.batch_size << "\n";
  }

  file.close();
  Log_info("Exported %zu transaction records to %s", records_.size(), filename.c_str());
  return true;
}

std::string EnhancedOccMetrics::GenerateReport() const {
  auto stats = CalculateStats();
  std::ostringstream report;

  report << "\n===== Enhanced OCC Metrics Report =====\n\n";

  // Transaction counts
  report << "Transaction Counts:\n";
  report << "  Total attempted: " << stats.total_attempted << "\n";
  report << "  Total committed: " << stats.total_committed << "\n";
  report << "  Total aborted:   " << stats.total_aborted << "\n";
  report << std::fixed << std::setprecision(2);
  report << "  Abort rate:      " << (stats.abort_rate * 100.0) << "%\n\n";

  // Throughput
  report << "Throughput:\n";
  report << "  TPS:             " << stats.throughput_tps << " transactions/sec\n";
  report << "  Duration:        " << (stats.total_duration_us / 1000000.0) << " seconds\n\n";

  // Latency
  if (stats.total_committed > 0) {
    report << "Latency (committed transactions only):\n";
    report << "  Min:    " << stats.min_latency_us << " us\n";
    report << "  Avg:    " << stats.avg_latency_us << " us\n";
    report << "  P50:    " << stats.p50_latency_us << " us\n";
    report << "  P95:    " << stats.p95_latency_us << " us\n";
    report << "  P99:    " << stats.p99_latency_us << " us\n";
    report << "  Max:    " << stats.max_latency_us << " us\n\n";
  }

  // Abort breakdown
  if (stats.total_aborted > 0) {
    report << "Abort Breakdown:\n";
    report << "  Early aborts:        " << stats.early_aborts << " ("
           << (100.0 * stats.early_aborts / stats.total_aborted) << "%)\n";
    report << "  Version mismatch:    " << stats.version_mismatch_aborts << " ("
           << (100.0 * stats.version_mismatch_aborts / stats.total_aborted) << "%)\n";
    report << "  Lock conflicts:      " << stats.lock_conflict_aborts << " ("
           << (100.0 * stats.lock_conflict_aborts / stats.total_aborted) << "%)\n";
    report << "  Unknown:             " << stats.unknown_aborts << " ("
           << (100.0 * stats.unknown_aborts / stats.total_aborted) << "%)\n\n";
  }

  // Batch statistics
  if (stats.total_batches > 0) {
    report << "Batch Statistics:\n";
    report << "  Total batches:   " << stats.total_batches << "\n";
    report << "  Avg batch size:  " << stats.avg_batch_size << "\n";
    report << "  Min batch size:  " << stats.min_batch_size << "\n";
    report << "  Max batch size:  " << stats.max_batch_size << "\n\n";
  }

  report << "========================================\n";

  return report.str();
}

void EnhancedOccMetrics::PrintReport() const {
  std::cout << GenerateReport();
}

void EnhancedOccMetrics::Reset() {
  records_.clear();
}

int64_t EnhancedOccMetrics::CalculatePercentile(const std::vector<int64_t>& sorted_latencies,
                                                  double percentile) {
  if (sorted_latencies.empty()) {
    return 0;
  }

  // Calculate index for percentile
  // E.g., for P50 of 100 samples: 0.50 * 100 = 50.0 → index 49 (0-indexed)
  double index_d = percentile * sorted_latencies.size();
  size_t index = static_cast<size_t>(index_d);

  // Clamp to valid range
  if (index >= sorted_latencies.size()) {
    index = sorted_latencies.size() - 1;
  }

  return sorted_latencies[index];
}

} // namespace janus
