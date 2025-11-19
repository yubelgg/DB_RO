#include "scheduler_enhanced.h"
#include "base/all.hpp"

namespace janus {

SchedulerOccEnhanced::SchedulerOccEnhanced() : SchedulerOcc() {
  // Create batch validator
  batch_validator_ =
      std::make_unique<BatchValidator>(batch_size_,
                                       1 // num_workers (unused in Step 2)
      );

  // Start background validation thread
  running_ = true;
  validation_thread_ = std::thread(&SchedulerOccEnhanced::ValidationLoop, this);

  Log_info(
      "SchedulerOccEnhanced: initialized with batch_size=%zu, timeout=%ldus",
      batch_size_, batch_timeout_.count());
}

SchedulerOccEnhanced::~SchedulerOccEnhanced() {
  // Stop background thread
  running_ = false;

  // Wake up thread if it's waiting
  validation_queue_.Enqueue(nullptr); // Sentinel value to wake thread

  // Wait for thread to finish
  if (validation_thread_.joinable()) {
    validation_thread_.join();
  }

  Log_info("SchedulerOccEnhanced: shut down");
}

bool SchedulerOccEnhanced::DoPrepare(txnid_t tx_id) {
  // Get enhanced transaction
  auto tx_box = std::dynamic_pointer_cast<TxOccEnhanced>(GetOrCreateTx(tx_id));
  verify(tx_box != nullptr);

  // Create promise/future for waiting on validation result
  auto promise = std::make_shared<std::promise<bool>>();
  auto future = promise->get_future();

  // Store promise in batch metadata
  tx_box->GetBatchMetadata().validation_promise = promise;

  // Enqueue transaction for batch validation
  validation_queue_.Enqueue(tx_box.get());

  Log_debug("DoPrepare: enqueued tx %" PRIx64 " for batch validation", tx_id);

  // Wait for validation result from background thread
  bool validation_passed = future.get();

  Log_debug("DoPrepare: tx %" PRIx64 " validation result: %s", tx_id,
            validation_passed ? "PASSED" : "FAILED");

  return validation_passed;
}

void SchedulerOccEnhanced::DoCommit(Tx &tx) {
  // Step 2: Just delegate to parent
  // TODO (Step 4): Notify EarlyAbortDetector of version changes
  SchedulerOcc::DoCommit(tx);
}

void SchedulerOccEnhanced::ValidationLoop() {
  Log_info("ValidationLoop: background thread started");

  while (running_) {
    // Dequeue batch with timeout
    auto batch = validation_queue_.DequeueBatch(batch_size_, batch_timeout_);

    // Check for shutdown sentinel
    if (!batch.empty() && batch[0] == nullptr) {
      Log_info("ValidationLoop: received shutdown signal");
      break;
    }

    // Skip empty batches
    if (batch.empty()) {
      continue;
    }

    Log_debug("ValidationLoop: processing batch of size %zu", batch.size());

    // Validate batch (serial validation in Step 2)
    auto result = batch_validator_->ValidateBatch(batch);

    Log_debug("ValidationLoop: batch %zu validation complete, "
              "%zu passed, %zu failed, time=%ldus",
              result.batch_id,
              std::count(result.passed.begin(), result.passed.end(), true),
              std::count(result.passed.begin(), result.passed.end(), false),
              result.total_time.count());
  }

  Log_info("ValidationLoop: background thread exiting");
}

} // namespace janus
