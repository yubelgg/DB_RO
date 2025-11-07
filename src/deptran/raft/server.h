#pragma once

#include "../__dep__.h"
#include "../constants.h"
#include "../scheduler.h"
#include "../classic/tpc_command.h"
#include "commo.h"

namespace janus {
class Command;
class CmdData;

#define HEARTBEAT_INTERVAL 150000

// Clean Raft log entry structure
struct LogEntry {
  uint64_t term;
  shared_ptr<Marshallable> command;
};

struct LogEntryRPC;

enum ServerState { FOLLOWER, CANDIDATE, LEADER };

class RaftServer : public TxLogServer {
private:
  // Core Raft state
  uint64_t current_term_ = 0;
  int64_t voted_for_ = -1;
  vector<LogEntry> log_;

  uint64_t commited_index_ = 0;
  uint64_t last_applied_ = 0;

  // Leader state
  vector<uint64_t> next_index_;
  vector<uint64_t> match_index_;
  vector<siteid_t> follower_ids_;

  ServerState state_ = FOLLOWER;
  uint64_t last_election_reset_ = 0;
  int votes_received_ = 0;
  set<uint64_t> voted_by_;
  size_t cluster_size_ = 0;

  // Infrastructure for target compatibility
  bool disconnected_ = false;

  // Internal Raft logic methods
  void HandleRequestVoteLogic(uint64_t term, uint64_t candidate_id,
                              uint64_t last_log_index, uint64_t last_log_term,
                              ballot_t *ret_term, bool_t *vote_granted);

  void HandleAppendEntriesLogic(uint64_t term, uint64_t leader_id,
                                uint64_t prev_log_index, uint64_t prev_log_term,
                                const vector<LogEntryRPC> &entries,
                                uint64_t leader_commit, uint64_t *ret_term,
                                bool_t *followerAppendOK);

  // Election functions
  void StartElection();
  void SendRequestVoteRPCs();
  void BecomeLeader();
  void ResetElectionTimeout();
  uint64_t GetRandomElectionTimeout();
  uint64_t TimeSinceLastReset();
  uint64_t GetCurrentTime();

  // Heartbeat functions
  void SendHeartbeats();
  void StartHeartbeatTimer();

  // Log replication functions
  void SendAppendEntriesToFollower(siteid_t server_id, size_t follower_idx);
  void AdvanceCommitIndex();
  void ApplyCommittedEntries();

  void Setup();

  RaftCommo* commo() {
    return (RaftCommo*) commo_;
  }

public:
  RaftServer(Frame *frame);
  ~RaftServer();

  // Client interface
  bool Start(shared_ptr<Marshallable> &cmd, uint64_t *index, uint64_t *term,
             slotid_t slot_id = -1, ballot_t ballot = 1);

  void GetState(bool *is_leader, uint64_t *term) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    *is_leader = (state_ == LEADER);
    *term = current_term_;
  }

  bool IsLeader() {
    return state_ == LEADER;
  }

  // Public getters for coordinator access
  uint64_t commitIndex() const {
    return commited_index_;
  }

  uint64_t currentTerm() const {
    return current_term_;
  }

  // RPC handlers - adapted to target's callback interface
  void OnRequestVote(const slotid_t& lst_log_idx,
                     const ballot_t& lst_log_term,
                     const siteid_t& can_id,
                     const ballot_t& can_term,
                     ballot_t *reply_term,
                     bool_t *vote_granted,
                     const function<void()> &cb);

  void OnAppendEntries(const slotid_t slot_id,
                       const ballot_t ballot,
                       const uint64_t leaderCurrentTerm,
                       const uint64_t leaderPrevLogIndex,
                       const uint64_t leaderPrevLogTerm,
                       const uint64_t leaderCommitIndex,
                       shared_ptr<Marshallable> &cmd,
                       const uint64_t leaderNextLogTerm,
                       uint64_t *followerAppendOK,
                       uint64_t *followerCurrentTerm,
                       uint64_t *followerLastLogIndex,
                       const function<void()> &cb);

  // Network simulation support
  void Disconnect(const bool disconnect = true);
  void Reconnect() {
    Disconnect(false);
    ResetElectionTimeout();
  }
  bool IsDisconnected();

  // Required by base class
  virtual bool HandleConflicts(Tx& dtxn,
                               innid_t inn_id,
                               vector<string>& conflicts) {
    verify(0);
  };

  void SyncRpcExample();
};
} // namespace janus
