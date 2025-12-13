# Enhanced OCC Implementation Plan

## Overview

**Goal**: Improve Mako's Optimistic Concurrency Control (OCC) to handle high-contention workloads better.

**Current Problem**: Baseline OCC has high abort rates (40-60%) when many transactions conflict, wasting CPU on aborted work.

**Our Solution - Two Techniques**:

1. **Parallel Validation**: Validate multiple transactions at once instead of one-by-one
2. **Early Abort Detection**: Detect conflicts during execution, abort immediately instead of wasting work

**Expected Results**:

- 40-60% reduction in abort rates
- 2-5× better throughput on high-contention workloads
- Minimal overhead on low-contention workloads

**Approach**:

- Extend existing OCC (inherit from `SchedulerOcc`, `TxOcc`, `TxnOCC`, `VersionedRow`)
- Keep baseline OCC unchanged for comparison
- Add new enhanced files alongside existing ones

---

## File Structure Tree

```
dslabs-cpp/
├── src/
│   ├── deptran/
│   │   ├── occ/
│   │   │   # Existing baseline files (unchanged)
│   │   │   ├── scheduler.h/cc
│   │   │   ├── coordinator.h
│   │   │   ├── tx.h/cc
│   │   │   │
│   │   │   # NEW: Core enhanced components
│   │   │   ├── scheduler_enhanced.h
│   │   │   ├── scheduler_enhanced.cc
│   │   │   ├── tx_enhanced.h
│   │   │   ├── tx_enhanced.cc
│   │   │   ├── coordinator_enhanced.h
│   │   │   │
│   │   │   # NEW: Batch validation
│   │   │   ├── validation_queue.h
│   │   │   ├── validation_queue.cc
│   │   │   ├── batch_validator.h          # Combines batching + parallel validation
│   │   │   ├── batch_validator.cc
│   │   │   │
│   │   │   # NEW: Early abort detection
│   │   │   ├── early_abort_detector.h
│   │   │   ├── early_abort_detector.cc
│   │   │   │
│   │   │   # NEW: Supporting data structures
│   │   │   ├── conflict_graph.h
│   │   │   ├── conflict_graph.cc
│   │   │   ├── bloom_filter.h             # Header-only template
│   │   │   ├── concurrent_map.h           # Header-only template
│   │   │   ├── batch_metadata.h           # Simple structs
│   │   │   │
│   │   │   # NEW: Tests
│   │   │   └── test/
│   │   │       ├── test_batch_validation.cc
│   │   │       ├── test_early_abort.cc
│   │   │       └── test_conflict_graph.cc
│   │   │
│   │   └── constants.h                    # MODIFY: Add MODE_OCC_ENHANCED
│   │
│   ├── memdb/
│   │   # Existing files (unchanged)
│   │   ├── txn_occ.h/cc
│   │   ├── row.h
│   │   │
│   │   # NEW: Enhanced versions
│   │   ├── txn_occ_enhanced.h
│   │   ├── txn_occ_enhanced.cc
│   │   ├── row_enhanced.h
│   │   └── row_enhanced.cc
│   │
│   └── rrr/                               # No changes
│
├── config/
│   # Existing baseline config (unchanged)
│   ├── occ.yml
│   │
│   # NEW: Enhanced OCC configs
│   ├── occ_enhanced.yml                   # Both features enabled
│   ├── occ_enhanced_batch_only.yml        # Just batching (for testing)
│   └── occ_enhanced_early_abort_only.yml  # Just early abort (for testing)
│
├── benchmark/                             # NEW: Testing scripts
│   ├── occ_comparison.py                  # Compare baseline vs enhanced
│   ├── contention_test.py                 # Test different contention levels
│   └── plot_results.py                    # Visualize results
│
└── docs/
    └── enhanced_occ_design.md             # Detailed design doc (create later)
```

**Total New Files**: ~20 files (10 source files, 5 headers, 3 configs, 3 tests, 3 scripts)

---

## Current OCC - Quick Summary

**How Baseline OCC Works**:

1. **Execution**: Transaction reads/writes, stores versions in `ver_check_read_` and `ver_check_write_`
2. **Validation** (`DoPrepare`): Check if versions changed → abort if yes, acquire locks if no
3. **Commit** (`DoCommit`): Apply writes, increment versions, release locks

**Key Files**:

- `src/memdb/txn_occ.cc` - Transaction with version tracking
- `src/deptran/occ/scheduler.cc` - Validation logic in `DoPrepare()` (line 38-113)

**Current Limitations**:

- Validates one transaction at a time (serial bottleneck)
- No conflict detection during execution (wastes work on doomed transactions)
- Lock acquisition is slow (row-by-row)

