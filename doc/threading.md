# Multi-threaded OCC Implementation Plan

## Overview

**Goal:** Enable parallel validation and early abort detection by adding multi-threading.

**Approach:** Incremental (2 phases)

| Phase | Scope | Benefit | Status |
|-------|-------|---------|--------|
| **Phase 1** | Validation threading | ~20-30% throughput | Planned |
| **Phase 2** | Execution threading | Early abort works | Future |

---

## Phase 1: Validation Threading

### Goal
Move transaction validation from single-threaded to thread pool, enabling parallel validation of non-conflicting transactions.

### Scope
- **IN SCOPE:** Validation phase (`DoPrepare`) runs on worker threads
- **OUT OF SCOPE:** Transaction execution still uses coroutines

### Expected Benefit
- ~20-30% throughput improvement
- Foundation for Phase 2

---

## Current State

```
RPC Request → Coroutine → Execute → Validate (serial) → Commit
                              ↑
                         Single thread
```

### After Phase 1

```
RPC Request → Coroutine → Execute → Validate (thread pool) → Commit
                              ↑              ↑
                         Coroutine      Worker threads
```

---

## Files to Modify

### 1. Row Lock Atomicity
**File:** `src/memdb/row.h` and `src/memdb/row.cc`

**Current:** Row locks may not be atomic
```cpp
bool rlock_row_by(txn_id_t id);  // Not thread-safe
bool wlock_row_by(txn_id_t id);  // Not thread-safe
```

**Change:** Use atomic operations or mutex
```cpp
std::atomic<txn_id_t> read_lock_holder_;
std::atomic<txn_id_t> write_lock_holder_;
// OR
std::mutex row_mutex_;
```

### 2. Version Counter Atomicity
**File:** `src/memdb/row.h`

**Current:** Version increment may not be atomic
```cpp
void incr_column_ver(column_id_t col_id);  // RMW - race condition
```

**Change:** Use atomic increment
```cpp
std::atomic<version_t> version_;
version_.fetch_add(1, std::memory_order_release);
```

### 3. Validation Integration
**File:** `src/deptran/occ/scheduler_enhanced.cc`

**Current:** BatchValidator has workers but not fully integrated
```cpp
// Workers exist but validation still mostly serial
batch_validator_->ValidateBatch(batch);
```

**Change:** Ensure workers actually parallelize
- Verify `ValidateBatchParallel()` is called for large batches
- Check `parallel_threshold` config is reasonable
- Ensure workers don't contend on shared state

### 4. Transaction State Protection
**File:** `src/deptran/scheduler.h`

**Current:** Transaction map protected by recursive_mutex
```cpp
std::recursive_mutex mtx_;
std::unordered_map<txid_t, shared_ptr<Tx>> dtxns_;
```

**Status:** Already thread-safe (mutex exists)

**Verify:** All access paths go through mutex

---

## Implementation Steps

### Step 1: Audit Row Thread-Safety (1-2 days)
1. Read `src/memdb/row.h` and `row.cc`
2. Identify all lock/version operations
3. Document which are thread-safe vs not
4. Create list of changes needed

### Step 2: Atomicize Row Operations (3-5 days)
1. Add atomic types to row lock state
2. Add atomic version counter
3. Update `rlock_row_by()` and `wlock_row_by()`
4. Update `incr_column_ver()`
5. Compile and fix errors

### Step 3: Verify BatchValidator Integration (2-3 days)
1. Add logging to confirm parallel path is taken
2. Tune `parallel_threshold` config
3. Test with multiple worker threads
4. Verify no deadlocks

### Step 4: Testing (3-5 days)
1. Unit tests for atomic row operations
2. Stress test with high concurrency
3. Compare throughput vs baseline
4. Check for race conditions (TSAN)

---

## Thread-Safety Checklist

| Component | Current | Required | Action |
|-----------|---------|----------|--------|
| Row read lock | Unknown | Atomic | Audit + fix |
| Row write lock | Unknown | Atomic | Audit + fix |
| Version counter | Unknown | Atomic | Audit + fix |
| Transaction map | Mutex | Mutex | Verify |
| Read/write sets | Per-tx | Per-tx | OK (no sharing) |
| BatchValidator queue | Mutex | Mutex | Already done |

---

## Testing Strategy

### Unit Tests
```cpp
// Test concurrent lock acquisition
TEST(RowLock, ConcurrentReadLocks) {
  Row row;
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([&, i]() {
      EXPECT_TRUE(row.rlock_row_by(i));
    });
  }
  for (auto& t : threads) t.join();
}
```

### Stress Test Config
```yaml
n_concurrent: 16
batch_validation:
  enabled: true
  batch_size: 32
  num_workers: 8
  parallel_threshold: 4
bench:
  ops_per_txn: 20
  population:
    history: 2000
```

### ThreadSanitizer (TSAN)
```bash
# Build with TSAN
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
make -j32

# Run tests
./build/labtest -f config/stress_test.yml -d 60
```

---

## Success Criteria

### Phase 1 Complete When:
1. Row operations are thread-safe (atomic)
2. BatchValidator workers run in parallel (verified by logs)
3. No TSAN errors under stress test
4. **Throughput improvement: >15%** vs baseline
5. Abort rate: same or lower than baseline

### Metrics to Capture
| Metric | Baseline | Target |
|--------|----------|--------|
| Throughput (TPS) | 2831 | >3250 (+15%) |
| Abort Rate | 37% | ≤37% |
| Lock Conflicts | 66664 | Similar |

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Deadlock | Medium | High | Use lock ordering, timeout |
| Race condition | High | High | TSAN testing, code review |
| Performance regression | Low | Medium | Benchmark after each change |
| Lock contention | Medium | Medium | Fine-grained locks per row |

---

## Timeline

| Week | Tasks |
|------|-------|
| 1 | Audit row thread-safety, design atomic operations |
| 2 | Implement atomic row locks and versions |
| 3 | Integrate and test BatchValidator parallelism |
| 4 | Stress testing, TSAN, benchmarking |

**Total: 3-4 weeks**

---

## Phase 2 Preview (Future)

After Phase 1, extend threading to transaction execution:

1. Replace coroutine execution with thread pool
2. Remove `Coroutine::Sleep()` yields
3. Enable early abort detection (no more cascade aborts)
4. Expected additional improvement: +20-30%

**Phase 2 detailed plan will be created after Phase 1 is complete.**
