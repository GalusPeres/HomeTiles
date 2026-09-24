#pragma once

#include <cmath>
#include "src/devices/device.h"
#include "src/types/tile_type.h"

namespace tile_geometry {
inline bool half_step(float value) {
  return std::isfinite(value) && value >= 0 && value * 2 == std::floor(value * 2);
}
inline bool sensor(int type) { return type == TILE_SENSOR || type == TILE_BINARY_SENSOR || type == TILE_ENERGY; }
// Types that may use half-cell sizes (mirrors supportsHalfSize in layout.js).
inline bool half_size(int type) { return sensor(type) || type == TILE_CLOCK; }
inline bool fractional(float value) { return value != std::floor(value); }
// Every type resizes in half steps from 1x1; only half-size types may be half
// a row high (mirrors supportedTileLayout in layout.js).
inline bool supported(int type, float col, float row, float w, float h) {
  if (!half_step(col) || !half_step(row) || !half_step(w) || !half_step(h) ||
      w < 1 || h < 0.5f || col + w > Device::kGridCols || row + h > Device::kGridRows) return false;
  // Settings/Back stay whole: the hidden-Settings snapshot stores uint8 geometry.
  if ((type == TILE_SETTINGS || type == TILE_BACK) &&
      (fractional(col) || fractional(row) || fractional(w) || fractional(h))) return false;
  return h >= 1 || (half_size(type) && h == 0.5f);
}
inline bool compact(int type, float w, float h) {
  return sensor(type) && w >= 1 && h == 0.5f;
}
inline bool compact_clock(int type, float w, float h) {
  return type == TILE_CLOCK && w >= 1 && h == 0.5f;
}
inline int edge(float position, int cell, int gap) {
  return static_cast<int>(std::lround(position * (cell + gap)));
}
inline int extent(float position, float span, int cell, int gap) {
  return edge(position + span, cell, gap) - edge(position, cell, gap) - gap;
}
// V7 quarter-header extension v1: four fractional bits per tile. Zero is legacy.
inline unsigned fraction_bits(float col, float row, float w, float h) {
  return fractional(col) | (fractional(row) << 1) |
         (fractional(w) << 2) | (fractional(h) << 3);
}
}  // namespace tile_geometry
