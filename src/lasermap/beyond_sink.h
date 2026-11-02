// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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
  std::function<int()> create, destroy, beyondExeReady;
  std::function<int(int zoneIndex0, const char* name)> createZoneImage;
  std::function<int(const char* name)> deleteZoneImage;
  std::function<int(const char* name, int count, void* points, void* zones, int rate)> sendFrameToImage;
  bool complete() const { return create && destroy && beyondExeReady && createZoneImage && deleteZoneImage && sendFrameToImage; }
};

// Loads the BEYOND SDK DLL (Windows only; Pangolin's BEYONDIO.dll is 32-bit, so the helper must be a 32-bit build).
// dllPath empty = try the usual names. On 32-bit builds the DLL's calling convention (cdecl or stdcall) is detected
// at load time. Calls ldbCreate once as part of that. Returns false with a reason.
bool loadSdkApi(const std::string& dllPath, SdkApi& api, std::string& error);

class BeyondSink {
 public:
  struct Options {
    bool flipX = false, flipY = false;
    float scale = 32000.f;   // laser +-1 -> +-scale (BEYOND takes +-32K)
    double watchdogSec = 1.0;  // no frame for this long: send "no light" so BEYOND never holds a stale frame
    // Non-empty: stream into an SDK Image that already exists inside a BEYOND cue (the "SDK Image inside Cue" mode of
    // the SDK). Nothing is created or deleted; the image is found by this name. Empty: one zone image per zone.
    std::string cueImageName;
  };
  BeyondSink(SdkApi api, Options opt);
  ~BeyondSink();

  // Calls ldbCreate and checks that BEYOND is up and ready. Safe to call repeatedly until it returns true.
  bool connect();
  bool ready() const { return ready_; }

  void onFrame(const WireFrame& f);  // thread-safe
  void idle();                       // call a few times a second: the watchdog
  uint64_t framesSent() const { return sent_; }
  double lastCallMs() const { return lastCallMs_; }  // duration of the most recent ldbSendFrameToImage call
  int zonesActive() const;

  static void convert(const WireFrame& f, const Options& o, std::vector<SdkPoint>& out);
  static constexpr int kMaxPoints = 8192;

 private:
  void sendBlankLocked(int zone);
  std::string imageName(int zone) const { return opt_.cueImageName.empty() ? "LaserMap.Z" + std::to_string(zone) : opt_.cueImageName; }
  SdkApi api_;
  Options opt_;
  mutable std::mutex m_;
  bool created_ = false, ready_ = false, lit_ = false;
  std::set<int> zones_;
  std::chrono::steady_clock::time_point last_{};
  uint64_t sent_ = 0;
  std::atomic<double> lastCallMs_{0.0};
};

}  // namespace lasermap
