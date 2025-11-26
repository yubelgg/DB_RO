#include "tx_enhanced.h"
#include "early_abort_detector.h"
#include "scheduler_enhanced.h"
#include "../memdb/row.h"
#include "base/all.hpp"

namespace janus {

TxOccEnhanced::TxOccEnhanced(epoch_t epoch, txnid_t tid, TxLogServer* mgr)
    : TxOcc(epoch, tid, mgr),
      early_abort_detector_(nullptr),
      operation_count_(0),
      check_interval_(10),
      early_aborted_(false) {
  
  // Try to get early abort detector from scheduler
  auto* enhanced_sched = dynamic_cast<SchedulerOccEnhanced*>(mgr);
  if (enhanced_sched) {
    early_abort_detector_ = enhanced_sched->GetEarlyAbortDetector();
  }
  
  Log_debug("TxOccEnhanced created: tid=%" PRIx64 ", detector=%p", 
            tid, early_abort_detector_);
}

TxOccEnhanced::~TxOccEnhanced() {
  // Clean up tracking in early abort detector
  if (early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    early_abort_detector_->RemoveTransaction(tid_);
  }
}

bool TxOccEnhanced::ReadColumn(mdb::Row *row, mdb::colid_t col_id, Value *value,
                               int hint_flag) {
  // Check if already marked for early abort
  if (early_aborted_) {
    Log_debug("TxOccEnhanced::ReadColumn: tx %" PRIx64 " already early aborted", tid_);
    return false;
  }
  
  // Perform the actual read using parent implementation
  bool success = TxOcc::ReadColumn(row, col_id, value, hint_flag);
  
  if (success && early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    // Get the version that was read
    auto* v_row = dynamic_cast<VersionedRow*>(row);
    if (v_row) {
      i64 version = v_row->get_column_ver(col_id);
      
      // Register this read with the detector
      early_abort_detector_->RegisterRead(tid_, row, col_id, version);
      
      Log_debug("Registered read: tx=%" PRIx64 " row=%p col=%d ver=%" PRIx64,
                tid_, row, col_id, version);
    }
    
    // Check for early abort periodically
    if (IncrementAndCheckAbort()) {
      return false;
    }
  }
  
  return success;
}

bool TxOccEnhanced::ReadColumns(Row *row, const std::vector<colid_t> &col_ids,
                                std::vector<Value> *values, int hint_flag) {
  // Check if already marked for early abort
  if (early_aborted_) {
    Log_debug("TxOccEnhanced::ReadColumns: tx %" PRIx64 " already early aborted", tid_);
    return false;
  }
  
  // Perform the actual read using parent implementation
  bool success = TxOcc::ReadColumns(row, col_ids, values, hint_flag);
  
  if (success && early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    auto* v_row = dynamic_cast<VersionedRow*>(row);
    if (v_row) {
      // Register all reads with detector
      for (const auto& col_id : col_ids) {
        i64 version = v_row->get_column_ver(col_id);
        early_abort_detector_->RegisterRead(tid_, row, col_id, version);
      }
      
      Log_debug("Registered %zu reads: tx=%" PRIx64 " row=%p",
                col_ids.size(), tid_, row);
    }
    
    // Check for early abort after batch
    if (IncrementAndCheckAbort()) {
      return false;
    }
  }
  
  return success;
}

bool TxOccEnhanced::WriteColumn(Row *row, colid_t col_id, const Value &value,
                                int hint_flag) {
  // Check if already marked for early abort
  if (early_aborted_) {
    Log_debug("TxOccEnhanced::WriteColumn: tx %" PRIx64 " already early aborted", tid_);
    return false;
  }
  
  // Perform the actual write using parent implementation
  bool success = TxOcc::WriteColumn(row, col_id, value, hint_flag);
  
  if (success && early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    // Register this write with the detector
    early_abort_detector_->RegisterWrite(tid_, row, col_id);
    
    Log_debug("Registered write: tx=%" PRIx64 " row=%p col=%d",
              tid_, row, col_id);
    
    // Check for early abort periodically
    if (IncrementAndCheckAbort()) {
      return false;
    }
  }
  
  return success;
}

bool TxOccEnhanced::WriteColumns(Row *row, const std::vector<colid_t> &col_ids,
                                 const std::vector<Value> &values,
                                 int hint_flag) {
  // Check if already marked for early abort
  if (early_aborted_) {
    Log_debug("TxOccEnhanced::WriteColumns: tx %" PRIx64 " already early aborted", tid_);
    return false;
  }
  
  // Perform the actual write using parent implementation
  bool success = TxOcc::WriteColumns(row, col_ids, values, hint_flag);
  
  if (success && early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    // Register all writes with detector
    for (const auto& col_id : col_ids) {
      early_abort_detector_->RegisterWrite(tid_, row, col_id);
    }
    
    Log_debug("Registered %zu writes: tx=%" PRIx64 " row=%p",
              col_ids.size(), tid_, row);
    
    // Check for early abort after batch
    if (IncrementAndCheckAbort()) {
      return false;
    }
  }
  
  return success;
}

bool TxOccEnhanced::InsertRow(Table *tbl, Row *row) {
  // Check if already marked for early abort
  if (early_aborted_) {
    Log_debug("TxOccEnhanced::InsertRow: tx %" PRIx64 " already early aborted", tid_);
    return false;
  }
  
  // Perform the actual insert using parent implementation
  bool success = TxOcc::InsertRow(tbl, row);
  
  if (success && early_abort_detector_ && early_abort_detector_->IsEnabled()) {
    // For inserts, we could register writes for all columns
    // but typically inserts don't conflict with existing transactions
    // since the row is new. May revisit this if needed.
    
    // Check for early abort
    if (IncrementAndCheckAbort()) {
      return false;
    }
  }
  
  return success;
}

bool TxOccEnhanced::ShouldAbortEarly() {
  if (!early_abort_detector_ || !early_abort_detector_->IsEnabled()) {
    return false;
  }
  
  bool should_abort = early_abort_detector_->ShouldAbort(tid_);
  
  if (should_abort && !early_aborted_) {
    early_aborted_ = true;
    Log_info("Transaction %" PRIx64 " detected for early abort", tid_);
  }
  
  return should_abort;
}

bool TxOccEnhanced::IncrementAndCheckAbort() {
  operation_count_++;
  
  // Check for early abort every N operations
  if (operation_count_ % check_interval_ == 0) {
    if (ShouldAbortEarly()) {
      Log_debug("Early abort detected at operation %zu for tx %" PRIx64,
                operation_count_, tid_);
      return true; // Signal abort
    }
  }
  
  return false; // Continue
}

} // namespace janus
