# Enhanced OCC Parameter Tuning Guide

This document describes the tunable parameters in the Enhanced OCC implementation and provides instructions for testing different configurations.

## Overview

The Enhanced OCC system has four main tuning parameters:

| Parameter | Description | Default | Impact |
|-----------|-------------|---------|--------|
| `batch_size` | Maximum transactions per validation batch | 32 | Larger = more parallelism, but higher latency |
| `batch_timeout` | Max wait time for batch to fill (μs) | 100 | Lower = less latency, but smaller batches |
| `num_workers` | Parallel validation worker threads | 8 | More = better throughput on multi-core systems |
| `check_interval` | Operations between early abort checks | 10 | Lower = faster abort detection, but more overhead |

---

## Parameter Locations

### 1. batch_size

**File**: `src/deptran/occ/scheduler_enhanced.h`  
**Line**: 96

```cpp
// Configuration
size_t batch_size_ = 32;                       // Max transactions per batch
```

**Values to test**: 8, 16, 32, 64, 128

**Expected behavior**:
- Smaller values (8-16): Lower latency, less parallelism benefit
- Default (32): Balanced trade-off
- Larger values (64-128): Higher throughput under load, but increased latency

---

### 2. batch_timeout

**File**: `src/deptran/occ/scheduler_enhanced.h`  
**Line**: 97

```cpp
std::chrono::microseconds batch_timeout_{100}; // Max wait time for batch
```

**Values to test**: 10, 50, 100, 200, 500 (microseconds)

**Expected behavior**:
- Lower values (10-50μs): Faster response, smaller average batch sizes
- Default (100μs): Balanced
- Higher values (200-500μs): Larger batches, better throughput, higher latency

---

### 3. num_workers

**File**: `src/deptran/occ/scheduler_enhanced.cc`  
**Line**: 16 (in constructor)

```cpp
batch_validator_ =
    std::make_unique<BatchValidator>(batch_size_,
                                     8 // num_workers for parallel validation
    );
```

**Values to test**: 1, 2, 4, 8, 16, 32

**Expected behavior**:
- 1 worker: Serial validation (baseline for comparison)
- 2-4 workers: Moderate parallelism
- 8 workers (default): Good for 8+ core systems
- 16-32 workers: May help on high-core-count systems, watch for overhead

---

### 4. check_interval

**File**: `src/deptran/occ/tx_enhanced.h`  
**Line**: 127

```cpp
// How often to check for early abort (every N operations)
size_t check_interval_ = 10; // Default: check every 10 operations
```

**Values to test**: 1, 5, 10, 20, 50

**Expected behavior**:
- 1 (every operation): Maximum abort detection, highest overhead
- 5-10: Good balance between detection speed and overhead
- 20-50: Lower overhead, but may do more wasted work before aborting

---

## How to Modify and Test

### Step 1: Edit Parameter

Open the relevant file and change the default value:

```bash
# Example: Change batch_size to 64
vim src/deptran/occ/scheduler_enhanced.h
# Edit line 96: size_t batch_size_ = 64;
```

### Step 2: Rebuild

```bash
cd build
make -j8
```

### Step 3: Run Benchmark

Using the test suite:
```bash
cd build
ctest -R "OCC" --output-on-failure
```

Or run individual tests:
```bash
./build/test_batch_validator_janus
./build/test_scheduler_enhanced_janus
```

### Step 4: Record Results

Note the following metrics:
- **Throughput**: Transactions per second
- **Latency**: Time per transaction (from BatchValidationResult.total_time)
- **Abort Rate**: Early aborts detected / total transactions

### Step 5: Restore Default

After testing, restore the original value to maintain consistency.

---

## Quick Reference: Parameter Combinations

### High Contention Workloads (many conflicts)
```
batch_size = 16       # Smaller batches, faster turnover
batch_timeout = 50    # Don't wait too long
num_workers = 8       # Good parallelism
check_interval = 5    # Detect aborts quickly
```

### Low Contention Workloads (few conflicts)
```
batch_size = 64       # Larger batches for better throughput
batch_timeout = 200   # Allow time for batches to fill
num_workers = 4       # Less parallelism needed
check_interval = 20   # Less frequent checks (lower overhead)
```

### Maximum Throughput
```
batch_size = 128      # Large batches
batch_timeout = 500   # Allow full batches
num_workers = 16      # Maximum parallelism
check_interval = 10   # Balanced
```

### Minimum Latency
```
batch_size = 8        # Small batches
batch_timeout = 10    # No waiting
num_workers = 8       # Good parallelism
check_interval = 5    # Quick abort detection
```

---

## Using tune_occ.sh Script

A helper script is available to automate parameter changes:

```bash
# Syntax: ./scripts/tune_occ.sh <batch_size> <batch_timeout> <num_workers> <check_interval>

# Example: Test with batch_size=64, timeout=200us, 4 workers, interval=20
./scripts/tune_occ.sh 64 200 4 20

# This will:
# 1. Edit the source files
# 2. Rebuild
# 3. Run tests
# 4. Restore original values
```

---

## Expected Results Format

Record results in the following format:

| batch_size | batch_timeout | num_workers | check_interval | Throughput (txn/s) | Avg Latency (μs) | Abort Rate (%) |
|------------|---------------|-------------|----------------|-------------------|------------------|----------------|
| 32 | 100 | 8 | 10 | (baseline) | (baseline) | (baseline) |
| 64 | 100 | 8 | 10 | ... | ... | ... |
| ... | ... | ... | ... | ... | ... | ... |

---

## Notes

1. **Rebuild required**: Any parameter change requires rebuilding the project
2. **Test isolation**: Change one parameter at a time for accurate measurements
3. **System dependent**: Optimal values depend on CPU cores, memory, and workload
4. **Warm-up**: Run several iterations before recording final measurements

