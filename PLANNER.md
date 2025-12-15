# Enhanced OCC Implementation Plan

## Overview

**Goal**: Improve OCC throughput and reduce abort rates for high-contention workloads.

**Solution**: Two optimizations:
1. **Early Abort** - Detect conflicts during execution, abort before wasting work
2. **Batch Validation** - Queue transactions, validate serially while allowing concurrency via coroutine yielding

## Results (TPC-C, 30s duration)

| Configuration | TPS | vs Baseline | Abort Rate |
|---------------|-----|-------------|------------|
| Baseline OCC | ~265 | 1.0x | ~90% |
| Early Abort | ~1017 | ~3.8x | ~34% |
| Batch Validation | ~481 | ~1.8x | ~20% |

## How It Works

### Early Abort
```
1. Transaction reads data → RegisterRead(tx_id, row, version)
2. Another transaction commits → NotifyVersionChange(row, new_version)
3. Conflicting transactions marked for early abort
4. Avoids wasting work on doomed transactions
```

### Batch Validation
```
1. Transaction calls DoPrepare()
2. Enqueues to ValidationQueue
3. BoxEvent::Wait() YIELDS coroutine
4. Reactor processes other RPCs (concurrency!)
5. ValidationLoop validates SERIALLY
6. BoxEvent::Set() resumes coroutine
```

**Key Insight**: Throughput comes from yielding (multiple transactions in-flight), not parallel validation.

## File Structure

```
src/deptran/occ/
├── scheduler_enhanced.h/cc   # Main coordinator + ValidationLoop
├── tx_enhanced.h/cc          # Enhanced transaction
├── validation_queue.h/cc     # Thread-safe queue with BoxEvent
├── batch_validator.h/cc      # Batch processing
├── early_abort_detector.h/cc # Conflict detection
└── bloom_filter.h            # Fast conflict pre-filtering
```

## Configuration

```yaml
mode:
  cc: occ_enhanced

batch_validation:
  enabled: true
  batch_size: 4
  batch_timeout_us: 5000

early_abort:
  enabled: true  # or false for batch-only
```

## Test Commands

```bash
# Recommended: Use comparison script
./scripts/run_occ_comparison.sh 30

# Manual testing
cd build
./labtest -f ../config/tpcc_occ_baseline.yml -d 30
./labtest -f ../config/tpcc_occ_early_abort.yml -d 30
./labtest -f ../config/tpcc_occ_batch_only.yml -d 30
```

## What We Learned

**What Works**:
- Early abort with conflict tracking (~3.8x TPS improvement)
- Batch validation with coroutine yielding (~1.8x TPS, best abort rate)
- Serial validation prevents lock contention

**What Doesn't Work**:
- Parallel validation workers (batch sizes too small in practice)
- Combining both features (interference issues)
