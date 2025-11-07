

#include "server.h"
#include "raft_rpc.h"
#include "../classic/tpc_command.h"
#include "coordinator.h"
#include "exec.h"
#include "frame.h"

namespace janus {

RaftServer::RaftServer(Frame *frame) {
  frame_ = frame;
  /* Server initialization */
}

RaftServer::~RaftServer() {
  /* Server teardown */
}

void RaftServer::Setup() {
  // Calculate cluster size from proxies
  auto &proxies = commo()->rpc_par_proxies_[0];
  cluster_size_ = proxies.size() + 1; // +1 for self

  // Initialize random seed for election timeouts
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  srand(ts.tv_sec * 1000000 + ts.tv_nsec / 1000 + loc_id_ * 997);

  ResetElectionTimeout();

  // Election timeout coroutine
  Coroutine::CreateRun([this]() {
    bool was_disconnected = false;
    while (true) {
      uint64_t timeout = GetRandomElectionTimeout();
      Coroutine::Sleep(timeout);

      std::lock_guard<std::recursive_mutex> lock(mtx_);

      if (IsDisconnected()) {
        was_disconnected = true;
        continue;
      }

      if (was_disconnected) {
        was_disconnected = false;
        state_ = FOLLOWER;
        voted_for_ = -1;
        ResetElectionTimeout();
        continue;
      }

      // Check if election timeout without getting heartbeat or granting vote
      if (TimeSinceLastReset() >= timeout) {
        if (state_ != LEADER) {
          StartElection();
        }
      }
    }
  });

  // Coroutine for applying committed entries
  Coroutine::CreateRun([this]() {
    while (true) {
      Coroutine::Sleep(10000);

      std::lock_guard<std::recursive_mutex> lock(mtx_);

      // Apply committed but not yet applied entries
      while (last_applied_ < commited_index_) {
        last_applied_++;

        if (last_applied_ <= log_.size()) {
          app_next_(last_applied_, log_[last_applied_ - 1].command);
        }
      }
    }
  });
}

bool RaftServer::Start(shared_ptr<Marshallable> &cmd, uint64_t *index,
                       uint64_t *term, slotid_t slot_id, ballot_t ballot) {
  /* Client command submission */
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  if (state_ != LEADER) {
    return false;
  }

  // Append to local log
  LogEntry entry;
  entry.term = current_term_;
  entry.command = cmd;
  log_.push_back(entry);

  *index = log_.size();
  *term = current_term_;

  return true;
}

void RaftServer::OnRequestVote(const slotid_t& lst_log_idx,
                               const ballot_t& lst_log_term,
                               const siteid_t& can_id,
                               const ballot_t& can_term,
                               ballot_t *reply_term,
                               bool_t *vote_granted,
                               const function<void()> &cb) {
  /* RPC handler for RequestVote */
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  HandleRequestVoteLogic(can_term, can_id, lst_log_idx, lst_log_term,
                         reply_term, vote_granted);

  cb(); // Always invoke callback
}

void RaftServer::HandleRequestVoteLogic(uint64_t term, uint64_t candidate_id,
                                        uint64_t last_log_index,
                                        uint64_t last_log_term,
                                        ballot_t *ret_term,
                                        bool_t *vote_granted) {

  // Rule 1: false if term < currentTerm
  if (term < current_term_) {
    *ret_term = current_term_;
    *vote_granted = false;
    return;
  }

  // Set currentTerm = T, convert to follower
  if (term > current_term_) {
    current_term_ = term;
    state_ = FOLLOWER;
    voted_for_ = -1;
  }

  // Check if candidate's log is up to date
  bool log_ok = false;
  uint64_t my_last_log_term = 0;
  uint64_t my_last_log_index = 0;

  if (!log_.empty()) {
    my_last_log_index = log_.size();
    my_last_log_term = log_.back().term;
  }

  if (last_log_term > my_last_log_term) {
    log_ok = true;
  } else if (last_log_term == my_last_log_term) {
    if (last_log_index >= my_last_log_index) {
      log_ok = true;
    }
  }

  // Rule 2: grant vote if haven't voted or voted for this candidate, and log is ok
  if ((voted_for_ == -1 || voted_for_ == (int64_t)candidate_id) && log_ok) {
    *vote_granted = true;
    voted_for_ = candidate_id;
    // Reset timeout after granting vote
    ResetElectionTimeout();
  } else {
    *vote_granted = false;
  }

  *ret_term = current_term_;
}

void RaftServer::OnAppendEntries(const slotid_t slot_id,
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
                                 const function<void()> &cb) {
  /* RPC handler for AppendEntries - adapted for single-entry interface */
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  // Convert single entry to vector for HandleAppendEntriesLogic
  vector<LogEntryRPC> entries;
  if (cmd) {
    LogEntryRPC entry;
    entry.term = leaderNextLogTerm;
    entry.command = MarshallDeputy(cmd);
    entries.push_back(entry);
  }

  bool_t success = false;
  HandleAppendEntriesLogic(leaderCurrentTerm, loc_id_, leaderPrevLogIndex,
                           leaderPrevLogTerm, entries, leaderCommitIndex,
                           followerCurrentTerm, &success);

  *followerAppendOK = success ? 1 : 0;
  *followerLastLogIndex = log_.size();

  cb(); // Always invoke callback
}

void RaftServer::HandleAppendEntriesLogic(
    uint64_t term, uint64_t leader_id, uint64_t prev_log_index,
    uint64_t prev_log_term, const vector<LogEntryRPC> &entries,
    uint64_t leader_commit, uint64_t *ret_term, bool_t *followerAppendOK) {

  // False if term < currentTerm
  if (term < current_term_) {
    *ret_term = current_term_;
    *followerAppendOK = false;
    return;
  }

  if (term > current_term_) {
    current_term_ = term;
    state_ = FOLLOWER;
    voted_for_ = -1;
  }

  // If receive append entry from a leader with term >= curr term
  // and we are candidate, convert to follower
  if (state_ == CANDIDATE && term >= current_term_) {
    state_ = FOLLOWER;
  }

  ResetElectionTimeout();

  // Log consistency check
  if (prev_log_index > 0) {
    if (prev_log_index > log_.size()) {
      *ret_term = current_term_;
      *followerAppendOK = false;
      return;
    }

    // Check if term matches at prev log index
    if (log_[prev_log_index - 1].term != prev_log_term) {
      log_.erase(log_.begin() + prev_log_index - 1, log_.end());
      *ret_term = current_term_;
      *followerAppendOK = false;
      return;
    }
  }

  // Append new entries
  uint64_t log_idx = prev_log_index;
  for (size_t i = 0; i < entries.size(); i++) {
    log_idx++;

    uint64_t entry_term = entries[i].term;
    shared_ptr<Marshallable> entry_command = entries[i].command.sp_data_;

    // Check if entry conflicts with new one
    if (log_idx <= log_.size()) {
      if (log_[log_idx - 1].term != entry_term) {
        // Delete entry and all that follow
        log_.erase(log_.begin() + log_idx - 1, log_.end());
      } else {
        continue;
      }
    }

    // Append new entry
    LogEntry new_entry;
    new_entry.term = entry_term;
    new_entry.command = entry_command;
    log_.push_back(new_entry);
  }

  // Update commit index
  if (leader_commit > commited_index_) {
    commited_index_ = std::min(leader_commit, (uint64_t)log_.size());
  }

  *ret_term = current_term_;
  *followerAppendOK = true;
}

uint64_t RaftServer::GetCurrentTime() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void RaftServer::ResetElectionTimeout() {
  last_election_reset_ = GetCurrentTime();
}

uint64_t RaftServer::TimeSinceLastReset() {
  return GetCurrentTime() - last_election_reset_;
}

uint64_t RaftServer::GetRandomElectionTimeout() {
  const uint64_t MIN_TIMEOUT = 600000;
  const uint64_t MAX_TIMEOUT = 1800000;
  return MIN_TIMEOUT + (rand() % (MAX_TIMEOUT - MIN_TIMEOUT + 1));
}

void RaftServer::StartElection() {
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  // Convert to candidate state
  state_ = CANDIDATE;
  current_term_++;
  voted_for_ = loc_id_;
  votes_received_ = 1;
  voted_by_.clear();
  voted_by_.insert(loc_id_);

  ResetElectionTimeout();

  // Send RequestVote RPCs to other servers
  SendRequestVoteRPCs();
}

void RaftServer::SendRequestVoteRPCs() {

  uint64_t my_term = current_term_;
  uint64_t my_last_log_index = log_.size();
  uint64_t my_last_log_term = log_.empty() ? 0 : log_.back().term;

  // Use BroadcastVote to send to all followers
  auto event = commo()->BroadcastVote(0, my_last_log_index, my_last_log_term,
                                      loc_id_, my_term);

  // Wait for quorum or timeout in a coroutine
  Coroutine::CreateRun([this, event, my_term]() {
    event->Wait(1000000); // 1 second timeout

    std::lock_guard<std::recursive_mutex> lock(mtx_);

    // Check if we're still a candidate with the same term
    if (state_ != CANDIDATE || my_term != current_term_) {
      return;
    }

    // Check for higher term from responses
    int64_t highest_term = event->Term();
    if (highest_term > (int64_t)current_term_) {
      current_term_ = highest_term;
      state_ = FOLLOWER;
      voted_for_ = -1;
      ResetElectionTimeout();
      return;
    }

    // Check if we got majority (quorum)
    if (event->Yes()) {
      BecomeLeader();
    }
  });
}

void RaftServer::BecomeLeader() {
  if (state_ == LEADER) {
    return;
  }

  state_ = LEADER;

  // Initialize leader state
  auto &proxies = commo()->rpc_par_proxies_[0];

  next_index_.clear();
  match_index_.clear();
  follower_ids_.clear();

  uint64_t last_log_index = log_.size();

  for (auto &pair : proxies) {
    siteid_t server_id = pair.first;

    if (server_id == loc_id_) {
      continue;
    }

    // Init next index to leader last log index + 1
    next_index_.push_back(last_log_index + 1);
    // Init match index to 0
    match_index_.push_back(0);
    follower_ids_.push_back(server_id);
  }

  SendHeartbeats();
  StartHeartbeatTimer();
}

void RaftServer::SendHeartbeats() {
  // Send AppendEntries RPCs to followers
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  if (state_ != LEADER) {
    return;
  }

  auto &proxies = commo()->rpc_par_proxies_[0];
  size_t follower_idx = 0;

  for (auto &pair : proxies) {
    siteid_t server_id = pair.first;

    if (server_id == loc_id_) {
      continue;
    }

    SendAppendEntriesToFollower(server_id, follower_idx);
    follower_idx++;
  }
}

void RaftServer::SendAppendEntriesToFollower(siteid_t server_id,
                                             size_t follower_idx) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  if (state_ != LEADER) {
    return;
  }

