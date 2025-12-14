# Distributed Systems Labs in C++

MIT 6.824 style distributed systems lab rebuilt in C++. This project includes a series of labs in which you will build a transactional, sharded, fault-tolerant key/value storage system.

## Enhanced OCC Project

**Goal**: Improve Optimistic Concurrency Control (OCC) throughput and reduce abort rates for high-contention workloads.

**Achievement**: **~2x throughput improvement** with batch validation on TPC-C benchmark.

| Configuration                       | TPS      | vs Baseline | Abort Rate |
| ----------------------------------- | -------- | ----------- | ---------- |
| Baseline OCC                        | ~490     | 1.0x        | ~59%       |
| Early Abort Only                    | ~595     | 1.22x       | ~34%       |
| **Batch Validation (batch_size=4)** | **~958** | **~1.96x**  | **~19%**   |

**Key Insight**: The throughput improvement comes from queue-based yielding (`BoxEvent::Wait()`) which allows multiple transactions to be in-flight simultaneously, combined with serial validation that prevents lock contention.

## Documentation

- [PLANNER.md](PLANNER.md) - Detailed implementation plan and technical design
- [doc/progress.md](doc/progress.md) - Implementation progress and benchmark results
- [TEAM_WORK.md](TEAM_WORK.md) - Team work distribution

## Getting Started

### Get Source Code

```bash
git clone --recursive [repo-addr]
cd janus
```

### Install Dependencies

#### Debian/Ubuntu

```bash
sudo bash apt_packages.sh
```

#### Arch Linux

```bash
sudo bash pacman_packages.sh
pip install -r requirements.txt
```

**Note**: On Arch Linux, development headers are included with base packages (no separate `-dev` packages needed). The script uses `base-devel` instead of `build-essential` and installs the latest LLVM/Clang versions from the rolling release.

### Build

```bash
make clean
make labtest
```

First time build could take time (10 minutes). You can add `-j32` to speed up building if you have enough CPU and memory.

## Enhanced OCC Integration Tests

Run TPC-C benchmark to compare baseline OCC vs Enhanced OCC performance.

### Quick Comparison (Recommended)

Run all three OCC configurations and see comparison results:

```bash
./scripts/run_occ_comparison.sh [duration] [timeout]

# Examples:
./scripts/run_occ_comparison.sh 10      # 10s tests, auto timeout (50s)
./scripts/run_occ_comparison.sh 30      # 30s tests, auto timeout (90s)
./scripts/run_occ_comparison.sh 10 120  # 10s tests, manual 120s timeout
```

**Features:**
- Runs baseline, early_abort, and batch_validation tests in sequence
- Clean progress output: `[1/3] Running baseline... DONE (12s)`
- Proportional timeout: `2×duration + 30s` (prevents hangs)
- Asks before deleting old result files
- Shows where results are saved
- Automatic comparison table at the end

**Sample Output:**
```
=== OCC Benchmark Comparison ===

Mode              TPS    Abort%   vs Baseline  Abort Reduction
------------------------------------------------------------------------
occ_batch        716.8    15.5%       -12.6%           +69.3%
occ_early_abort  931.0    34.4%       +13.6%           +31.8%
occ              819.8    50.4%   (baseline)                -
------------------------------------------------------------------------
```

### Analyze Existing Results

```bash
./scripts/analyze_occ_results.sh       # Analyze latest 3 CSV files
./scripts/analyze_occ_results.sh 5     # Analyze latest 5 CSV files
```

### Manual Testing

```bash
cd build

# Baseline OCC
./labtest -f ../config/tpcc_occ_baseline.yml -d 10

# Batch Validation Only (Best Performance)
./labtest -f ../config/tpcc_occ_batch_only.yml -d 10

# Early Abort Only
./labtest -f ../config/tpcc_occ_early_abort.yml -d 10
```

### Run Multiple Tests for Consistency

```bash
cd build

# Run 5 tests with batch validation
for i in {1..5}; do
  echo "=== Run $i ==="
  ./labtest -f ../config/tpcc_occ_batch_only.yml -d 10 2>&1 | grep -E "Total:|TPS"
done
```

### Available OCC Configurations

| Config                      | Workload | Abort Rate | TPS  | Description                         |
| --------------------------- | -------- | ---------- | ---- | ----------------------------------- |
| `tpcc_occ_baseline.yml`     | TPC-C    | ~50-60%    | ~500 | Baseline OCC, 1 warehouse           |
| `tpcc_occ_batch_only.yml`   | TPC-C    | ~15-20%    | ~700 | **Best - batch validation**         |
| `tpcc_occ_early_abort.yml`  | TPC-C    | ~34%       | ~900 | Early abort only                    |

### Test Parameters

| Flag | Description             | Default  |
| ---- | ----------------------- | -------- |
| `-f` | Config file path        | Required |
| `-d` | Test duration (seconds) | 10       |

### Results Location

Results are exported to CSV in `build/`:

```
build/results_YYYYMMDD_HHMMSS.csv
```

CSV columns: timestamp, mode, duration, attempted, committed, aborted, abort_rate, tps, early_aborts

## Authors and Acknowledgements

Authors of the lab framework:

- Shuai Mu
- Julie Lee
- Devika Sudheer
- Radhika Agarwal

Many of the lab structure and guideline text are adapted from MIT 6.824.

Thanks for external users of the labs for feedback and fixes: Seo Jin Park (USC) and their students.

The code is based on academic prototypes of previous research works including but not limited to:

- **Mako**: [OSDI'25] "Speculative Distributed Transactions with Geo-Replication"
- **NCC**: [OSDI'23] "Natural Concurrency Control for Strictly Serializable Datastores by Avoiding the Timestamp-Inversion Pitfall"
- **Janus**: [OSDI'16] "Consolidating Concurrency Control and Consensus for Commits under Conflicts"
- **Rococo**: [OSDI'14] "Extracting More Concurrency from Distributed Transactions"

## Additional Resources

- Course website: Check the guidelines on the course web page for lab-specific instructions.

## License

MIT
