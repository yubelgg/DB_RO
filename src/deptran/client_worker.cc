#include <cmath>
#include "client_worker.h"
#include "frame.h"
#include "procedure.h"
#include "coordinator.h"
#include "workload.h"
#include "benchmark_control_rpc.h"

namespace janus {

ClientWorker::~ClientWorker() {
  if (tx_generator_) {
    delete tx_generator_;
  }
  for (auto c : created_coordinators_) {
    delete c;
  }
//  dispatch_pool_->release();

  // Shutdown PollThreadWorker if we own it
  if (poll_thread_worker_) {
    poll_thread_worker_->shutdown();
  }
}

void ClientWorker::ForwardRequestDone(Coordinator* coo,
                                      TxReply* output,
                                      DeferredReply* defer,
                                      TxReply& txn_reply) {
  verify(coo != nullptr);
  verify(output != nullptr);

  *output = txn_reply;

  bool have_more_time = timer_->elapsed() < duration;
  if (have_more_time && config_->client_type_ == Config::Open) {
    std::lock_guard<std::mutex> lock(coordinator_mutex);
    coo->forward_status_ = NONE;
    free_coordinators_.push_back(coo);
  } else if (!have_more_time) {
    Log_debug("times up. stop.");
    Log_debug("n_concurrent_ = %d", n_concurrent_);
//    finish_mutex.lock();
    n_concurrent_--;
    if (n_concurrent_ == 0) {
      Log_debug("all coordinators finished... signal done");
//      finish_cond.signal();
    } else {
      Log_debug("waiting for %d more coordinators to finish", n_concurrent_);
    }
//    finish_mutex.unlock();
  }

  defer->reply();
}

void ClientWorker::RequestDone(Coordinator* coo, TxReply& txn_reply) {
  verify(coo != nullptr);

  if (txn_reply.res_ == SUCCESS)
    success++;
  num_txn++;
  num_try.fetch_add(txn_reply.n_try_);

  bool have_more_time = timer_->elapsed() < duration;
  Log_info("RequestDone: tx_id=%" PRIx64 ", result=%s, total=%u, success=%u",
           txn_reply.tx_id_,
           txn_reply.res_ == SUCCESS ? "SUCCESS" : "REJECT",
           num_txn.load(),
           success.load());
  Log_debug("elapsed: %2.2f; duration: %d", timer_->elapsed(), duration);
  if (have_more_time && config_->client_type_ == Config::Open) {
    std::lock_guard<std::mutex> lock(coordinator_mutex);
    free_coordinators_.push_back(coo);
  } else if (have_more_time && config_->client_type_ == Config::Closed) {
    Log_debug("there is still time to issue another request. continue.");
    DispatchRequest(coo);
  } else if (!have_more_time) {
    Log_debug("times up. stop.");
    Log_debug("n_concurrent_ = %d", n_concurrent_);
//    finish_mutex.lock();
    n_concurrent_--;
    verify(n_concurrent_ >= 0);
    if (n_concurrent_ == 0) {
      Log_debug("all coordinators finished... signal done");
//      finish_cond.signal();
    } else {
      Log_debug("waiting for %d more coordinators to finish", n_concurrent_);
      Log_debug("transactions they are processing:");
      // for debug purpose, print ongoing transaction ids.
      for (auto c : created_coordinators_) {
        if (c->ongoing_tx_id_ > 0) {
          Log_debug("\t %" PRIx64, c->ongoing_tx_id_);
        }
      }
    }
//    finish_mutex.unlock();
  } else {
    verify(0);
  }
}

Coordinator* ClientWorker::FindOrCreateCoordinator() {
  std::lock_guard<std::mutex> lock(coordinator_mutex);

  Coordinator* coo = nullptr;

  if (free_coordinators_.size() > 0) {
    coo = dynamic_cast<Coordinator*>(free_coordinators_.back());
    free_coordinators_.pop_back();
  } else {
    if (created_coordinators_.size() == UINT16_MAX) {
      return nullptr;
    }
    verify(created_coordinators_.size() <= UINT16_MAX);
    coo = CreateCoordinator(created_coordinators_.size());
  }

  return coo;
}

Coordinator* ClientWorker::CreateCoordinator(uint16_t offset_id) {

  cooid_t coo_id = cli_id_;
  coo_id = (coo_id << 16) + offset_id;
  auto coo = frame_->CreateCoordinator(coo_id,
                                       config_,
                                       benchmark,
                                       ccsi,
                                       id,
                                       txn_reg_);
  coo->loc_id_ = my_site_.locale_id;
  coo->commo_ = commo_;
  coo->forward_status_ = forward_requests_to_leader_ ? FORWARD_TO_LEADER : NONE;
  Log_debug("coordinator %d created at site %d: forward %d",
            coo->coo_id_,
            this->my_site_.id,
            coo->forward_status_);
  created_coordinators_.push_back(coo);
  return coo;
}

void ClientWorker::Work() {
  Log_debug("%s: %d", __FUNCTION__, this->cli_id_);
  txn_reg_ = std::make_shared<TxnRegistry>();
  verify(config_ != nullptr);
  Workload* workload = Workload::CreateWorkload(config_);
  workload->txn_reg_ = txn_reg_;
  workload->RegisterPrecedures();

  commo_->WaitConnectClientLeaders();
  if (ccsi) {
    ccsi->wait_for_start(id);
  }
  Log_debug("after wait for start");

  timer_ = new Timer();
  timer_->start();

  if (config_->client_type_ == Config::Closed) {
    Log_info("closed loop clients: n_concurrent=%d, duration=%u", n_concurrent_, duration);
    verify(n_concurrent_ > 0);
    int n = n_concurrent_;
    auto sp_job = std::make_shared<OneTimeJob>([this] () {
      Log_info("Starting to create %d coordinators and dispatch transactions", n_concurrent_);
      for (uint32_t n_tx = 0; n_tx < n_concurrent_; n_tx++) {
        auto coo = CreateCoordinator(n_tx);
        Log_info("Created coordinator %d (coo_id=%d), dispatching request...", n_tx, coo->coo_id_);
        this->DispatchRequest(coo);
      }
      Log_info("All %d initial transactions dispatched", n_concurrent_);
    });
    Log_info("Adding job to poll_thread_worker...");
    poll_thread_worker_->add(dynamic_pointer_cast<Job>(sp_job));
    Log_info("Job added to poll_thread_worker, waiting for completion...");
  } else {
    Log_info("open loop clients.");
    const std::chrono::nanoseconds wait_time
        ((int) (pow(10, 9) * 1.0 / (double) config_->client_rate_));
    double tps = 0;
    long txn_count = 0;
    auto start = std::chrono::steady_clock::now();
    std::chrono::nanoseconds elapsed;

    while (timer_->elapsed() < duration) {
      while (tps < config_->client_rate_ && timer_->elapsed() < duration) {
        auto coo = FindOrCreateCoordinator();
        if (coo != nullptr) {
          auto p_job = (Job*)new OneTimeJob([this, coo] () {
            this->DispatchRequest(coo);
          });
          shared_ptr<Job> sp_job(p_job);
          poll_thread_worker_->add(sp_job);
          txn_count++;
          elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>
              (std::chrono::steady_clock::now() - start);
          tps = (double) txn_count / elapsed.count() * pow(10, 9);
        }
      }
      auto next_time = std::chrono::steady_clock::now() + wait_time;
      std::this_thread::sleep_until(next_time);
      elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>
          (std::chrono::steady_clock::now() - start);
      tps = (double) txn_count / elapsed.count() * pow(10, 9);
    }
    Log_debug("exit client dispatch loop...");
  }

//  finish_mutex.lock();
  Log_info("Entering wait loop: n_concurrent=%d, elapsed=%2.2f, duration=%u",
           n_concurrent_, timer_->elapsed(), duration);
  int wait_count = 0;
  while (n_concurrent_ > 0) {
    wait_count++;
    Log_info("Waiting for transactions to complete... n_concurrent=%d, wait_count=%d, elapsed=%2.2f",
             n_concurrent_, wait_count, timer_->elapsed());
    sleep(1);
//    finish_cond.wait(finish_mutex);
    // Safety: exit after waiting too long (duration + 30 seconds)
    if (wait_count > (int)(duration + 30)) {
      Log_error("TIMEOUT: Still waiting for %d transactions after %d seconds, forcing clean exit",
                n_concurrent_, wait_count);
      n_concurrent_ = 0;  // Force clean exit so metrics are printed
      break;
    }
  }
//  finish_mutex.unlock();

  Log_info("Finish:\nTotal: %u, Commit: %u, Attempts: %u, Running for %u\n",
           num_txn.load(),
           success.load(),
           num_try.load(),
           Config::GetConfig()->get_duration());
  fflush(stderr);
  fflush(stdout);
  if (ccsi) {
    Log_info("%s: wait_for_shutdown at client %d", __FUNCTION__, cli_id_);
    ccsi->wait_for_shutdown();
  }
  delete timer_;
  return;
}

void ClientWorker::AcceptForwardedRequest(TxRequest& request,
                                          TxReply* txn_reply,
                                          rrr::DeferredReply* defer) {
  const char* f = __FUNCTION__;

  // obtain free a coordinator first
  Coordinator* coo = nullptr;
  while (coo == nullptr) {
    coo = FindOrCreateCoordinator();
  }
  coo->forward_status_ = PROCESS_FORWARD_REQUEST;

  // run the task
  std::function<void()> task = [=]() {
    TxRequest req(request);
    req.callback_ = std::bind(&ClientWorker::ForwardRequestDone,
                              this,
                              coo,
                              txn_reply,
                              defer,
                              std::placeholders::_1);
    Log_debug("%s: running forwarded request at site %d", f, my_site_.id);
    coo->DoTxAsync(req);
  };
  task();
//  dispatch_pool_->run_async(task); // this causes bug
}

void ClientWorker::DispatchRequest(Coordinator* coo) {
  const char* f = __FUNCTION__;
  std::function<void()> task = [=]() {
    Log_info("DispatchRequest: cli_id=%d, coo_id=%d", cli_id_, coo->coo_id_);
    TxRequest req;
    {
      std::lock_guard<std::mutex> lock(this->request_gen_mutex);
      tx_generator_->GetTxRequest(&req, coo->coo_id_);
    }
    Log_info("DispatchRequest: tx_type=%d, dispatching transaction", req.tx_type_);
    req.callback_ = std::bind(&ClientWorker::RequestDone,
                              this,
                              coo,
                              std::placeholders::_1);
    coo->DoTxAsync(req);
  };
  task();
//  dispatch_pool_->run_async(task); // this causes bug
}

ClientWorker::ClientWorker(
    uint32_t id,
    Config::SiteInfo& site_info,
    Config* config,
    ClientControlServiceImpl* ccsi,
    rusty::Arc<PollThreadWorker> poll_thread_worker) :
    id(id),
    my_site_(site_info),
    config_(config),
    cli_id_(site_info.id),
    benchmark(config->benchmark()),
    mode(config->get_mode()),
    duration(config->get_duration()),
    ccsi(ccsi),
    n_concurrent_(config->get_concurrent_txn()) {
  poll_thread_worker_ = !poll_thread_worker ? PollThreadWorker::create() : poll_thread_worker;
  frame_ = Frame::GetFrame(config->tx_proto_);
  tx_generator_ = frame_->CreateTxGenerator();
  config->get_all_site_addr(servers_);
  num_txn.store(0);
  success.store(0);
  num_try.store(0);
  commo_ = frame_->CreateCommo(poll_thread_worker_);
  commo_->loc_id_ = my_site_.locale_id;
  forward_requests_to_leader_ =
      (config->replica_proto_ == MODE_MULTI_PAXOS && site_info.locale_id != 0) ? true
                                                                         : false;
  Log_debug("client %d created; forward %d",
            cli_id_,
            forward_requests_to_leader_);
}

} // namespace janus

