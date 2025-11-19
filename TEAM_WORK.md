# Enhanced OCC Team Work Distribution

**Status**: Step 1 Complete (Skeleton classes merged)
**Current Goal**: Parallel development of Step 2-4 components

---

## ✅ Can Start NOW (Fully Independent Work)

These components have **no dependencies** on enhanced OCC logic and can be implemented, tested, and merged immediately.

### 1. Data Structures (Person A - 6-8 hours total)

**Files to create in `src/deptran/occ/`:**

#### `batch_metadata.h` (15 minutes)

```cpp
// Simple struct definitions - no implementation needed
struct BatchMetadata {
  size_t batch_id;
  size_t position_in_batch;
  // ... timestamps, validation results
};

enum class ConflictType {
  READ_WRITE,
  WRITE_READ,
  WRITE_WRITE
};
```

**Deliverable**: Header-only file with struct definitions

#### `bloom_filter.h` (2-3 hours)

```cpp
// Template class for probabilistic set membership testing
template<typename T>
class BloomFilter {
  void Add(const T& element);
  bool MayContain(const T& element) const;
  // ... hash functions, bit array
};
```

**Deliverable**: Header-only template + unit tests
**Reference**: Standard Bloom filter algorithm (Wikipedia, textbooks)

#### `concurrent_map.h` (2-3 hours)

```cpp
// Thread-safe hash map using sharding (256 shards)
template<typename K, typename V>
class ConcurrentMap {
  void Insert(const K& key, const V& value);
  bool TryGet(const K& key, V& value) const;
  void Remove(const K& key);
  // ... per-shard locks
};
```

**Deliverable**: Header-only template + unit tests
**Pattern**: Sharded hash map with per-shard mutexes

**Tests to write**:

- `test/test_bloom_filter.cc` - False positive rate, no false negatives
- `test/test_concurrent_map.cc` - Thread safety, correctness

---

### 2. Validation Queue (Person B - 3-4 hours)

**Files to create:**

#### `src/deptran/occ/validation_queue.h/cc`

```cpp
class ValidationQueue {
public:
  void Enqueue(TxOccEnhanced* tx);
  std::vector<TxOccEnhanced*> DequeueBatch(size_t max_size,
                                            std::chrono::microseconds timeout);
  bool HasBatch(size_t min_size) const;
  size_t Size() const;
private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<TxOccEnhanced*> queue_;
};
```

**Implementation notes**:

- Thread-safe with mutex + condition variable
- `DequeueBatch()` blocks until batch ready OR timeout
- Returns early if queue has enough transactions

**Deliverable**: Working queue + unit tests
**Reference**: Standard producer-consumer queue pattern

**Tests to write**:

- `test/test_validation_queue.cc` - Thread safety, timeout behavior, batch size

---

### 3. Conflict Graph (Person C - 4-6 hours)

**Files to create:**

#### `src/deptran/occ/conflict_graph.h/cc`

```cpp
class ConflictGraph {
public:
  static ConflictGraph Build(const std::vector<TxOccEnhanced*>& transactions);
  void AddConflict(txnid_t from, txnid_t to, ConflictType type);
  std::vector<std::vector<txnid_t>> FindIndependentSets();
  std::vector<txnid_t> TopologicalSort();
  bool HasCycle();
private:
  std::unordered_map<txnid_t, std::set<txnid_t>> adjacency_list_;
};
```

**Algorithms needed**:

1. **FindIndependentSets()**: Greedy graph coloring
2. **TopologicalSort()**: Kahn's algorithm or DFS-based
3. **HasCycle()**: DFS with back-edge detection

**Deliverable**: Graph algorithms + unit tests
**Reference**: Standard graph algorithms textbook

**Tests to write**:

- `test/test_conflict_graph.cc` - Graph construction, coloring, topological sort

