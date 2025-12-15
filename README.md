# Enhanced OCC - Distributed Systems Lab

Optimistic Concurrency Control improvements for high-contention workloads, built on MIT 6.824 style distributed systems labs.

## What We Built

Two optimizations to reduce OCC abort rates and improve throughput:

- **Early Abort**: Detects conflicts during transaction execution (before validation) by tracking read/write sets. Transactions abort early instead of wasting work.

- **Batch Validation**: Queues transactions and validates them in batches using coroutine yielding (`BoxEvent::Wait()`). Allows multiple transactions to be in-flight simultaneously while serial validation prevents lock contention.

## Results

| Configuration | TPS | vs Baseline | Abort Rate |
|---------------|-----|-------------|------------|
| Baseline OCC | ~265 | 1.0x | ~90% |
| Early Abort | ~1017 | ~3.8x | ~34% |
| Batch Validation | ~481 | ~1.8x | ~20% |

*Results from 30-second TPC-C benchmark runs*

## Quick Start

### Install Dependencies

```bash
# Debian/Ubuntu
sudo bash apt_packages.sh

# Arch Linux
sudo bash pacman_packages.sh
pip install -r requirements.txt
```

### Build

```bash
make clean
make labtest -j32
```

First build takes ~10 minutes.

## Running Benchmarks

### OCC Comparison Suite (Recommended)

```bash
./scripts/run_occ_comparison.sh [duration]

# Examples:
./scripts/run_occ_comparison.sh 10    # Quick test (10s)
./scripts/run_occ_comparison.sh 30    # Recommended (30s)
./scripts/run_occ_comparison.sh 60    # Thorough test (60s)
```

**Note**: Higher duration gives more stable/accurate results. Start with 30 seconds for meaningful comparisons.

**Features:**
- Live stopwatch showing elapsed time
- Runs baseline, early_abort, and batch tests
- Automatic comparison table
- Handles shutdown crashes gracefully

**Sample Output:**
```
======================================
  OCC Benchmark Comparison Suite
======================================

  Duration: 30s per test
  Timeout:  90s max per test
  Configs:  3 configurations

[1/3] Running baseline... 25s
[1/3] Running baseline... DONE (35s, shutdown crash ignored)
[2/3] Running early_abort... DONE (33s, shutdown crash ignored)
[3/3] Running batch... DONE (34s, shutdown crash ignored)

======================================
  Results: 3 passed, 0 failed
======================================

=== OCC Benchmark Comparison ===

Mode                  TPS     Abort%  vs Baseline  Abort Reduction
------------------------------------------------------------------------
occ_batch           481.6      19.9%       +90.4%           +78.3%
occ_early_abort    1085.7      34.3%      +329.1%           +62.7%
occ                 253.0      91.8%   (baseline)                -
```

### Manual Testing

```bash
cd build

# Baseline OCC
./labtest -f ../config/tpcc_occ_baseline.yml -d 30

# Early Abort (Best TPS)
./labtest -f ../config/tpcc_occ_early_abort.yml -d 30

# Batch Validation (Best Abort Rate)
./labtest -f ../config/tpcc_occ_batch_only.yml -d 30
```

### Analyze Results

```bash
./scripts/analyze_occ_results.sh      # Analyze latest 3 results
./scripts/analyze_occ_results.sh 5    # Analyze latest 5 results
```

Results are saved to `build/results_YYYYMMDD_HHMMSS.csv`

## Documentation

- [PLANNER.md](PLANNER.md) - Technical design and implementation details
- [doc/progress.md](doc/progress.md) - Implementation progress

## License

MIT
