// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
// The bridge helper: runs on the computer that drives the laser (the BEYOND PC), receives the frames the LaserMap
// plugin streams over the network and passes them to a sink: statistics, an ILDA recorder, and BEYOND.
//   lasermap_bridge [--port 7788] [--ild out.ild] [--seconds N] [--beyond [--sdk-dll path] [--flip-x] [--flip-y]]
// --beyond hands the frames to Pangolin BEYOND through its SDK DLL (Windows x64). It never arms, disarms or blacks out.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "lasermap/beyond_sink.h"
#include "lasermap/ilda.h"
#include "lasermap/net.h"

using namespace lasermap;

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }

int main(int argc, char** argv) {
  uint16_t port = 7788;
  std::string ildPath;
  double seconds = 0;
  bool beyond = false, flipX = false, flipY = false;
  std::string sdkDll;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--port") && i + 1 < argc) port = uint16_t(std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--ild") && i + 1 < argc) ildPath = argv[++i];
    else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--beyond")) beyond = true;
    else if (!std::strcmp(argv[i], "--sdk-dll") && i + 1 < argc) sdkDll = argv[++i];
    else if (!std::strcmp(argv[i], "--flip-x")) flipX = true;
    else if (!std::strcmp(argv[i], "--flip-y")) flipY = true;
    else { std::fprintf(stderr, "usage: %s [--port N] [--ild out.ild] [--seconds N] [--beyond [--sdk-dll path] [--flip-x] [--flip-y]]\n", argv[0]); return 2; }
  }
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  std::mutex m;
  std::vector<PointFrame> recorded;
  std::atomic<uint64_t> frames{0}, points{0}, lit{0};
  std::atomic<int> zone{0};
  std::unique_ptr<BeyondSink> sink;
  if (beyond) {
    SdkApi api;
    std::string err;
    if (!loadSdkApi(sdkDll, api, err)) { std::fprintf(stderr, "BEYOND: %s\n", err.c_str()); return 1; }
    BeyondSink::Options o;
    o.flipX = flipX; o.flipY = flipY;
    sink = std::make_unique<BeyondSink>(api, o);
    std::printf("BEYOND SDK loaded (%s). Output arming stays in BEYOND; this helper only streams frames to zone images.\n", err.c_str());
  }
  TcpServer server;
  if (!server.start(port, [&](const WireFrame& f) {
        ++frames;
        if (sink) sink->onFrame(f);
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
  std::printf("LaserMap bridge listening on port %u. Waiting for the plugin (Output > Send To Bridge)...\n", unsigned(server.port()));

  const auto t0 = std::chrono::steady_clock::now();
  uint64_t lastFrames = 0;
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
    std::fflush(stdout);
    lastFrames = fr;
    if (seconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= seconds) break;
  }
  server.stop();
  sink.reset();  // blanks and removes its zone images
  if (!ildPath.empty()) {
    std::lock_guard<std::mutex> lk(m);
    if (writeIldaFile(ildPath, recorded)) std::printf("wrote %zu frames to %s\n", recorded.size(), ildPath.c_str());
    else std::fprintf(stderr, "could not write %s\n", ildPath.c_str());
  }
  return 0;
}
