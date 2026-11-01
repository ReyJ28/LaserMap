// LaserMap — Copyright (c) 2026 ReyJ28. All rights reserved. See LICENSE.
#pragma once
#include <string>
#include <vector>

#include "types.h"

namespace lasermap {

// ILDA image data transfer format, section 5 (2D true colour): a 32-byte big-endian header per frame
// followed by 8-byte records (X, Y as int16, status byte, B, G, R), then a zero-record terminator header.
// Status bit 0x40 = blanked point, 0x80 = last point of the frame. Empty frames are skipped and frames
// longer than 65535 points are truncated.
std::vector<uint8_t> encodeIlda(const std::vector<PointFrame>& frames, const std::string& frameName = "LaserMap",
                                const std::string& company = "LaserMap");
bool writeIldaFile(const std::string& path, const std::vector<PointFrame>& frames);

}  // namespace lasermap