**Note**: Can test with mock transaction data (doesn't need real OCC transactions yet)

---

### 4. Configuration Files (Person D - 1 hour)

**Files to create in `config/`:**

#### `occ_enhanced.yml`

```yaml
mode: occ_enhanced

batch_validation:
  enabled: true
  batch_size: 32
  batch_timeout_us: 100
  num_workers: 8

early_abort:
  enabled: true
  check_interval: 10
  bloom_filter_size: 10000
  bloom_filter_hashes: 3

version_cache:
  enabled: true
  size: 10000

monitoring:
  track_abort_reasons: true
  track_batch_metrics: true
  track_validation_latency: true
```

#### `occ_enhanced_batch_only.yml`

```yaml
# Same as above but early_abort.enabled: false
```

#### `occ_enhanced_early_abort_only.yml`

```yaml
# Same as above but batch_validation.enabled: false
```

**Deliverable**: 3 configuration files for different modes

---

### 5. Test Infrastructure (Person E - 3-4 hours)

**Directory structure to create:**

```
src/deptran/occ/test/
├── test_batch_validation.cc
├── test_early_abort.cc
├── test_conflict_graph.cc
└── CMakeLists.txt (if needed)
```

**Benchmark scripts in `benchmark/`:**

#### `occ_comparison.py`

```python
# Run same workload with baseline vs enhanced OCC
# Compare throughput, latency, abort rate
```

#### `contention_test.py`

```python
# Vary TPC-C warehouses to control contention
# Measure performance at different conflict rates
```

#### `plot_results.py`

```python
# Generate graphs from benchmark results
```

**Deliverable**: Test framework + benchmark scripts (can use mock data initially)

---

## 🔄 Can Start with Stubs (Interface Design)

These need the core logic eventually, but **interfaces can be defined now** so others can code against them.

### 6. Batch Validator Skeleton (Person A/B - 2 hours)

**Files to create:**

#### `src/deptran/occ/batch_validator.h`

```cpp
class BatchValidator {
public:
  BatchValidator(size_t batch_size, std::chrono::microseconds timeout,
                 int num_workers);

  // To be implemented later - just define signatures now
  void AddTransaction(TxOccEnhanced* tx);
  BatchValidationResult ValidateBatch();
  void ValidateInParallel(std::vector<TxOccEnhanced*>& batch);

private:
  std::vector<std::unique_ptr<ValidationWorker>> workers_;
  size_t batch_size_threshold_;
  std::chrono::microseconds batch_timeout_;
};
```

#### `src/deptran/occ/batch_validator.cc`

```cpp
// Empty implementations for now
void BatchValidator::AddTransaction(TxOccEnhanced* tx) {
  // TODO: Implement in Step 2
}

BatchValidationResult BatchValidator::ValidateBatch() {
  // TODO: Implement in Step 3
  return BatchValidationResult{};
}
```

**Deliverable**: Class skeleton with method signatures

---

### 7. Early Abort Detector Skeleton (Person C/D - 2 hours)

**Files to create:**

#### `src/deptran/occ/early_abort_detector.h`

```cpp
class EarlyAbortDetector {
public:
  void RegisterRead(txnid_t tx_id, Row* row, colid_t col_id, version_t version);
  void RegisterWrite(txnid_t tx_id, Row* row, colid_t col_id);
  void NotifyVersionChange(Row* row, colid_t col_id, version_t new_version);
  bool ShouldAbort(txnid_t tx_id);
  void UnregisterTransaction(txnid_t tx_id);

private:
  ConcurrentMap<RowColumn, std::set<TxVersionPair>> active_reads_;
  ConcurrentMap<RowColumn, std::set<txnid_t>> active_writes_;
  std::atomic<std::set<txnid_t>> aborted_txns_;
};
```

#### `src/deptran/occ/early_abort_detector.cc`

```cpp
// Empty implementations for now
void EarlyAbortDetector::RegisterRead(...) {
  // TODO: Implement in Step 4
}

bool EarlyAbortDetector::ShouldAbort(txnid_t tx_id) {
  // TODO: Implement in Step 4
  return false;
}
```

**Deliverable**: Class skeleton with API defined

---

## 🚫 Must Wait for Core Logic

**DO NOT start these yet** - they require understanding the full OCC flow:

### Requires Core Logic:

1. ❌ Actual validation logic in `BatchValidator::ValidateBatch()`
2. ❌ Actual conflict detection in `EarlyAbortDetector::NotifyVersionChange()`
3. ❌ Modifying `SchedulerOccEnhanced::DoPrepare()` to use validation queue
4. ❌ Modifying `TxOccEnhanced::ReadColumn/WriteColumn()` to register accesses
5. ❌ Integration between scheduler, validator, and detector

**These are Step 2-4 work** and require coordination.

---

## 📋 Suggested Work Distribution

### Option 1: By Component (5 People)

```
Person A: Data structures (batch_metadata, bloom_filter, concurrent_map)
         Time: 6-8 hours
         Skills: Template programming, data structures

Person B: Validation queue + batch validator skeleton
         Time: 5-6 hours
         Skills: Threading, synchronization

Person C: Conflict graph + early abort detector skeleton
         Time: 6-8 hours
         Skills: Graph algorithms

Person D: Configuration files + test infrastructure setup
         Time: 4-5 hours
         Skills: YAML, testing frameworks

Person E: Benchmark scripts + documentation
         Time: 4-5 hours
         Skills: Python, testing
```

### Option 2: By Priority (Smaller Team)

```
Week 1 (Everyone):
- Person 1: bloom_filter.h + concurrent_map.h
- Person 2: validation_queue.h/cc
- Person 3: conflict_graph.h/cc
- Everyone: Review each other's PRs

Week 2 (Everyone):
- Integrate components
- Start Step 2 together (batch validation logic)
```

---

## 🎯 Immediate Action Items (This Week)

**Priority 1 (Can merge ASAP):**

1. ✅ Create `batch_metadata.h` (15 min)
2. ✅ Create `bloom_filter.h` + tests (3 hours)
3. ✅ Create `concurrent_map.h` + tests (3 hours)

**Priority 2 (End of week):** 4. ✅ Create `validation_queue.h/cc` + tests (4 hours) 5. ✅ Create `conflict_graph.h/cc` + tests (6 hours)

**Priority 3 (Start of next week):** 6. ✅ Create all config YAML files (1 hour) 7. ✅ Set up test directory structure (30 min) 8. ✅ Create batch_validator.h skeleton (1 hour) 9. ✅ Create early_abort_detector.h skeleton (1 hour)

---

## 📦 Suggested First PRs

### PR #1: "Add Enhanced OCC Data Structures"

**Branch**: `feature/enhanced-occ-data-structures`

**Files**:

- `src/deptran/occ/batch_metadata.h`
- `src/deptran/occ/bloom_filter.h`
- `src/deptran/occ/concurrent_map.h`
- `test/test_bloom_filter.cc`
- `test/test_concurrent_map.cc`

**Can merge**: Immediately (no dependencies)

---

### PR #2: "Add Validation Queue"

**Branch**: `feature/enhanced-occ-validation-queue`

**Files**:

- `src/deptran/occ/validation_queue.h/cc`
- `test/test_validation_queue.cc`

**Can merge**: After PR #1 (needs ConcurrentMap)

---

### PR #3: "Add Conflict Graph"

**Branch**: `feature/enhanced-occ-conflict-graph`

**Files**:

- `src/deptran/occ/conflict_graph.h/cc`
- `test/test_conflict_graph.cc`

**Can merge**: Immediately (no dependencies)

---

### PR #4: "Add Component Skeletons and Configs"

**Branch**: `feature/enhanced-occ-skeletons`

**Files**:

- `src/deptran/occ/batch_validator.h/cc` (skeleton)
- `src/deptran/occ/early_abort_detector.h/cc` (skeleton)
- `config/occ_enhanced*.yml` (all 3 configs)

**Can merge**: After PR #1, #2, #3 (references their types)

---

## 🔗 Integration Plan

**After all independent components are merged:**

### Week 3-4: Integrate Components (Coordinated Work)

1. Implement actual validation logic in `BatchValidator`
2. Implement actual abort detection in `EarlyAbortDetector`
3. Wire up `SchedulerOccEnhanced::DoPrepare()` to use validator
4. Wire up `TxOccEnhanced` to use detector

**This is when the team needs to work together on the core logic.**

---

## 📝 Notes

- **All independent work can happen in parallel**
- **Merge frequently** - small PRs are easier to review
- **Write tests first** - use mock data where needed
- **Reference PLANNER.md** for detailed design
- **Ask questions early** - use PR comments for design discussion

---

## ✅ Progress Tracking

Update this section as components are completed:

- [ ] batch_metadata.h
- [ ] bloom_filter.h + tests
- [ ] concurrent_map.h + tests
- [ ] validation_queue.h/cc + tests
- [ ] conflict_graph.h/cc + tests
- [ ] config files (3 files)
- [ ] batch_validator.h skeleton
- [ ] early_abort_detector.h skeleton
- [ ] test infrastructure setup
- [ ] benchmark scripts

---

**Questions?** Check PLANNER.md or ask in team chat/PR comments.

**Last Updated**: Step 1 complete, ready for parallel development
