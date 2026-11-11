// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "react.h"
#include <algorithm>
#include <cmath>

namespace lasermap {

ReactSignals ReactAnalyser::update(const Frame& f, float keyHue, float keyTol) {
  ReactSignals s;
  if (!f.valid()) return s;
  const size_t n = size_t(f.w) * f.h;
  std::vector<uint8_t> luma(n);
  double sumL = 0, sumD = 0, colourCount = 0;
  const bool havePrev = prev_.size() == n;
  for (size_t i = 0; i < n; ++i) {
    const float r = f.rgba[i * 4] / 255.f, g = f.rgba[i * 4 + 1] / 255.f, b = f.rgba[i * 4 + 2] / 255.f;
    const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    luma[i] = (uint8_t)std::lround(std::clamp(l, 0.f, 1.f) * 255.f);
    sumL += l;
    if (havePrev) sumD += std::fabs(luma[i] - prev_[i]) / 255.0;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    if (mx > 0.2f && d / mx > 0.3f) {
      float h;
      if (mx == r) h = std::fmod((g - b) / d, 6.f);
      else if (mx == g) h = (b - r) / d + 2.f;
      else h = (r - g) / d + 4.f;
      h = h / 6.f;
      if (h < 0) h += 1.f;
      float dh = std::fabs(h - keyHue);
      dh = std::min(dh, 1.f - dh);  // 0..0.5
      if (dh <= keyTol * 0.5f) colourCount += 1;
    }
  }
  s.brightness = (float)(sumL / n);
  s.motion = havePrev ? std::min(1.f, (float)(sumD / n) * 10.f) : 0.f;
  s.colour = std::min(1.f, (float)(colourCount / n) * 5.f);
  prev_ = std::move(luma);
  return s;
}

bool ReactGate::update(ReactCond c, const ReactSignals& s, float th, float holdSeconds, double dt) {
  if (c == RAlways) { on_ = true; hold_ = 0; return true; }
  const float hyst = 0.04f;
  float v = 0;
  bool invert = false;
  switch (c) {
    case RBright: v = s.brightness; break;
    case RDark: v = s.brightness; invert = true; break;
    case RMotion: v = s.motion; break;
    case RStill: v = s.motion; invert = true; break;
    case RColour: v = s.colour; break;
    default: break;
  }
  if (invert) v = 1.f - v, th = 1.f - th;  // "below th" becomes "above 1-th"
  bool cond;
  if (on_ && hold_ >= 0) cond = v > th - hyst;  // stay on until clearly below
  else cond = v > th + hyst;                    // switch on only when clearly above
  if (cond) {
    hold_ = holdSeconds;
    on_ = true;
  } else if (hold_ > 0) {
    hold_ -= (float)dt;
    on_ = true;
  } else {
    on_ = false;
    hold_ = -1;  // fully off: next switch-on needs the higher threshold
  }
  return on_;
}

}  // namespace lasermap