---

## What We're Building

### Feature 1: Parallel Batch Validation

**Problem**: Validating transactions one-by-one is slow

**Solution**:

1. Collect transactions into a batch (e.g., 32 transactions)
2. Build conflict graph: which transactions conflict?
3. Partition non-conflicting transactions
4. Validate them in parallel using worker threads
5. Commit in safe order

**Key Components**:

- `ValidationQueue` - Thread-safe queue for transactions awaiting validation
- `BatchValidator` - Coordinates batch processing and parallel validation
- `ConflictGraph` - Analyzes dependencies, finds independent sets

**Benefits**: N transactions validated in ~O(log N) time instead of O(N)

### Feature 2: Early Abort Detection

**Problem**: Transactions execute fully even if they'll fail validation (wasted CPU)

**Solution**:

1. During execution, register all reads/writes with detector
2. When a transaction commits and increments versions, notify detector
3. Detector finds transactions reading old versions → mark for abort
4. Transactions check periodically: "Should I abort?" → stop immediately if yes

**Key Components**:

- `EarlyAbortDetector` - Tracks active reads/writes, detects conflicts
- `TxnOCCEnhanced` - Hooks into read/write to register with detector

**Benefits**: Abort early, save 50%+ of wasted CPU cycles

---

## File Descriptions (Grouped by Purpose)

### Core Enhanced Classes

**scheduler_enhanced.h/cc** - `SchedulerOccEnhanced` class

- Inherits from `SchedulerOcc`
- Overrides `DoPrepare()` to enqueue transactions instead of immediate validation
- Runs background thread to process batches
- Integrates with `BatchValidator` and `EarlyAbortDetector`

**tx_enhanced.h/cc** - `TxOccEnhanced` class

- Inherits from `TxOcc`
- Hooks `ReadColumn()` and `WriteColumn()` to register with early abort detector
- Checks for early abort every N operations
- Maintains metadata for batch validation

**coordinator_enhanced.h** - `CoordinatorOccEnhanced` class

- Inherits from `CoordinatorOcc`
- Simple: just creates enhanced transactions and schedulers

### Batch Validation Components

**validation_queue.h/cc** - `ValidationQueue` class

- Thread-safe queue for transactions waiting for validation
- Supports timeout-based and size-based batching
- `Enqueue(tx)` - Add transaction
- `DequeueBatch(size, timeout)` - Get batch when ready

**batch_validator.h/cc** - `BatchValidator` class

- Main coordinator for batch validation
- Collects transactions into batches
- Builds conflict graph
- Partitions non-conflicting transactions
- Validates them in parallel (using worker thread pool)
- Determines commit order (topological sort)

### Early Abort Components

**early_abort_detector.h/cc** - `EarlyAbortDetector` class

- Tracks active reads: `(row, col)` → `set of (tx_id, version)`
- Tracks active writes: `(row, col)` → `set of tx_id`
- `RegisterRead(tx_id, row, col, version)` - Record read
- `RegisterWrite(tx_id, row, col)` - Record write
- `NotifyVersionChange(row, col, new_version)` - Called on commit → abort conflicting txs
- `ShouldAbort(tx_id)` - Check if transaction should abort

### Supporting Data Structures

**conflict_graph.h/cc** - `ConflictGraph` class

- Graph of transaction dependencies
- `Build(transactions)` - Construct from read/write sets
- `FindIndependentSets()` - Graph coloring for parallel validation
- `TopologicalSort()` - Safe commit order
- Uses adjacency list representation

**bloom_filter.h** - `BloomFilter<T>` template (header-only)

- Probabilistic set membership testing
- Fast conflict pre-filtering
- `Add(element)`, `MayContain(element)`

**concurrent_map.h** - `ConcurrentMap<K,V>` template (header-only)

- Thread-safe hash map using sharding
- 256 shards with per-shard locks
- `Insert()`, `TryGet()`, `Remove()`

**batch_metadata.h** - Simple structs (header-only)

- `BatchMetadata` - Batch ID, position, timestamps, validation result
- `ConflictType` - Enum: READ_WRITE, WRITE_READ, WRITE_WRITE

### Single-Node Layer (memdb)

**txn_occ_enhanced.h/cc** - `TxnOCCEnhanced` class

- Inherits from `TxnOCC`
- Overrides `read_column()` and `write_column()`
- Registers accesses with `EarlyAbortDetector`
- Checks for early abort during execution

**row_enhanced.h/cc** - `VersionedRowEnhanced` class

- Inherits from `VersionedRow`
- May add fields for tracking (or use external tracking in ConcurrentMap)
- Keeps same interface as `VersionedRow`

