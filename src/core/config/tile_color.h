#pragma once

#include <stdint.h>

namespace tile_color {
// Background of tiles without their own color until the user picks another
// global default tile color in Web Admin.
constexpr uint32_t kDefault = 0x2A2A2A;
constexpr uint32_t kRgbMask = 0x00FFFFFFu;
constexpr uint32_t normalize(uint32_t rgb) { return rgb & kRgbMask; }
}  // namespace tile_color
