# Distributed Systems Labs in C++

MIT 6.824 style distributed systems lab rebuilt in C++. This project includes a series of labs in which you will build a transactional, sharded, fault-tolerant key/value storage system.

## Status

Read [PLANNER.md](PLANNER.md) for a more detailed info on the project

Read [progress.md](doc/progress.md) for a more detailed on the progress of the project and what is being worked on

Read [team_work](TEAM_WORK.md) for more info on team work distribution

## Lab Assignments

- **Lab 1** - Replicated State Machine (Raft Consensus)
- **Lab 2** - Fault-tolerant Key-value Store
- **Lab 3** - Sharded Key-value Store

## Lab Environment

A modern Linux environment (e.g., Debian 12 or Arch Linux x86-64) with 8-core/16G-memory is recommended for the labs. If you do not have access to this, consider using a cloud virtual machine. The labs possibly work on other environments (Mac, WSL, Other Linux distros, or with fewer CPU/memory resources) but support may vary.

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

### Running Tests

#### Raft Tests (Lab 1)

```bash
./build/labtest -f config/raft_lab_test.yml
```

#### KV Tests (Lab 2)

```bash
./build/labtest -f config/kv_lab_test.yml
```

#### Shard Tests (Lab 3)

```bash
./build/labtest -f config/shard_lab_test.yml
```

#### OCC Integration Tests (Enhanced OCC)

Run integration tests to compare baseline OCC vs Enhanced OCC performance.

##### TPC-C Benchmark (Recommended - High Contention)

TPC-C with 1 warehouse creates guaranteed hotspots (81% abort rate baseline):

```bash
cd build

# TPC-C Baseline OCC (expect ~80% abort rate, ~385 TPS)
./labtest -f ../config/tpcc_occ_baseline.yml -d 10

# TPC-C Enhanced OCC with Early Abort (expect ~34% abort rate, ~900 TPS)
./labtest -f ../config/tpcc_occ_enhanced.yml -d 10
```

**TPC-C Results:**
| Configuration | Abort Rate | TPS | Improvement |
|---------------|------------|-----|-------------|
| Baseline OCC | 81% | 385 | - |
| Enhanced (early abort) | 34% | 901 | **+134% TPS** |

##### RW Benchmark (Low-Medium Contention)

```bash
cd build

# High-contention RW (50 keys, 80% writes) - ~14% abort rate
./labtest -f ../config/occ_high_contention.yml -d 10
./labtest -f ../config/occ_high_contention_enhanced.yml -d 10

# Comprehensive contention sweep
bash ../scripts/contention_test.sh
```

**Parameters:**

| Flag | Description | Default |
|------|-------------|---------|
| `-f` | Config file path | Required |
| `-d` | Test duration (seconds) | 10 |

**Available OCC Configs:**

| Config | Workload | Abort Rate | Description |
|--------|----------|------------|-------------|
| `tpcc_occ_baseline.yml` | TPC-C | ~81% | Baseline OCC, 1 warehouse |
| `tpcc_occ_enhanced.yml` | TPC-C | ~34% | Early abort enabled |
| `occ_high_contention.yml` | RW | ~14% | 50 keys, 80% writes |
| `occ_high_contention_enhanced.yml` | RW | ~14% | Early abort enabled |

**Results Location:**

Results are exported to CSV in `build/`:
```
build/results_YYYYMMDD_HHMMSS.csv
```

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
