#include "tx_enhanced.h"

namespace janus {

bool TxOccEnhanced::ReadColumn(mdb::Row *row, mdb::colid_t col_id, Value *value,
                               int hint_flag) {
  // Current: Delegate to baseline OCC read
  // TODO (Step 4): Register read with EarlyAbortDetector
  // TODO: Get version after read, register (tx_id, row, col, version)
  // TODO: Check for early abort if op_count % check_interval == 0
  return TxOcc::ReadColumn(row, col_id, value, hint_flag);
}

bool TxOccEnhanced::ReadColumns(Row *row, const std::vector<colid_t> &col_ids,
                                std::vector<Value> *values, int hint_flag) {
  // Current: Delegate to baseline OCC read
  // TODO (Step 4): Register all reads with EarlyAbortDetector in batch
  // TODO: Check for early abort after batch registration
  return TxOcc::ReadColumns(row, col_ids, values, hint_flag);
}

bool TxOccEnhanced::WriteColumn(Row *row, colid_t col_id, const Value &value,
                                int hint_flag) {
  // Current: Delegate to baseline OCC write
  // TODO (Step 4): Register write with EarlyAbortDetector
  // TODO: Increment operation counter
  // TODO: Check for early abort every N operations
  // TODO: Return false immediately if early_aborted_ flag is set
  return TxOcc::WriteColumn(row, col_id, value, hint_flag);
}

bool TxOccEnhanced::WriteColumns(Row *row, const std::vector<colid_t> &col_ids,
                                 const std::vector<Value> &values,
                                 int hint_flag) {
  // Current: Delegate to baseline OCC write
  // TODO (Step 4): Register all writes with EarlyAbortDetector in batch
  // TODO: Check for early abort after batch registration
  return TxOcc::WriteColumns(row, col_ids, values, hint_flag);
}

bool TxOccEnhanced::InsertRow(Table *tbl, Row *row) {
  // Current: Delegate to baseline OCC insert
  // TODO (Step 4): Track insert in batch metadata
  // TODO: May need to register all column versions with detector
  return TxOcc::InsertRow(tbl, row);
}

} // namespace janus
