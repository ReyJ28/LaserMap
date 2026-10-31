// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <cmath>

#include "trace.h"

namespace lasermap {

// Optional mask the host plugin applies to the VIDEO so picture and laser stay locked together.
// Coordinates are laser-space ([-1,1], longer image side spans the range, y up).
struct Disc {
  float x = 0, y = 0, r = 0, a = 1;  // centre, radius, visibility 0..1
};
struct VideoMask {
  enum Kind { None, HalfPlane, Discs, Texture } kind = None;
  float nx = 1, ny = 0, edge = 0;  // HalfPlane: visible where dot(p,n) >= edge (keepGreater) or <= edge
  bool keepGreater = true;
  std::vector<Disc> discs;         // Discs: visible inside any disc (max 16)
  float gain = 0;                  // minimum visibility everywhere (0 = hidden outside the mask)
};

inline void hueToRgb(float h, float& r, float& g, float& b) {
  h = h - float(int(h));
  if (h < 0) h += 1.f;
  float x = h * 6.f;
  r = std::fabs(x - 3.f) - 1.f; g = 2.f - std::fabs(x - 2.f); b = 2.f - std::fabs(x - 4.f);
  r = r < 0 ? 0 : (r > 1 ? 1 : r); g = g < 0 ? 0 : (g > 1 ? 1 : g); b = b < 0 ? 0 : (b > 1 ? 1 : b);
}

struct ModeParams {
  float time = 0;       // seconds, monotonically increasing
  int maxPoints = 4000;
  float spacing = 0;    // minimum point spacing in laser units; 0 = automatic
  float syncPhase = -1; // >= 0: host-beat phase 0..1 replaces time for this mode
  int maskW = 256, maskH = 144;  // size of the analysis frame (for raster masks)

  // Beam Runners
  int beamCount = 3;
  int beamDirection = 0;     // 0 forward, 1 reverse, 2 alternate per beam
  float beamSpeed = 0.9f;    // laps per second
  float tailLength = 0.15f;  // fraction of the outline
  float scatter = 0.3f;      // 0..1 random phase/speed spread between beams

  // Trace Bars
  int barsDirection = 0;     // 0 L->R, 1 R->L, 2 T->B, 3 B->T, 4 grid (both)
  float barWidth = 0.13f;    // fraction of the full width
  float barGap = 0.24f;
  float barSpeed = 0.6f;     // widths per second

  // Sweep
  float sweepProgress = 0;   // 0..1
  bool sweepReveal = false;  // false = erase the clip, true = draw it in
  float sweepAngleDeg = 0;   // 0 = left to right, counter-clockwise
  float barGlow = 0.7f;      // extra lines either side of the sweep beam
  float sweepTrail = 0.25f;  // fading echoes behind the beam

  // Spotlight
  float spotRadius = 0.25f;  // laser units
  float spotRange = 0.5f;    // 0..1 of the available travel
  float spotSpeed = 0.4f;    // cycles per second
  float spotRandom = 0.5f;   // 0 = smooth loop, 1 = erratic
  float spotAfterglow = 0.25f; // 0..1 (up to 4 s) fading echoes of past positions
  float spotRim = 0.7f;      // brightness of the outer rim ring
  float spotHue = -1;        // < 0 = white

  // Trace Reveal
  int revealDir = 0;         // 0 L->R, 1 R->L, 2 T->B, 3 B->T
  float revealSpeed = 0.79f; // widths per second
  float fadeTail = 0.16f;    // comet tail, fraction of the width
};

struct ModeOutput {
  PointFrame points;
  VideoMask mask;
  std::vector<Vec2> emitters;        // hot spots where sparks / glow can be spawned
  std::vector<uint8_t> maskImage;    // Texture mask, maskW*maskH, row 0 = top
};

ModeOutput renderBeamRunners(const std::vector<Shape>& shapes, const ModeParams& p);
ModeOutput renderTraceBars(const std::vector<Shape>& shapes, const ModeParams& p);
ModeOutput renderSweep(const std::vector<Shape>& shapes, const ModeParams& p);
ModeOutput renderSpotlight(const ModeParams& p);
ModeOutput renderGrid(const ModeParams& p);
// Shapes are traced one after another; each is revealed (filled in the video mask) the moment its trace closes.
ModeOutput renderTraceReveal(const std::vector<Shape>& shapes, const ModeParams& p);

// Stateful: bolts hit the outlines, clearing the "covering" off the video hit by hit, then the rest floods away.
struct CannonParams {
  float fireRate = 2.4f;   // hits per second
  float chaos = 0.3f;      // timing randomness 0..1
  int hitsToClear = 13;
  float ripple = 0.7f;     // ripple size/strength 0..1
  float rippleHue = 0.09f;
  bool matchLaser = true;  // ripples use the clip colour
  float syncPhase = -1;    // >= 0: fire on each wrap of the host-beat phase
};
class Cannon {
 public:
  ModeOutput render(const std::vector<Shape>& shapes, const CannonParams& p, double dt, const ModeParams& mp);
  void reset();

 private:
  struct Hit { float x, y, age; Point colour; };
  std::vector<Hit> hits_;
  int hitCount_ = 0;
  double untilFire_ = 0.3, floodT_ = -1;
  float lastSync_ = -1;
  uint32_t rng_ = 12345u;
  float rand01();
};

// Sparks and glow spawned at the emitters of any mode.
struct TrailParams {
  int style = 0;            // 0 none, 1 glow, 2 sparks, 3 glow + sparks
  float amount = 0.6f;      // glow strength
  float density = 0.35f;
  float size = 0.3f;
  float gravity = 0.3f;
  float fadeSeconds = 1.0f;
  float hue = 0.09f;
};
class TrailFX {
 public:
  void update(double dt, const std::vector<Vec2>& emitters, const TrailParams& p);
  PointFrame render(const std::vector<Vec2>& emitters, const TrailParams& p) const;
  void clear() { parts_.clear(); }
  size_t count() const { return parts_.size(); }

 private:
  struct P { float x, y, vx, vy, life, maxLife; };
  std::vector<P> parts_;
  uint32_t rng_ = 987654321u;
  float rand01();
};

// Galvo-friendly post-processing: dwell points on sharp corners and at both ends of blanked jumps.
struct ScanParams {
  int cornerDwell = 3;       // extra points on each corner sharper than cornerAngle
  float cornerAngle = 0.6f;  // radians of turn
  int blankDwell = 4;        // blank points at the start and end of every jump
};
PointFrame scanOptimise(const PointFrame& in, const ScanParams& p);

}  // namespace lasermap