  // Next index for this follower
  uint64_t next_idx = next_index_[follower_idx];
  uint64_t prev_log_idx = next_idx - 1;
  uint64_t prev_log_term = 0;

  if (prev_log_idx > 0 && prev_log_idx <= log_.size()) {
    prev_log_term = log_[prev_log_idx - 1].term;
  }

  // Get single entry to send (commo interface only supports one entry per RPC)
  shared_ptr<Marshallable> cmd = nullptr;
  uint64_t cmd_term = 0;
  bool has_entry = false;

  if (next_idx <= log_.size()) {
    cmd = log_[next_idx - 1].command;
    cmd_term = log_[next_idx - 1].term;
    has_entry = true;
  }

  uint64_t my_term = current_term_;
  uint64_t my_commit = commited_index_;

  // Send AppendEntries RPC async using SendAppendEntries2 with proper event timeout
  Coroutine::CreateRun([this, server_id, follower_idx, my_term, next_idx,
                        has_entry, prev_log_idx, prev_log_term, my_commit,
                        cmd, cmd_term]() {
    uint64_t ret_status = 0;
    uint64_t ret_term = 0;
    uint64_t ret_last_log_index = 0;

    auto event = commo()->SendAppendEntries2(server_id, 0, next_idx - 1, 0, true,
                                             my_term, prev_log_idx, prev_log_term,
                                             my_commit, cmd, cmd_term,
                                             &ret_status, &ret_term, &ret_last_log_index);

    event->Wait(1000000); // 1 second timeout

    // Check for timeout
    if (event->status_ == Event::TIMEOUT) {
      return;
    }

    std::lock_guard<std::recursive_mutex> lock(mtx_);

    // Check if we stepped down due to higher term
    if (ret_term > current_term_) {
      current_term_ = ret_term;
      state_ = FOLLOWER;
      voted_for_ = -1;
      ResetElectionTimeout();
      return;
    }

    if (state_ != LEADER || my_term != current_term_) {
      return;
    }

    if (ret_status) {
      // Success: update match and next index
      if (has_entry) {
        uint64_t new_match_index = next_idx;
        if (new_match_index > match_index_[follower_idx]) {
          match_index_[follower_idx] = new_match_index;
          next_index_[follower_idx] = match_index_[follower_idx] + 1;
          AdvanceCommitIndex();
        }
      }

      // If more entries to send, send next one
      if (next_index_[follower_idx] <= log_.size()) {
        Coroutine::CreateRun([this, server_id, follower_idx]() {
          Coroutine::Sleep(1000);
          std::lock_guard<std::recursive_mutex> lock(mtx_);
          if (state_ == LEADER) {
            SendAppendEntriesToFollower(server_id, follower_idx);
          }
        });
      }
    } else {
      // Failure: decrement next_index and retry
      if (next_index_[follower_idx] > 1) {
        next_index_[follower_idx]--;
      }
      Coroutine::CreateRun([this, server_id, follower_idx]() {
        Coroutine::Sleep(10000);
        std::lock_guard<std::recursive_mutex> lock(mtx_);
        if (state_ == LEADER) {
          SendAppendEntriesToFollower(server_id, follower_idx);
        }
      });
    }
  });
}

