# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repository contains two related distributed transaction systems:

- **Janus**: Implementation of the OSDI'16 paper "Consolidating Concurrency Control and Consensus for Commits under Conflicts"
- **Mako**: A speculative distributed transaction system with geo-replication (OSDI'25)

The codebase is primarily C++17 with multiple build systems (CMake, Makefile, WAF).

## Build Commands

### Important: Build Time Expectations

**WARNING**: This is a large C++ project with extensive template usage and multiple dependencies. Build times can be significant:

- **Initial full build**: 10-30 minutes depending on CPU and parallelism
- **Incremental builds**: 2-10 minutes depending on changes
- **Docker image build**: 10-30 minutes for first build
- **RustyCpp borrow checking**: Adds 1-2 minutes per file

**When running build commands, DO NOT use short timeouts (e.g., 30s, 60s, 120s). Use longer timeouts or no timeout:**

- For full builds: Use at least 30 minutes timeout (1800000ms)
- For incremental builds: Use at least 10 minutes timeout (600000ms)
- For Docker builds: Use at least 30 minutes timeout
- Better: Don't specify a timeout and let the build complete naturally

### Primary Build (CMake - Recommended for Mako)

```bash
# Default: Build only dbtest (fastest)
make -j32

# Build all executables (dbtest, simpleTransaction, simplePaxos, etc.)
make build -j32

# Clean and rebuild
make clean
make -j32
```

**Note**: The default `make` target now builds only `dbtest` for faster iteration. Use `make build` to build all executables.

## Testing Commands

```bash
# run all experiments
./ci/ci.sh all

# simple transactions
./ci/ci.sh simpleTransaction

# simple replication
./ci/ci.sh simplePaxos

# two shards without replication
./ci/ci.sh shardNoReplication

# 1 shard with replication on dbtest
./ci/ci.sh shard1Replication

# 2 shards with replication on dbtest
./ci/ci.sh shard2Replication

# 1 shard with replication on simple transaction
./ci/ci.sh shard1ReplicationSimple

# 2 shards with replication on simple transaction
./ci/ci.sh shard2ReplicationSimple

# RocksDB persistence and partitioned queues tests
./ci/ci.sh rocksdbTests
```

## Code Architecture

### Core Directory Structure

- `src/deptran/`: Transaction protocol implementations (Janus, 2PL, OCC, RCC, Paxos, TAPIR, Snow)
- `src/mako/`: Mako system with Masstree storage engine and speculative execution
- `src/bench/`: Benchmark implementations (TPC-C, TPC-A, RW, Micro)
- `src/rrr/`: Custom RPC framework and networking layer
- `config/`: YAML configuration files for experiments and cluster topology

### Key Protocol Implementations

The system implements multiple distributed transaction protocols:

- **Janus** (`src/deptran/janus/`): Main protocol with graph-based dependency tracking
- **2PL** (`src/deptran/2pl/`): Traditional two-phase locking
- **OCC** (`src/deptran/occ/`): Optimistic concurrency control
- **RCC/Rococo** (`src/deptran/rcc/`): Distributed consensus protocol
- **Paxos** (`src/deptran/paxos/`): Consensus for replication

### Transport Layer Architecture

The system supports multiple transport mechanisms:

- Standard Ethernet via `src/rrr/` RPC framework
- DPDK for kernel bypass (`DPDK_ENABLED` flag)
- InfiniBand/RDMA support (`src/deptran/rcc_rpc.cpp`)
- eRPC integration for high-performance networking

### Configuration System

- **Host configuration**: `config/hosts*.yml` defines cluster topology and network settings
- **Benchmark configuration**: YAML files specify workload parameters
- **Build configuration**: Controlled via CMake flags or Makefile variables (SHARDS, PAXOS_LIB_ENABLED, etc.)

### Key Classes and Components

- `TxnCoordinator`: Coordinates distributed transactions across shards
- `TxnScheduler`: Handles transaction scheduling and execution
- `Communicator`: Manages RPC communication between nodes
- `Frame`: Protocol-specific transaction processing logic
- `Masstree`: High-performance in-memory index structure (Mako)

### Memory Management

- Uses jemalloc for optimized memory allocation
- Lock-free data structures in performance-critical paths
- Custom memory pools for reduced allocation overhead
- **RustyCpp Migration**: Incrementally migrating to Rust-style smart pointers for memory safety

## Development Notes

### RustyCpp Smart Pointer Migration (In Progress)

The RRR framework is being migrated to use RustyCpp smart pointers for enhanced memory safety.

#### Successfully Migrated Components

- ✅ Event system: Cell<EventStatus> for interior mutability
- ✅ IntEvent: Cell<int> for value field
- ✅ Custom Weak<Coroutine> wrapper replacing std::weak_ptr
- ✅ Collections: std::list → Vec (aliased to std::vector)
- ✅ PollMgr: Raw array → Vec<std::unique_ptr<PollThread>>

#### Migration Guidelines

1. **Make small incremental changes**: Change one field/function at a time
2. **Test after each change**: Run `ctest` immediately after each modification
3. **Use RustyCpp types exclusively for new code**:
   - `rusty::Box<T>` for single ownership (instead of unique_ptr)
   - `rusty::Arc<T>` for thread-safe sharing (instead of shared_ptr)
   - `rusty::Rc<T>` for single-thread sharing
   - `rusty::Cell<T>` for interior mutability of Copy types
   - `rusty::RefCell<T>` for interior mutability of complex types
4. **Never use C++ standard library smart pointers** in new code
5. **Document safety annotations**: Add `// @safe` or `// @unsafe` comments

### Writing Safe C++ Code (Following RustyCpp Guidelines)

When writing new C++ code or modifying existing code, follow these safety guidelines to ensure compatibility with RustyCpp borrow checking:

#### Memory Safety Rules

1. **Ownership**: Every object should have a single owner at any given time
2. **Borrowing**: Use references (`&`) for read-only access, avoid raw pointers when possible
3. **Lifetime**: Ensure references don't outlive the objects they refer to
4. **Move Semantics**: Prefer `std::move` for transferring ownership, avoid use-after-move

#### Best Practices for RustyCpp Compliance

- **Smart Pointers**: Use RustyCpp types exclusively:
  - `rusty::Box<T>` for single ownership (NOT std::unique_ptr)
  - `rusty::Arc<T>`/`rusty::Rc<T>` for shared ownership (NOT std::shared_ptr)
  - Custom `Weak<T>` wrapper for weak references (NOT std::weak_ptr)
- **RAII**: Always use RAII (Resource Acquisition Is Initialization) patterns
- **Const Correctness**: Mark methods and parameters `const` when they don't modify state
- **Avoid Global State**: Minimize global variables and static mutable state
- **Reference Parameters**: Prefer `const&` for input parameters, avoid non-const references when possible
- **Return Values**: Return by value for small objects, use move semantics for large objects

#### Common Patterns to Avoid

- Double deletion or use-after-free
- Returning references to local variables
- Storing raw pointers without clear ownership
- Circular references without weak pointers
- Mutable aliasing (multiple mutable references to the same object)

#### Borrow Checking Integration

The project uses RustyCpp for static analysis. To ensure your code passes borrow checking:

- Build with `ENABLE_BORROW_CHECKING=ON` during development
- Run `make borrow_check_all_dbtest` to verify all files
- Address any violations before committing

#### Interior Mutability Patterns

When you need to mutate data through shared references:

- Use `Cell<T>` for Copy types (int, bool, etc.) - zero overhead
- Use `RefCell<T>` for complex types - runtime borrow checking
- Example:

```cpp
mutable CopyMut<int> counter_{0};  // Cell<int>
counter_.set(counter_.get() + 1);  // Mutation through const method
```

### Adding New Transaction Protocols

New protocols should be added under `src/deptran/` following the existing pattern:

1. Create protocol directory with coordinator, scheduler, and frame implementations
2. Register in `src/deptran/frame.cc` and `src/deptran/scheduler.cc`
3. Add configuration support in benchmark YAML files

### Modifying Benchmarks

Benchmarks are in `src/bench/`. Each benchmark has:

- Workload generator (`*_workload.cc`)
- Piece registration (`*_pieces.cc`)
- Stored procedures (`*_procedures.cc`)

### Debugging

- Use `MODE=debug` for debug builds with symbols
- Enable logging with environment variables or config files
- Use `gdb` or `lldb` with the generated executables

### Performance Profiling

- Build with `MODE=perf` for optimized builds
- Use Google perftools (linked automatically)
- Profile with `perf record` and analyze with `perf report`

## Platform-Specific Installation Notes

### Arch Linux

The project now supports Arch Linux in addition to Debian/Ubuntu. Use `pacman_packages.sh` for installation:

```bash
sudo bash pacman_packages.sh
pip install -r requirements.txt
```

#### Key Differences from Debian

- **Package naming**: `base-devel` instead of `build-essential`, `the_silver_searcher` instead of `silversearcher-ag`
- **Development headers**: Included with base packages (no separate `-dev` packages)
- **LLVM/Clang versions**: Uses latest from rolling release (currently 17+) instead of LLVM 14
- **Python**: Python 3 is default; Python 2 deprecated (available via AUR if needed: `yay -S python2`)
- **System libraries**: `systemd-libs` provides both `libsystemd` and `libudev` functionality
- **Cgroup support**: Built into systemd (no separate `libcgroup` package needed)
- **PMDK**: Not available in official repos (only AUR), not required for lab tests
- **Boost 1.89**: `boost_system` is header-only (removed as compiled library), CMakeLists.txt updated accordingly

#### Arch Linux Package Mapping

| Debian Package                   | Arch Equivalent        | Notes                          |
| -------------------------------- | ---------------------- | ------------------------------ |
| `build-essential`                | `base-devel`           |                                |
| `libboost-all-dev`               | `boost` + `boost-libs` | boost_system is header-only    |
| `silversearcher-ag`              | `the_silver_searcher`  |                                |
| `libgoogle-perftools-dev`        | `gperftools`           |                                |
| `libibverbs-dev`                 | `rdma-core`            |                                |
| `libsystemd-dev` + `libudev-dev` | `systemd-libs`         |                                |
| `cgroup-tools` / `libcgroup-dev` | Built into systemd     | No separate package            |
| `libpmem-dev` / `pmdk`           | Not included           | AUR only, not needed for labs  |
| `ifmetric`                       | Not included           | Use iproute2 for route metrics |

### Optional Dependencies (All Platforms)

#### Python 2 (Deprecated)

Python 2 is largely removed from the codebase. Only install if absolutely necessary:

- **Debian**: `sudo apt-get install python2`
- **Arch**: `yay -S python2` (AUR package)

#### RDMA-Core (for InfiniBand/RDMA Support)

If you need InfiniBand or RDMA support, build from source:

```bash
git clone https://github.com/linux-rdma/rdma-core
cd rdma-core
bash build.sh
cmake .
sudo make install -j10
```

#### eRPC (High-Performance RPC Framework)

For high-performance networking with DPDK support:

```bash
git clone https://github.com/shenweihai1/eRPC.git
cd eRPC
cmake . -DTRANSPORT=dpdk -DAZURE=on -DPERF=ON
make -j32
make latency
```

#### Rust 1.80.0 (Specific Version for RustyCpp)

The project uses Rust 1.80.0 for borrow checking. Install via rustup:

```bash
rustup install 1.80.0
rustup default 1.80.0
```

Or manually:

```bash
curl -LO https://static.rust-lang.org/dist/rust-1.80.0-x86_64-unknown-linux-gnu.tar.gz
tar xzf rust-1.80.0-x86_64-unknown-linux-gnu.tar.gz
cd rust-1.80.0-x86_64-unknown-linux-gnu/
./install.sh --prefix=$HOME/.local-rust
```

#### DPDK 19.11.5 (Specific Version for eRPC)

If you need the exact DPDK version for eRPC with Mellanox NICs:

```bash
wget http://static.dpdk.org/rel/dpdk-19.11.5.tar.gz
tar -xvf dpdk-19.11.5.tar.gz
cd dpdk-stable-19.11.5
make config T=x86_64-native-linuxapp-gcc
# Enable MLX4/MLX5 PMD for Mellanox NICs
sed -ri 's,(MLX._PMD=)n,\1y,' build/.config
make -j32
sudo make install T=x86_64-native-linuxapp-gcc DESTDIR=/usr -j32
```

## Raft Lab Testing

### Build and Test Commands

#### CMake Build (Recommended)

```bash
# Build with Raft tests enabled (equivalent to WAF's --enable-raft-test)
cd build
cmake -DBUILD_RAFT_LAB_TESTS=ON ..
make -j32

# Or rebuild if already configured
cd build
rm -rf *
cmake -DBUILD_RAFT_LAB_TESTS=ON ..
make -j32

# Run Raft tests using labtest executable
./labtest -f ../config/raft_lab_test.yml
```

#### WAF Build (Legacy, for lab-solution branch)

```bash
# Build with Raft tests enabled
python3 waf configure build --enable-raft-test

# Run Raft tests
./build/deptran_server -f config/raft_lab_test.yml
```

### Important: Test Timeout

**When running Raft lab tests, always use a timeout of at least 10 minutes (600 seconds)**. The tests can take a long time to complete, especially test 2 which can hang occasionally.

Example:

```bash
timeout 600 ./build/labtest -f config/raft_lab_test.yml
```

### How It Works

- The `BUILD_RAFT_LAB_TESTS` CMake option adds the `RAFT_TEST_CORO` compile definition to the txlog library
- This enables Raft test code in `src/deptran/raft/test.cc` and `src/deptran/raft/testconf.cc`
- The `labtest` executable is built from `src/deptran/s_main.cc` (equivalent to WAF's `deptran_server`)
- Tests are triggered through the RaftFrame when running `labtest` with appropriate config files
- Test code includes: election tests, agreement tests, Figure 8 scenarios, KV operations, and sharding tests
- **Note**: `dbtest` is for Mako benchmarks only, not for lab tests

### Prompts

- Ask clarifying questions for each prompt that you need to understand
- keep the responses detailed and concise
