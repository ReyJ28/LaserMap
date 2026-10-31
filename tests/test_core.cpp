// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include <cmath>
#include <cstdio>
#include <limits>

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

int main() {
  testContours();
  testSimplify();
  testTrace();
  testSafety();
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("all tests passed\n");
  return 0;
}