---

## Getting Started - Implementation Order

### Step 1: Set Up Structure (Week 1)

1. Create all new files with skeleton classes
2. Add `MODE_OCC_ENHANCED` to `src/deptran/constants.h`
3. Update `src/deptran/frame.cc` to register enhanced classes
4. Add includes and factory methods
5. **Goal**: Code compiles, can create enhanced instances (no functionality yet)

### Step 2: Basic Batching (Week 2)

1. Implement `ValidationQueue` - thread-safe queue with timeout
2. Implement `BatchValidator` - collect batches (serial validation first, no parallelism)
3. Update `SchedulerOccEnhanced::DoPrepare()` - enqueue instead of validate
4. Background thread dequeues batches and validates serially
5. **Goal**: Transactions processed in batches (correctness verified)

### Step 3: Parallel Validation (Week 3)

1. Implement `ConflictGraph` - build from transactions, find independent sets
2. Add worker thread pool to `BatchValidator`
3. Partition batch using conflict graph
4. Validate partitions in parallel
5. **Goal**: Parallel validation working, throughput improvement measurable

### Step 4: Early Abort Detection (Week 4-5)

1. Implement `EarlyAbortDetector` - track reads/writes
2. Update `TxnOCCEnhanced` - register accesses, check abort status
3. Hook into commit to notify detector
4. **Goal**: Early abort working, reduced wasted work

### Step 5: Testing & Optimization (Week 6-8)

1. Write unit tests for each component
2. Run TPC-C benchmarks, compare with baseline
3. Profile and optimize hot paths
4. Tune parameters (batch size, timeout, check interval)
5. **Goal**: Performance targets met, all tests passing

---

## Configuration

### config/occ_enhanced.yml (Both Features)

```yaml
mode: occ_enhanced

batch_validation:
  enabled: true
  batch_size: 32 # Max transactions per batch
  batch_timeout_us: 100 # Wait up to 100μs for batch
  num_workers: 8 # Parallel validation threads

early_abort:
  enabled: true
  check_interval: 10 # Check for abort every 10 operations
  bloom_filter_size: 10000 # Bits for bloom filter
```

### config/occ_enhanced_batch_only.yml (Ablation Test)

```yaml
mode: occ_enhanced
batch_validation:
  enabled: true
  batch_size: 32
  num_workers: 8
early_abort:
  enabled: false # Disable to test batching alone
```

---

## Testing Strategy

### Unit Tests

- `test_batch_validation.cc` - Queue, batching, correctness
- `test_early_abort.cc` - Conflict detection, abort notification
- `test_conflict_graph.cc` - Graph algorithms, independent sets

### Benchmark Scripts

- `occ_comparison.py` - Run same workload with baseline and enhanced, compare metrics
- `contention_test.py` - Vary contention (TPC-C warehouses), measure performance
- `plot_results.py` - Generate graphs

### Metrics to Track

- **Throughput**: Transactions per second
- **Abort Rate**: % of transactions aborted
- **Latency**: P50, P95, P99
- **Wasted Work**: Operations executed in aborted transactions
- **Batch Size**: Average transactions per batch

---

## Success Criteria

**Correctness**:

- ✅ All tests pass
- ✅ Same results as baseline OCC on deterministic workloads
- ✅ No deadlocks or crashes in stress tests

**Performance**:

- ✅ 40-60% abort rate reduction on TPC-C with 1-2 warehouses
- ✅ 2-5× throughput improvement on high contention
- ✅ <10% overhead on low contention (8+ warehouses)

**Code Quality**:

- ✅ Clean, well-commented code
- ✅ Follows project conventions
- ✅ Easy to configure and use

---

## Key Design Decisions

### Why Extend Instead of Modify?

- Keep baseline OCC working for comparison
- Safe fallback if issues arise
- Easy A/B testing

### Why Both Layers (deptran + memdb)?

- deptran: Distributed protocol coordination
- memdb: Single-node transaction execution
- Both need enhancements for full benefit

### Why Batch + Early Abort Together?

- **Batching**: Improves throughput via parallelism
- **Early Abort**: Reduces wasted work
- **Combined**: Multiplicative effect (batch more, waste less)

---

## Next Steps After Implementation

### Phase 1: Evaluation

1. Run comprehensive benchmarks
2. Write evaluation report
3. Tune parameters for different workloads

### Phase 2: Future Enhancements

- Adaptive batching (dynamic batch size)
- MOCC-style selective pessimistic locking for hotspots
- Read-only transaction fast path
- ML-based conflict prediction

---

## Quick Reference

### Main Classes to Implement

