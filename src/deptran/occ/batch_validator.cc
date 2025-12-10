#include "batch_validator.h"
#include "conflict_graph.h"
#include "../memdb/row.h"
#include "../memdb/txn_occ.h"
#include "../config.h"
#include "base/all.hpp"
#include <algorithm>
#include <thread>

namespace janus {

BatchValidator::BatchValidator(size_t batch_size, int num_workers)
    : batch_size_(batch_size), 
      num_workers_(num_workers), 
      batch_counter_(0),
      shutdown_(false) {
  
  // Initialize worker thread pool
  for (int i = 0; i < num_workers_; i++) {
    workers_.emplace_back(&BatchValidator::WorkerThread, this, i);
  }
  
  Log_info("BatchValidator initialized with %d workers, batch_size=%zu", 
           num_workers_, batch_size_);
}

BatchValidator::~BatchValidator() {
  // Signal shutdown
  {
    std::unique_lock<std::mutex> lock(work_queue_mutex_);
    shutdown_ = true;
  }
  work_queue_cv_.notify_all();
  
  // Wait for all workers to finish
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  
  Log_info("BatchValidator shutdown complete");
}

BatchValidationResult
BatchValidator::ValidateBatch(const std::vector<TxOccEnhanced *> &batch) {
  
  if (batch.empty()) {
    return BatchValidationResult(0);
  }

  // Create result structure
  BatchValidationResult result(batch.size());
  result.batch_id = batch_counter_++;

  auto start_time = std::chrono::steady_clock::now();

  // Decide: serial or parallel validation?
  // Use parallel validation if batch is large enough and we have workers
  bool use_parallel = (batch.size() >= static_cast<size_t>(Config::GetConfig()->get_parallel_threshold())
                       && num_workers_ > 0);

  if (use_parallel) {
    ValidateBatchParallel(batch, result);
  } else {
    ValidateBatchSerial(batch, result);
  }

  auto end_time = std::chrono::steady_clock::now();
  result.total_time = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time);

  Log_debug("Batch %llu validated: %zu txns, %lld us, parallel=%d",
            result.batch_id, batch.size(), result.total_time.count(), use_parallel);

  return result;
}

void BatchValidator::ValidateBatchSerial(
    const std::vector<TxOccEnhanced *> &batch,
    BatchValidationResult &result) {
  
  // Validate transactions one by one
  for (size_t i = 0; i < batch.size(); i++) {
    TxOccEnhanced *tx = batch[i];

    // Validate single transaction
    bool passed = ValidateSingle(tx);
    result.passed[i] = passed;

    // Update transaction's batch metadata
    tx->GetBatchMetadata().batch_id = result.batch_id;
    tx->GetBatchMetadata().position_in_batch = i;
    tx->GetBatchMetadata().validated = true;
    tx->GetBatchMetadata().passed = passed;

    // Signal waiting DoPrepare() that validation is complete
    if (tx->GetBatchMetadata().validation_promise) {
      tx->GetBatchMetadata().validation_promise->set_value(passed);
    }
  }
}

void BatchValidator::ValidateBatchParallel(
    const std::vector<TxOccEnhanced *> &batch,
    BatchValidationResult &result) {
  
  // Step 1: Build conflict graph
  ConflictGraph conflict_graph;
  conflict_graph.Build(batch);
  
  // Step 2: Find independent sets (transactions that can validate in parallel)
  auto independent_sets = conflict_graph.FindIndependentSets();
  
  Log_debug("Batch %llu: found %zu independent sets for parallel validation",
            result.batch_id, independent_sets.size());
  
  // Step 3: Validate each independent set in parallel
  for (const auto& independent_set : independent_sets) {
    ValidateIndependentSet(batch, independent_set, result);
  }
  
  // Step 4: All transactions validated, signal completion
  for (size_t i = 0; i < batch.size(); i++) {
    TxOccEnhanced *tx = batch[i];
    
    // Update batch metadata
    tx->GetBatchMetadata().batch_id = result.batch_id;
    tx->GetBatchMetadata().position_in_batch = i;
    tx->GetBatchMetadata().validated = true;
    tx->GetBatchMetadata().passed = result.passed[i];
    
    // Signal completion
    if (tx->GetBatchMetadata().validation_promise) {
      tx->GetBatchMetadata().validation_promise->set_value(result.passed[i]);
    }
  }
}

