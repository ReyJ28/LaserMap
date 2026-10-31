// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include "trace.h"

namespace lasermap {

// Optional mask the host plugin applies to the VIDEO so picture and laser stay locked together.
// Coordinates are laser-space ([-1,1], longer image side spans the range, y up).
struct VideoMask {
  enum Kind { None, HalfPlane, Circle } kind = None;
  float nx = 1, ny = 0, edge = 0;  // HalfPlane: visible where dot(p,n) >= edge (keepGreater) or <= edge
  bool keepGreater = true;
  float cx = 0, cy = 0, radius = 0;  // Circle: visible inside
};

struct ModeParams {
  float time = 0;      // seconds, monotonically increasing
  float speed = 0.5f;  // 0..1
  float size = 0.5f;   // 0..1, meaning depends on mode
  float count = 0.5f;  // 0..1, meaning depends on mode
  bool invert = false; // sweep: false = erase the clip, true = draw it in
  int maxPoints = 4000;
};

struct ModeOutput {
  PointFrame points;
  VideoMask mask;
};

// Beams race along the outlines with fading tails. count = beams per shape, size = tail length.
ModeOutput renderBeamRunners(const std::vector<Shape>& shapes, const ModeParams& p);
// Outlines drawn only inside scrolling horizontal bands. size = band period.
ModeOutput renderTraceBars(const std::vector<Shape>& shapes, const ModeParams& p);
// A vertical beam sweeps left to right; the clip is erased (or drawn in) behind it, laser and video share the edge.
ModeOutput renderSweep(const std::vector<Shape>& shapes, const ModeParams& p);
// A laser circle wanders; the video is visible only inside it. size = radius.
ModeOutput renderSpotlight(const ModeParams& p);
// Calibration grid for aligning laser and picture.
ModeOutput renderGrid(const ModeParams& p);

}  // namespace lasermap
