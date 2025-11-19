#pragma once

#include "batch_metadata.h"
#include "tx.h"

namespace janus {

/**
 * Enhanced OCC Transaction with Early Abort Detection
 *
 * This transaction class extends baseline OCC transactions to support:
 * - Early abort detection during execution phase
 * - Registration of reads/writes with EarlyAbortDetector
 * - Periodic checking for conflicts to abort before wasted work
 *
 * Current Implementation: Skeleton class that behaves identically to baseline
 * OCC Future Enhancements: Will integrate with EarlyAbortDetector
 */
class TxOccEnhanced : public TxOcc {
public:
  // Use parent constructor (epoch, tid, mgr)
  using TxOcc::TxOcc;

  /**
   * Read single column - currently delegates to parent TxOcc::ReadColumn()
   *
   * Future Implementation:
   * - Register read with EarlyAbortDetector (row, col, version)
   * - Check for early abort every N operations
   * - Return false immediately if marked for abort
   */
  virtual bool ReadColumn(mdb::Row *row, mdb::colid_t col_id, Value *value,
                          int hint_flag = TXN_SAFE) override;

  /**
   * Read multiple columns - currently delegates to parent TxOcc::ReadColumns()
   *
   * Future Implementation:
   * - Register all reads with EarlyAbortDetector in batch
   * - Check for early abort after batch registration
   */
  virtual bool ReadColumns(Row *row, const std::vector<colid_t> &col_ids,
                           std::vector<Value> *values,
                           int hint_flag = TXN_SAFE) override;

  /**
   * Write single column - currently delegates to parent TxOcc::WriteColumn()
   *
   * Future Implementation:
   * - Register write with EarlyAbortDetector (row, col)
   * - Increment operation counter and check for early abort
   * - Abort immediately if conflict detected
   */
  virtual bool WriteColumn(Row *row, colid_t col_id, const Value &value,
                           int hint_flag = TXN_SAFE) override;

  /**
   * Write multiple columns - currently delegates to parent
   * TxOcc::WriteColumns()
   *
   * Future Implementation:
   * - Register all writes with EarlyAbortDetector in batch
   * - Check for early abort after batch registration
   */
  virtual bool WriteColumns(Row *row, const std::vector<colid_t> &col_ids,
                            const std::vector<Value> &values,
                            int hint_flag = TXN_SAFE) override;

  /**
   * Insert row - currently delegates to parent TxOcc::InsertRow()
   *
   * Future Implementation:
   * - Track insert for batch metadata
   * - Register all column versions with detector
   */
  virtual bool InsertRow(Table *tbl, Row *row) override;

  /**
   * Get batch metadata for this transaction
   * Used by BatchValidator to track validation state
   */
  BatchMetadata &GetBatchMetadata() { return batch_meta_; }
  const BatchMetadata &GetBatchMetadata() const { return batch_meta_; }

private:
  // Batch validation metadata
  BatchMetadata batch_meta_;

  // TODO (Step 4): Add operation counter for periodic abort checking
  // TODO (Step 4): Add reference to EarlyAbortDetector
  // TODO (Step 4): Add early_aborted_ flag
};

} // namespace janus
