// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include <cmath>
#include <cstdio>
#include <limits>

#include "lasermap/modes.h"
#include "lasermap/safety.h"
#include "lasermap/trace.h"

using namespace lasermap;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static Frame blank(int w, int h) {
  Frame f; f.w = w; f.h = h; f.rgba.assign(size_t(w) * h * 4, 0);
  for (size_t i = 3; i < f.rgba.size(); i += 4) f.rgba[i] = 255;
  return f;
}
static void fillRect(Frame& f, int x0, int y0, int x1, int y1) {
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      uint8_t* p = &f.rgba[(size_t(y) * f.w + x) * 4];
      p[0] = p[1] = p[2] = 255;
    }
}

static void testContours() {
  Frame f = blank(64, 64);
  CHECK(findContours(f, 0.5f).empty());
  fillRect(f, 10, 10, 30, 30);
  CHECK(findContours(f, 0.5f).size() == 1);
  fillRect(f, 40, 40, 50, 50);
  CHECK(findContours(f, 0.5f).size() == 2);
  // Ring: outer + inner (hole) contour.
  Frame r = blank(64, 64);
  fillRect(r, 10, 10, 50, 50);
  for (int y = 20; y < 40; ++y) for (int x = 20; x < 40; ++x)
    r.rgba[(size_t(y) * 64 + x) * 4] = r.rgba[(size_t(y) * 64 + x) * 4 + 1] = r.rgba[(size_t(y) * 64 + x) * 4 + 2] = 0;
  CHECK(findContours(r, 0.5f).size() == 2);
  // Shape touching the image border must still close.
  Frame e = blank(32, 32);
  fillRect(e, 0, 0, 16, 16);
  CHECK(findContours(e, 0.5f).size() == 1);
}

static void testSimplify() {
  std::vector<Vec2> sq;
  for (int i = 0; i <= 10; ++i) sq.push_back({float(i), 0});
  for (int i = 1; i <= 10; ++i) sq.push_back({10, float(i)});
  for (int i = 9; i >= 0; --i) sq.push_back({float(i), 10});
  for (int i = 9; i >= 1; --i) sq.push_back({0, float(i)});
  auto s = simplifyClosed(sq, 0.1f);
  CHECK(s.size() == 4);
}

static void testTrace() {
  Frame f = blank(100, 50);
  fillRect(f, 10, 10, 40, 40);
  PointFrame p = traceClip(f);
  CHECK(!p.empty());
  CHECK(p.front().blank);       // jump to start is blanked
  CHECK(!p.back().blank);
  for (const auto& q : p) CHECK(std::fabs(q.x) <= 1.f && std::fabs(q.y) <= 1.f);
  TraceParams tight; tight.maxPoints = 3;
  CHECK(traceClip(f, tight).size() <= 3);
  CHECK(traceClip(Frame{}).empty());
}

static void testSafety() {
  Frame f = blank(100, 100);
  fillRect(f, 40, 40, 60, 60);
  PointFrame pts = traceClip(f);

  SafetyGuard off;  // default: disabled
  CHECK(off.process(pts, 0.033).empty());

  SafetyParams sp; sp.outputEnabled = true; sp.maxBrightness = 0.5f;
  SafetyGuard g(sp);
  auto out = g.process(pts, 0.033);
  CHECK(!out.empty());
  for (const auto& q : out) CHECK(q.r <= 0.5f + 1e-6f && q.g <= 0.5f + 1e-6f);

  // NaN input must never reach the output as a lit point.
  PointFrame bad = pts;
  bad[2].x = std::numeric_limits<float>::quiet_NaN();
  for (const auto& q : g.process(bad, 0.033)) CHECK(std::isfinite(q.x));

  // Parked tiny beam fades to black.
  PointFrame dot;
  for (int i = 0; i < 4; ++i) { Point q; q.x = 0.001f * i; q.y = 0; q.r = q.g = q.b = 1; q.blank = (i == 0); dot.push_back(q); }
  SafetyGuard g2(sp);
  sp.maxBrightness = 1.f; g2.setParams(sp);
  float lastGain = 1.f;
  for (int i = 0; i < 150; ++i) { g2.process(dot, 0.033); lastGain = g2.currentGain(); }
  CHECK(lastGain <= 0.f);
  // Moving content recovers immediately.
  PointFrame moved = dot;
  for (auto& q : moved) q.x += 0.5f;
  g2.process(moved, 0.033);
  CHECK(g2.currentGain() > 0.99f);
}

