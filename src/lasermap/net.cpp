// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#include "net.h"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t kBad = INVALID_SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
static bool wouldBlock() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
static void setNonBlocking(sock_t s, bool nb) { u_long v = nb ? 1 : 0; ioctlsocket(s, FIONBIO, &v); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static const sock_t kBad = -1;
static void closeSock(sock_t s) { ::close(s); }
static bool wouldBlock() { return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN; }
static void setNonBlocking(sock_t s, bool nb) {
  int fl = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, nb ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
}
#endif

namespace lasermap {

namespace {
void netInit() {
#ifdef _WIN32
  static bool done = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
  (void)done;
#endif
}

// Waits until the socket is readable (or writable); false on timeout/error.
bool waitFor(sock_t s, bool write, int ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  timeval tv{ms / 1000, (ms % 1000) * 1000};
  int r = select(int(s) + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
  return r > 0;
}

sock_t connectTo(const std::string& host, uint16_t port, int timeoutMs) {
  netInit();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return kBad;
  sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (s == kBad) { freeaddrinfo(res); return kBad; }
  setNonBlocking(s, true);
  int r = connect(s, res->ai_addr, int(res->ai_addrlen));
  freeaddrinfo(res);
  bool ok = (r == 0);
  if (!ok && wouldBlock() && waitFor(s, true, timeoutMs)) {
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
    ok = (err == 0);
  }
  if (!ok) { closeSock(s); return kBad; }
  setNonBlocking(s, false);
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
  return s;
}

bool sendAll(sock_t s, const uint8_t* d, size_t n) {
  while (n > 0) {
#ifdef _WIN32
    int w = send(s, reinterpret_cast<const char*>(d), int(n), 0);
#elif defined(MSG_NOSIGNAL)
    ssize_t w = send(s, d, n, MSG_NOSIGNAL);
#else
    ssize_t w = send(s, d, n, 0);
#endif
    if (w <= 0) return false;
    d += w;
    n -= size_t(w);
  }
  return true;
}
}  // namespace

// ---------------- client ----------------

TcpClientOutput::TcpClientOutput(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {
  thread_ = std::thread([this] { run(); });
}

TcpClientOutput::~TcpClientOutput() {
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void TcpClientOutput::send(const PointFrame& frame) noexcept {
  try {
    {
      std::lock_guard<std::mutex> lk(m_);
      pending_ = frame;
      havePending_ = true;
    }
    cv_.notify_one();
  } catch (...) {
  }
}

void TcpClientOutput::run() {
  uint32_t seq = 0;
  while (!stop_) {
    sock_t s = connectTo(host_, port_, 500);
    if (s == kBad) {
      connected_ = false;
      for (int i = 0; i < 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    connected_ = true;
    while (!stop_) {
      WireFrame f;
      {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::milliseconds(500), [this] { return havePending_ || stop_; });
        if (stop_) break;
        if (havePending_) f.pts = std::move(pending_);  // else: keep-alive (empty frame)
        havePending_ = false;
      }
      f.zone = zone_;
      f.kpps = kpps_;
      f.seq = seq++;
      const auto bytes = encodeWire(f);
      if (!sendAll(s, bytes.data(), bytes.size())) break;
      ++framesSent_;
    }
    connected_ = false;
    closeSock(s);
  }
}

// ---------------- server ----------------

bool TcpServer::start(uint16_t port, Handler handler) {
  netInit();
  handler_ = std::move(handler);
  sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == kBad) return false;
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(s, 1) != 0) { closeSock(s); return false; }
  socklen_t len = sizeof a;
  getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
  port_ = ntohs(a.sin_port);
  listen_ = intptr_t(s);
  stop_ = false;
  thread_ = std::thread([this] { run(); });
  return true;
}

void TcpServer::stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
  if (listen_ != -1) { closeSock(sock_t(listen_)); listen_ = -1; }
}

void TcpServer::run() {
  const sock_t ls = sock_t(listen_);
  while (!stop_) {
    if (!waitFor(ls, false, 100)) continue;
    sock_t c = accept(ls, nullptr, nullptr);
    if (c == kBad) continue;
    client_ = true;
    WireDecoder dec;
    uint8_t buf[16384];
    while (!stop_) {
      if (!waitFor(c, false, 100)) continue;
      int n = int(recv(c, reinterpret_cast<char*>(buf), sizeof buf, 0));
      if (n <= 0) break;  // closed or error
      dec.feed(buf, size_t(n));
      WireFrame f;
      while (dec.next(f)) if (handler_) handler_(f);
      dropped_ = dec.droppedBytes();
    }
    client_ = false;
    closeSock(c);
  }
}

}  // namespace lasermap
