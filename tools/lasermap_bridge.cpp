// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
// The bridge helper: runs on the computer that drives the laser (the BEYOND PC), receives the frames the LaserMap
// plugin streams over the network and passes them to a sink: statistics, an ILDA recorder, and BEYOND.
//   lasermap_bridge [--port 7788] [--ild out.ild] [--seconds N] [--beyond [--sdk-dll path] [--cue-image NAME] [--flip-x] [--flip-y]]
// --test-pattern sends a grid and a moving circle to BEYOND with no plugin involved (to test the BEYOND side alone).
// --cue-image NAME streams into an SDK Image you placed inside a BEYOND cue (use it in the grid or timeline).
// --beyond hands the frames to Pangolin BEYOND through its SDK DLL (Windows x64). It never arms, disarms or blacks out.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "lasermap/beyond_sink.h"
#include "lasermap/ilda.h"
#include "lasermap/modes.h"
#include "lasermap/net.h"

using namespace lasermap;

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }

int main(int argc, char** argv) {
  uint16_t port = 7788;
  std::string ildPath;
  double seconds = 0;
  bool beyond = false, flipX = false, flipY = false;
  std::string sdkDll, cueImage;
  bool testPattern = false;
  int testZone = 1;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = uint16_t(std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--ild") && i + 1 < argc) ildPath = argv[++i];
    else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--beyond")) beyond = true;
    else if (!std::strcmp(argv[i], "--sdk-dll") && i + 1 < argc) sdkDll = argv[++i];
    else if (!std::strcmp(argv[i], "--cue-image") && i + 1 < argc) cueImage = argv[++i];
    else if (!std::strcmp(argv[i], "--test-pattern")) testPattern = true;
    else if (!std::strcmp(argv[i], "--zone") && i + 1 < argc) testZone = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--flip-x")) flipX = true;
    else if (!std::strcmp(argv[i], "--flip-y")) flipY = true;
    else { std::fprintf(stderr, "usage: %s [--port N] [--ild out.ild] [--seconds N] [--beyond [--sdk-dll path] [--cue-image NAME] [--test-pattern [--zone N]] [--flip-x] [--flip-y]]\n", argv[0]); return 2; }
  }
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  std::mutex m;
  std::vector<PointFrame> recorded;
  std::atomic<uint64_t> frames{0}, points{0}, lit{0};
  std::atomic<int> zone{0};
  std::unique_ptr<BeyondSink> sink;
  // BEYOND can be slow to accept a frame (the SDK call is a cross-process window message). The network thread must not
  // wait for it, so frames go through a mailbox that keeps only the newest frame per zone; one worker feeds BEYOND.
  std::mutex mbMutex;
  std::condition_variable mbCv;
  std::map<int, WireFrame> mailbox;
  bool workerStop = false;
  std::thread worker;
  if (beyond) {
    SdkApi api;
    std::string err;
    if (!loadSdkApi(sdkDll, api, err)) { std::fprintf(stderr, "BEYOND: %s\n", err.c_str()); return 1; }
    BeyondSink::Options o;
    o.flipX = flipX; o.flipY = flipY; o.cueImageName = cueImage;
    sink = std::make_unique<BeyondSink>(api, o);
    std::printf("BEYOND SDK loaded (%s). Output arming stays in BEYOND; this helper only streams frames to zone images.\n", err.c_str());
  }
  TcpServer server;
  if (!server.start(port, [&](const WireFrame& f) {
        ++frames;
        if (sink) {
          { std::lock_guard<std::mutex> lk(mbMutex); mailbox[f.zone] = f; }
          mbCv.notify_one();
        }
        points += f.pts.size();
        zone = f.zone;
        if (!f.pts.empty()) {
          ++lit;
          if (!ildPath.empty()) {
            std::lock_guard<std::mutex> lk(m);
            if (recorded.size() < 36000) recorded.push_back(f.pts);
          }
        }
      })) {
    std::fprintf(stderr, "cannot listen on port %u\n", unsigned(port));
    return 1;
  }
  if (sink) {
    worker = std::thread([&] {
      std::unique_lock<std::mutex> lk(mbMutex);
      while (!workerStop) {
        mbCv.wait_for(lk, std::chrono::milliseconds(200), [&] { return workerStop || !mailbox.empty(); });
        if (mailbox.empty()) continue;
        std::map<int, WireFrame> batch;
        batch.swap(mailbox);
        lk.unlock();
        for (auto& kv : batch) sink->onFrame(kv.second);
        lk.lock();
      }
    });
  }
  std::thread pattern;
  if (testPattern) {
    if (!sink) { std::fprintf(stderr, "--test-pattern needs --beyond\n"); return 2; }
    pattern = std::thread([&] {
      const auto t0 = std::chrono::steady_clock::now();
      while (!g_stop) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        WireFrame f;
        f.zone = uint8_t(std::clamp(testZone, 1, 200));
        f.kpps = 30;
        f.pts = renderGrid(ModeParams{}).points;
        const float cx = 0.5f * float(std::cos(t * 1.5)), cy = 0.5f * float(std::sin(t * 1.1)), r = 0.12f;
        for (int i = 0; i <= 40; ++i) {
          const float a = 6.2831853f * float(i) / 40.f;
          Point p;
          p.x = cx + r * std::cos(a); p.y = cy + r * std::sin(a);
          p.r = 1.f; p.g = 0.4f; p.b = 0.f;
          p.blank = (i == 0);
          f.pts.push_back(p);
        }
        { std::lock_guard<std::mutex> lk(mbMutex); mailbox[f.zone] = f; }
        mbCv.notify_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
      }
    });
    std::printf("TEST PATTERN: a grid and a moving circle are being sent to zone %d. Nothing else is needed (no Resolume).\n", testZone);
  }
  std::printf("LaserMap bridge listening on port %u. Waiting for the plugin (Output > Send To Bridge)...\n", unsigned(server.port()));

  const auto t0 = std::chrono::steady_clock::now();
  uint64_t lastFrames = 0, lastDelivered = 0;
  int tick = 0;
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (sink) { sink->idle(); if (!sink->ready() && tick % 10 == 0) sink->connect(); }
    if (++tick % 10 != 0) continue;
    const uint64_t fr = frames;
    std::printf("%s | %llu frames/s | zone %d | %llu frames with light | %llu bytes skipped%s\n",
                server.clientConnected() ? "CONNECTED" : "waiting  ", (unsigned long long)(fr - lastFrames), int(zone),
                (unsigned long long)lit, (unsigned long long)server.droppedBytes(),
                sink ? (sink->ready() ? " | BEYOND ready" : " | waiting for BEYOND") : "");
    if (sink) {
      const uint64_t dl = sink->framesSent();
      std::printf("          to BEYOND: %llu frames/s | last SDK call %.1f ms\n", (unsigned long long)(dl - lastDelivered), sink->lastCallMs());
      lastDelivered = dl;
    }
    std::fflush(stdout);
    lastFrames = fr;
    if (seconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds) break;
  }
  server.stop();
  g_stop = true;
  if (pattern.joinable()) pattern.join();
  { std::lock_guard<std::mutex> lk(mbMutex); workerStop = true; }
  mbCv.notify_all();
  if (worker.joinable()) worker.join();
  sink.reset();  // blanks and removes its zone images
  if (!ildPath.empty()) {
    std::lock_guard<std::mutex> lk(m);
    if (writeIldaFile(ildPath, recorded)) std::printf("wrote %zu frames to %s\n", recorded.size(), ildPath.c_str());
    else std::fprintf(stderr, "could not write %s\n", ildPath.c_str());
  }
  return 0;
}
