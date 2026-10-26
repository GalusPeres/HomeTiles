#pragma once

#include <stdint.h>

// Values with a unit everywhere (user 2026-10-26: "so wie bei weather"): the
// unit a font step smaller than its number, as the weather forecast shows it
// (20 -> 16), after a thin gap, its top level with the number's digits. No
// LVGL here: host tests run these (test-value-unit.mjs).
namespace value_unit {

// The unit's size for a number of `value_px`: the largest of `sizes`
// (ascending) at most 80 % of it and smaller than it; the smallest when none
// is; 0 without sizes.
inline uint8_t unit_px(uint8_t value_px, const uint8_t* sizes, uint8_t count) {
  if (!sizes || !count) return 0;
  uint8_t best = sizes[0];
  for (uint8_t i = 0; i < count; ++i) {
    if (sizes[i] < value_px && sizes[i] * 10 <= value_px * 8) best = sizes[i];
  }
  return best;
}

// The gap between number and unit: about a sixth of the unit's size, a thin
// space.
inline int32_t gap_px(uint8_t unit_px) { return (unit_px + 3) / 6; }

// Where the unit's line starts below the number's line so their digits'
// tops meet: each `digit_top` is a digit's top below its own line's top.
inline int32_t unit_y(int32_t value_digit_top, int32_t unit_digit_top) { return value_digit_top - unit_digit_top; }

}  // namespace value_unit
