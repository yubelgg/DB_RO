

//
// Created by shuai on 11/25/15.
//

#include "../__dep__.h"
#include "../scheduler.h"
#include "../config.h"
#include "../rcc_rpc.h"
#include "tx.h"
#include "scheduler.h"
#include "scheduler_enhanced.h"  // For AbortReason enum
#include <signal.h>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace janus {

// Global pointer for signal handler (only one scheduler instance per process)
static SchedulerOcc* g_scheduler_occ = nullptr;

// Signal handler for SIGTERM/SIGINT - print metrics and export CSV before exit
void sigterm_handler_baseline(int signum) {
  if (g_scheduler_occ) {
    Log_info("SchedulerOcc (Baseline): Caught signal %d, printing metrics:", signum);
    Log_info("  Total attempted: %llu", g_scheduler_occ->num_transactions_attempted_.load());
    Log_info("  Total committed: %llu", g_scheduler_occ->num_transactions_committed_.load());
    Log_info("  Total aborted: %llu", g_scheduler_occ->num_transactions_aborted_.load());
    Log_info("  Abort rate: %.2f%%", g_scheduler_occ->GetAbortRate() * 100.0);
    Log_info("  Throughput: %.2f TPS", g_scheduler_occ->GetThroughput());

    Log_info("SchedulerOcc (Baseline): Abort breakdown:");
    Log_info("  Version mismatch: %llu", g_scheduler_occ->GetAbortCount(AbortReason::VERSION_MISMATCH));
    Log_info("  Lock conflicts: %llu", g_scheduler_occ->GetAbortCount(AbortReason::LOCK_CONFLICT));
    Log_info("  Unknown: %llu", g_scheduler_occ->GetAbortCount(AbortReason::UNKNOWN));

    // Export results to CSV (signal handler context - limited but should work for simple file I/O)
    g_scheduler_occ->ExportResultsToCSV();
  }
  exit(0);
}

SchedulerOcc::SchedulerOcc()
    : SchedulerClassic(),
      start_time_(std::chrono::steady_clock::now()) {
  mdb_txn_mgr_ = make_shared<mdb::TxnMgrOCC>();

  // Initialize abort reason counters
  aborts_by_reason_[AbortReason::EARLY] = 0;
  aborts_by_reason_[AbortReason::VERSION_MISMATCH] = 0;
  aborts_by_reason_[AbortReason::LOCK_CONFLICT] = 0;
  aborts_by_reason_[AbortReason::UNKNOWN] = 0;

  // Register signal handler to print metrics on SIGTERM/SIGINT
  g_scheduler_occ = this;
  signal(SIGTERM, sigterm_handler_baseline);
  signal(SIGINT, sigterm_handler_baseline);
}

SchedulerOcc::~SchedulerOcc() {
  // Clear global pointer
  g_scheduler_occ = nullptr;

  // Export results to CSV before logging
  ExportResultsToCSV();

  // Log final statistics
  Log_info("SchedulerOcc (Baseline): Transaction metrics:");
  Log_info("  Total attempted: %llu", num_transactions_attempted_.load());
  Log_info("  Total committed: %llu", num_transactions_committed_.load());
  Log_info("  Total aborted: %llu", num_transactions_aborted_.load());
  Log_info("  Abort rate: %.2f%%", GetAbortRate() * 100.0);
  Log_info("  Throughput: %.2f TPS", GetThroughput());

  Log_info("SchedulerOcc (Baseline): Abort breakdown:");
  Log_info("  Version mismatch: %llu", GetAbortCount(AbortReason::VERSION_MISMATCH));
  Log_info("  Lock conflicts: %llu", GetAbortCount(AbortReason::LOCK_CONFLICT));
  Log_info("  Unknown: %llu", GetAbortCount(AbortReason::UNKNOWN));
}

uint64_t SchedulerOcc::GetAbortCount(AbortReason reason) const {
  auto it = aborts_by_reason_.find(reason);
  return (it != aborts_by_reason_.end()) ? it->second.load() : 0;
}

mdb::Txn* SchedulerOcc::get_mdb_txn(const i64 tid) {
  mdb::Txn *txn = nullptr;
  auto it = mdb_txns_.find(tid);
  if (it == mdb_txns_.end()) {
    //verify(IS_MODE_2PL);
    txn = mdb_txn_mgr_->start(tid);
    //XXX using occ lazy mode: increment version at commit time
    ((mdb::TxnOCC *) txn)->set_policy(mdb::OCC_LAZY);
    auto ret = mdb_txns_.insert(std::pair<i64, mdb::Txn *>(tid, txn));
    verify(ret.second);
  } else {
    txn = it->second;
  }
  verify(mdb_txn_mgr_->rtti() == mdb::symbol_t::TXN_OCC);
  verify(txn->rtti() == mdb::symbol_t::TXN_OCC);
  verify(txn != nullptr);
  return txn;
}