1. `SchedulerOccEnhanced` - Batch validation coordinator
2. `TxOccEnhanced` - Transaction with early abort
3. `ValidationQueue` - Thread-safe transaction queue
4. `BatchValidator` - Batch processing + parallel validation
5. `EarlyAbortDetector` - Runtime conflict detection
6. `ConflictGraph` - Dependency analysis
7. `TxnOCCEnhanced` - Single-node transaction with early abort
8. `VersionedRowEnhanced` - Enhanced row structure

### Key Algorithms

- **Batch Collection**: Size threshold OR timeout
- **Conflict Graph**: Adjacency list from read/write sets
- **Independent Sets**: Graph coloring (greedy)
- **Commit Order**: Topological sort
- **Early Abort**: Track reads → notify on version change → mark conflicting txs

### Integration Points

- `src/deptran/frame.cc` - Register factories
- `src/deptran/constants.h` - Add mode constant
- `CMakeLists.txt` - Add new source files

---

## Summary

This plan provides a clear path to implementing enhanced OCC with:

- **Parallel batch validation** for throughput
- **Early abort detection** for efficiency
- **~20 new files** organized logically
- **Phased implementation** over 6-8 weeks
- **Clear success metrics** to validate improvements

Start with the file structure and skeleton classes, then build up functionality incrementally. Test after each phase to ensure correctness.

---

## Implementation Status (Updated 2025-12-12)

### ✅ COMPLETE: All 4 Implementation Steps (Code Complete)

**Step 1: Set Up Structure** ✅
**Step 2: Basic Batching** ✅
**Step 3: Parallel Validation** ✅
**Step 4: Early Abort Detection** ✅

**Build Status**: ✅ SUCCESS - All code compiles

---

## 🚨 CRITICAL DISCOVERY: Architecture Mismatch

### The Problem

Both optimizations were designed for **multi-threaded** systems, but this codebase uses **coroutines**.

| Feature | With Coroutines | With Multi-threading |
|---------|-----------------|---------------------|
| **Parallel Validation** | ❌ Batches always size 1 | ✅ True concurrent arrivals |
| **Early Abort** | ❌ Cascade aborts | ✅ Detects real conflicts |

### Root Cause

1. **Coroutine `future.get()` blocks** - Only 1 transaction in validation queue at a time
2. **Coroutine yields create artificial conflicts** - All concurrent TXs interleave on single thread
3. **One commit aborts all others** - When TX1 commits, ALL reading same keys abort

### Solution: Revised Phased Approach

See **Revised Implementation Plan** section below.

---

## Revised Implementation Plan (Dec 2025)

### Phase 1: Thread-Safety Foundation ✅ COMPLETE

Make row operations thread-safe for future multi-threading:

| Component | Status | Change |
|-----------|--------|--------|
| `RWLock` | ✅ Done | Added `std::mutex` protection |
| `VersionedRow::ver_` | ✅ Done | Changed to `std::atomic<version_t>` |
| Parallel Validation Code | ✅ Done | Verified working (limited by coroutines) |
| Early Abort Code | ✅ Done | Verified working (cascade aborts) |

**Test Result:** 2,725 TPS (vs baseline 2,831) - minimal mutex overhead

### Phase 2: Execution Threading 🔄 NEXT

Replace coroutine-based execution with thread pool:

| Task | Files | Description |
|------|-------|-------------|
| Thread pool for TX execution | `server_worker.cc` | Replace coroutine dispatch |
| Remove blocking yields | `rrr/coroutine/` | Bypass `Coroutine::Sleep()` |
| Re-enable batch validation | `scheduler_enhanced.cc` | Uncomment batch validator |

**Expected Benefit:** 15-30% throughput improvement

### Phase 3: Optimization & Benchmarking

1. Tune batch parameters (size, timeout, threshold)
2. Run comprehensive benchmarks
3. Compare with baseline OCC
4. Document findings

---

## Current Configuration (Until Phase 2)

```yaml
batch_validation:
  enabled: false  # Disabled - adds overhead without benefit

early_abort:
  enabled: true   # Enabled but limited by coroutines
```

**Rationale:** Batch validation adds promise/future overhead but batches are always size 1 due to coroutine blocking. Early abort is enabled but causes cascade aborts.

---

## Important Discovery: Two Independent Implementations

During integration, we discovered two team members independently implemented the Enhanced OCC system with fundamentally different approaches:

**Conway's Implementation (Current Main Branch):**

- Namespace: `janus::`
- Strategy: Tightly integrated with existing framework
- Performance: 256-shard ConcurrentMap for high concurrency
- Types: Uses framework types (`Row*`, `mdb::colid_t`)
- Size: More comprehensive (+1,695 lines)

