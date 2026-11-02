// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "beyond_sink.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lasermap {

namespace {
int32_t colorRef(float r, float g, float b) {
  auto c = [](float v) { return int(std::lround(std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 1.f) * 255.f)); };
  return int32_t(c(r) | (c(g) << 8) | (c(b) << 16));
}
}  // namespace

void BeyondSink::convert(const WireFrame& f, const Options& o, std::vector<SdkPoint>& out) {
  out.clear();
  const size_t n = std::min<size_t>(f.pts.size(), kMaxPoints);
  out.reserve(std::max<size_t>(n, 1));
  for (size_t i = 0; i < n; ++i) {
    const Point& p = f.pts[i];
    SdkPoint q{};
    float x = std::clamp(std::isfinite(p.x) ? p.x : 0.f, -1.f, 1.f), y = std::clamp(std::isfinite(p.y) ? p.y : 0.f, -1.f, 1.f);
    q.x = (o.flipX ? -x : x) * o.scale;
    q.y = (o.flipY ? -y : y) * o.scale;
    q.z = 0.f;
    // A blanked point is a black point: BEYOND's SDK image has no blank flag ("Status ... leave it zero").
    q.color = p.blank ? 0 : colorRef(p.r, p.g, p.b);
    out.push_back(q);
  }
  if (out.empty()) out.push_back(SdkPoint{});  // "no light": one black point at the centre
}

BeyondSink::BeyondSink(SdkApi api, Options opt) : api_(api), opt_(opt) {}

BeyondSink::~BeyondSink() {
  std::lock_guard<std::mutex> lk(m_);
  if (!api_.complete()) return;
  if (ready_) {
    for (int z : zones_) sendBlankLocked(z);
    if (opt_.cueImageName.empty())  // images inside a cue belong to the user's show: never delete those
      for (int z : zones_) api_.deleteZoneImage(imageName(z).c_str());
  }
  if (created_) api_.destroy();
}

bool BeyondSink::connect() {
  std::lock_guard<std::mutex> lk(m_);
  if (!api_.complete()) return false;
  if (!created_) created_ = api_.create() == 1;
  if (!created_) return false;
  ready_ = api_.beyondExeReady() == 1;
  return ready_;
}

void BeyondSink::sendBlankLocked(int zone) {
  SdkPoint black{};
  uint8_t zl[256] = {};
  zl[0] = uint8_t(zone);
  api_.sendFrameToImage(imageName(zone).c_str(), 1, &black, zl, 100);
}

void BeyondSink::onFrame(const WireFrame& f) {
  if (!ready_ && !connect()) return;
  std::lock_guard<std::mutex> lk(m_);
  const int zone = std::clamp<int>(f.zone, 1, 200);
  if (!zones_.count(zone)) {
    if (opt_.cueImageName.empty())
      api_.createZoneImage(zone - 1, imageName(zone).c_str());  // BEYOND counts zones from 0; the list below from 1
    zones_.insert(zone);
  }
  std::vector<SdkPoint> pts;
  convert(f, opt_, pts);
  uint8_t zl[256] = {};
  zl[0] = uint8_t(zone);  // zone list: 1-based indexes, 0 terminates
  const int rate = f.kpps > 0 ? -int(f.kpps) * 1000 : 100;  // negative = points per second, positive = percent
  const int count = int(pts.size());
  const auto t0 = std::chrono::steady_clock::now();
  api_.sendFrameToImage(imageName(zone).c_str(), count, pts.data(), zl, rate);
  lastCallMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  ++sent_;
  lit_ = !f.pts.empty();
  last_ = std::chrono::steady_clock::now();
}

void BeyondSink::idle() {
  std::lock_guard<std::mutex> lk(m_);
  if (!ready_ || !lit_) return;
  const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - last_).count();
  if (age < opt_.watchdogSec) return;
  for (int z : zones_) sendBlankLocked(z);  // the plugin went quiet: do not leave the last frame on the wall
  lit_ = false;
}

int BeyondSink::zonesActive() const {
  std::lock_guard<std::mutex> lk(m_);
  return int(zones_.size());
}