bool SchedulerOcc::DoPrepare(txnid_t tx_id) {
  auto start_time = std::chrono::steady_clock::now();
  
  // Increment attempted counter
  num_transactions_attempted_++;

  // do nothing here?
  auto tx_box = dynamic_pointer_cast<TxOcc>(GetOrCreateTx(tx_id));
  // TODO do version control, locks, etc.
  auto txn = (mdb::TxnOCC*) get_mdb_txn(tx_id);
  verify(txn != nullptr);
  verify(txn->outcome_ == symbol_t::NONE);
  verify(!txn->verified_);

  auto setup_time = std::chrono::steady_clock::now();
  auto setup_us = std::chrono::duration_cast<std::chrono::microseconds>(setup_time - start_time).count();

  // only do version check on leader.
  if (tx_box->is_leader_hint_ && !txn->version_check()) {
    Log_debug("txn: occ validation failed. id %" PRIx64 "site: %x",
        (int64_t) tx_id, (int) this->site_id_);
    txn->__debug_abort_ = 1;
    RecordAbort(AbortReason::VERSION_MISMATCH);  // Version check failed
    auto end_time = std::chrono::steady_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    Log_debug("DoPrepare (baseline) tx %" PRIx64 ": setup=%ldus, total=%ldus (version_fail)", tx_id, setup_us, total_us);
    return false;
  } else {
    // now lock the commit
    for (auto &it : txn->ver_check_read_) {
      Row *row = it.first.row;
      auto *v_row = (VersionedRow *) row;
      // Log_debug removed - per-row logging causes overhead
      if (!v_row->rlock_row_by(txn->id())) {
#ifdef CONFLICT_COUNT
        const Table *tbl = v_row->get_table();
      auto cc_it = TxnMgr::rl_conflict_count_.find(tbl);
      if (cc_it == TxnMgr::rl_conflict_count_.end())
          TxnMgr::rl_conflict_count_[tbl] = 1;
      else
          cc_it->second++;
#endif
        for (auto &lit : txn->locks_) {
          Row* r = lit.first;
          verify(r->rtti() == symbol_t::ROW_VERSIONED);
          auto vr = (VersionedRow *) r;
          vr->unlock_row_by(txn->id());
        }
        txn->locks_.clear();
        Log_debug("txn: occ read locks failed. id %" PRIx64 ", site: %x, is-leader: %d",
            (int64_t)tx_id, (int)this->site_id_, tx_box->is_leader_hint_);
        txn->__debug_abort_ = 1;
        RecordAbort(AbortReason::LOCK_CONFLICT);  // Read lock acquisition failed
        auto end_time = std::chrono::steady_clock::now();
        auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        Log_debug("DoPrepare (baseline) tx %" PRIx64 ": setup=%ldus, total=%ldus (rlock_fail)", tx_id, setup_us, total_us);
        return false;
      }
      insert_into_map(txn->locks_, row, -1);
    }
    for (auto &it : txn->updates_) {
      Row *row = it.first;
      auto v_row = (VersionedRow *) row;
      Log_debug("w_lock row: %llx", row);
      if (!v_row->wlock_row_by(txn->id())) {
#ifdef CONFLICT_COUNT
        const Table *tbl = v_row->get_table();
      auto cc_it = TxnMgr::wl_conflict_count_.find(tbl);
      if (cc_it == TxnMgr::wl_conflict_count_.end())
          TxnMgr::wl_conflict_count_[tbl] = 1;
      else
          cc_it->second++;
#endif
        for (auto &lit : txn->locks_) {
          Row* r = lit.first;
          verify(r->rtti() == symbol_t::ROW_VERSIONED);
          auto vr = (VersionedRow *) row;
          vr->unlock_row_by(txn->id());
        }
        txn->locks_.clear();
        Log_debug("txn: occ write locks failed. id %" PRIx64 "site: %x", (int64_t)tx_id, (int)this->site_id_);
        txn->__debug_abort_ = 1;
        RecordAbort(AbortReason::LOCK_CONFLICT);  // Write lock acquisition failed
        auto end_time = std::chrono::steady_clock::now();
        auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        Log_debug("DoPrepare (baseline) tx %" PRIx64 ": setup=%ldus, total=%ldus (wlock_fail)", tx_id, setup_us, total_us);
        return false;
      }
      insert_into_map(txn->locks_, row, -1);
    }
    Log_debug("txn: %llx occ locks succeed.", (int64_t)tx_id);
    txn->__debug_abort_ = 0;
    txn->verified_ = true;
  }
  
  auto end_time = std::chrono::steady_clock::now();
  auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
  Log_debug("DoPrepare (baseline) tx %" PRIx64 ": setup=%ldus, total=%ldus (success)", tx_id, setup_us, total_us);
  return true;
}

