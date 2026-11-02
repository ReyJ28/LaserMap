// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "output.h"
#include "wire.h"

namespace lasermap {

// Plugin side. Streams frames to a LaserMap bridge over TCP from a background thread, so the render thread never
// waits on the network: send() just drops the newest frame into a one-slot mailbox (older unsent frames are
// replaced). Reconnects by itself. While idle it sends an empty keep-alive frame twice a second.
class TcpClientOutput final : public ILaserOutput {
 public:
  TcpClientOutput(std::string host, uint16_t port);
  ~TcpClientOutput() override;
  void send(const PointFrame& frame) noexcept override;
  void setZone(uint8_t zone) { zone_ = zone; }
  void setKpps(uint16_t kpps) { kpps_ = kpps; }
  bool connected() const { return connected_; }
  uint64_t framesSent() const { return framesSent_; }
  const std::string& host() const { return host_; }
  uint16_t port() const { return port_; }

 private:
  void run();
  std::string host_;
  uint16_t port_;
  std::atomic<uint8_t> zone_{1};
  std::atomic<uint16_t> kpps_{30};
  std::atomic<bool> stop_{false}, connected_{false};
  std::atomic<uint64_t> framesSent_{0};
  std::mutex m_;
  std::condition_variable cv_;
  bool havePending_ = false;
  PointFrame pending_;
  std::thread thread_;
};

// Bridge side. Accepts one client at a time and hands every decoded frame to the handler (on the server thread).
class TcpServer {
 public:
  using Handler = std::function<void(const WireFrame&)>;
  ~TcpServer() { stop(); }
  bool start(uint16_t port, Handler handler);  // port 0 = pick a free one; see port()
  void stop();
  uint16_t port() const { return port_; }
  bool clientConnected() const { return client_; }
  uint64_t droppedBytes() const { return dropped_; }

 private:
  void run();
  Handler handler_;
  uint16_t port_ = 0;
  intptr_t listen_ = -1;
  std::atomic<bool> stop_{false}, client_{false};
  std::atomic<uint64_t> dropped_{0};
  std::thread thread_;
};

}  // namespace lasermap
