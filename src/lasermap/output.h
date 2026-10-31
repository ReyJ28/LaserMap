// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include "types.h"

namespace lasermap {

// Sink for finished (already safety-processed) laser frames. Implementations must not block the
// render thread and must never throw. A Pangolin BEYOND backend will implement this; none exists yet.
class ILaserOutput {
 public:
  virtual ~ILaserOutput() = default;
  virtual void send(const PointFrame& frame) noexcept = 0;
};

class NullOutput final : public ILaserOutput {
 public:
  void send(const PointFrame&) noexcept override {}
};

}  // namespace lasermap
