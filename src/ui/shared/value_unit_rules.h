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

// Where a finished text's unit starts ("21,5 °C", "Open · 58 %", "75%"): the
// last word, or the letters right after the last digit, when it holds no
// digit, follows a digit (a space between allowed) and the text is one line;
// -1 for none ("-- °C", "Off", two lines). `value_end` gets where the number
// ends (before the space).
inline int split_unit(const char* text, int length, int& value_end) {
  value_end = length;
  if (!text || length <= 0) return -1;
  for (int i = 0; i < length; ++i) {
    if (text[i] == '\n') return -1;
  }
  int start = length;
  while (start > 0 && text[start - 1] != ' ' && !(text[start - 1] >= '0' && text[start - 1] <= '9')) --start;
  if (start == length || length - start > 12) return -1;
  int end = start;
  while (end > 0 && text[end - 1] == ' ') --end;
  if (end == 0 || end < start - 1 || !(text[end - 1] >= '0' && text[end - 1] <= '9')) return -1;
  value_end = end;
  return start;
}

}  // namespace value_unit
