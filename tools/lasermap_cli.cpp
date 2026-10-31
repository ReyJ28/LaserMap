// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
// Desktop harness: image (PPM P6 / PGM P5, or built-in test pattern) -> laser point preview SVG.
//   lasermap_cli [in.ppm|-] out.svg [threshold]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "lasermap/safety.h"
#include "lasermap/trace.h"

using namespace lasermap;

static Frame testPattern() {
  Frame f;
  f.w = 320; f.h = 240;
  f.rgba.assign(size_t(f.w) * f.h * 4, 0);
  auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t* p = &f.rgba[(size_t(y) * f.w + x) * 4];
    p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
  };
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) {
      double d = std::hypot(x - 110.0, y - 120.0);
      if (d < 70 && d > 40) put(x, y, 0, 255, 80);            // ring
      if (x > 200 && x < 290 && y > 60 && y < 180) put(x, y, 255, 40, 40);  // box
    }
  return f;
}

static bool readPnm(const std::string& path, Frame& f) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string magic;
  int w, h, maxv;
  in >> magic >> w >> h >> maxv;
  if ((magic != "P6" && magic != "P5") || w <= 0 || h <= 0 || maxv != 255) return false;
  in.get();
  int ch = magic == "P6" ? 3 : 1;
  std::string raw(size_t(w) * h * ch, '\0');
  in.read(raw.data(), std::streamsize(raw.size()));
  if (size_t(in.gcount()) != raw.size()) return false;
  f.w = w; f.h = h;
  f.rgba.resize(size_t(w) * h * 4);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    for (int c = 0; c < 3; ++c) f.rgba[i * 4 + c] = uint8_t(raw[i * ch + (ch == 3 ? c : 0)]);
    f.rgba[i * 4 + 3] = 255;
  }
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s [in.ppm|-] out.svg [threshold]\n", argv[0]);
    return 2;
  }
  Frame f;
  std::string inPath = argv[1];
  if (inPath == "-") f = testPattern();
  else if (!readPnm(inPath, f)) { std::fprintf(stderr, "cannot read %s\n", inPath.c_str()); return 1; }

  TraceParams tp;
  if (argc > 3) tp.threshold = std::strtof(argv[3], nullptr);
  PointFrame pts = traceClip(f, tp);

  SafetyParams sp;
  sp.outputEnabled = true;
  SafetyGuard guard(sp);
  PointFrame safe = guard.process(pts, 1.0 / 30);

  std::ostringstream svg;
  svg << "<svg xmlns='http://www.w3.org/2000/svg' viewBox='-1.05 -1.05 2.1 2.1' width='640' height='640'>"
         "<rect x='-1.05' y='-1.05' width='2.1' height='2.1' fill='black'/>";
  for (size_t i = 1; i < safe.size(); ++i) {
    const Point& a = safe[i - 1];
    const Point& b = safe[i];
    if (b.blank) continue;
    char col[32];
    std::snprintf(col, sizeof col, "#%02x%02x%02x", int(b.r * 255), int(b.g * 255), int(b.b * 255));
    svg << "<line x1='" << a.x << "' y1='" << -a.y << "' x2='" << b.x << "' y2='" << -b.y
        << "' stroke='" << col << "' stroke-width='0.012'/>";
  }
  svg << "</svg>";
  std::ofstream(argv[2]) << svg.str();
  std::printf("%dx%d -> %zu points (%zu after safety)\n", f.w, f.h, pts.size(), safe.size());
  return 0;
}
