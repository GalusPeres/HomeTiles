#pragma once

#include <stdint.h>

#include "src/tiles/config/tile_config.h"
#include "src/types/switch/layout.h"

// The upright control bar of Switch and Fan tiles (user 2026-10-26, mockup
// build/design-mockups/switch-vertical): the header on top, the bar below
// over the rest of the tile, filled (or switched, or stepped) from the
// bottom. Web field "bar_orientation" (0 horizontal, 1 upright); the packed
// tile keeps it without a format change:
//   - Switch: sensor_decimals 4..6 (switch_layout::to_stored);
//   - Fan: sensor_decimals 1 (a Fan shows no decimals and stores 0xFF).
namespace bar_orientation {

inline constexpr uint8_t kFanUpright = 1;
inline constexpr uint8_t kFanHorizontal = 0xFF;

inline bool upright(const Tile& tile) {
  if (tile.type == TILE_SWITCH) return switch_layout::upright_stored(tile.sensor_decimals);
  return tile.type == TILE_FAN && tile.sensor_decimals == kFanUpright;
}

// A Fan's stored value for the web field.
inline uint8_t fan_stored(bool upright) { return upright ? kFanUpright : kFanHorizontal; }

}  // namespace bar_orientation
