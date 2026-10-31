// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include "types.h"

namespace lasermap {

struct SafetyParams {
  bool staticGuard = true;        // fade beams parked on one spot
  bool outputEnabled = false;     // fail-safe default: no light until explicitly enabled
  float maxBrightness = 1.0f;     // global cap 0..1
  int maxPoints = 4000;           // per-frame point cap
  float dwellExtent = 0.04f;      // content smaller than this (normalized units) counts as "parked"
  float dwellMove = 0.01f;        // centroid must move more than this to count as moving
  float dwellSeconds = 1.5f;      // parked this long -> start fading
  float fadeSeconds = 0.5f;       // fade to black over this long
};

// Last line of defence between a tracing mode and the output.
// Never throws; any invalid input produces an empty (dark) frame.
class SafetyGuard {
 public:
  explicit SafetyGuard(SafetyParams p = {}) : p_(p) {}
  void setParams(const SafetyParams& p) { p_ = p; }
  const SafetyParams& params() const { return p_; }
  // dt = seconds since the previous call.
  PointFrame process(const PointFrame& in, double dt);
  float currentGain() const { return gain_; }

 private:
  SafetyParams p_;
  double parkedFor_ = 0;
  Vec2 lastCentroid_{};
  bool haveLast_ = false;
  float gain_ = 1.f;
};

}  // namespace lasermap
