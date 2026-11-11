// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include "types.h"

namespace lasermap {

// Simple measurements of what is on screen, each 0..1.
struct ReactSignals {
  float brightness = 0;  // mean luminance
  float motion = 0;      // change since the previous analysed frame (scaled so ordinary movement reaches ~0.5)
  float colour = 0;      // how much of the picture is near the key hue (scaled, saturated pixels only)
};

class ReactAnalyser {
 public:
  ReactSignals update(const Frame& f, float keyHue, float keyTolerance);
  void reset() { prev_.clear(); }
 private:
  std::vector<uint8_t> prev_;
};

enum ReactCond { RAlways = 0, RBright, RDark, RMotion, RStill, RColour };

// Per-mode switch with hysteresis (no flicker around the threshold) and a hold time.
class ReactGate {
 public:
  bool update(ReactCond c, const ReactSignals& s, float threshold, float holdSeconds, double dt);
  bool open() const { return on_; }
 private:
  bool on_ = true;
  float hold_ = 0;
};

}  // namespace lasermap
