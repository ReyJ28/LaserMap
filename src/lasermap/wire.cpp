// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "wire.h"

#include <algorithm>
#include <cmath>

namespace lasermap {

namespace {
void put16(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v & 0xff)); o.push_back(uint8_t((v >> 8) & 0xff)); }
void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, v & 0xffff); put16(o, v >> 16); }
uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8); }
uint32_t get32(const uint8_t* p) { return get16(p) | (get16(p + 2) << 16); }
int16_t coord(float v) {
  if (!std::isfinite(v)) v = 0.f;
  return int16_t(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f));
}
uint8_t colour(float v) { return uint8_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 1.f) * 255.f)); }
}  // namespace

std::vector<uint8_t> encodeWire(const WireFrame& f) {
  const uint32_t n = uint32_t(std::min<size_t>(f.pts.size(), kWireMaxPoints));
  std::vector<uint8_t> o;
  o.reserve(kWireHeaderSize + n * kWirePointSize);
  o.push_back('L'); o.push_back('M'); o.push_back('B'); o.push_back('R');
  o.push_back(1);
  o.push_back(f.zone);
  put16(o, f.kpps);
  put32(o, f.seq);
  put32(o, n);
  for (uint32_t i = 0; i < n; ++i) {
    const Point& p = f.pts[i];
    put16(o, uint16_t(coord(p.x)));
    put16(o, uint16_t(coord(p.y)));
    o.push_back(colour(p.r)); o.push_back(colour(p.g)); o.push_back(colour(p.b));
    o.push_back(p.blank ? 1 : 0);
  }
  return o;
}

void WireDecoder::feed(const uint8_t* data, size_t n) { buf_.insert(buf_.end(), data, data + n); }

bool WireDecoder::next(WireFrame& out) {
  for (;;) {
    if (buf_.size() < kWireHeaderSize) return false;
    const uint8_t* b = buf_.data();
    const bool magic = b[0] == 'L' && b[1] == 'M' && b[2] == 'B' && b[3] == 'R' && b[4] == 1;
    const uint32_t count = magic ? get32(b + 12) : 0;
    if (!magic || count > kWireMaxPoints) {
      buf_.erase(buf_.begin());  // not a frame start: slide forward one byte and try again
      ++dropped_;
      continue;
    }
    const size_t need = kWireHeaderSize + size_t(count) * kWirePointSize;
    if (buf_.size() < need) return false;
    out.zone = b[5];
    out.kpps = uint16_t(get16(b + 6));
    out.seq = get32(b + 8);
    out.pts.resize(count);
    const uint8_t* p = b + kWireHeaderSize;
    for (uint32_t i = 0; i < count; ++i, p += kWirePointSize) {
      Point& q = out.pts[i];
      q.x = float(int16_t(get16(p))) / 32767.f;
      q.y = float(int16_t(get16(p + 2))) / 32767.f;
      q.r = p[4] / 255.f; q.g = p[5] / 255.f; q.b = p[6] / 255.f;
      q.blank = (p[7] & 1) != 0;
    }
    buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(need));
    return true;
  }
}

}  // namespace lasermap
