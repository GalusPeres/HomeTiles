#pragma once

#include <stdint.h>

namespace tile_color {
// Background of tiles without their own color until the user picks another
// global default tile color in Web Admin.
constexpr uint32_t kDefault = 0x222222;
// The earlier built-in default, still stored by older tiles and descriptors.
constexpr uint32_t kLegacyDefault = 0x2A2A2A;
constexpr uint32_t kRgbMask = 0x00FFFFFFu;
constexpr uint32_t normalize(uint32_t rgb) { return rgb & kRgbMask; }
// Built-in default greys follow the global default tile color.
constexpr bool isDefaultGrey(uint32_t rgb) {
  return normalize(rgb) == kDefault || normalize(rgb) == kLegacyDefault;
}
}  // namespace tile_color
