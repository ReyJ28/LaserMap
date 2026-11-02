// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
// Runs every mode for 10 s at 30 fps on a scene image and dumps the per-frame laser points and video masks
// (consumed by tools/demo/render_video.py). Mode parameters mirror the plugin's defaults.
//   lasermap_demo scene.ppm out.bin
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "lasermap/modes.h"
#include "lasermap/trace.h"

using namespace lasermap;

static bool readPpm(const char* path, Frame& f) {
  std::ifstream in(path, std::ios::binary);
  std::string magic;
  int w, h, mx;
  if (!(in >> magic >> w >> h >> mx) || magic != "P6") return false;
  in.get();
  std::vector<char> raw(size_t(w) * h * 3);
  in.read(raw.data(), std::streamsize(raw.size()));
  if (size_t(in.gcount()) != raw.size()) return false;
  f.w = w; f.h = h; f.rgba.resize(size_t(w) * h * 4);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    for (int c = 0; c < 3; ++c) f.rgba[i * 4 + c] = uint8_t(raw[i * 3 + c]);
    f.rgba[i * 4 + 3] = 255;
  }
  return true;
}

template <class T> static void put(std::ofstream& o, const T& v) { o.write(reinterpret_cast<const char*>(&v), sizeof v); }

static float perimeter(const Shape& s) {
  float p = 0;
  for (size_t i = 0; i < s.pts.size(); ++i) {
    const Point& a = s.pts[i];
    const Point& b = s.pts[(i + 1) % s.pts.size()];
    p += std::hypot(b.x - a.x, b.y - a.y);
  }
  return p;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: %s scene.ppm out.bin\n", argv[0]); return 2; }
  Frame scene;
  if (!readPpm(argv[1], scene)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

  TraceParams tp;
  tp.threshold = 0.35f;
  tp.simplifyEps = 1.5f;
  tp.minPerimeter = 20.f;
  tp.maxShapes = 40;
  tp.smoothing = 0.25f;
  tp.maxPoints = 2214;
  const auto shapes = extractShapes(scene, tp);
  float totalPerim = 0;
  for (const auto& s : shapes) totalPerim += perimeter(s);
  std::fprintf(stderr, "%zu shapes, total perimeter %.2f laser units\n", shapes.size(), totalPerim);

  const int fps = 30, secPerMode = 10, nModes = 8;
  ModeParams base;
  base.maxPoints = 2214;
  base.spacing = 0.0422f;
  base.maskW = 480;
  base.maskH = 270;

  Cannon cannon;
  TrailFX trail;
  TrailParams trp;  // plugin default: glow + sparks
  trp.style = 3; trp.amount = 0.6f; trp.density = 0.35f; trp.size = 0.3f; trp.gravity = 0.3f;
  trp.fadeSeconds = 0.1f + 0.3f * 3.f; trp.hue = 0.09f;

  std::ofstream out(argv[2], std::ios::binary);
  const int32_t magic = 0x4C4D4150, mw = base.maskW, mh = base.maskH, total = nModes * secPerMode * fps;
  put(out, magic); put(out, mw); put(out, mh); put(out, total);

  for (int mode = 0; mode < nModes; ++mode) {
    cannon.reset();
    trail.clear();
    for (int i = 0; i < secPerMode * fps; ++i) {
      const float t = float(i) / fps, dt = 1.f / fps;
      ModeParams mp = base;
      mp.time = t;
      ModeOutput o;
      switch (mode) {
        case 0: o.points = emitOutline(shapes, base.spacing); break;  // Trace Clip
        case 1:                                                      // Beam Runners
          mp.beamCount = 8; mp.beamSpeed = 0.9f; mp.tailLength = 0.145f; mp.scatter = 0.35f;
          mp.beamDirection = t < 5.f ? 0 : 2;
          o = renderBeamRunners(shapes, mp);
          break;
        case 2:                                                      // Trace Bars
          mp.barWidth = 0.132f; mp.barGap = 0.24f; mp.barSpeed = 0.6f;
          mp.barsDirection = t < 5.f ? 0 : 4;
          o = renderTraceBars(shapes, mp);
          break;
        case 3: {                                                    // Sweep: erase, then reveal
          mp.sweepReveal = t >= 5.f;
          mp.sweepProgress = std::fmod(t, 5.f) / 5.f;
          mp.barGlow = 0.7f; mp.sweepTrail = 0.25f; mp.sweepAngleDeg = 0.f;
          o = renderSweep(shapes, mp);
          break;
        }
        case 4:                                                      // Spotlight
          mp.spotRadius = 0.345f; mp.spotRange = 0.5f; mp.spotSpeed = 0.375f; mp.spotRandom = 0.5f;
          mp.spotAfterglow = 0.25f; mp.spotRim = 0.7f; mp.spotHue = -1.f;
          o = renderSpotlight(mp);
          break;
        case 5: {                                                    // Laser Cannon
          CannonParams cp;
          o = cannon.render(shapes, cp, dt, mp);
          break;
        }
        case 6:                                                      // Trace Reveal (one pass fits in ~8 s)
          mp.revealSpeed = totalPerim / 8.f / 2.f; mp.fadeTail = 0.16f;
          o = renderTraceReveal(shapes, mp);
          break;
        case 7: o = renderGrid(mp); break;                           // Grid
      }
      // Trail (glow + sparks) from the mode's hot spots, like the plugin.
      trail.update(dt, o.emitters, trp);
      auto fx = trail.render(o.emitters, trp);
      o.points.insert(o.points.end(), fx.begin(), fx.end());

      const int32_t kind = o.mask.kind, keepG = o.mask.keepGreater ? 1 : 0, nd = int32_t(std::min<size_t>(o.mask.discs.size(), 16));
      const int32_t hasTex = (o.mask.kind == VideoMask::Texture && o.maskImage.size() == size_t(mw) * mh) ? 1 : 0;
      const int32_t np = int32_t(o.points.size());
      put(out, int32_t(mode)); put(out, kind); put(out, o.mask.nx); put(out, o.mask.ny); put(out, o.mask.edge);
      put(out, keepG); put(out, o.mask.gain); put(out, nd);
      for (int k = 0; k < 16; ++k) {
        Disc d = k < nd ? o.mask.discs[k] : Disc{0, 0, 0, 0};
        put(out, d.x); put(out, d.y); put(out, d.r); put(out, d.a);
      }
      put(out, hasTex);
      if (hasTex) out.write(reinterpret_cast<const char*>(o.maskImage.data()), std::streamsize(o.maskImage.size()));
      put(out, np);
      for (const auto& p : o.points) {
        const float rec[6] = {p.x, p.y, p.r, p.g, p.b, p.blank ? 1.f : 0.f};
        out.write(reinterpret_cast<const char*>(rec), sizeof rec);
      }
    }
    std::fprintf(stderr, "mode %d done\n", mode + 1);
  }
  return 0;
}
