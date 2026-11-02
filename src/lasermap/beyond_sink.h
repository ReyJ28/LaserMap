// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "wire.h"

namespace lasermap {

// Point layout of the Pangolin BEYOND SDK ("TSdkImagePoint", 20 bytes, packed).
#pragma pack(push, 1)
struct SdkPoint {
  float x, y, z;     // -32K..+32K
  int32_t color;     // Windows COLORREF: 0x00BBGGRR
  uint8_t repCount;  // 0 = no repeats
  uint8_t focus;     // reserved, zero
  uint8_t status;    // reserved, zero
  uint8_t zero;
};
#pragma pack(pop)
static_assert(sizeof(SdkPoint) == 20, "SdkPoint must match the BEYOND SDK layout");

// The subset of the BEYOND SDK DLL that LaserMap uses. Deliberately absent: ldbEnableLaserOutput,
// ldbDisableLaserOutput and ldbBlackout. Arming and blackout stay entirely with the operator in BEYOND.
struct SdkApi {
  int (*create)() = nullptr;
  int (*destroy)() = nullptr;
  int (*beyondExeReady)() = nullptr;
  int (*createZoneImage)(int zoneIndex0, const char* name) = nullptr;
  int (*deleteZoneImage)(const char* name) = nullptr;
  int (*sendFrameToImage)(const char* name, int count, void* points, void* zones, int rate) = nullptr;
  bool complete() const { return create && destroy && beyondExeReady && createZoneImage && deleteZoneImage && sendFrameToImage; }
};

// Loads the BEYOND SDK DLL (Windows x64 only). dllPath empty = try the usual names. Returns false with a reason.
bool loadSdkApi(const std::string& dllPath, SdkApi& api, std::string& error);

class BeyondSink {
 public:
  struct Options {
    bool flipX = false, flipY = false;
    float scale = 32000.f;   // laser +-1 -> +-scale (BEYOND takes +-32K)
    double watchdogSec = 1.0;  // no frame for this long: send "no light" so BEYOND never holds a stale frame
  };
  BeyondSink(SdkApi api, Options opt);
  ~BeyondSink();

  // Calls ldbCreate and checks that BEYOND is up and ready. Safe to call repeatedly until it returns true.
  bool connect();
  bool ready() const { return ready_; }

  void onFrame(const WireFrame& f);  // thread-safe
  void idle();                       // call a few times a second: the watchdog
  uint64_t framesSent() const { return sent_; }
  int zonesActive() const;

  static void convert(const WireFrame& f, const Options& o, std::vector<SdkPoint>& out);
  static constexpr int kMaxPoints = 8192;

 private:
  void sendBlankLocked(int zone);
  static std::string imageName(int zone) { return "LaserMap.Z" + std::to_string(zone); }
  SdkApi api_;
  Options opt_;
  mutable std::mutex m_;
  bool created_ = false, ready_ = false, lit_ = false;
  std::set<int> zones_;
  std::chrono::steady_clock::time_point last_{};
  uint64_t sent_ = 0;
};

}  // namespace lasermap
