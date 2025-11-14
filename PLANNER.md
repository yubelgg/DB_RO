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
