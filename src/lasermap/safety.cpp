// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "safety.h"

#include <algorithm>
#include <cmath>

namespace lasermap {

namespace {
bool finite(const Point& p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.r) && std::isfinite(p.g) &&
         std::isfinite(p.b);
}
}  // namespace

PointFrame SafetyGuard::process(const PointFrame& in, double dt) {
  PointFrame out;
  if (!p_.outputEnabled || in.empty() || !(dt >= 0) || !std::isfinite(dt)) {
    parkedFor_ = 0;
    haveLast_ = false;
    gain_ = 1.f;
    return out;
  }

  // Lit-content statistics for the static (parked beam) guard.
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, sx = 0, sy = 0;
  size_t lit = 0;
  for (const auto& pt : in) {
    if (pt.blank || !finite(pt)) continue;
    minx = std::min(minx, pt.x); maxx = std::max(maxx, pt.x);
    miny = std::min(miny, pt.y); maxy = std::max(maxy, pt.y);
    sx += pt.x; sy += pt.y;
    ++lit;
  }
  if (lit == 0) {
    parkedFor_ = 0;
    haveLast_ = false;
    gain_ = 1.f;
    return out;
  }
  Vec2 c{sx / lit, sy / lit};
  float extent = std::max(maxx - minx, maxy - miny);
  bool small = extent < p_.dwellExtent;
  bool still = haveLast_ && std::hypot(c.x - lastCentroid_.x, c.y - lastCentroid_.y) < p_.dwellMove;
  if (small && still) parkedFor_ += dt;
  else parkedFor_ = 0;
  lastCentroid_ = c;
  haveLast_ = true;

  float fade = 1.f;
  if (parkedFor_ > p_.dwellSeconds)
    fade = 1.f - float(std::min(1.0, (parkedFor_ - p_.dwellSeconds) / std::max(1e-3f, p_.fadeSeconds)));
  gain_ = fade * std::clamp(p_.maxBrightness, 0.f, 1.f);

  size_t n = std::min<size_t>(in.size(), size_t(std::max(p_.maxPoints, 0)));
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    Point q = in[i];
    if (!finite(q)) { q = Point{}; q.blank = true; }
    q.x = std::clamp(q.x, -1.f, 1.f);
    q.y = std::clamp(q.y, -1.f, 1.f);
    q.r = std::clamp(q.r, 0.f, 1.f) * gain_;
    q.g = std::clamp(q.g, 0.f, 1.f) * gain_;
    q.b = std::clamp(q.b, 0.f, 1.f) * gain_;
    if (gain_ <= 0.f) q.blank = true;
    out.push_back(q);
  }
  return out;
}

}  // namespace lasermap
