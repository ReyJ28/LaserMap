// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "trace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <unordered_map>

namespace lasermap {

std::vector<float> luminance(const Frame& f) {
  std::vector<float> out(size_t(std::max(f.w, 0)) * std::max(f.h, 0), 0.f);
  if (!f.valid()) return out;
  for (size_t i = 0; i < out.size(); ++i) {
    const uint8_t* p = &f.rgba[i * 4];
    float l = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.f;
    out[i] = l * (p[3] / 255.f);
  }
  return out;
}

namespace {

using Key = long long;
inline Key makeKey(int x2, int y2) { return (Key(x2 + 2) << 32) | Key(uint32_t(y2 + 2)); }
inline Vec2 keyToVec(Key k) {
  int x2 = int(k >> 32) - 2, y2 = int(uint32_t(k & 0xffffffffu)) - 2;
  return {x2 * 0.5f, y2 * 0.5f};
}

struct Seg {
  Key a, b;
};

}  // namespace

std::vector<std::vector<Vec2>> findContours(const Frame& f, float threshold) {
  std::vector<std::vector<Vec2>> loops;
  if (!f.valid()) return loops;
  const auto lum = luminance(f);
  auto in = [&](int x, int y) -> int {
    if (x < 0 || y < 0 || x >= f.w || y >= f.h) return 0;  // zero-padded border
    return lum[size_t(y) * f.w + x] >= threshold ? 1 : 0;
  };

  std::vector<Seg> segs;
  // Cell (i,j) has corners TL=(i,j) TR=(i+1,j) BR=(i+1,j+1) BL=(i,j+1).
  // Edge midpoints in doubled coords: T=(2i+1,2j) R=(2i+2,2j+1) B=(2i+1,2j+2) L=(2i,2j+1).
  for (int j = -1; j < f.h; ++j) {
    for (int i = -1; i < f.w; ++i) {
      int tl = in(i, j), tr = in(i + 1, j), br = in(i + 1, j + 1), bl = in(i, j + 1);
      int c = tl * 8 + tr * 4 + br * 2 + bl;
      if (c == 0 || c == 15) continue;
      Key T = makeKey(2 * i + 1, 2 * j), R = makeKey(2 * i + 2, 2 * j + 1),
          B = makeKey(2 * i + 1, 2 * j + 2), L = makeKey(2 * i, 2 * j + 1);
      switch (c) {
        case 1: case 14: segs.push_back({L, B}); break;
        case 2: case 13: segs.push_back({B, R}); break;
        case 3: case 12: segs.push_back({L, R}); break;
        case 4: case 11: segs.push_back({T, R}); break;
        case 5: segs.push_back({T, R}); segs.push_back({L, B}); break;  // saddle, kept separate
        case 6: case 9: segs.push_back({T, B}); break;
        case 7: case 8: segs.push_back({L, T}); break;
        case 10: segs.push_back({L, T}); segs.push_back({B, R}); break;  // saddle
        default: break;
      }
    }
  }

  // Each midpoint is shared by exactly two segments -> closed loops.
  std::unordered_map<Key, std::array<int, 2>> adj;
  adj.reserve(segs.size() * 2);
  for (int s = 0; s < int(segs.size()); ++s) {
    for (Key k : {segs[s].a, segs[s].b}) {
      auto it = adj.find(k);
      if (it == adj.end()) adj.emplace(k, std::array<int, 2>{s, -1});
      else it->second[1] = s;
    }
  }
  std::vector<char> used(segs.size(), 0);
  for (int s0 = 0; s0 < int(segs.size()); ++s0) {
    if (used[s0]) continue;
    std::vector<Vec2> loop;
    int s = s0;
    Key cur = segs[s0].a;
    while (s >= 0 && !used[s]) {
      used[s] = 1;
      loop.push_back(keyToVec(cur));
      Key next = (segs[s].a == cur) ? segs[s].b : segs[s].a;
      const auto& n = adj[next];
      int ns = (n[0] == s) ? n[1] : n[0];
      cur = next;
      s = ns;
    }
    if (loop.size() >= 3) loops.push_back(std::move(loop));
  }
  return loops;
}

namespace {

float distToSeg(Vec2 p, Vec2 a, Vec2 b) {
  float dx = b.x - a.x, dy = b.y - a.y;
  float l2 = dx * dx + dy * dy;
  float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.f;
  t = std::clamp(t, 0.f, 1.f);
  float px = a.x + t * dx - p.x, py = a.y + t * dy - p.y;
  return std::sqrt(px * px + py * py);
}

void dp(const std::vector<Vec2>& pts, size_t lo, size_t hi, float eps, std::vector<char>& keep) {
  if (hi <= lo + 1) return;
  float best = -1;
  size_t idx = lo;
  for (size_t i = lo + 1; i < hi; ++i) {
    float d = distToSeg(pts[i], pts[lo], pts[hi]);
    if (d > best) { best = d; idx = i; }
  }
  if (best > eps) {
    keep[idx] = 1;
    dp(pts, lo, idx, eps, keep);
    dp(pts, idx, hi, eps, keep);
  }
}

float perimeter(const std::vector<Vec2>& p) {
  float s = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    const Vec2& a = p[i];
    const Vec2& b = p[(i + 1) % p.size()];
    s += std::hypot(b.x - a.x, b.y - a.y);
  }
  return s;
}

}  // namespace