**Aditya's Implementation (working-dev-branch):**

- Namespace: `deptran::`
- Strategy: Modular, standalone design
- Performance: Single `std::shared_mutex` (simpler)
- Types: Simple types (`key_t`, `txn_id_t`)
- Size: More concise (-705 net lines)
- Features: Cycle detection, multiple abort strategies

**Decision**: Keep Conway's implementation (already integrated, better performance)
**Action**: Document Aditya's unique features for future consideration

---

## Next Steps After Implementation

### Phase 1: Testing (Current Priority)

1. **Write Tests Compatible with janus:: Implementation**
   - Test ConflictGraph functionality
   - Test EarlyAbortDetector behavior
   - Test ValidationQueue thread safety
   - Aditya's tests serve as reference

2. **Integration Testing**
   - Run with config/occ_integration_quick.yml
   - Run with config/occ_integration_full.yml
   - Compare results with baseline OCC
   - Verify correctness

3. **Unit Testing**
   - Test individual components
   - Test edge cases (empty batches, single transaction, max batch size)
   - Test error handling

### Phase 2: Configuration

1. Create comprehensive config files:
   - `config/occ_enhanced.yml` - Both features enabled
   - `config/occ_enhanced_batch_only.yml` - Just batching
   - `config/occ_enhanced_early_abort_only.yml` - Just early abort

2. Tune parameters:
   - `batch_size` - Optimal batch size for different workloads
   - `batch_timeout_us` - Balance between latency and batching
   - `num_workers` - Match to available CPU cores
   - `check_interval` - Balance between overhead and responsiveness

### Phase 3: Evaluation (Week 6-8)

1. **Benchmarking**
   - Run TPC-C with varying contention (1-16 warehouses)
   - Measure throughput (transactions per second)
   - Measure abort rates (%)
   - Measure latency (P50, P95, P99)
   - Measure wasted work (operations in aborted transactions)

2. **Comparison**
   - Baseline OCC vs Enhanced OCC
   - Batch-only vs Early-abort-only vs Combined
   - Different parameter settings

3. **Documentation**
   - Write evaluation report
   - Document findings
   - Create performance graphs

---

## Future Enhancements (Potential Features from Aditya's Implementation)

Features worth considering for integration:

1. **Cycle Detection in ConflictGraph**
   - Add explicit `has_cycle()` method
   - Useful for deadlock detection
   - Can complement existing dependency analysis

2. **Multiple Early Abort Strategies**
   - Current: Version-based detection
   - Add: Cycle detection strategy
   - Add: Excessive conflict threshold
   - Allow configurable strategy selection

3. **Bloom Filter Optimizations**
   - Consider C++20 `<bit>` header usage
   - Evaluate performance impact
   - May require C++20 compiler support

4. **Modular Testing Framework**
   - Aditya's standalone design easier to unit test
   - Consider extracting core algorithms for isolated testing
   - Keep integration tests with framework

---

## Summary

This plan provided a clear path to implementing enhanced OCC with:

- ✅ **Parallel batch validation** code complete (needs threading to work)
- ✅ **Early abort detection** code complete (needs threading to work)
- ✅ **~20 new files** organized logically
- ✅ **Thread-safety foundation** complete (Phase 1)
- 🚨 **Critical discovery**: Coroutine architecture incompatible
- 🔄 **Revised approach**: Phase 2 execution threading required

**Status**: Phase 1 complete, Phase 2 (execution threading) is next priority.

---

## Current Focus: Phase 2 Execution Threading

### Why Threading is Required

Both optimizations need true parallel execution:

| Without Threading | With Threading |
|-------------------|----------------|
| Batches size 1 | Batches size N |
| Cascade aborts | Real conflict detection |
| Workers idle | Workers utilized |
| No improvement | 15-30% improvement |

### Phase 2 Tasks

1. **Analyze coroutine usage** in `server_worker.cc`
2. **Design thread pool** for transaction execution
3. **Replace or bypass** `Coroutine::Sleep()` yields
4. **Re-enable batch validation** once threading works
5. **Benchmark and optimize**

### Key Files for Phase 2

- `src/deptran/server_worker.cc` - Transaction dispatch
- `src/rrr/coroutine/` - Coroutine implementation
- `src/deptran/occ/scheduler_enhanced.cc` - Re-enable batch validator

### Success Criteria

- [ ] Transactions run on separate threads
- [ ] Batch sizes > 1 in logs
- [ ] Early abort reduces abort rate
- [ ] >15% throughput improvement

See `doc/threading.md` for detailed Phase 2 plan.
