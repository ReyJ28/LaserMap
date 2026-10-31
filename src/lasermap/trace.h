// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include "types.h"

namespace lasermap {

struct TraceParams {
  float threshold = 0.5f;     // luminance iso-level 0..1 that defines the outline
  float simplifyEps = 1.0f;   // Douglas-Peucker tolerance in source pixels
  float minPerimeter = 8.0f;  // drop contours shorter than this (pixels)
  int maxPoints = 4000;       // hard budget for one output frame
};

// Per-pixel luminance 0..1 (Rec.709), alpha-weighted.
std::vector<float> luminance(const Frame& f);

// Closed iso-contours (marching squares) of luminance >= threshold.
// Coordinates are in pixel space (x right, y down).
std::vector<std::vector<Vec2>> findContours(const Frame& f, float threshold);

// Douglas-Peucker on a closed polygon.
std::vector<Vec2> simplifyClosed(const std::vector<Vec2>& poly, float eps);

// "Trace Clip" mode: outline the bright shapes of the frame as laser points.
// Output is a blank jump to each contour start followed by the lit loop.
PointFrame traceClip(const Frame& f, const TraceParams& p = {});

}  // namespace lasermap
