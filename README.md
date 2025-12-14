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

### Quick Start (TPC-C - High Contention)

```bash
cd build

# Baseline OCC
./labtest -f ../config/tpcc_occ_baseline.yml -d 10

# Batch Validation Only
./labtest -f ../config/tpcc_occ_batch_only.yml -d 10

# Early Abort Only
./labtest -f ../config/tpcc_occ_enhanced.yml -d 10
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

### Analysis Scripts

```bash
# Comprehensive contention sweep (tests different population sizes)
bash scripts/contention_test.sh

# Compare OCC configurations
python3 scripts/compare_occ.py

# Results are exported to CSV files in the current directory
ls *.csv
```

### Available OCC Configurations

| Config                    | Workload | Abort Rate | TPS  | Description                         |
| ------------------------- | -------- | ---------- | ---- | ----------------------------------- |
| `tpcc_occ_baseline.yml`   | TPC-C    | ~59%       | ~490 | Baseline OCC, 1 warehouse           |
| `tpcc_occ_batch_only.yml` | TPC-C    | ~19%       | ~958 | **Best - batch validation (1.96x)** |
| `tpcc_occ_enhanced.yml`   | TPC-C    | ~34%       | ~595 | Early abort only (1.22x)            |

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
