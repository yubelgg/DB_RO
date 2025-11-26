#pragma once

#include "batch_metadata.h"
#include "tx.h"

namespace janus {

// Forward declaration
class EarlyAbortDetector;

/**
 * Enhanced OCC Transaction with Early Abort Detection
 *
 * This transaction class extends baseline OCC transactions to support:
 * - Early abort detection during execution phase
 * - Registration of reads/writes with EarlyAbortDetector
 * - Periodic checking for conflicts to abort before wasted work
 * - Batch validation metadata tracking
 */
class TxOccEnhanced : public TxOcc {
public:
  // Constructor
  TxOccEnhanced(epoch_t epoch, txnid_t tid, Scheduler* mgr);

  virtual ~TxOccEnhanced();

  /**
   * Read single column
   * - Delegates to parent TxOcc::ReadColumn()
   * - Registers read with EarlyAbortDetector (row, col, version)
   * - Checks for early abort every N operations
   */
  virtual bool ReadColumn(mdb::Row *row, mdb::colid_t col_id, Value *value,
                          int hint_flag = TXN_SAFE) override;

  /**
   * Read multiple columns
   * - Delegates to parent TxOcc::ReadColumns()
   * - Registers all reads with EarlyAbortDetector in batch
   * - Checks for early abort after batch registration
   */
  virtual bool ReadColumns(Row *row, const std::vector<colid_t> &col_ids,
                           std::vector<Value> *values,
                           int hint_flag = TXN_SAFE) override;

  /**
   * Write single column
   * - Delegates to parent TxOcc::WriteColumn()
   * - Registers write with EarlyAbortDetector (row, col)
   * - Increments operation counter and checks for early abort
   */
  virtual bool WriteColumn(Row *row, colid_t col_id, const Value &value,
                           int hint_flag = TXN_SAFE) override;

  /**
   * Write multiple columns
   * - Delegates to parent TxOcc::WriteColumns()
   * - Registers all writes with EarlyAbortDetector in batch
   * - Checks for early abort after batch registration
   */
  virtual bool WriteColumns(Row *row, const std::vector<colid_t> &col_ids,
                            const std::vector<Value> &values,
                            int hint_flag = TXN_SAFE) override;

  /**
   * Insert row
   * - Delegates to parent TxOcc::InsertRow()
   * - Tracks insert for batch metadata
   */
  virtual bool InsertRow(Table *tbl, Row *row) override;

  /**
   * Get batch metadata for this transaction
   * Used by BatchValidator to track validation state
   */
  BatchMetadata &GetBatchMetadata() { return batch_meta_; }
  const BatchMetadata &GetBatchMetadata() const { return batch_meta_; }

  /**
   * Set the early abort detector
   */
  void SetEarlyAbortDetector(EarlyAbortDetector* detector) {
    early_abort_detector_ = detector;
  }

  /**
   * Get the early abort detector
   */
  EarlyAbortDetector* GetEarlyAbortDetector() const {
    return early_abort_detector_;
  }

  /**
   * Set interval for checking early abort (every N operations)
   */
  void SetCheckInterval(size_t interval) {
    check_interval_ = interval;
  }

  /**
   * Check if transaction should abort early
   * Queries the EarlyAbortDetector for this transaction's status
   */
  bool ShouldAbortEarly();

  /**
   * Mark this transaction as early aborted
   */
  void MarkEarlyAborted() { early_aborted_ = true; }

  /**
   * Check if this transaction has been early aborted
   */
  bool IsEarlyAborted() const { return early_aborted_; }

private:
  // Batch validation metadata
  BatchMetadata batch_meta_;

  // Early abort detection
  EarlyAbortDetector* early_abort_detector_ = nullptr;
  
  // Operation counter for periodic abort checking
  size_t operation_count_ = 0;
  
  // How often to check for early abort (every N operations)
  size_t check_interval_ = 10; // Default: check every 10 operations
  
  // Flag indicating this transaction has been marked for early abort
  bool early_aborted_ = false;

  /**
   * Increment operation counter and check for early abort if needed
   * Returns: true if should abort, false if can continue
   */
  bool IncrementAndCheckAbort();
};

} // namespace janus
