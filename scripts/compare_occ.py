#!/usr/bin/env python3
"""
Compare Enhanced OCC vs Baseline OCC Metrics

This script compares metrics exported from Enhanced OCC and Baseline OCC
implementations to evaluate performance improvements.

Usage:
    python compare_occ.py <enhanced_csv> <baseline_csv>

Example:
    python compare_occ.py metrics_enhanced.csv metrics_baseline.csv
"""

import argparse
import csv
import sys
from collections import defaultdict
from typing import Dict, List, Tuple


def load_csv(filename: str) -> List[Dict]:
    """Load transaction records from CSV file."""
    records = []
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                records.append(row)
    except FileNotFoundError:
        print(f"Error: File not found: {filename}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error loading {filename}: {e}", file=sys.stderr)
        sys.exit(1)
    return records


def calculate_metrics(records: List[Dict]) -> Dict:
    """Calculate aggregate metrics from transaction records."""
    if not records:
        return {}

    total_attempted = len(records)
    total_committed = sum(1 for r in records if r['committed'] == 'true')
    total_aborted = total_attempted - total_committed

    # Latencies (only for committed transactions)
    latencies = [int(r['duration_us']) for r in records if r['committed'] == 'true']
    latencies.sort()

    # Abort reasons
    abort_reasons = defaultdict(int)
    for r in records:
        if r['committed'] == 'false':
            abort_reasons[r['abort_reason']] += 1

    # Batch sizes
    batch_sizes = [int(r['batch_size']) for r in records]

    metrics = {
        'total_attempted': total_attempted,
        'total_committed': total_committed,
        'total_aborted': total_aborted,
        'abort_rate': total_aborted / total_attempted if total_attempted > 0 else 0.0,
        'latencies': latencies,
        'abort_reasons': dict(abort_reasons),
        'batch_sizes': batch_sizes,
    }

    # Calculate percentiles
    if latencies:
        metrics['min_latency'] = latencies[0]
        metrics['max_latency'] = latencies[-1]
        metrics['avg_latency'] = sum(latencies) / len(latencies)
        metrics['p50_latency'] = percentile(latencies, 0.50)
        metrics['p95_latency'] = percentile(latencies, 0.95)
        metrics['p99_latency'] = percentile(latencies, 0.99)

    # Batch statistics
    if batch_sizes:
        metrics['avg_batch_size'] = sum(batch_sizes) / len(batch_sizes)
        metrics['min_batch_size'] = min(batch_sizes)
        metrics['max_batch_size'] = max(batch_sizes)

    return metrics


def percentile(sorted_list: List[float], p: float) -> float:
    """Calculate percentile from sorted list."""
    if not sorted_list:
        return 0.0
    index = int(p * len(sorted_list))
    if index >= len(sorted_list):
        index = len(sorted_list) - 1
    return sorted_list[index]