std::vector<Vec2> simplifyClosed(const std::vector<Vec2>& poly, float eps) {
  if (poly.size() < 4 || eps <= 0) return poly;
  // Split at point 0 and the point farthest from it so both halves are well-conditioned.
  size_t far = 0;
  float best = -1;
  for (size_t i = 1; i < poly.size(); ++i) {
    float d = std::hypot(poly[i].x - poly[0].x, poly[i].y - poly[0].y);
    if (d > best) { best = d; far = i; }
  }
  std::vector<Vec2> ring = poly;
  ring.push_back(poly[0]);
  std::vector<char> keep(ring.size(), 0);
  keep[0] = keep[far] = keep[ring.size() - 1] = 1;
  dp(ring, 0, far, eps, keep);
  dp(ring, far, ring.size() - 1, eps, keep);
  std::vector<Vec2> out;
  for (size_t i = 0; i + 1 < ring.size(); ++i)
    if (keep[i]) out.push_back(ring[i]);
  return out;
}

PointFrame traceClip(const Frame& f, const TraceParams& p) {
  PointFrame out;
  if (!f.valid()) return out;
  auto contours = findContours(f, p.threshold);
  contours.erase(std::remove_if(contours.begin(), contours.end(),
                                [&](const auto& c) { return perimeter(c) < p.minPerimeter; }),
                 contours.end());

  auto build = [&](float eps) {
    std::vector<std::vector<Vec2>> s;
    size_t total = 0;
    for (const auto& c : contours) {
      s.push_back(simplifyClosed(c, eps));
      total += s.back().size() + 2;  // blank jump + closing point
    }
    return std::make_pair(std::move(s), total);
  };

  float eps = std::max(p.simplifyEps, 0.f);
  auto [simp, total] = build(eps);
  for (int iter = 0; iter < 8 && int(total) > p.maxPoints; ++iter) {
    eps = std::max(eps * 1.6f, 0.5f);
    std::tie(simp, total) = build(eps);
  }
  // Still over budget: drop the smallest contours first.
  while (int(total) > p.maxPoints && !simp.empty()) {
    size_t smallest = 0;
    for (size_t i = 1; i < simp.size(); ++i)
      if (perimeter(simp[i]) < perimeter(simp[smallest])) smallest = i;
    total -= simp[smallest].size() + 2;
    simp.erase(simp.begin() + smallest);
  }

  const float scale = 2.f / float(std::max(f.w, f.h));
  const float cx = f.w * 0.5f, cy = f.h * 0.5f;
  auto emit = [&](Vec2 v, bool blank) {
    Point pt;
    pt.x = (v.x + 0.5f - cx) * scale;
    pt.y = -(v.y + 0.5f - cy) * scale;
    int sx = std::clamp(int(std::lround(v.x)), 0, f.w - 1);
    int sy = std::clamp(int(std::lround(v.y)), 0, f.h - 1);
    // Sample inside the shape (contour lies between pixels); take brightest neighbour.
    float br = -1;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        int x = std::clamp(sx + dx, 0, f.w - 1), y = std::clamp(sy + dy, 0, f.h - 1);
        const uint8_t* q = &f.rgba[(size_t(y) * f.w + x) * 4];
        float l = float(q[0]) + q[1] + q[2];
        if (l > br) { br = l; pt.r = q[0] / 255.f; pt.g = q[1] / 255.f; pt.b = q[2] / 255.f; }
      }
    pt.blank = blank;
    out.push_back(pt);
  };
  for (const auto& c : simp) {
    if (c.size() < 3) continue;
    emit(c[0], true);
    for (size_t i = 1; i < c.size(); ++i) emit(c[i], false);
    emit(c[0], false);  // close the loop
  }
  return out;
}

}  // namespace lasermap
