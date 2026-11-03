// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "modes.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace lasermap {

namespace {

constexpr float kPi = 3.14159265358979f;

float frac(float x) { return x - std::floor(x); }
float hash01(int n) {
  unsigned x = unsigned(n) * 2654435761u;
  x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
  return float(x & 0xffffu) / 65535.f;
}

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

// Even spacing along a closed loop; every original vertex (corner) is kept.
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

std::vector<std::vector<Point>> resampleAll(const std::vector<Shape>& shapes, const ModeParams& p) {
  float total = 0;
  for (const auto& s : shapes) if (s.pts.size() >= 3) total += loopLength(s.pts);
  float step = std::max({0.008f, p.spacing, total / (std::max(p.maxPoints, 50) * 0.6f)});
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
  auto loops = resampleAll(shapes, p);
  const int beams = std::clamp(p.beamCount, 1, 32);
  const float tailFrac = std::clamp(p.tailLength, 0.01f, 0.9f);
  const float sc = std::clamp(p.scatter, 0.f, 1.f);
  int shapeIdx = 0;
  for (const auto& loop : loops) {
    const int n = int(loop.size());
    ++shapeIdx;
    if (n < 4) continue;
    const int tail = std::clamp(int(n * tailFrac), 3, n - 1);
    for (int b = 0; b < beams; ++b) {
      int id = shapeIdx * 131 + b;
      float dir = (p.beamDirection == 1 || (p.beamDirection == 2 && (b & 1))) ? -1.f : 1.f;
      float speed = p.beamSpeed * (1.f + sc * (hash01(id) - 0.5f) * 0.6f);
      float phase = float(b) / beams + sc * (hash01(id + 7) - 0.5f) / beams;
      float lapPhase = p.syncPhase >= 0 ? dir * p.syncPhase : dir * p.time * speed;
      int head = int(frac(lapPhase + phase) * n) % n;
      if (o.emitters.size() < 24) o.emitters.push_back({loop[head].x, loop[head].y});
      for (int k = 0; k < tail; ++k) {
        // The tail trails behind the head, whichever way the beam is running.
        int idx = dir > 0 ? head - tail + 1 + k : head + tail - 1 - k;
        idx = ((idx % n) + n) % n;
        Point q = loop[idx];
        float g = float(k + 1) / tail;
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
  auto loops = resampleAll(shapes, p);
  const float w = std::max(0.01f, p.barWidth) * 2.f;  // laser space spans 2 units
  const float period = w + std::max(0.f, p.barGap) * 2.f;
  const float scroll = p.syncPhase >= 0 ? p.syncPhase * period : p.time * p.barSpeed * 2.f;
  auto band = [&](float coord) { return frac(coord / period) * period < w; };
  auto pred = [&](const Point& q) {
    switch (p.barsDirection) {
      case 1: return band(q.x + scroll);   // right to left
      case 2: return band(q.y + scroll);   // top to bottom
      case 3: return band(q.y - scroll);   // bottom to top
      case 4: return band(q.x - scroll) || band(q.y + scroll);
      default: return band(q.x - scroll);  // left to right
    }
  };
  emitMasked(loops, pred, o.points);
  truncate(o.points, p.maxPoints);
  return o;
}

ModeOutput renderSweep(const std::vector<Shape>& shapes, const ModeParams& p) {
  ModeOutput o;
  const float a = p.sweepAngleDeg * kPi / 180.f;
  const float nx = std::cos(a), ny = std::sin(a);
  const float R = std::fabs(nx) + std::fabs(ny) + 0.25f;  // sweep distance covering the whole frame
  const float edge = -R + 2.f * R * std::clamp(p.sweepProgress, 0.f, 1.f);
  const bool keepGreater = !p.sweepReveal;  // erase: what the beam has not reached stays
  auto loops = resampleAll(shapes, p);
  emitMasked(loops, [&](const Point& q) {
    float d = q.x * nx + q.y * ny;
    return keepGreater ? d >= edge : d <= edge;
  }, o.points);
  // The sweeping beam (plus glow lines and fading echoes behind it): lines perpendicular to the
  // sweep direction, clipped to the frame.
  const float tx = -ny, ty = nx;
  auto beamLine = [&](float e, float gain) {
    bool prev = false;
    const int steps = 48;
    for (int i = 0; i <= steps; ++i) {
      float sp = -1.5f + 3.f * float(i) / steps;
      float x = e * nx + sp * tx, y = e * ny + sp * ty;
      bool in = std::fabs(x) <= 1.f && std::fabs(y) <= 1.f;
      if (in) {
        Point q = white(x, y, !prev);
        q.r = q.g = q.b = gain;
        o.points.push_back(q);
      }
      prev = in;
    }
  };
  beamLine(edge, 1.f);
  const float glow = std::clamp(p.barGlow, 0.f, 1.f);
  if (glow > 0.02f) {
    beamLine(edge - 0.025f, glow * 0.5f);
    beamLine(edge + 0.025f, glow * 0.5f);
  }
  const float trail = std::clamp(p.sweepTrail, 0.f, 1.f);
  if (trail > 0.02f)
    for (int k = 1; k <= 3; ++k) beamLine(edge - (p.sweepReveal ? -1.f : 1.f) * 0.f - k * trail * 0.12f * R, (1.f - k / 4.f) * 0.5f);
  for (const auto& loop : loops)
    for (const auto& q : loop)
      if (std::fabs(q.x * nx + q.y * ny - edge) < 0.03f && o.emitters.size() < 24) o.emitters.push_back({q.x, q.y});
  truncate(o.points, p.maxPoints);
  o.mask.kind = VideoMask::HalfPlane;
  o.mask.nx = nx; o.mask.ny = ny;
  o.mask.edge = edge;
  o.mask.keepGreater = keepGreater;
  return o;
}

ModeOutput renderSpotlight(const ModeParams& p) {
  ModeOutput o;
  const float r = std::clamp(p.spotRadius, 0.05f, 0.9f);
  const float rnd = std::clamp(p.spotRandom, 0.f, 1.f);
  const float travel = std::clamp(p.spotRange, 0.f, 1.f) * (1.f - r);
  const float speed = std::max(p.spotSpeed, 0.01f);
  const float w = 2.f * kPi * speed;
  const float now = p.syncPhase >= 0 ? p.syncPhase / speed : p.time;
  auto pos = [&](float t, float& x, float& y) {
    auto wander = [&](float ph) {
      float smooth = std::sin(t * w + ph);
      float wild = 0.5f * std::sin(t * w * 2.3f + ph + 1.7f) + 0.5f * std::sin(t * w * 3.7f + ph + 0.4f);
      return (1.f - rnd) * smooth + rnd * wild;
    };
    x = travel * wander(0.f);
    y = travel * wander(1.3f);
  };
  float cr = 1, cg = 1, cb = 1;
  if (p.spotHue >= 0.01f) hueToRgb(p.spotHue, cr, cg, cb);
  auto ring = [&](float cx, float cy, float rad, float gain, int n) {
    Point first{cx + rad, cy, cr * gain, cg * gain, cb * gain, true};
    o.points.push_back(first);
    for (int i = 1; i <= n; ++i) {
      float ang = 2.f * kPi * i / n;
      o.points.push_back(Point{cx + rad * std::cos(ang), cy + rad * std::sin(ang), cr * gain, cg * gain, cb * gain, false});
    }
  };
  float cx, cy;
  pos(now, cx, cy);
  ring(cx, cy, r, 1.f, 64);
  const float rim = std::clamp(p.spotRim, 0.f, 1.f);
  if (rim > 0.02f) ring(cx, cy, r * 1.12f, rim * 0.6f, 48);
  o.mask.kind = VideoMask::Discs;
  o.mask.discs.push_back({cx, cy, r, 1.f});
  const float after = std::clamp(p.spotAfterglow, 0.f, 1.f) * 4.f;  // seconds
  if (after > 0.05f) {
    for (int j = 1; j <= 5; ++j) {
      float ex, ey;
      pos(now - after * j / 5.f, ex, ey);
      float a = 1.f - j / 6.f;
      o.mask.discs.push_back({ex, ey, r, a * 0.6f});  // the video "cools" where the spot just left
      ring(ex, ey, r, a * 0.25f, 32);
    }
  }
  o.emitters.push_back({cx, cy});
  return o;
}

ModeOutput renderGrid(const ModeParams& p) {
  // Laser space spans the longer image side over [-1,1], so the frame is ex x ey.
  ModeOutput o;
  const float M = float(std::max({p.maskW, p.maskH, 1}));
  const float ex = float(std::max(p.maskW, 1)) / M, ey = float(std::max(p.maskH, 1)) / M;
  if (p.gridStyle == 1) {  // 4 x 4 cells: a border and three lines each way (cells are rectangles unless the frame is square)
    const float cx[5] = {-ex, ex, ex, -ex, -ex}, cy[5] = {-ey, -ey, ey, ey, -ey};
    o.points.push_back(white(cx[0], cy[0], true));
    for (int k = 1; k < 5; ++k)
      for (int i = 1; i <= 24; ++i) {
        float t = float(i) / 24;
        o.points.push_back(white(cx[k - 1] + (cx[k] - cx[k - 1]) * t, cy[k - 1] + (cy[k] - cy[k - 1]) * t, false));
      }
    for (int i = 1; i < 4; ++i) {
      const float f = -1.f + 0.5f * i;
      addLine(o.points, ex * f, -ey, ex * f, ey, 16);
      addLine(o.points, -ex, ey * f, ex, ey * f, 24);
    }
    return o;
  }
  // Square cells that fill the frame edge to edge. Pick the number of cells on the shorter side (near the preferred
  // count) so that the longer side holds a whole number of the same cells; 16:9 with 9 cells gives exactly 16 x 9.
  const bool landscape = p.maskW >= p.maskH;
  const float ratio = landscape ? ex / ey : ey / ex;  // long side / short side, >= 1
  const int pref = std::clamp(p.gridCells, 2, 24);
  int bestK = pref, bestM = std::max(1, int(std::lround(pref * ratio)));
  float bestErr = std::fabs(float(bestM) / bestK - ratio);
  for (int k = std::max(2, pref - 2); k <= pref + 2; ++k) {
    const int m = std::max(1, int(std::lround(k * ratio)));
    const float err = std::fabs(float(m) / k - ratio);
    if (err < bestErr - 1e-4f || (std::fabs(err - bestErr) <= 1e-4f && std::abs(k - pref) < std::abs(bestK - pref))) {
      bestK = k; bestM = m; bestErr = err;
    }
  }
  const int cols = landscape ? bestM : bestK, rows = landscape ? bestK : bestM;
  // Lines alternate direction so the beam does not jump back to the start of every line.
  for (int i = 0; i <= cols; ++i) {
    const float x = -ex + 2.f * ex * float(i) / cols;
    if (i % 2 == 0) addLine(o.points, x, -ey, x, ey, 16);
    else addLine(o.points, x, ey, x, -ey, 16);
  }
  for (int j = 0; j <= rows; ++j) {
    const float y = -ey + 2.f * ey * float(j) / rows;
    if (j % 2 == 0) addLine(o.points, -ex, y, ex, y, 24);
    else addLine(o.points, ex, y, -ex, y, 24);
  }
  const float t = 0.06f;  // a small crosshair marks the exact centre even when the grid has no centre line
  addLine(o.points, -t, 0.f, t, 0.f, 4);
  addLine(o.points, 0.f, -t, 0.f, t, 4);
  return o;
}

PointFrame scanOptimise(const PointFrame& in, const ScanParams& sp) {
  PointFrame out;
  out.reserve(in.size() * 2);
  auto lit = [&](size_t i) { return i < in.size() && !in[i].blank; };
  for (size_t i = 0; i < in.size(); ++i) {
    const Point& q = in[i];
    if (q.blank) {
      if (!out.empty()) {
        Point from = out.back();
        from.blank = true;
        for (int k = 0; k < sp.blankDwell; ++k) out.push_back(from);  // park at the origin before the jump
      }
      out.push_back(q);
      for (int k = 0; k < sp.blankDwell; ++k) out.push_back(q);       // settle at the destination
      continue;
    }
    out.push_back(q);
    if (i > 0 && lit(i + 1)) {  // segment (i-1 -> i) is lit because point i is not blank
      const Point& a = in[i - 1];
      const Point& c = in[i + 1];
      float ux = q.x - a.x, uy = q.y - a.y, vx = c.x - q.x, vy = c.y - q.y;
      float lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
      if (lu > 1e-6f && lv > 1e-6f) {
        float cs = std::clamp((ux * vx + uy * vy) / (lu * lv), -1.f, 1.f);
        if (std::acos(cs) > sp.cornerAngle)
          for (int k = 0; k < sp.cornerDwell; ++k) out.push_back(q);
      }
    }
  }
  return out;
}


namespace {
// Even-odd fill of a shape into an 8-bit buffer (toggles pixels, so holes work when all loops are filled).
void fillShape(const Shape& sh, int w, int h, std::vector<uint8_t>& buf) {
  const float m = float(std::max(w, h));
  const size_t n = sh.pts.size();
  if (n < 3) return;
  std::vector<float> xs;
  for (int y = 0; y < h; ++y) {
    const float ly = -((float(y) + 0.5f) - h * 0.5f) * 2.f / m;  // laser-space y of this pixel row
    xs.clear();
    for (size_t i = 0; i < n; ++i) {
      const Point& a = sh.pts[i];
      const Point& b = sh.pts[(i + 1) % n];
      if ((a.y <= ly) == (b.y <= ly)) continue;
      float t = (ly - a.y) / (b.y - a.y);
      xs.push_back(a.x + t * (b.x - a.x));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t k = 0; k + 1 < xs.size(); k += 2) {
      int x0 = std::max(0, int(std::ceil(xs[k] * m * 0.5f + w * 0.5f - 0.5f)));
      int x1 = std::min(w - 1, int(std::floor(xs[k + 1] * m * 0.5f + w * 0.5f - 0.5f)));
      for (int x = x0; x <= x1; ++x) buf[size_t(y) * w + x] ^= 255;
    }
  }
}
}  // namespace

ModeOutput renderTraceReveal(const std::vector<Shape>& shapes, const ModeParams& p) {
  ModeOutput o;
  o.mask.kind = VideoMask::Texture;
  o.maskImage.assign(size_t(p.maskW) * p.maskH, 0);
  std::vector<const Shape*> order;
  for (const auto& s : shapes) if (s.pts.size() >= 3) order.push_back(&s);
  if (order.empty()) return o;
  auto key = [&](const Shape* s) {
    float cx = 0, cy = 0;
    for (const auto& q : s->pts) { cx += q.x; cy += q.y; }
    cx /= s->pts.size(); cy /= s->pts.size();
    switch (p.revealDir) { case 1: return -cx; case 2: return -cy; case 3: return cy; default: return cx; }
  };
  std::sort(order.begin(), order.end(), [&](const Shape* a, const Shape* b) { return key(a) < key(b); });

  const float unitsPerSec = std::max(0.05f, p.revealSpeed * 2.f);
  std::vector<float> dur;
  float total = 0;
  for (auto* s : order) { dur.push_back(loopLength(s->pts) / unitsPerSec); total += dur.back(); }
  const float hold = 1.0f;
  const float cycle = total + hold;
  const float tc = p.syncPhase >= 0 ? p.syncPhase * cycle : std::fmod(p.time, cycle);

  float acc = 0;
  size_t current = order.size();
  float local = 0;
  for (size_t i = 0; i < order.size(); ++i) {
    if (tc < acc + dur[i]) { current = i; local = (tc - acc) / dur[i]; break; }
    acc += dur[i];
  }
  const float step = std::max({0.008f, p.spacing, total * unitsPerSec / (std::max(p.maxPoints, 50) * 0.6f)});
  for (size_t i = 0; i < order.size(); ++i) {
    if (i < current) {  // closed: revealed and outlined
      fillShape(*order[i], p.maskW, p.maskH, o.maskImage);
      auto loop = resample(order[i]->pts, step);
      for (size_t k = 0; k <= loop.size(); ++k) {
        Point q = loop[k % loop.size()];
        q.blank = (k == 0);
        o.points.push_back(q);
      }
    } else if (i == current) {  // being traced: a comet running around the outline
      auto loop = resample(order[i]->pts, step);
      const int n = int(loop.size());
      const int head = std::clamp(int(local * n), 0, n - 1);
      const int tail = std::clamp(int(std::max(0.01f, p.fadeTail) * 2.f / step), 3, n);
      for (int k = 0; k < tail && head - k >= 0; ++k) {
        int idx = head - (tail - 1 - k);
        if (idx < 0) continue;
        Point q = loop[idx];
        float g = float(k + 1) / tail;
        q.r *= g; q.g *= g; q.b *= g;
        q.blank = (k == 0) || idx == 0;
        o.points.push_back(q);
      }
      o.emitters.push_back({loop[head].x, loop[head].y});
    }
  }
  truncate(o.points, p.maxPoints);
  return o;
}

float Cannon::rand01() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float((rng_ >> 8) & 0xffffu) / 65535.f;
}

void Cannon::reset() {
  hits_.clear();
  hitCount_ = 0;
  floodT_ = -1;
  untilFire_ = 0.3;
}

ModeOutput Cannon::render(const std::vector<Shape>& shapes, const CannonParams& p, double dt, const ModeParams& mp) {
  ModeOutput o;
  o.mask.kind = VideoMask::Discs;
  const int need = std::max(1, p.hitsToClear);

  // Fire: on the clock, or on each wrap of the host-beat phase.
  bool fire = false;
  if (floodT_ < 0) {
    if (p.syncPhase >= 0) {
      fire = lastSync_ >= 0 && p.syncPhase < lastSync_;
    } else {
      untilFire_ -= dt;
      fire = untilFire_ <= 0;
    }
  }
  lastSync_ = p.syncPhase;
  if (fire) {
    std::vector<const Shape*> ok;
    for (const auto& s : shapes) if (s.pts.size() >= 3) ok.push_back(&s);
    if (!ok.empty()) {
      const Shape& s = *ok[size_t(rand01() * ok.size()) % ok.size()];
      const Point& q = s.pts[size_t(rand01() * s.pts.size()) % s.pts.size()];
      hits_.push_back({q.x, q.y, 0.f, q});
      if (hits_.size() > 16) hits_.erase(hits_.begin());
      ++hitCount_;
      if (hitCount_ >= need) floodT_ = 0;
    }
    float jitter = 1.f + std::clamp(p.chaos, 0.f, 1.f) * (rand01() - 0.5f) * 1.8f;
    untilFire_ = std::max(0.03, double(jitter) / std::max(0.2f, p.fireRate));
  }
  for (auto& h : hits_) h.age += float(dt);

  float gain = 0;
  if (floodT_ >= 0) {
    floodT_ += dt;
    gain = float(std::min(1.0, floodT_ / 1.2));  // the rest floods away
    if (floodT_ > 2.5) reset();
  }
  o.mask.gain = gain;

  // Cleared areas grow as the clip gets closer to being cleared.
  const float progress = std::min(1.f, float(hitCount_) / need);
  const float discR = 0.15f + 0.45f * progress;
  for (const auto& h : hits_) o.mask.discs.push_back({h.x, h.y, discR, 1.f});

  // Laser: the outline, but only where it has been cleared.
  auto loops = resampleAll(shapes, mp);
  emitMasked(loops, [&](const Point& q) {
    if (gain > 0.5f) return true;
    for (const auto& d : o.mask.discs)
      if (std::hypot(q.x - d.x, q.y - d.y) < d.r) return true;
    return false;
  }, o.points);

  // Bolts (a fast line from the cannon at the bottom edge) and expanding ripple rings.
  for (const auto& h : hits_) {
    if (h.age < 0.12f) {
      o.points.push_back(Point{0.f, -1.f, 1, 1, 1, true});
      for (int i = 1; i <= 10; ++i) {
        float t = float(i) / 10;
        o.points.push_back(Point{h.x * t, -1.f + (h.y + 1.f) * t, 1, 1, 1, false});
      }
    }
    const float life = 0.9f;
    if (h.age < life && p.ripple > 0.02f) {
      float rr = 0.03f + h.age * (0.2f + 0.6f * p.ripple);
      float g = (1.f - h.age / life) * std::clamp(p.ripple, 0.f, 1.f);
      float cr = h.colour.r, cg = h.colour.g, cb = h.colour.b;
      if (!p.matchLaser) hueToRgb(p.rippleHue, cr, cg, cb);
      const int n = 28;
      o.points.push_back(Point{h.x + rr, h.y, cr * g, cg * g, cb * g, true});
      for (int i = 1; i <= n; ++i) {
        float ang = 2.f * kPi * i / n;
        o.points.push_back(Point{h.x + rr * std::cos(ang), h.y + rr * std::sin(ang), cr * g, cg * g, cb * g, false});
      }
    }
    if (h.age < 0.3f) o.emitters.push_back({h.x, h.y});
  }
  truncate(o.points, mp.maxPoints);
  return o;
}

float TrailFX::rand01() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return float((rng_ >> 8) & 0xffffu) / 65535.f;
}

void TrailFX::update(double dt, const std::vector<Vec2>& emitters, const TrailParams& p) {
  const float d = float(dt);
  for (auto& q : parts_) {
    q.vy -= p.gravity * 1.5f * d;
    q.x += q.vx * d; q.y += q.vy * d;
    q.life -= d;
  }
  parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const P& q) { return q.life <= 0; }), parts_.end());
  if ((p.style & 2) == 0) { parts_.clear(); return; }
  const float rate = std::clamp(p.density, 0.f, 1.f) * 40.f * d;  // sparks per emitter per second
  for (size_t e = 0; e < emitters.size() && e < 24; ++e) {
    float n = rate;
    while (n > 0 && parts_.size() < 160) {
      if (rand01() < std::min(1.f, n)) {
        float ang = rand01() * 2.f * kPi, sp = 0.1f + rand01() * 0.5f;
        float life = std::max(0.05f, p.fadeSeconds) * (0.5f + 0.5f * rand01());
        parts_.push_back({emitters[e].x, emitters[e].y, std::cos(ang) * sp, std::sin(ang) * sp, life, life});
      }
      n -= 1.f;
    }
  }
}

