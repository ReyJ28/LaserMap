// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <cstdint>
#include <vector>

namespace lasermap {

// Input video frame, tightly packed RGBA8, row 0 = top.
struct Frame {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;
  bool valid() const { return w > 0 && h > 0 && rgba.size() == size_t(w) * h * 4; }
};

struct Vec2 {
  float x = 0, y = 0;
};

// One laser point. x,y are normalized to [-1,1] (y up). Colors 0..1.
struct Point {
  float x = 0, y = 0;
  float r = 0, g = 0, b = 0;
  bool blank = true;  // true = beam off while moving here
};

using PointFrame = std::vector<Point>;

}  // namespace lasermap