void BatchValidator::ValidateIndependentSet(
    const std::vector<TxOccEnhanced *> &batch,
    const std::vector<size_t> &indices,
    BatchValidationResult &result) {
  
  if (indices.empty()) return;
  
  // If only one transaction, validate directly
  if (indices.size() == 1) {
    size_t idx = indices[0];
    result.passed[idx] = ValidateSingle(batch[idx]);
    return;
  }
  
  // Parallel validation: submit work items to thread pool
  std::vector<std::future<bool>> futures;
  futures.reserve(indices.size());
  
  for (size_t idx : indices) {
    // Create work item
    WorkItem work;
    work.tx = batch[idx];
    work.tx_index = idx;
    
    // Get future for result
    futures.push_back(work.result.get_future());
    
    // Enqueue work
    {
      std::unique_lock<std::mutex> lock(work_queue_mutex_);
      work_queue_.push(std::move(work));
    }
    work_queue_cv_.notify_one();
  }
  
  // Wait for all validations to complete
  for (size_t i = 0; i < indices.size(); i++) {
    size_t idx = indices[i];
    result.passed[idx] = futures[i].get();
  }
}

bool BatchValidator::ValidateSingle(TxOccEnhanced *tx) {
  // Get underlying mdb transaction
  auto txn = dynamic_cast<mdb::TxnOCC *>(tx->mdb_txn());
  verify(txn != nullptr);
  verify(txn->outcome_ == symbol_t::NONE);
  verify(!txn->verified_);

  // Only do version check on leader
  if (tx->is_leader_hint_ && !txn->version_check()) {
    Log_debug("batch validation: version check failed for tx %" PRIx64,
              tx->tid_);
    txn->__debug_abort_ = 1;
    return false;
  }

  // Acquire read locks
  for (auto &it : txn->ver_check_read_) {
    Row *row = it.first.row;
    auto *v_row = (VersionedRow *)row;

    if (!v_row->rlock_row_by(txn->id())) {
      // Read lock failed - unlock everything acquired so far
      for (auto &lit : txn->locks_) {
        Row *r = lit.first;
        verify(r->rtti() == symbol_t::ROW_VERSIONED);
        auto vr = (VersionedRow *)r;
        vr->unlock_row_by(txn->id());
      }
      txn->locks_.clear();
      Log_debug("batch validation: read lock failed for tx %" PRIx64, tx->tid_);
      txn->__debug_abort_ = 1;
      return false;
    }
    insert_into_map(txn->locks_, row, -1);
  }

  // Acquire write locks
  for (auto &it : txn->updates_) {
    Row *row = it.first;
    auto *v_row = (VersionedRow *)row;

    if (!v_row->wlock_row_by(txn->id())) {
      // Write lock failed - unlock everything
      for (auto &lit : txn->locks_) {
        Row *r = lit.first;
        verify(r->rtti() == symbol_t::ROW_VERSIONED);
        auto vr = (VersionedRow *)r;
        vr->unlock_row_by(txn->id());
      }
      txn->locks_.clear();
      Log_debug("batch validation: write lock failed for tx %" PRIx64,
                tx->tid_);
      txn->__debug_abort_ = 1;
      return false;
    }
    insert_into_map(txn->locks_, row, -1);
  }

  // Validation succeeded
  Log_debug("batch validation: locks acquired for tx %" PRIx64, tx->tid_);
  txn->__debug_abort_ = 0;
  txn->verified_ = true;

  return true;
}

void BatchValidator::WorkerThread(int worker_id) {
  Log_debug("BatchValidator worker %d started", worker_id);
  
  while (true) {
    WorkItem work;
    
    // Wait for work or shutdown signal
    {
      std::unique_lock<std::mutex> lock(work_queue_mutex_);
      work_queue_cv_.wait(lock, [this] {
        return shutdown_ || !work_queue_.empty();
      });
      
      if (shutdown_ && work_queue_.empty()) {
        break;
      }
      
      if (!work_queue_.empty()) {
        work = std::move(work_queue_.front());
        work_queue_.pop();
      } else {
        continue;
      }
    }
    
    // Perform validation
    bool result = ValidateSingle(work.tx);
    
    // Set result
    work.result.set_value(result);
  }
  
  Log_debug("BatchValidator worker %d stopped", worker_id);
}

// Helper: Get read/write sets from transaction for conflict detection
void BatchValidator::GetTransactionAccessSets(
    TxOccEnhanced* tx,
    std::unordered_set<Row*>& read_set,
    std::unordered_set<Row*>& write_set) {
  
  auto txn = dynamic_cast<mdb::TxnOCC*>(tx->mdb_txn());
  if (!txn) return;
  
  // Collect reads
  for (auto& it : txn->ver_check_read_) {
    read_set.insert(it.first.row);
  }
  
  // Collect writes
  for (auto& it : txn->updates_) {
    write_set.insert(it.first);
  }
}

} // namespace janus