void RaftServer::AdvanceCommitIndex() {
  std::lock_guard<std::recursive_mutex> lock(mtx_);

  if (state_ != LEADER) {
    return;
  }

  // Find highest n where majority has match index >= n
  for (uint64_t n = log_.size(); n > commited_index_; n--) {
    if (log_[n - 1].term != current_term_) {
      continue;
    }

    // Count replicas
    int count = 1; // Self

    auto &proxies = commo()->rpc_par_proxies_[0];

    for (size_t i = 0; i < match_index_.size(); i++) {
      siteid_t server_id = follower_ids_[i];

      // Skip server if disconnected
      bool found = false;
      for (const auto &pair : proxies) {
        if (pair.first == server_id) {
          found = true;
          break;
        }
      }

      if (!found) {
        continue;
      }

      if (match_index_[i] >= n) {
        count++;
      }
    }

    if (count > (int)cluster_size_ / 2) {
      commited_index_ = n;
      break;
    }
  }
}

void RaftServer::StartHeartbeatTimer() {
  // Background coroutine to send heartbeats
  Coroutine::CreateRun([this]() {
    while (true) {
      Coroutine::Sleep(HEARTBEAT_INTERVAL);

      std::lock_guard<std::recursive_mutex> lock(mtx_);

      if (state_ == LEADER && !IsDisconnected()) {
        SendHeartbeats();
      } else if (state_ != LEADER) {
        break;
      }
    }
  });
}

