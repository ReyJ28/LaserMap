// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <cstdint>
#include <vector>

#include "types.h"

namespace lasermap {

// LaserMap bridge wire format (little endian), one frame per message, streamed over TCP:
//   header (16 bytes): 'L' 'M' 'B' 'R', version u8 (=1), zone u8 (1-based), kpps u16, seq u32, count u32
//   count x point (8 bytes): x i16, y i16 (y up, +-32767), r u8, g u8, b u8, flags u8 (bit0 = blanked)
// An empty frame (count 0) means "no light" and doubles as a keep-alive.
struct WireFrame {
  uint32_t seq = 0;
  uint8_t zone = 1;
  uint16_t kpps = 30;
  PointFrame pts;
};

constexpr size_t kWireHeaderSize = 16;
constexpr size_t kWirePointSize = 8;
constexpr uint32_t kWireMaxPoints = 20000;

std::vector<uint8_t> encodeWire(const WireFrame& f);

// Incremental decoder for a byte stream: feed it whatever arrives, pull out complete frames.
// Garbage between frames is skipped byte by byte (resynchronises on the next valid header).
class WireDecoder {
 public:
  void feed(const uint8_t* data, size_t n);
  bool next(WireFrame& out);
  size_t droppedBytes() const { return dropped_; }

 private:
  std::vector<uint8_t> buf_;
  size_t dropped_ = 0;
};

}  // namespace lasermap