PointFrame TrailFX::render(const std::vector<Vec2>& emitters, const TrailParams& p) const {
  PointFrame out;
  float r, g, b;
  hueToRgb(p.hue, r, g, b);
  if (p.style & 2) {
    const float len = 0.005f + p.size * 0.03f;
    for (const auto& q : parts_) {
      float f = std::clamp(q.life / q.maxLife, 0.f, 1.f);
      float sp = std::hypot(q.vx, q.vy) + 1e-4f;
      float dx = q.vx / sp * len, dy = q.vy / sp * len;
      out.push_back(Point{q.x, q.y, r * f, g * f, b * f, true});
      out.push_back(Point{q.x + dx, q.y + dy, r * f, g * f, b * f, false});
    }
  }
  if ((p.style & 1) && p.amount > 0.02f) {
    const float rad = 0.015f + p.amount * 0.04f;
    for (size_t e = 0; e < emitters.size() && e < 24; ++e) {
      const int n = 10;
      float a = std::clamp(p.amount, 0.f, 1.f);
      out.push_back(Point{emitters[e].x + rad, emitters[e].y, a, a, a, true});
      for (int i = 1; i <= n; ++i) {
        float ang = 2.f * kPi * i / n;
        out.push_back(Point{emitters[e].x + rad * std::cos(ang), emitters[e].y + rad * std::sin(ang), a, a, a, false});
      }
    }
  }
  return out;
}

}  // namespace lasermap