void RaftServer::SyncRpcExample() {
  /* Example of synchronous RPC using coroutine */
  // SendString is not implemented in RaftCommo, this is just a placeholder example
  /*
  Coroutine::CreateRun([this]() {
    string res;
    auto event = commo()->SendString(0, // partition id is always 0 for lab1
                                     0, "hello", &res);
    event->Wait(1000000); // timeout after 1000000us=1s
    if (event->status_ == Event::TIMEOUT) {
      Log_info("timeout happens");
    } else {
      Log_info("rpc response is: %s", res.c_str());
    }
  });
  */
}

/* Do not modify any code below here */

void RaftServer::Disconnect(const bool disconnect) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  verify(disconnected_ != disconnect);
  // Global map of rpc_par_proxies_ values accessed by partition then by site
  static map<parid_t, map<siteid_t, map<siteid_t, vector<SiteProxyPair>>>>
      _proxies{};
  if (_proxies.find(partition_id_) == _proxies.end()) {
    _proxies[partition_id_] = {};
  }
  RaftCommo *c = (RaftCommo *)commo();
  if (disconnect) {
    verify(_proxies[partition_id_][loc_id_].size() == 0);
    verify(c->rpc_par_proxies_.size() > 0);
    auto sz = c->rpc_par_proxies_.size();
    _proxies[partition_id_][loc_id_].insert(c->rpc_par_proxies_.begin(),
                                            c->rpc_par_proxies_.end());
    c->rpc_par_proxies_ = {};
    verify(_proxies[partition_id_][loc_id_].size() == sz);
    verify(c->rpc_par_proxies_.size() == 0);
  } else {
    verify(_proxies[partition_id_][loc_id_].size() > 0);
    auto sz = _proxies[partition_id_][loc_id_].size();
    c->rpc_par_proxies_ = {};
    c->rpc_par_proxies_.insert(_proxies[partition_id_][loc_id_].begin(),
                               _proxies[partition_id_][loc_id_].end());
    _proxies[partition_id_][loc_id_] = {};
    verify(_proxies[partition_id_][loc_id_].size() == 0);
    verify(c->rpc_par_proxies_.size() == sz);
  }
  disconnected_ = disconnect;
}

bool RaftServer::IsDisconnected() { return disconnected_; }

} // namespace janus
