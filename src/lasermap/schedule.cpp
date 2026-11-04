// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "schedule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace lasermap {

namespace {
std::string lowerNoSpace(const std::string& s) {
  std::string o;
  for (unsigned char c : s) if (!std::isspace(c)) o.push_back(char(std::tolower(c)));
  return o;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}
bool parseTime(const std::string& in, double& out) {
  const std::string s = trim(in);
  if (s.empty()) return false;
  const size_t colon = s.find(':');
  char* end = nullptr;
  if (colon == std::string::npos) {
    out = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && out >= 0;
  }
  const double m = std::strtod(s.substr(0, colon).c_str(), &end);
  if (!end || *end != '\0' || m < 0) return false;
  const double sec = std::strtod(s.substr(colon + 1).c_str(), &end);
  if (!end || *end != '\0' || sec < 0 || sec >= 60.0) return false;
  out = m * 60.0 + sec;
  return true;
}
bool modeFromName(const std::string& raw, uint32_t& bit) {
  const std::string n = lowerNoSpace(raw);
  struct E { const char* name; uint32_t bit; };
  static const E table[] = {
      {"traceclip", kModeClip}, {"clip", kModeClip},        {"beamrunners", kModeBeams}, {"beams", kModeBeams},
      {"beam", kModeBeams},     {"sweep", kModeSweep},      {"tracebars", kModeBars},    {"bars", kModeBars},
      {"spotlight", kModeSpot}, {"spot", kModeSpot},        {"lasercannon", kModeCannon}, {"cannon", kModeCannon},
      {"tracereveal", kModeReveal}, {"reveal", kModeReveal}, {"grid", kModeGrid},        {"none", 0},
      {"off", 0},               {"black", 0},               {"blank", 0}};
  for (const auto& e : table) if (n == e.name) { bit = e.bit; return true; }
  return false;
}
}  // namespace

double Schedule::length() const {
  double l = 0;
  for (const auto& s : segments) l = std::max(l, s.end);
  return l;
}

uint32_t Schedule::activeAt(double t, bool loop) const {
  const double len = length();
  if (loop && len > 0) t = std::fmod(t, len);
  if (t < 0) return 0;
  uint32_t m = 0;
  for (const auto& s : segments) if (t >= s.start && t < s.end) m |= s.modes;
  return m;
}

Schedule parseSchedule(const std::string& text) {
  Schedule sch;
  std::string norm = text;
  for (auto& c : norm) if (c == '\n' || c == '\r') c = ';';
  size_t pos = 0;
  int index = 0;
  while (pos <= norm.size()) {
    size_t semi = norm.find(';', pos);
    if (semi == std::string::npos) semi = norm.size();
    const std::string seg = trim(norm.substr(pos, semi - pos));
    pos = semi + 1;
    if (seg.empty()) continue;
    ++index;
    const std::string label = "part " + std::to_string(index) + " (\"" + seg + "\")";
    // everything before the first letter is the time range, the rest names the modes
    size_t firstAlpha = 0;
    while (firstAlpha < seg.size() && !std::isalpha(static_cast<unsigned char>(seg[firstAlpha]))) ++firstAlpha;
    std::string times = trim(seg.substr(0, firstAlpha));
    const std::string modes = trim(seg.substr(firstAlpha));
    while (!times.empty() && (times.back() == ':' || std::isspace(static_cast<unsigned char>(times.back())))) times.pop_back();
    size_t dash = times.find('-', 1);
    double a = 0, b = 0;
    if (dash == std::string::npos || !parseTime(times.substr(0, dash), a) || !parseTime(times.substr(dash + 1), b) || b <= a) {
      sch.problems.push_back(label + ": expected a time range like 0-8 or 1:00-1:20");
      continue;
    }
    uint32_t mask = 0;
    bool ok = !modes.empty();
    std::string name;
    auto flush = [&]() {
      if (trim(name).empty()) return;
      uint32_t bit = 0;
      if (modeFromName(name, bit)) mask |= bit;
      else { sch.problems.push_back(label + ": unknown mode \"" + trim(name) + "\""); ok = false; }
      name.clear();
    };
    for (char c : modes) {
      if (c == '+' || c == ',' || c == '&') flush();
      else name.push_back(c);
    }
    flush();
    if (modes.empty()) sch.problems.push_back(label + ": no mode named");
    if (!ok) continue;
    sch.segments.push_back({a, b, mask});
  }
  return sch;
}

std::string modeNames(uint32_t modes) {
  struct E { uint32_t bit; const char* name; };
  static const E all[] = {{kModeClip, "Trace Clip"}, {kModeBeams, "Beam Runners"}, {kModeSweep, "Sweep"}, {kModeBars, "Trace Bars"},
                          {kModeSpot, "Spotlight"},  {kModeCannon, "Laser Cannon"}, {kModeReveal, "Trace Reveal"}, {kModeGrid, "Grid"}};
  std::string out;
  for (const auto& e : all) if (modes & e.bit) { if (!out.empty()) out += " + "; out += e.name; }
  return out.empty() ? "none" : out;
}

}  // namespace lasermap
