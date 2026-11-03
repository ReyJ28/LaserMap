// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include <cmath>
#include <cstdio>
#include <limits>

#include <fstream>

#include "lasermap/ilda.h"
#include <atomic>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>

#include "lasermap/beyond_sink.h"
#include "lasermap/modes.h"
#include "lasermap/net.h"
#include "lasermap/wire.h"
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

static void testGridFitsFrame() {
  ModeParams wide; wide.maskW = 160; wide.maskH = 90;  // 16:9
  auto g = renderGrid(wide).points;
  float minx = 9, maxx = -9, miny = 9, maxy = -9;
  for (const auto& q : g) { minx = std::min(minx, q.x); maxx = std::max(maxx, q.x); miny = std::min(miny, q.y); maxy = std::max(maxy, q.y); }
  CHECK(std::fabs(minx + 1.f) < 1e-4f && std::fabs(maxx - 1.f) < 1e-4f);        // fills the width
  CHECK(std::fabs(maxy - 0.5625f) < 1e-3f && std::fabs(miny + 0.5625f) < 1e-3f);  // and exactly the height: no lines off-frame
  ModeParams tall; tall.maskW = 90; tall.maskH = 160;  // portrait fits too
  auto t = renderGrid(tall).points;
  float tx = 0, ty = 0;
  for (const auto& q : t) { tx = std::max(tx, std::fabs(q.x)); ty = std::max(ty, std::fabs(q.y)); }
  CHECK(std::fabs(ty - 1.f) < 1e-4f && std::fabs(tx - 0.5625f) < 1e-3f);
  CHECK(!g.empty() && g.front().blank);
  // border + 3 vertical + 3 horizontal interior lines + the crosshair among them
  int blanks = 0; for (const auto& q : g) blanks += q.blank;
  CHECK(blanks == 1 + 6);
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

static int be16(const std::vector<uint8_t>& b, size_t o) { return int(int16_t((b[o] << 8) | b[o + 1])); }
static int beU16(const std::vector<uint8_t>& b, size_t o) { return (b[o] << 8) | b[o + 1]; }

static void testIlda() {
  PointFrame f1{{-1.f, 1.f, 1.f, 0.5f, 0.f, true}, {0.f, 0.f, 0.f, 1.f, 0.f, false}, {1.f, -1.f, 0.f, 0.f, 1.f, false}};
  PointFrame f2{{0.5f, 0.5f, 1, 1, 1, false}, {0.25f, -0.25f, 1, 1, 1, false}};
  auto b = encodeIlda({f1, PointFrame{}, f2}, "Test");  // the empty frame is skipped
  // 2 frames: (32 + 3*8) + (32 + 2*8), plus a 32-byte terminator
  CHECK(b.size() == (32 + 24) + (32 + 16) + 32);
  CHECK(b[0] == 'I' && b[1] == 'L' && b[2] == 'D' && b[3] == 'A');
  CHECK(b[4] == 0 && b[5] == 0 && b[6] == 0);
  CHECK(b[7] == 5);                                    // format 5
  CHECK(b[8] == 'T' && b[9] == 'e' && b[12] == 0);     // name, zero padded to 8
  CHECK(beU16(b, 24) == 3);                            // records in frame 0
  CHECK(beU16(b, 26) == 0 && beU16(b, 28) == 2);       // frame number, total frames
  // record 0: blanked, top-left, orange (R=255 G=127/128 B=0), stored as B,G,R
  CHECK(be16(b, 32) == -32767 && be16(b, 34) == 32767);
  CHECK(b[36] == 0x40);
  CHECK(b[37] == 0 && (b[38] == 127 || b[38] == 128) && b[39] == 255);
  // record 1 centre, lit, green
  CHECK(be16(b, 40) == 0 && be16(b, 42) == 0 && b[44] == 0x00 && b[45] == 0 && b[46] == 255 && b[47] == 0);
  // record 2 is the last point of the frame
  CHECK(b[52] == 0x80);
  // second header
  const size_t h2 = 32 + 24;
  CHECK(b[h2] == 'I' && beU16(b, h2 + 24) == 2 && beU16(b, h2 + 26) == 1 && beU16(b, h2 + 28) == 2);
  CHECK(b[h2 + 32 + 8 + 4] == 0x80);                   // last point of frame 1
  // terminator: header with zero records
  const size_t t = b.size() - 32;
  CHECK(b[t] == 'I' && beU16(b, t + 24) == 0);
  // Out-of-range and NaN coordinates are clamped / zeroed.
  PointFrame bad{{5.f, -5.f, 2.f, -1.f, 0.f, false}, {std::numeric_limits<float>::quiet_NaN(), 0.f, 0, 0, 0, false}};
  auto bb = encodeIlda({bad});
  CHECK(be16(bb, 32) == 32767 && be16(bb, 34) == -32767 && bb[39] == 255 && bb[38] == 0 /*R=2 -> 255 at idx 39*/);
  CHECK(be16(bb, 40) == 0);
  // Over-long frames are truncated to 65535 points.
  PointFrame big(70000);
  auto bg = encodeIlda({big});
  CHECK(bg.size() == size_t(32 + 65535 * 8 + 32));
  CHECK(beU16(bg, 24) == 65535);
  // Only empty frames: just a terminator.
  CHECK(encodeIlda({PointFrame{}, PointFrame{}}).size() == 32);
  // File round trip.
  const std::string path = "lasermap_test.ild";
  CHECK(writeIldaFile(path, {f1, f2}));
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  CHECK(in && size_t(in.tellg()) == (32 + 24) + (32 + 16) + 32);
  in.close();
  std::remove(path.c_str());
  CHECK(!writeIldaFile("/nonexistent_dir_lasermap/x.ild", {f1}));
}

static void testWireAndBridge() {
  WireFrame a;
  a.seq = 7; a.zone = 3; a.kpps = 12;
  a.pts = {{-1.f, 1.f, 1.f, 0.5f, 0.f, true}, {0.f, 0.f, 0.f, 1.f, 0.f, false}, {0.25f, -0.5f, 0.f, 0.f, 1.f, false}};
  auto bytes = encodeWire(a);
  CHECK(bytes.size() == kWireHeaderSize + 3 * kWirePointSize);
  CHECK(bytes[0] == 'L' && bytes[3] == 'R' && bytes[4] == 1 && bytes[5] == 3);

  // Decode in awkward chunks, with garbage in front and a second frame behind.
  WireFrame b; b.seq = 8; b.zone = 3;
  auto bytes2 = encodeWire(b);  // empty keep-alive frame
  std::vector<uint8_t> stream = {1, 2, 3, 'L', 'M', 9};
  stream.insert(stream.end(), bytes.begin(), bytes.end());
  stream.insert(stream.end(), bytes2.begin(), bytes2.end());
  WireDecoder dec;
  std::vector<WireFrame> got;
  for (size_t i = 0; i < stream.size(); i += 5) {
    dec.feed(&stream[i], std::min<size_t>(5, stream.size() - i));
    WireFrame f;
    while (dec.next(f)) got.push_back(f);
  }
  CHECK(got.size() == 2);
  CHECK(got.size() == 2 && got[0].seq == 7 && got[0].zone == 3 && got[0].kpps == 12 && got[0].pts.size() == 3);
  CHECK(got.size() == 2 && std::fabs(got[0].pts[0].x + 1.f) < 1e-3f && got[0].pts[0].blank && !got[0].pts[1].blank);
  CHECK(got.size() == 2 && std::fabs(got[0].pts[2].y + 0.5f) < 1e-3f && got[0].pts[2].b > 0.99f);
  CHECK(got.size() == 2 && got[1].seq == 8 && got[1].pts.empty());
  CHECK(dec.droppedBytes() == 6);
  // Absurd point counts are rejected, not allocated.
  std::vector<uint8_t> bad = {'L', 'M', 'B', 'R', 1, 1, 30, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff};
  WireDecoder d2; d2.feed(bad.data(), bad.size());
  WireFrame none; CHECK(!d2.next(none));

  // Loopback: plugin-side client -> bridge-side server on this machine.
  TcpServer server;
  std::mutex m;
  std::vector<WireFrame> recv;
  CHECK(server.start(0, [&](const WireFrame& f) { std::lock_guard<std::mutex> lk(m); recv.push_back(f); }));
  {
    TcpClientOutput client("127.0.0.1", server.port());
    client.setZone(5);
    client.setKpps(20);
    PointFrame frame = a.pts;
    for (int i = 0; i < 80; ++i) {
      client.send(frame);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      std::lock_guard<std::mutex> lk(m);
      size_t withPts = 0;
      for (const auto& f : recv) if (!f.pts.empty()) ++withPts;
      if (withPts >= 3) break;
    }
    CHECK(client.connected());
    CHECK(client.framesSent() > 0);
  }
  server.stop();
  std::lock_guard<std::mutex> lk(m);
  size_t withPts = 0;
  bool allOk = true;
  for (const auto& f : recv)
    if (!f.pts.empty()) { ++withPts; allOk = allOk && f.zone == 5 && f.kpps == 20 && f.pts.size() == 3; }
  CHECK(withPts >= 3);
  CHECK(allOk);
  // A client with nothing to talk to never blocks or crashes.
  TcpClientOutput orphan("127.0.0.1", 1);
  orphan.send(a.pts);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(!orphan.connected());
}

// ---- BEYOND SDK mock ----
struct MockSdk {
  std::vector<std::string> calls;
  std::string lastName;
  int lastCount = 0, lastRate = 0;
  std::vector<SdkPoint> lastPts;
  uint8_t lastZones[256] = {};
  bool ready = true;
  int sends = 0;
};
static MockSdk g_sdk;
static int mockCreate() { g_sdk.calls.push_back("create"); return 1; }
static int mockDestroy() { g_sdk.calls.push_back("destroy"); return 1; }
static int mockReady() { return g_sdk.ready ? 1 : 0; }
static int mockCreateImage(int z, const char* n) { g_sdk.calls.push_back("createimage:" + std::to_string(z) + ":" + n); return 1; }
static int mockDeleteImage(const char* n) { g_sdk.calls.push_back(std::string("deleteimage:") + n); return 1; }
static int mockSend(const char* n, int count, void* pts, void* zones, int rate) {
  g_sdk.lastName = n; g_sdk.lastCount = count; g_sdk.lastRate = rate; ++g_sdk.sends;
  g_sdk.lastPts.assign(static_cast<SdkPoint*>(pts), static_cast<SdkPoint*>(pts) + count);
  std::memcpy(g_sdk.lastZones, zones, 256);
  return 1;
}
static SdkApi mockApi() {
  SdkApi a;
  a.create = mockCreate; a.destroy = mockDestroy; a.beyondExeReady = mockReady; a.createZoneImage = mockCreateImage;
  a.deleteZoneImage = mockDeleteImage; a.sendFrameToImage = mockSend;
  return a;
}

static void testBeyondSink() {
  g_sdk = MockSdk();
  WireFrame f;
  f.zone = 3; f.kpps = 25;
  f.pts = {{-1.f, 1.f, 1.f, 0.f, 0.f, true}, {0.5f, -0.5f, 1.f, 0.5f, 0.f, false}, {2.f, -2.f, 0.f, 0.f, 1.f, false}};
  {
    BeyondSink::Options o;
    BeyondSink sink(mockApi(), o);
    CHECK(sink.connect() && sink.ready());
    sink.onFrame(f);
    CHECK(g_sdk.calls.size() == 2 && g_sdk.calls[0] == "create" && g_sdk.calls[1] == "createimage:2:LaserMap.Z3");
    CHECK(g_sdk.lastName == "LaserMap.Z3" && g_sdk.lastCount == 3 && g_sdk.lastRate == -25000);
    CHECK(g_sdk.lastZones[0] == 3 && g_sdk.lastZones[1] == 0);  // 1-based zone list, zero terminated
    CHECK(g_sdk.lastPts.size() == 3);
    CHECK(g_sdk.lastPts[0].color == 0 && g_sdk.lastPts[0].x == -32000.f && g_sdk.lastPts[0].y == 32000.f);  // blanked -> black
    CHECK(g_sdk.lastPts[1].color == (255 | (128 << 8)));  // Windows COLORREF 0x00BBGGRR: R low, G next
    CHECK(g_sdk.lastPts[2].color == (255 << 16));
    CHECK(g_sdk.lastPts[2].x == 32000.f && g_sdk.lastPts[2].y == -32000.f);  // out-of-range input is clamped
    CHECK(sizeof(SdkPoint) == 20 && g_sdk.lastPts[0].z == 0.f && g_sdk.lastPts[0].status == 0);
    sink.onFrame(f);
    int creates = 0; for (const auto& c : g_sdk.calls) if (c.rfind("createimage", 0) == 0) ++creates;
    CHECK(creates == 1);  // the zone image is created once

    // Empty frame -> a single black point, never a zero-length frame.
    WireFrame e; e.zone = 3; e.kpps = 25;
    sink.onFrame(e);
    CHECK(g_sdk.lastCount == 1 && g_sdk.lastPts[0].color == 0);

    // Over-long frames are truncated to the SDK's 8192 point limit.
    WireFrame big; big.zone = 3; big.pts.assign(10000, Point{0.f, 0.f, 1.f, 1.f, 1.f, false});
    sink.onFrame(big);
    CHECK(g_sdk.lastCount == 8192);

    // Flips.
    std::vector<SdkPoint> out;
    BeyondSink::Options fl; fl.flipX = true; fl.flipY = true;
    BeyondSink::convert(f, fl, out);
    CHECK(out[0].x == 32000.f && out[0].y == -32000.f);

    // Watchdog: a lit frame followed by silence must be followed by a blank frame.
    BeyondSink::Options quick; quick.watchdogSec = 0.05;
    g_sdk = MockSdk();
    BeyondSink w(mockApi(), quick);
    w.onFrame(f);
    const int before = g_sdk.sends;
    w.idle();
    CHECK(g_sdk.sends == before);  // not yet
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    w.idle();
    CHECK(g_sdk.sends == before + 1 && g_sdk.lastCount == 1 && g_sdk.lastPts[0].color == 0);
    w.idle();
    CHECK(g_sdk.sends == before + 1);  // only once
  }
  // Destroying the sink blanks, deletes its images and releases the SDK; it never arms or blacks out.
  bool tidy = false, leaked = false;
  for (const auto& c : g_sdk.calls) {
    if (c == "deleteimage:LaserMap.Z3") tidy = true;
    if (c.find("enable") != std::string::npos || c.find("blackout") != std::string::npos) leaked = true;
  }
  CHECK(tidy && !leaked && g_sdk.calls.back() == "destroy");

  // BEYOND not ready: frames are dropped quietly and no zone image is created.
  g_sdk = MockSdk();
  g_sdk.ready = false;
  {
    BeyondSink s(mockApi(), BeyondSink::Options{});
    s.onFrame(f);
    CHECK(!s.ready() && g_sdk.sends == 0 && s.zonesActive() == 0);
    g_sdk.ready = true;
    s.onFrame(f);
    CHECK(s.ready() && g_sdk.sends == 1);
  }
  // Cue-image mode: stream into an existing named image; never create or delete anything.
  g_sdk = MockSdk();
  {
    BeyondSink::Options co; co.cueImageName = "LaserMap";
    BeyondSink cs(mockApi(), co);
    cs.onFrame(f);
    CHECK(g_sdk.lastName == "LaserMap" && g_sdk.lastCount == 3);
    bool created = false;
    for (const auto& c : g_sdk.calls) if (c.rfind("createimage", 0) == 0) created = true;
    CHECK(!created);
  }
  {
    bool deleted = false;
    for (const auto& c : g_sdk.calls) if (c.rfind("deleteimage", 0) == 0) deleted = true;
    CHECK(!deleted && g_sdk.calls.back() == "destroy");
  }
  // Missing DLL on a platform without BEYOND reports a reason instead of crashing.
  SdkApi none; std::string err;
  CHECK(!loadSdkApi("definitely-not-here.dll", none, err) && !err.empty());
}

int main() {
  testGridFitsFrame();
  testBeyondSink();
  testWireAndBridge();
  testIlda();
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
