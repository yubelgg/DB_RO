#include "batch_validator.h"
#include "../memdb/row.h"
#include "../memdb/txn_occ.h"
#include "base/all.hpp"

namespace janus {

BatchValidator::BatchValidator(size_t batch_size, int num_workers)
    : batch_size_(batch_size), num_workers_(num_workers), batch_counter_(0) {
  // TODO (Step 3): Initialize worker thread pool
}

BatchValidator::~BatchValidator() {
  // TODO (Step 3): Shutdown worker threads
}

BatchValidationResult
BatchValidator::ValidateBatch(const std::vector<TxOccEnhanced *> &batch) {

  // Create result structure
  BatchValidationResult result(batch.size());
  result.batch_id = batch_counter_++;

  auto start_time = std::chrono::steady_clock::now();

  // Step 2: Serial validation (one by one)
  // TODO (Step 3): Replace with parallel validation using conflict graph
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

  auto end_time = std::chrono::steady_clock::now();
  result.total_time = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time);

  return result;
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

} // namespace janus