def print_comparison(enhanced_metrics: Dict, baseline_metrics: Dict):
    """Print side-by-side comparison of metrics."""
    print("\n" + "=" * 80)
    print("Enhanced OCC vs Baseline OCC Comparison")
    print("=" * 80 + "\n")

    # Transaction counts
    print("Transaction Counts:")
    print(f"  {'Metric':<30} {'Enhanced':>15} {'Baseline':>15} {'Improvement':>15}")
    print("  " + "-" * 77)

    print(f"  {'Total attempted':<30} {enhanced_metrics['total_attempted']:>15,} "
          f"{baseline_metrics['total_attempted']:>15,} {'-':>15}")

    print(f"  {'Total committed':<30} {enhanced_metrics['total_committed']:>15,} "
          f"{baseline_metrics['total_committed']:>15,} {'-':>15}")

    print(f"  {'Total aborted':<30} {enhanced_metrics['total_aborted']:>15,} "
          f"{baseline_metrics['total_aborted']:>15,} {'-':>15}")

    # Abort rate with improvement
    enhanced_abort_rate = enhanced_metrics['abort_rate'] * 100
    baseline_abort_rate = baseline_metrics['abort_rate'] * 100
    if baseline_abort_rate > 0:
        abort_improvement = ((baseline_abort_rate - enhanced_abort_rate) / baseline_abort_rate) * 100
        improvement_str = f"{abort_improvement:+.1f}%"
    else:
        improvement_str = "N/A"

    print(f"  {'Abort rate':<30} {enhanced_abort_rate:>14.2f}% "
          f"{baseline_abort_rate:>14.2f}% {improvement_str:>15}")

    # Latency comparison
    if 'latencies' in enhanced_metrics and enhanced_metrics['latencies']:
        print("\nLatency (committed transactions, microseconds):")
        print(f"  {'Metric':<30} {'Enhanced':>15} {'Baseline':>15} {'Improvement':>15}")
        print("  " + "-" * 77)

        latency_metrics = ['min_latency', 'avg_latency', 'p50_latency', 'p95_latency', 'p99_latency', 'max_latency']
        latency_labels = ['Min', 'Avg', 'P50', 'P95', 'P99', 'Max']

        for label, metric_key in zip(latency_labels, latency_metrics):
            if metric_key in enhanced_metrics and metric_key in baseline_metrics:
                enhanced_val = enhanced_metrics[metric_key]
                baseline_val = baseline_metrics[metric_key]
                if baseline_val > 0:
                    improvement = ((baseline_val - enhanced_val) / baseline_val) * 100
                    improvement_str = f"{improvement:+.1f}%"
                else:
                    improvement_str = "N/A"
                print(f"  {label:<30} {enhanced_val:>15,.0f} {baseline_val:>15,.0f} {improvement_str:>15}")

    # Abort breakdown
    print("\nAbort Reasons (Enhanced OCC):")
    for reason, count in enhanced_metrics['abort_reasons'].items():
        percentage = (count / enhanced_metrics['total_aborted'] * 100) if enhanced_metrics['total_aborted'] > 0 else 0
        print(f"  {reason:<30} {count:>10,} ({percentage:>5.1f}%)")

    print("\nAbort Reasons (Baseline OCC):")
    for reason, count in baseline_metrics['abort_reasons'].items():
        percentage = (count / baseline_metrics['total_aborted'] * 100) if baseline_metrics['total_aborted'] > 0 else 0
        print(f"  {reason:<30} {count:>10,} ({percentage:>5.1f}%)")

    # Batch statistics (Enhanced only)
    if 'batch_sizes' in enhanced_metrics and enhanced_metrics['batch_sizes']:
        print("\nBatch Statistics (Enhanced OCC only):")
        print(f"  Avg batch size: {enhanced_metrics['avg_batch_size']:.1f}")
        print(f"  Min batch size: {enhanced_metrics['min_batch_size']}")
        print(f"  Max batch size: {enhanced_metrics['max_batch_size']}")

    # Summary
    print("\n" + "=" * 80)
    print("Summary:")
    print("=" * 80)

    if baseline_abort_rate > 0:
        print(f"  Abort rate reduction: {abort_improvement:+.1f}% "
              f"({baseline_abort_rate:.2f}% → {enhanced_abort_rate:.2f}%)")

    if 'p50_latency' in enhanced_metrics and 'p50_latency' in baseline_metrics:
        p50_improvement = ((baseline_metrics['p50_latency'] - enhanced_metrics['p50_latency']) /
                          baseline_metrics['p50_latency']) * 100 if baseline_metrics['p50_latency'] > 0 else 0
        print(f"  Median latency change: {p50_improvement:+.1f}%")

    # Check if goals are met
    print("\nEvaluation Goals:")
    if abs(abort_improvement) >= 40:
        print(f"  ✓ Abort reduction goal MET: {abs(abort_improvement):.1f}% ≥ 40%")
    else:
        print(f"  ✗ Abort reduction goal NOT MET: {abs(abort_improvement):.1f}% < 40%")

    print("=" * 80 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description='Compare Enhanced OCC vs Baseline OCC metrics',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s metrics_enhanced.csv metrics_baseline.csv
  %(prog)s results/enhanced.csv results/baseline.csv
        """
    )
    parser.add_argument('enhanced_csv', help='CSV file with Enhanced OCC metrics')
    parser.add_argument('baseline_csv', help='CSV file with Baseline OCC metrics')

    args = parser.parse_args()

    # Load data
    print(f"Loading Enhanced OCC metrics from: {args.enhanced_csv}")
    enhanced_records = load_csv(args.enhanced_csv)
    print(f"  Loaded {len(enhanced_records)} transaction records")

    print(f"Loading Baseline OCC metrics from: {args.baseline_csv}")
    baseline_records = load_csv(args.baseline_csv)
    print(f"  Loaded {len(baseline_records)} transaction records")

    # Calculate metrics
    enhanced_metrics = calculate_metrics(enhanced_records)
    baseline_metrics = calculate_metrics(baseline_records)

    # Print comparison
    print_comparison(enhanced_metrics, baseline_metrics)


if __name__ == '__main__':
    main()
