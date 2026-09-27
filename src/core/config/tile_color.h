#pragma once

#include <stdint.h>

namespace tile_color {
// Background of tiles without their own color until the user picks another
// global default tile color in Web Admin.
constexpr uint32_t kDefault = 0x1A1A1A;
// Earlier built-in defaults, still stored by older tiles and descriptors:
// 0x2A2A2A up to v0.6.x, 0x222222 in the v0.7.0 test builds.
constexpr uint32_t kLegacyDefault = 0x2A2A2A;
constexpr uint32_t kPreviousDefault = 0x222222;
constexpr uint32_t kRgbMask = 0x00FFFFFFu;
constexpr uint32_t normalize(uint32_t rgb) { return rgb & kRgbMask; }
// Built-in default greys follow the global default tile color.
constexpr bool isDefaultGrey(uint32_t rgb) {
  return normalize(rgb) == kDefault || normalize(rgb) == kLegacyDefault ||
         normalize(rgb) == kPreviousDefault;
}
}  // namespace tile_color