static bool sane(const PointFrame& pts, int maxPoints) {
  if (pts.size() > size_t(maxPoints)) return false;
  for (const auto& q : pts)
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || std::fabs(q.x) > 1.51f || std::fabs(q.y) > 1.51f) return false;
  return true;
}

static void setPix(Frame& f, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  uint8_t* p = &f.rgba[(size_t(y) * f.w + x) * 4];
  p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static void testExtract() {
  // Two shapes: red box (left) and green box (right).
  Frame f = blank(100, 50);
  for (int y = 10; y < 40; ++y) for (int x = 10; x < 40; ++x) setPix(f, x, y, 255, 0, 0);
  for (int y = 10; y < 40; ++y) for (int x = 60; x < 90; ++x) setPix(f, x, y, 0, 255, 0);
  TraceParams tp;
  tp.threshold = 0.15f;  // pure red has luma 0.21, so use a low iso-level for both boxes
  CHECK(extractShapes(f, tp).size() == 2);
  tp.maxShapes = 1;
  CHECK(extractShapes(f, tp).size() == 1);
  tp.maxShapes = 64;
  tp.minPerimeter = 1000;
  CHECK(extractShapes(f, tp).empty());
  tp.minPerimeter = 8;
  // Colour key: only the red box survives (hue 0), only green with hue 1/3.
  tp.colourKey = true; tp.keyHue = 0.f; tp.keyTolerance = 0.2f;
  auto red = extractShapes(f, tp);
  CHECK(red.size() == 1 && red[0].pts[0].x < 0);
  tp.keyHue = 1.f / 3.f;
  auto green = extractShapes(f, tp);
  CHECK(green.size() == 1 && green[0].pts[0].x > 0);
  tp.colourKey = false;
  // Key white: a white box is kept, coloured ones are not.
  Frame w2 = blank(100, 50);
  for (int y = 10; y < 40; ++y) for (int x = 10; x < 40; ++x) setPix(w2, x, y, 255, 255, 255);
  for (int y = 10; y < 40; ++y) for (int x = 60; x < 90; ++x) setPix(w2, x, y, 255, 0, 0);
  tp.keyWhite = true;
  CHECK(extractShapes(w2, tp).size() == 1);
  tp.keyWhite = false;
  // Alpha source: transparent black vs opaque black.
  Frame al = blank(60, 60);
  for (int y = 0; y < 60; ++y) for (int x = 0; x < 60; ++x) setPix(al, x, y, 0, 0, 0, (x > 15 && x < 45 && y > 15 && y < 45) ? 255 : 0);
  tp.source = TraceParams::Luma;
  CHECK(extractShapes(al, tp).empty());
  tp.source = TraceParams::Alpha;
  CHECK(extractShapes(al, tp).size() == 1);
  // Edges source finds outlines of a soft gradient box that a hard luma threshold would miss.
  Frame ed = blank(60, 60);
  for (int y = 20; y < 40; ++y) for (int x = 20; x < 40; ++x) setPix(ed, x, y, 120, 120, 120);
  tp.source = TraceParams::Edges; tp.threshold = 0.3f;
  CHECK(!extractShapes(ed, tp).empty());
  // Smoothing doesn't break anything and Show Mask returns a full-size binary image.
  tp.source = TraceParams::Luma; tp.smoothing = 1.f; tp.threshold = 0.5f;
  CHECK(!extractShapes(f, tp).empty());
  auto mk = maskImage(f, tp);
  CHECK(mk.size() == size_t(f.w) * f.h);
}

static void testModes() {
  Frame f = blank(100, 100);
  fillRect(f, 20, 20, 80, 80);
  auto shapes = extractShapes(f);
  CHECK(shapes.size() == 1);
  ModeParams mp;
  for (float t : {0.f, 0.7f, 3.1f, 100.3f}) {
    mp.time = t;
    // Beam Runners in every direction
    for (int dir = 0; dir < 3; ++dir) {
      mp.beamDirection = dir;
      auto br = renderBeamRunners(shapes, mp);
      CHECK(sane(br.points, mp.maxPoints));
      CHECK(!br.points.empty());
      CHECK(br.points.front().blank);
    }
    // Trace Bars in every direction
    for (int dir = 0; dir < 5; ++dir) {
      mp.barsDirection = dir;
      CHECK(sane(renderTraceBars(shapes, mp).points, mp.maxPoints));
    }
    // Sweep, erase/reveal, several angles
    for (bool reveal : {false, true})
      for (float ang : {0.f, 45.f, 90.f, 200.f}) {
        mp.sweepReveal = reveal; mp.sweepAngleDeg = ang; mp.sweepProgress = std::fmod(t * 0.37f, 1.f);
        auto sw = renderSweep(shapes, mp);
        CHECK(sane(sw.points, mp.maxPoints));
        CHECK(sw.mask.kind == VideoMask::HalfPlane);
        CHECK(sw.mask.keepGreater == !reveal);
      }
    auto sp = renderSpotlight(mp);
    CHECK(sane(sp.points, mp.maxPoints));
    CHECK(sp.mask.kind == VideoMask::Discs && !sp.mask.discs.empty() && sp.mask.discs.size() <= 16);
    CHECK(std::hypot(sp.mask.discs[0].x, sp.mask.discs[0].y) + sp.mask.discs[0].r <= 1.001f);
    CHECK(sane(renderGrid(mp).points, mp.maxPoints));
    auto rv = renderTraceReveal(shapes, mp);
    CHECK(sane(rv.points, mp.maxPoints));
    CHECK(rv.maskImage.size() == size_t(mp.maskW) * mp.maskH);
  }
  // Sync phase replaces time.
  ModeParams a, b; a.time = 0; b.time = 99; a.syncPhase = b.syncPhase = 0.3f;
  CHECK(renderBeamRunners(shapes, a).points.size() == renderBeamRunners(shapes, b).points.size());
  // Beam Runners: more beams -> more points; scatter 0 is deterministic.
  ModeParams m1, m2; m1.beamCount = 1; m2.beamCount = 8;
  CHECK(renderBeamRunners(shapes, m2).points.size() > renderBeamRunners(shapes, m1).points.size());
  // Trace Bars: narrower bars show less outline than wide ones.
  ModeParams narrow, wide; narrow.barWidth = 0.05f; wide.barWidth = 0.6f;
  CHECK(renderTraceBars(shapes, narrow).points.size() < renderTraceBars(shapes, wide).points.size());
  // Sweep erase: at progress 1 nothing of the clip remains visible, only the beam; at 0 the whole clip does.
  ModeParams se; se.sweepReveal = false; se.sweepProgress = 0.f;
  size_t full = renderSweep(shapes, se).points.size();
  se.sweepProgress = 1.f;
  size_t done = renderSweep(shapes, se).points.size();
  CHECK(full > done);
  // Reveal: nothing revealed at the start, everything by the end of the trace.
  ModeParams rv; rv.time = 0.f;
  auto r0 = renderTraceReveal(shapes, rv);
  size_t lit0 = 0; for (auto v : r0.maskImage) lit0 += v != 0;
  CHECK(lit0 == 0);
  rv.syncPhase = 0.999f;  // within the hold at the end of the cycle
  auto r1 = renderTraceReveal(shapes, rv);
  size_t lit1 = 0; for (auto v : r1.maskImage) lit1 += v != 0;
  CHECK(lit1 > size_t(rv.maskW) * rv.maskH / 10);
  // Limits & degenerate input
  mp.maxPoints = 5;
  CHECK(renderBeamRunners(shapes, mp).points.size() <= 5);
  CHECK(renderBeamRunners({}, ModeParams{}).points.empty());
  CHECK(renderTraceBars({}, ModeParams{}).points.empty());
  CHECK(renderTraceReveal({}, ModeParams{}).points.empty());
}

static void testCannonTrailScan() {
  Frame f = blank(100, 100);
  fillRect(f, 20, 20, 80, 80);
  auto shapes = extractShapes(f);
  Cannon c;
  CannonParams cp; cp.hitsToClear = 5; cp.fireRate = 10.f; cp.chaos = 0.f;
  ModeParams mp;
  bool sawFlood = false, sawDisc = false, sawReset = false;
  for (int i = 0; i < 600; ++i) {  // 6 s at 100 fps
    auto o = c.render(shapes, cp, 0.01, mp);
    CHECK(sane(o.points, mp.maxPoints));
    CHECK(o.mask.kind == VideoMask::Discs && o.mask.discs.size() <= 16);
    if (!o.mask.discs.empty()) sawDisc = true;
    if (o.mask.gain > 0.9f) sawFlood = true;
    if (sawFlood && o.mask.gain == 0.f) sawReset = true;
  }
  CHECK(sawDisc); CHECK(sawFlood); CHECK(sawReset);
  // Beat-synced cannon fires on phase wrap.
  Cannon cs; CannonParams sp; sp.syncPhase = 0.5f;
  cs.render(shapes, sp, 0.01, mp);
  sp.syncPhase = 0.9f; cs.render(shapes, sp, 0.01, mp);
  sp.syncPhase = 0.1f; auto wrapped = cs.render(shapes, sp, 0.01, mp);
  CHECK(!wrapped.mask.discs.empty());

  // Trail FX: none produces nothing; sparks spawn, fall under gravity, and die.
  TrailFX tf; TrailParams tp; tp.style = 0;
  std::vector<Vec2> em{{0.f, 0.f}};
  tf.update(0.05, em, tp);
  CHECK(tf.render(em, tp).empty());
  tp.style = 2; tp.density = 1.f; tp.fadeSeconds = 0.5f;
  for (int i = 0; i < 20; ++i) tf.update(0.02, em, tp);
  CHECK(tf.count() > 0);
  CHECK(!tf.render(em, tp).empty());
  for (int i = 0; i < 100; ++i) tf.update(0.02, {}, tp);
  CHECK(tf.count() == 0);
  tp.style = 1; CHECK(!tf.render(em, tp).empty());  // glow only

  // Scanner optimiser: dwell on corners and around jumps; blank_dwell 0 / corner_dwell 0 add nothing.
  Point a{-0.5f, -0.5f, 1, 1, 1, true}, b{0.5f, -0.5f, 1, 1, 1, false}, d{0.5f, 0.5f, 1, 1, 1, false},
      e{-0.5f, 0.5f, 1, 1, 1, false};
  PointFrame sq{a, b, d, e, a};
  ScanParams z; z.cornerDwell = 0; z.blankDwell = 0;
  CHECK(scanOptimise(sq, z).size() == sq.size());
  ScanParams s2; s2.cornerDwell = 3; s2.blankDwell = 2; s2.cornerAngle = 0.5f;
  auto opt = scanOptimise(sq, s2);
  CHECK(opt.size() > sq.size());
  size_t atCorner = 0;
  for (const auto& q : opt) if (q.x == 0.5f && q.y == -0.5f) ++atCorner;
  CHECK(atCorner == 4);  // the point itself + 3 dwell
}

int main() {
  testExtract();
  testModes();
  testCannonTrailScan();
  testContours();
  testSimplify();
  testTrace();
  testSafety();
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("all tests passed\n");
  return 0;
}
