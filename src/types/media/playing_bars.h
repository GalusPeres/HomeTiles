#pragma once

#include <lvgl.h>
#include <stdint.h>

#include <algorithm>

// The playing bars of the Media popup and the media tile (user 2026-10-23:
// the tile shows them over its title too): four round-ended bars whose
// height moves while the player plays and stands while it pauses. One object
// draws all four from its DRAW_MAIN event, so a frame redraws one small area.
namespace media_bars {

// A bar's height as a share of the object's height at `now` (ms).
inline float level(uint32_t now, int index, bool playing) {
  static const uint16_t kPeriods[4] = {900, 600, 1100, 750};
  static const uint16_t kOffsets[4] = {0, 300, 700, 150};
  static const uint8_t kStill[4] = {35, 70, 50, 85};
  if (!playing) return kStill[index] / 100.0f;
  const uint32_t period = kPeriods[index];
  const uint32_t t = (now + kOffsets[index]) % (2 * period);
  float x = t < period ? static_cast<float>(t) / period : 2.0f - static_cast<float>(t) / period;
  x = x * x * (3.0f - 2.0f * x);
  return 0.25f + 0.75f * x;
}

// The four bars in `area`, bottom-aligned, `bar_width` wide with `bar_gap`
// between them.
inline void draw(lv_layer_t* layer, const lv_area_t& area, int bar_width, int bar_gap, lv_color_t color,
                 bool playing) {
  const int height = lv_area_get_height(&area);
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.radius = bar_width / 2;
  const uint32_t now = lv_tick_get();
  for (int i = 0; i < 4; ++i) {
    const int bar = std::max(bar_width, static_cast<int>(height * level(now, i, playing) + 0.5f));
    lv_area_t a;
    a.x1 = area.x1 + i * (bar_width + bar_gap);
    a.x2 = a.x1 + bar_width - 1;
    a.y2 = area.y2;
    a.y1 = area.y2 - bar + 1;
    lv_draw_rect(layer, &dsc, &a);
  }
}

}  // namespace media_bars