#ifdef _WIN32
namespace {
enum class Conv { Cdecl, Stdcall };

#if defined(_MSC_VER) && defined(_M_IX86)
// 32-bit: observe how the callee leaves the stack. Calling a stdcall function through a cdecl pointer leaves ESP 4 bytes
// higher per argument than expected, because both sides pop the argument. The stack is restored right after.
__declspec(noinline) int espDelta1(FARPROC fn, const char* arg) {
  unsigned before = 0, after = 0;
  __asm { mov before, esp }
  reinterpret_cast<int(__cdecl*)(const char*)>(fn)(arg);
  __asm { mov after, esp }
  __asm { mov esp, before }
  return int(after) - int(before);
}
Conv detectConvention(FARPROC oneArgFn) { return espDelta1(oneArgFn, "LaserMap.probe") == 0 ? Conv::Cdecl : Conv::Stdcall; }
#else
Conv detectConvention(FARPROC) { return sizeof(void*) == 8 ? Conv::Cdecl : Conv::Stdcall; }  // x64 has one convention
#endif

template <class Sig> struct Call;
template <> struct Call<int()> {
  static std::function<int()> make(FARPROC p, Conv c) {
    if (c == Conv::Stdcall) return [p] { return reinterpret_cast<int(__stdcall*)()>(p)(); };
    return [p] { return reinterpret_cast<int(__cdecl*)()>(p)(); };
  }
};
template <> struct Call<int(const char*)> {
  static std::function<int(const char*)> make(FARPROC p, Conv c) {
    if (c == Conv::Stdcall) return [p](const char* a) { return reinterpret_cast<int(__stdcall*)(const char*)>(p)(a); };
    return [p](const char* a) { return reinterpret_cast<int(__cdecl*)(const char*)>(p)(a); };
  }
};
template <> struct Call<int(int, const char*)> {
  static std::function<int(int, const char*)> make(FARPROC p, Conv c) {
    if (c == Conv::Stdcall) return [p](int a, const char* b) { return reinterpret_cast<int(__stdcall*)(int, const char*)>(p)(a, b); };
    return [p](int a, const char* b) { return reinterpret_cast<int(__cdecl*)(int, const char*)>(p)(a, b); };
  }
};
template <> struct Call<int(const char*, int, void*, void*, int)> {
  static std::function<int(const char*, int, void*, void*, int)> make(FARPROC p, Conv c) {
    if (c == Conv::Stdcall)
      return [p](const char* a, int b, void* d, void* e, int f) { return reinterpret_cast<int(__stdcall*)(const char*, int, void*, void*, int)>(p)(a, b, d, e, f); };
    return [p](const char* a, int b, void* d, void* e, int f) { return reinterpret_cast<int(__cdecl*)(const char*, int, void*, void*, int)>(p)(a, b, d, e, f); };
  }
};
}  // namespace

bool loadSdkApi(const std::string& dllPath, SdkApi& api, std::string& error) {
  std::vector<std::string> names;
  if (!dllPath.empty()) names = {dllPath};
  else names = {"BEYONDIO.dll", "BEYONDIOx64.dll", "BeyondIO.dll"};
  HMODULE h = nullptr;
  DWORD lastErr = 0;
  for (const auto& n : names) {
    h = LoadLibraryA(n.c_str());
    if (h) break;
    lastErr = GetLastError();
  }
  if (!h) {
    if (lastErr == 193)
      error = sizeof(void*) == 8 ? "the SDK DLL is 32-bit (Windows error 193) but this helper is 64-bit: use the 32-bit helper (lasermap_bridge32.exe)"
                                 : "the SDK DLL is 64-bit but this helper is 32-bit (Windows error 193)";
    else
      error = "could not load BEYONDIO.dll (put it next to the helper or give its path with --sdk-dll), Windows error " + std::to_string(lastErr);
    return false;
  }
  auto get = [&](const char* base) -> FARPROC {
    for (const char* pre : {"ldb", "ld"}) {  // the SDK document uses both prefixes
      FARPROC p = GetProcAddress(h, (std::string(pre) + base).c_str());
      if (p) return p;
    }
    return nullptr;
  };
  FARPROC pCreate = get("Create"), pDestroy = get("Destroy"), pReady = get("BeyondExeReady"), pNew = get("CreateZoneImage"),
          pDel = get("DeleteZoneImage"), pSend = get("SendFrameToImage");
  if (!pCreate || !pDestroy || !pReady || !pNew || !pDel || !pSend) {
    error = "the DLL loaded but is missing some of the functions LaserMap needs (Create, Destroy, BeyondExeReady, "
            "CreateZoneImage, DeleteZoneImage, SendFrameToImage)";
    return false;
  }
  // ldbCreate takes no arguments, so its calling convention does not matter. It must run before anything else.
  if (Call<int()>::make(pCreate, Conv::Cdecl)() != 1) {
    error = "ldbCreate failed";
    return false;
  }
  const Conv conv = detectConvention(pDel);
  api.create = [] { return 1; };  // already created above; the SDK wants exactly one ldbCreate
  api.destroy = Call<int()>::make(pDestroy, conv);
  api.beyondExeReady = Call<int()>::make(pReady, conv);
  api.createZoneImage = Call<int(int, const char*)>::make(pNew, conv);
  api.deleteZoneImage = Call<int(const char*)>::make(pDel, conv);
  api.sendFrameToImage = Call<int(const char*, int, void*, void*, int)>::make(pSend, conv);
  error = std::string("calling convention: ") + (conv == Conv::Stdcall ? "stdcall" : "cdecl");  // informational
  return true;
}
#else
bool loadSdkApi(const std::string&, SdkApi&, std::string& error) {
  error = "the BEYOND SDK is a Windows DLL; this helper can only drive BEYOND on Windows";
  return false;
}
#endif

}  // namespace lasermap
