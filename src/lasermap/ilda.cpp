// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "ilda.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace lasermap {

namespace {
void put16(std::vector<uint8_t>& o, int v) {
  o.push_back(uint8_t((v >> 8) & 0xff));
  o.push_back(uint8_t(v & 0xff));
}
void putName(std::vector<uint8_t>& o, const std::string& s) {
  for (size_t i = 0; i < 8; ++i) o.push_back(i < s.size() ? uint8_t(s[i]) : uint8_t(0));
}
void header(std::vector<uint8_t>& o, const std::string& name, const std::string& company, int records, int frameNo,
            int total) {
  o.push_back('I'); o.push_back('L'); o.push_back('D'); o.push_back('A');
  o.push_back(0); o.push_back(0); o.push_back(0);  // reserved
  o.push_back(5);                                  // format 5: 2D true colour
  putName(o, name);
  putName(o, company);
  put16(o, records);
  put16(o, frameNo);
  put16(o, total);
  o.push_back(0);  // scanner head
  o.push_back(0);  // reserved
}
uint8_t colour(float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); }
int coord(float v) {
  if (!std::isfinite(v)) v = 0.f;
  return int(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f));
}
}  // namespace

std::vector<uint8_t> encodeIlda(const std::vector<PointFrame>& frames, const std::string& frameName,
                                const std::string& company) {
  std::vector<uint8_t> out;
  int total = 0;
  for (const auto& f : frames) if (!f.empty()) ++total;
  total = std::min(total, 65535);
  int frameNo = 0;
  for (const auto& f : frames) {
    if (f.empty()) continue;
    if (frameNo >= 65535) break;
    const size_t n = std::min<size_t>(f.size(), 65535);
    header(out, frameName, company, int(n), frameNo, total);
    for (size_t i = 0; i < n; ++i) {
      const Point& p = f[i];
      put16(out, coord(p.x));
      put16(out, coord(p.y));
      uint8_t status = p.blank ? 0x40 : 0x00;
      if (i + 1 == n) status |= 0x80;
      out.push_back(status);
      out.push_back(colour(p.b));
      out.push_back(colour(p.g));
      out.push_back(colour(p.r));
    }
    ++frameNo;
  }
  header(out, frameName, company, 0, 0, total);  // terminator
  return out;
}

bool writeIldaFile(const std::string& path, const std::vector<PointFrame>& frames) {
  auto bytes = encodeIlda(frames);
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  return bool(f);
}

}  // namespace lasermap
