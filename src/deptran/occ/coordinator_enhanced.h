#pragma once

#include "coordinator.h"

namespace janus {

/**
 * Enhanced OCC Coordinator
 *
 * Simple coordinator for enhanced OCC protocol.
 * Currently uses delegating constructor to reuse baseline CoordinatorOcc.
 *
 * Future Enhancements: May add factory methods to create enhanced
 * schedulers and transactions if needed.
 */
class CoordinatorOccEnhanced : public CoordinatorOcc {
public:
  // Use parent constructor
  using CoordinatorOcc::CoordinatorOcc;
};

} // namespace janus
