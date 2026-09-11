#pragma once

#include "src/devices/device.h"

namespace tile_radius {
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kMinimum = 15;
#else
constexpr int kMinimum = 22;
#endif
// Two half-height tiles plus one gap occupy exactly one current tile.
// Keep quarter-pixel geometry until the final rounding to LVGL pixels.
constexpr int maximum(int cell_height, int gap) {
  return (cell_height - gap + 2) / 4;
}
constexpr int kMaximum = maximum(Device::kGridCellH, Device::kGridGap);
static_assert(kMaximum >= kMinimum, "Half-tile radius must cover the current radius");
constexpr int clamp(int value) {
  return value < kMinimum ? kMinimum : value > kMaximum ? kMaximum : value;
}
constexpr int inset(int outer, int distance) {
  return outer > distance ? outer - distance : 0;
}
}  // namespace tile_radius
