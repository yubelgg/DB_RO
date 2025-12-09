# Enhanced OCC: 2-Week Testing & Evaluation Plan

**Status**: Implementation Complete ✅ | Testing Phase Started
**Deadline**: 2 weeks (Dec 13, 2025)
**Team**: 3 people working in parallel

---

## 👤 Person 1: Testing Infrastructure

**Week 1 Focus**: Write all unit tests
**Week 2 Focus**: Performance tuning and optimization

### Week 1 Tasks (30-35 hours)

**Day 1-2: Data Structure Tests** (10h)

- [ ] `test/test_bloom_filter_janus.cc` - BloomFilter tests
- [ ] `test/test_concurrent_map_janus.cc` - ConcurrentMap tests
- [ ] `test/test_validation_queue_janus.cc` - ValidationQueue tests

**Day 3-4: Algorithm Tests** (8h)

- [ ] `test/test_conflict_graph_janus.cc` - ConflictGraph tests
  - Build(), FindIndependentSets(), TopologicalSort()

**Day 5: Detector Tests** (6h)

- [ ] `test/test_early_abort_detector_janus.cc` - EarlyAbortDetector tests

**Day 6-7: Integration Tests** (10h)

- [ ] `test/test_batch_validator_janus.cc` - BatchValidator tests
- [ ] `test/test_scheduler_enhanced_janus.cc` - SchedulerEnhanced tests

**Deliverables**: 7 test files, all passing, CMakeLists.txt updated

### Week 2 Tasks (30-35 hours)

**Parameter Tuning**:

- [ ] Day 1-2: Optimize batch_size (8, 16, 32, 64, 128)
- [ ] Day 3: Optimize batch_timeout_us (10, 50, 100, 200, 500)
- [ ] Day 4: Optimize num_workers (1, 2, 4, 8, 16, 32)
- [ ] Day 5: Optimize check_interval (1, 5, 10, 20, 50)
- [ ] Day 6-7: Create optimized configs, document recommendations

**Deliverables**: Tuning report, optimal config files

---

## 👤 Person 2: Integration Testing & Benchmarking

**Week 1 Focus**: Verify correctness, create configs
**Week 2 Focus**: Run full benchmark suite

### Week 1 Tasks (30-35 hours)

**Day 1-2: Simple Workloads** (10h)

- [ ] Create `test/simple_workloads/` directory
- [ ] Write single_transaction.yml
- [ ] Write two_conflicting.yml
- [ ] Write two_independent.yml
- [ ] Write batch_of_8.yml, batch_of_32_conflicting.yml
- [ ] Verify correctness vs baseline OCC

**Day 3-4: Config Files** (10h)

- [ ] Test existing 4 configs (occ*integration*\*.yml)
- [ ] Create config/occ_enhanced.yml
- [ ] Create config/occ_enhanced_batch_only.yml
- [ ] Create config/occ_enhanced_abort_only.yml
- [ ] Create config/occ_baseline_comparison.yml

**Day 5-7: Integration Tests** (10h)

- [ ] Write `test/test_occ_enhanced_integration.cc`
  - SingleTransactionCommits test
  - ConflictingTransactionsSerializable test
  - BatchValidationCorrectness test
  - EarlyAbortReducesWastedWork test
  - NoDeadlocks test
  - MatchesBaselineResults test

**Deliverables**: Simple workloads, 8 config files, integration test suite

### Week 2 Tasks (30-35 hours)

**Benchmarking**:

- [ ] Day 1-2: TPC-C suite (1, 2, 4, 8, 16 warehouses)
- [ ] Day 3: Ablation study (baseline, batch-only, abort-only, both)
- [ ] Day 4-5: Stress testing (max throughput, failure scenarios, stability)
- [ ] Day 6-7: Organize results, create tables/graphs

**Deliverables**: Complete benchmark results, all graphs/tables, raw data

---

## 👤 Person 3: Benchmarking Infrastructure & Documentation

**Week 1 Focus**: Build benchmark framework, collect baseline
**Week 2 Focus**: Write evaluation report and all documentation

### Week 1 Tasks (30-35 hours)

**Day 1-2: Benchmark Scripts** (12h)

- [ ] Write `benchmark/occ_benchmark.py` - Run workloads, measure metrics
- [ ] Write `benchmark/compare_occ.py` - Compare baseline vs enhanced
- [ ] Write `benchmark/contention_sweep.py` - Test different contention levels
- [ ] Write `benchmark/plot_results.py` - Visualization functions

**Day 3-4: Baseline Measurements** (10h)

- [ ] Collect baseline: 1, 2, 4, 8, 16 warehouse TPC-C
- [ ] Measure: throughput, latency (P50/P95/P99), abort rate
- [ ] Save all results

**Day 5-7: Visualization Tools** (8h)

- [ ] Implement plotting functions (throughput, latency CDF, abort rates)
- [ ] Create initial baseline graphs

**Deliverables**: Benchmark framework, baseline measurements, visualization tools

### Week 2 Tasks (30-35 hours)

**Documentation**:

- [ ] Day 1-2: Analyze all results (throughput, abort rate, latency, scalability)
- [ ] Day 3-4: Write `doc/evaluation_report.md` (15-20 pages)
  - Executive summary, methodology, results, discussion, conclusion
- [ ] Day 5-6: Update PLANNER.md, progress.md, create usage_guide.md, testing_guide.md
- [ ] Day 7: Create presentation slides, update README

**Deliverables**: Evaluation report, updated docs, presentation slides

---

## Success Criteria

**Week 1 Checkpoint**:

- ✅ All unit tests pass
- ✅ Integration tests verify correctness
- ✅ Baseline measurements collected
- ✅ Benchmark framework working

**Week 2 Completion**:

- ✅ Full benchmark suite complete
- ✅ Performance goals demonstrated (40-60% abort reduction, 2-5× throughput)
- ✅ Evaluation report complete
- ✅ All documentation updated

---

## Communication

**Daily Standup**: 15 min/day

1. What did I complete?
2. What am I working on today?
3. Any blockers?

**Sync Points**:

- Dec 3 (Day 3): Review test results
- Dec 6 (End Week 1): Integration check
- Dec 10 (Day 3 Week 2): Review benchmarks
- Dec 13 (End Week 2): Final review

---

## Getting Started

1. **Create your branch**:

   ```bash
   git checkout -b feature/testing-yourname      # Person 1
   git checkout -b feature/benchmarking-yourname # Person 2
   git checkout -b feature/evaluation-yourname   # Person 3
   ```

2. **Start your Week 1 Day 1 tasks immediately** - all tasks are independent!

3. **Check in daily** - 15 minutes keeps everyone aligned

---

**Last Updated**: 2025-11-29
**Phase**: Testing & Evaluation (Weeks 1-2)
**Next Deadline**: Dec 13, 2025