void SchedulerOcc::DoCommit(Tx& tx) {
  // Increment committed counter
  num_transactions_committed_++;

  // TODO do version control, locks, etc.
  auto cmd_id_ = tx.tid_;
  auto mdb_txn_ = (mdb::TxnOCC*) get_mdb_txn(cmd_id_);

  verify(mdb_txn_ == RemoveMTxn(cmd_id_));

  auto txn = dynamic_cast<mdb::TxnOCC*>(mdb_txn_);
  if (txn->__debug_abort_) {
    Log_fatal("2pc commit request received after prepare failure for %" PRIx64,
              tx.tid_);
  }
  verify(txn->outcome_ == symbol_t::NONE);
  verify(txn->verified_);

  for (auto &it : txn->inserts_) {
    it.table->insert(it.row);
  }
  for (auto it = txn->updates_.begin(); it != txn->updates_.end(); /* no ++it! */) {
    Row *row = it->first;
    verify(row->rtti() == mdb::ROW_VERSIONED);
    auto v_row = (VersionedRow *) row;
    const Table *tbl = row->get_table();
    if (tbl->rtti() == mdb::TBL_SNAPSHOT) {
      // update on snapshot table (remove then insert)
      Row *new_row = row->copy();
      auto v_new_row = (VersionedRow *) new_row;

      // batch update all values
      auto it_end = txn->updates_.upper_bound(row);
      while (it != it_end) {
        colid_t column_id = it->second.first;
        Value &value = it->second.second;
        new_row->update(column_id, value);
        if (txn->policy_ == symbol_t::OCC_LAZY) {
          // increase version for both old and new row
          // so that other Txn will verify fail on old row
          // and also the version info is passed onto new row
          v_row->incr_column_ver(column_id);
          v_new_row->incr_column_ver(column_id);
        }
        ++it;
      }

      auto ss_tbl = (SnapshotTable *) tbl;
      ss_tbl->remove(row);
      ss_tbl->insert(new_row);

      redirect_locks(txn->locks_, new_row, row);

      // redirect the accessed_rows_
      auto it_accessed = txn->accessed_rows_.find(row);
      if (it_accessed != txn->accessed_rows_.end()) {
        (*it_accessed)->release();
        txn->accessed_rows_.erase(it_accessed);
        new_row->ref_copy();
        txn->accessed_rows_.insert(new_row);
      }
    } else {
      colid_t column_id = it->second.first;
      Value &value = it->second.second;
      row->update(column_id, value);
      if (txn->policy_ == symbol_t::OCC_LAZY) {
        v_row->incr_column_ver(column_id);
      }
      ++it;
    }
  }
  for (auto &it : txn->removes_) {
    if (txn->policy_ == symbol_t::OCC_LAZY) {
      Row *row = it.row;
      verify(row->rtti() == symbol_t::ROW_VERSIONED);
      auto v_row = (VersionedRow *) row;
      for (size_t col_id = 0; col_id < v_row->schema()->columns_count();
           col_id++) {
        v_row->incr_column_ver(col_id);
      }
    }
    // remove the locks since the row has gone already
    txn->locks_.erase(it.row);
    it.table->remove(it.row);
  }
  txn->outcome_ = symbol_t::TXN_COMMIT;
  txn->release_resource();
  delete mdb_txn_;
  tx.mdb_txn_ = nullptr;
}

bool SchedulerOcc::Dispatch(cmdid_t cmd_id,
                            shared_ptr<Marshallable> cmd,
                            TxnOutput& ret_output) {
  // Create dummy DepId (required by SchedulerClassic::Dispatch signature)
  DepId dep_id;
  dep_id.str = "dep";
  dep_id.id = 0;

  // Call parent SchedulerClassic::Dispatch with 4 parameters
  return SchedulerClassic::Dispatch(cmd_id, dep_id, cmd, ret_output);
}

void SchedulerOcc::ExportResultsToCSV() {
  // Generate timestamped filename
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm tm_now;
  localtime_r(&time_t_now, &tm_now);

  char filename[256];
  snprintf(filename, sizeof(filename), "results_%04d%02d%02d_%02d%02d%02d.csv",
           tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
           tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

  // Check if file exists (append) or new (write header)
  bool write_header = (access(filename, F_OK) == -1);

  FILE* fp = fopen(filename, "a");
  if (!fp) {
    Log_warn("Failed to open CSV file: %s", filename);
    return;
  }

  if (write_header) {
    fprintf(fp, "timestamp,mode,duration,attempted,committed,aborted,abort_rate,tps\n");
  }

  // Get metrics
  uint64_t attempted = num_transactions_attempted_.load();
  uint64_t committed = num_transactions_committed_.load();
  uint64_t aborted = num_transactions_aborted_.load();
  double abort_rate = attempted > 0 ? (double)aborted / attempted : 0.0;
  uint32_t duration = Config::GetConfig()->duration_;
  double tps = duration > 0 ? (double)committed / duration : 0.0;

  // Write ISO timestamp
  char timestamp[64];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);

  fprintf(fp, "%s,occ,%u,%lu,%lu,%lu,%.4f,%.2f\n",
          timestamp, duration, attempted, committed, aborted, abort_rate, tps);

  fclose(fp);
  Log_info("Results exported to %s", filename);
}

} // namespace janus
