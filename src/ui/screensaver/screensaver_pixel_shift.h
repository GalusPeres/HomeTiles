#pragma once

#include <stdint.h>

// Screensaver pixel shift: the clock and tiles move together by a few pixels
// now and then, so their edges never stand on the same pixels for hours
// (image retention on LCD panels). The picture behind them stays put. Each
// step moves by unit pixels along the outline of a square with half side
// radius, so a full orbit visits every x and y offset from -radius to
// +radius in steps of unit. Device only: the Web Admin preview shows the
// placed positions. Pure arithmetic, no LVGL
// (tools/tests/ui/test-screensaver-pixel-shift.mjs).
namespace screensaver_pixel_shift {

// Largest offset. Kept below half a grid gap so a tile one gap from the
// screen edge never touches it.
inline constexpr int16_t kMaxRadius = 4;
// No step sooner than this after the last one (a camera picture may arrive
// every few seconds) ...
inline constexpr uint32_t kMinIntervalMs = 60UL * 1000UL;
// ... and one at the latest after this without a new picture.
inline constexpr uint32_t kMaxIntervalMs = 3UL * 60UL * 1000UL;

struct Offset {
  int16_t dx = 0;
  int16_t dy = 0;
};

inline int16_t radius_for_gap(int gap) {
  int16_t r = static_cast<int16_t>(gap / 2);
  if (r > kMaxRadius) r = kMaxRadius;
  return r < 0 ? 0 : r;
}

inline int16_t unit_for(int16_t radius) {
  return radius >= 2 ? static_cast<int16_t>(radius / 2) : 1;
}

// Positions in one orbit (16 for radius 4).
inline uint32_t steps_per_orbit(int16_t radius) {
  if (radius <= 0) return 1;
  const int16_t unit = unit_for(radius);
  return 8U * static_cast<uint32_t>(radius / unit);
}

inline Offset offset_at(uint32_t step, int16_t radius) {
  Offset o;
  if (radius <= 0) return o;
  const int16_t unit = unit_for(radius);
  const uint32_t side = 2U * static_cast<uint32_t>(radius / unit);
  const uint32_t i = step % (4U * side);
  const int16_t t = static_cast<int16_t>((i % side) * unit);
  switch (i / side) {
    case 0: o.dx = static_cast<int16_t>(-radius + t); o.dy = -radius; break;  // top, rightwards
    case 1: o.dx = radius; o.dy = static_cast<int16_t>(-radius + t); break;   // right, down
    case 2: o.dx = static_cast<int16_t>(radius - t); o.dy = radius; break;    // bottom, leftwards
    default: o.dx = -radius; o.dy = static_cast<int16_t>(radius - t); break;  // left, up
  }
  return o;
}

}  // namespace screensaver_pixel_shift
