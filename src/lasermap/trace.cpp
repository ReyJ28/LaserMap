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

std::vector<std::vector<Vec2>> findContoursField(const std::vector<float>& lum, int W, int H, float threshold) {
  std::vector<std::vector<Vec2>> loops;
  if (W <= 0 || H <= 0 || lum.size() != size_t(W) * H) return loops;
  auto in = [&](int x, int y) -> int {
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;  // zero-padded border
    return lum[size_t(y) * W + x] >= threshold ? 1 : 0;
  };

  std::vector<Seg> segs;
  // Cell (i,j) has corners TL=(i,j) TR=(i+1,j) BR=(i+1,j+1) BL=(i,j+1).
  // Edge midpoints in doubled coords: T=(2i+1,2j) R=(2i+2,2j+1) B=(2i+1,2j+2) L=(2i,2j+1).
  for (int j = -1; j < H; ++j) {
    for (int i = -1; i < W; ++i) {
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

std::vector<std::vector<Vec2>> findContours(const Frame& f, float threshold) {
  if (!f.valid()) return {};
  return findContoursField(luminance(f), f.w, f.h, threshold);
}

namespace {
void boxBlur(std::vector<float>& v, int w, int h) {
  std::vector<float> o(v.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float s = 0; int n = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
          s += v[size_t(yy) * w + xx]; ++n;
        }
      o[size_t(y) * w + x] = s / n;
    }
  v.swap(o);
}

// rgb 0..1 -> hue 0..1, sat, val
void rgb2hsv(float r, float g, float b, float& h, float& s, float& v) {
  float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  v = mx; s = mx > 0 ? d / mx : 0; h = 0;
  if (d > 0) {
    if (mx == r) h = std::fmod((g - b) / d, 6.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
    if (h < 0) h += 1.f;
  }
}
}  // namespace

std::vector<float> buildField(const Frame& f, const TraceParams& p) {
  if (!f.valid()) return {};
  const int w = f.w, h = f.h;
  std::vector<float> lum = luminance(f);
  std::vector<float> field(lum.size());
  if (p.source == TraceParams::Alpha) {
    for (size_t i = 0; i < field.size(); ++i) field[i] = f.rgba[i * 4 + 3] / 255.f;
  } else if (p.source == TraceParams::Edges) {
    auto L = [&](int x, int y) { return lum[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        float gx = -L(x - 1, y - 1) - 2 * L(x - 1, y) - L(x - 1, y + 1) + L(x + 1, y - 1) + 2 * L(x + 1, y) + L(x + 1, y + 1);
        float gy = -L(x - 1, y - 1) - 2 * L(x, y - 1) - L(x + 1, y - 1) + L(x - 1, y + 1) + 2 * L(x, y + 1) + L(x + 1, y + 1);
        field[size_t(y) * w + x] = std::min(1.f, std::hypot(gx, gy) * 0.5f);
      }
  } else {
    field = lum;
  }
  int blurs = int(std::lround(std::clamp(p.smoothing, 0.f, 1.f) * 3.f));
  for (int i = 0; i < blurs; ++i) boxBlur(field, w, h);

  if (p.colourKey || p.keyWhite) {
    const float tol = std::clamp(p.keyTolerance, 0.f, 1.f) * 0.5f;
    for (size_t i = 0; i < field.size(); ++i) {
      float hh, ss, vv;
      rgb2hsv(f.rgba[i * 4] / 255.f, f.rgba[i * 4 + 1] / 255.f, f.rgba[i * 4 + 2] / 255.f, hh, ss, vv);
      bool match = false;
      if (p.colourKey && ss > 0.2f && vv > 0.1f) {
        float d = std::fabs(hh - p.keyHue);
        d = std::min(d, 1.f - d);
        match = d <= tol;
      }
      if (p.keyWhite && vv > 0.85f && ss < 0.2f) match = true;
      if (!match) field[i] = 0.f;
    }
  }
  return field;
}

std::vector<uint8_t> maskImage(const Frame& f, const TraceParams& p) {
  auto field = buildField(f, p);
  std::vector<uint8_t> m(field.size());
  for (size_t i = 0; i < field.size(); ++i) m[i] = field[i] >= p.threshold ? 255 : 0;
  return m;
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

std::vector<Shape> extractShapes(const Frame& f, const TraceParams& p) {
  std::vector<Shape> shapes;
  if (!f.valid()) return shapes;
  auto contours = findContoursField(buildField(f, p), f.w, f.h, p.threshold);
  contours.erase(std::remove_if(contours.begin(), contours.end(),
                                [&](const auto& c) { return perimeter(c) < p.minPerimeter; }),
                 contours.end());
  if (int(contours.size()) > std::max(p.maxShapes, 0)) {
    std::sort(contours.begin(), contours.end(),
              [](const auto& a, const auto& b) { return perimeter(a) > perimeter(b); });
    contours.resize(size_t(std::max(p.maxShapes, 0)));
  }

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
  auto convert = [&](Vec2 v) {
    Point pt;
    pt.x = (v.x + 0.5f - cx) * scale;
    pt.y = -(v.y + 0.5f - cy) * scale;
    int sx = std::clamp(int(std::lround(v.x)), 0, f.w - 1);
    int sy = std::clamp(int(std::lround(v.y)), 0, f.h - 1);
    // The contour lies between pixels; take the brightest neighbour as the colour.
    float br = -1;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        int x = std::clamp(sx + dx, 0, f.w - 1), y = std::clamp(sy + dy, 0, f.h - 1);
        const uint8_t* q = &f.rgba[(size_t(y) * f.w + x) * 4];
        float l = float(q[0]) + q[1] + q[2];
        if (l > br) { br = l; pt.r = q[0] / 255.f; pt.g = q[1] / 255.f; pt.b = q[2] / 255.f; }
      }
    pt.blank = false;
    return pt;
  };
  for (const auto& c : simp) {
    if (c.size() < 3) continue;
    Shape sh;
    sh.pts.reserve(c.size());
    for (const auto& v : c) sh.pts.push_back(convert(v));
    shapes.push_back(std::move(sh));
  }
  return shapes;
}

static std::vector<Point> spaced(const std::vector<Point>& pts, float step) {
  if (step <= 0) return pts;
  std::vector<Point> out;
  for (size_t i = 0; i < pts.size(); ++i) {
    const Point& a = pts[i];
    const Point& b = pts[(i + 1) % pts.size()];
    int k = std::max(1, int(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / step)));
    for (int j = 0; j < k; ++j) {
      float t = float(j) / k;
      Point o = a;
      o.x = a.x + (b.x - a.x) * t; o.y = a.y + (b.y - a.y) * t;
      o.r = a.r + (b.r - a.r) * t; o.g = a.g + (b.g - a.g) * t; o.b = a.b + (b.b - a.b) * t;
      out.push_back(o);
    }
  }
  return out;
}

PointFrame emitOutline(const std::vector<Shape>& shapes, float spacing) {
  PointFrame out;
  for (const auto& sh : shapes) {
    if (sh.pts.size() < 3) continue;
    std::vector<Point> pts = spaced(sh.pts, spacing);
    Point first = pts[0];
    first.blank = true;
    out.push_back(first);
    for (size_t i = 1; i < pts.size(); ++i) out.push_back(pts[i]);
    out.push_back(pts[0]);  // close the loop
  }
  return out;
}

PointFrame traceClip(const Frame& f, const TraceParams& p) { return emitOutline(extractShapes(f, p)); }

}  // namespace lasermap
