// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "modes.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace lasermap {

namespace {

constexpr float kPi = 3.14159265358979f;

float frac(float x) { return x - std::floor(x); }

Point lerp(const Point& a, const Point& b, float t) {
  Point o;
  o.x = a.x + (b.x - a.x) * t;
  o.y = a.y + (b.y - a.y) * t;
  o.r = a.r + (b.r - a.r) * t;
  o.g = a.g + (b.g - a.g) * t;
  o.b = a.b + (b.b - a.b) * t;
  o.blank = false;
  return o;
}

float loopLength(const std::vector<Point>& pts) {
  float s = 0;
  for (size_t i = 0; i < pts.size(); ++i) {
    const Point& a = pts[i];
    const Point& b = pts[(i + 1) % pts.size()];
    s += std::hypot(b.x - a.x, b.y - a.y);
  }
  return s;
}

// Even spacing along a closed loop (implicit closing edge, no repeated end point).
std::vector<Point> resample(const std::vector<Point>& pts, float step) {
  std::vector<Point> out;
  for (size_t i = 0; i < pts.size(); ++i) {
    const Point& a = pts[i];
    const Point& b = pts[(i + 1) % pts.size()];
    float len = std::hypot(b.x - a.x, b.y - a.y);
    int k = std::max(1, int(std::ceil(len / step)));
    for (int j = 0; j < k; ++j) out.push_back(lerp(a, b, float(j) / k));
  }
  return out;
}

std::vector<std::vector<Point>> resampleAll(const std::vector<Shape>& shapes, int maxPoints) {
  float total = 0;
  for (const auto& s : shapes) if (s.pts.size() >= 3) total += loopLength(s.pts);
  float step = std::max(0.012f, total / (std::max(maxPoints, 50) * 0.6f));
  std::vector<std::vector<Point>> loops;
  for (const auto& s : shapes)
    if (s.pts.size() >= 3) loops.push_back(resample(s.pts, step));
  return loops;
}

// Emit only the parts of the loops where pred is true; every run starts with a blanked jump.
void emitMasked(const std::vector<std::vector<Point>>& loops, const std::function<bool(const Point&)>& pred,
                PointFrame& out) {
  for (const auto& loop : loops) {
    bool prev = false;
    for (const auto& pt : loop) {
      bool in = pred(pt);
      if (in) {
        if (!prev) { Point j = pt; j.blank = true; out.push_back(j); }
        Point q = pt; q.blank = false; out.push_back(q);
      }
      prev = in;
    }
  }
}

void truncate(PointFrame& pts, int maxPoints) {
  if (maxPoints >= 0 && pts.size() > size_t(maxPoints)) pts.resize(size_t(maxPoints));
}

Point white(float x, float y, bool blank) {
  Point p;
  p.x = x; p.y = y; p.r = p.g = p.b = 1.f; p.blank = blank;
  return p;
}

void addLine(PointFrame& out, float x0, float y0, float x1, float y1, int n) {
  out.push_back(white(x0, y0, true));
  for (int i = 1; i <= n; ++i) {
    float t = float(i) / n;
    out.push_back(white(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, false));
  }
}

}  // namespace

ModeOutput renderBeamRunners(const std::vector<Shape>& shapes, const ModeParams& p) {
  ModeOutput o;
  auto loops = resampleAll(shapes, p.maxPoints);
  const int beams = 1 + int(std::lround(std::clamp(p.count, 0.f, 1.f) * 5));
  const float tailFrac = 0.04f + std::clamp(p.size, 0.f, 1.f) * 0.3f;
  const float loopsPerSec = 0.1f + std::clamp(p.speed, 0.f, 1.f) * 1.5f;
  for (const auto& loop : loops) {
    const int n = int(loop.size());
    if (n < 4) continue;
    const int tail = std::clamp(int(n * tailFrac), 3, n - 1);
    for (int b = 0; b < beams; ++b) {
      int head = int(frac(p.time * loopsPerSec + float(b) / beams) * n) % n;
      for (int k = 0; k < tail; ++k) {
        int idx = ((head - tail + 1 + k) % n + n) % n;
        Point q = loop[idx];
        float g = float(k + 1) / tail;  // dark at the tail end, full at the head
        g *= g;
        q.r *= g; q.g *= g; q.b *= g;
        q.blank = (k == 0);
        o.points.push_back(q);
      }
    }
  }
  truncate(o.points, p.maxPoints);
  return o;
}

ModeOutput renderTraceBars(const std::vector<Shape>& shapes, const ModeParams& p) {
  ModeOutput o;
  auto loops = resampleAll(shapes, p.maxPoints);
  const float period = 0.1f + std::clamp(p.size, 0.f, 1.f) * 0.6f;
  const float scroll = p.time * (0.05f + std::clamp(p.speed, 0.f, 1.f) * 1.0f);
  emitMasked(loops, [&](const Point& q) { return frac((q.y + scroll) / period) < 0.4f; }, o.points);
  truncate(o.points, p.maxPoints);
  return o;
}

ModeOutput renderSweep(const std::vector<Shape>& shapes, const ModeParams& p) {
  ModeOutput o;
  const float phase = frac(p.time * (0.05f + std::clamp(p.speed, 0.f, 1.f) * 0.5f));
  const float edge = -1.25f + 2.5f * phase;
  const bool keepGreater = !p.invert;  // erase: what the beam has not reached yet stays
  auto loops = resampleAll(shapes, p.maxPoints);
  emitMasked(loops, [&](const Point& q) { return keepGreater ? q.x >= edge : q.x <= edge; }, o.points);
  if (std::fabs(edge) <= 1.f) addLine(o.points, edge, -1.f, edge, 1.f, 24);  // the sweeping beam itself
  truncate(o.points, p.maxPoints);
  o.mask.kind = VideoMask::HalfPlane;
  o.mask.nx = 1; o.mask.ny = 0;
  o.mask.edge = edge;
  o.mask.keepGreater = keepGreater;
  return o;
}

ModeOutput renderSpotlight(const ModeParams& p) {
  ModeOutput o;
  const float s = 0.2f + std::clamp(p.speed, 0.f, 1.f) * 1.5f;
  const float r = 0.1f + std::clamp(p.size, 0.f, 1.f) * 0.5f;
  const float cx = (1.f - r) * 0.9f * std::sin(p.time * s * 1.3f);
  const float cy = (1.f - r) * 0.9f * std::sin(p.time * s * 1.9f + 1.f);
  const int n = 64;
  o.points.push_back(white(cx + r, cy, true));
  for (int i = 1; i <= n; ++i) {
    float a = 2.f * kPi * i / n;
    o.points.push_back(white(cx + r * std::cos(a), cy + r * std::sin(a), false));
  }
  o.mask.kind = VideoMask::Circle;
  o.mask.cx = cx; o.mask.cy = cy; o.mask.radius = r;
  return o;
}

ModeOutput renderGrid(const ModeParams&) {
  ModeOutput o;
  for (int i = 0; i <= 4; ++i) {
    float v = -1.f + 0.5f * i;
    addLine(o.points, v, -1.f, v, 1.f, 8);
    addLine(o.points, -1.f, v, 1.f, v, 8);
  }
  return o;
}

}  // namespace lasermap
