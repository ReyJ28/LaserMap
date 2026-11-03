// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
// Traces every frame of a raw RGB video (read from stdin) with the Trace Clip mode and writes the laser points per frame
// in the same binary format as lasermap_demo (consumed by tools/demo/render_mapping_preview.py).
//   ffmpeg -i clip.mp4 -vf scale=480:270 -f rawvideo -pix_fmt rgb24 - | lasermap_trace_video 480 270 out.bin [out.ild]
// With out.ild every traced frame is also written to an ILDA file (one ILDA frame per video frame) that BEYOND can import.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#include "lasermap/ilda.h"
#include "lasermap/modes.h"
#include "lasermap/trace.h"

using namespace lasermap;
template <class T> static void put(std::ofstream& o, const T& v) { o.write(reinterpret_cast<const char*>(&v), sizeof v); }

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: %s W H out.bin < rawvideo\n", argv[0]); return 2; }
  const int w = std::atoi(argv[1]), h = std::atoi(argv[2]);
  std::ofstream out(argv[3], std::ios::binary);
  const int32_t magic = 0x4C4D4150, mw = w, mh = h;
  int32_t total = 0;
  put(out, magic); put(out, mw); put(out, mh); put(out, total);
  TraceParams tp;
  tp.threshold = 0.35f; tp.simplifyEps = 1.2f; tp.minPerimeter = 10.f; tp.maxShapes = 24; tp.smoothing = 0.25f; tp.maxPoints = 2214;
  std::vector<uint8_t> raw(size_t(w) * h * 3);
  Frame f; f.w = w; f.h = h; f.rgba.resize(size_t(w) * h * 4);
  std::vector<PointFrame> ildFrames;
  while (std::fread(raw.data(), 1, raw.size(), stdin) == raw.size()) {
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      f.rgba[i * 4] = raw[i * 3]; f.rgba[i * 4 + 1] = raw[i * 3 + 1]; f.rgba[i * 4 + 2] = raw[i * 3 + 2]; f.rgba[i * 4 + 3] = 255;
    }
    const PointFrame pts = emitOutline(extractShapes(f, tp), 0.0422f);
    const int32_t zero = 0, np = int32_t(pts.size());
    const float zf = 0.f;
    put(out, zero); put(out, zero); put(out, zf); put(out, zf); put(out, zf); put(out, zero); put(out, zf); put(out, zero);
    for (int k = 0; k < 64; ++k) put(out, zf);
    put(out, zero);
    put(out, np);
    for (const auto& p : pts) { const float rec[6] = {p.x, p.y, p.r, p.g, p.b, p.blank ? 1.f : 0.f}; out.write(reinterpret_cast<const char*>(rec), sizeof rec); }
    if (argc > 4) ildFrames.push_back(pts);
    ++total;
  }
  out.seekp(12);
  put(out, total);
  std::fprintf(stderr, "traced %d frames\n", int(total));
  if (argc > 4) std::fprintf(stderr, writeIldaFile(argv[4], ildFrames) ? "wrote %s\n" : "could not write %s\n", argv[4]);
  return 0;
}
