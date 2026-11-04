// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lasermap {

// One bit per mode, in the order of the plugin's Modes group.
enum ModeBit : uint32_t {
  kModeClip = 1, kModeBeams = 2, kModeSweep = 4, kModeBars = 8, kModeSpot = 16, kModeCannon = 32, kModeReveal = 64, kModeGrid = 128
};

struct ScheduleSegment {
  double start = 0, end = 0;  // seconds, start inclusive, end exclusive
  uint32_t modes = 0;         // 0 = laser off for this stretch ("none")
};

// Text such as   "0-8: Trace Clip; 8-16: Beam Runners + Sweep; 1:00-1:20: Grid"
//   * segments are separated by ';' or a new line
//   * times are seconds ("8", "8.5") or minutes:seconds ("1:30", "1:30.5")
//   * modes are separated by '+', ',' or '&'; names are not case sensitive and short forms work:
//     clip, beams, sweep, bars, spot, cannon, reveal, grid; "none" or "off" means a blank stretch
struct Schedule {
  std::vector<ScheduleSegment> segments;
  std::vector<std::string> problems;  // human-readable notes about anything that could not be understood
  double length() const;              // end of the last segment
  // Modes active at time t. With loop, t wraps around the schedule length. Outside every segment: no modes.
  uint32_t activeAt(double t, bool loop) const;
};

Schedule parseSchedule(const std::string& text);
std::string modeNames(uint32_t modes);  // "Trace Clip + Grid", or "none"

}  // namespace lasermap
