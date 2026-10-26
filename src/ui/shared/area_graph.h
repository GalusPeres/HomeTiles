#pragma once

#include <lvgl.h>

#include <algorithm>
#include <cstdint>

// The area under a Sensor graph's line (an lv_chart with one line series,
// mockup build/design-mockups/sensor-graph-2, issue #131): the tile fills it
// with its circle color, the popup shows the icon color fading to nothing
// there. The area is computed once into a picture (render_fade, render_mask)
// and drawn as one image: built from a rect and a triangle per segment it
// took several hundred LVGL draw tasks, about 150 ms per popup frame on the
// P4 (b558 log). It is computed again only when the line, the colors or the
// size change (area_key).
namespace area_graph {

// The line's top in every column of the chart's content (rows from the
// content's top, as lv_chart draws its line: linear between the points);
// -1 where a gap (LV_CHART_POINT_NONE) leaves no line. False without two
// points in a row.
inline bool column_tops(lv_obj_t* chart, lv_chart_series_t* series, int16_t* tops, int32_t width) {
  std::fill(tops, tops + width, static_cast<int16_t>(-1));
  const uint32_t count = chart && series ? lv_chart_get_point_count(chart) : 0;
  const int32_t* values = count > 1 ? lv_chart_get_series_y_array(chart, series) : nullptr;
  if (!values) return false;
  lv_area_t coords, content;
  lv_obj_get_coords(chart, &coords);
  lv_obj_get_content_coords(chart, &content);
  const int32_t dx = coords.x1 - content.x1, dy = coords.y1 - content.y1;
  bool any = false;
  lv_point_t prev = {0, 0};
  bool have = false;
  for (uint32_t i = 0; i < count; ++i) {
    if (values[i] == LV_CHART_POINT_NONE) {
      have = false;
      continue;
    }
    lv_point_t point;
    lv_chart_get_point_pos_by_id(chart, series, i, &point);
    point.x += dx;
    point.y += dy;
    if (have) {
      const int32_t from = std::max<int32_t>(0, prev.x), to = std::min<int32_t>(width - 1, point.x);
      for (int32_t x = from; x <= to; ++x) {
        const int32_t y = point.x == prev.x ? std::min(prev.y, point.y)
                                            : prev.y + (point.y - prev.y) * (x - prev.x) / (point.x - prev.x);
        const int16_t top = static_cast<int16_t>(std::max<int32_t>(0, y));
        tops[x] = tops[x] < 0 ? top : std::min(tops[x], top);
      }
      any = true;
    }
    prev = point;
    have = true;
  }
  return any;
}

// A key of what the picture shows: the tops, its size and two colors.
inline uint32_t area_key(const int16_t* tops, int32_t width, int32_t height, uint32_t a, uint32_t b) {
  uint32_t hash = 2166136261u;
  auto mix = [&hash](uint32_t value) { hash = (hash ^ value) * 16777619u; };
  for (int32_t x = 0; x < width; ++x) mix(static_cast<uint16_t>(tops[x]));
  mix(static_cast<uint32_t>(width));
  mix(static_cast<uint32_t>(height));
  mix(a);
  mix(b);
  return hash;
}

// The 16-bit value LVGL shows for an 8-bit color (lv_color_to_u16).
inline uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb & 0xFF) >> 3));
}

// The popup's picture (issue #131), RGB565, `width` x `height`: below the
// line `color` over the card `card` fading from `top_opa` in the first row
// to nothing in the last, above the line and in gaps the card. Ordered
// dithering (Bayer 8x8) picks for every pixel one of the two RGB565 steps
// around its exact color, so the 16-bit panels show a smooth fade instead of
// bands.
inline void render_fade(uint16_t* pixels, uint32_t stride_px, int32_t width, int32_t height, const int16_t* tops,
                        uint32_t color, uint32_t card, uint8_t top_opa) {
  static constexpr uint8_t kBayer[8][8] = {
      {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
      {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
      {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21}};
  // Steps per channel: 5, 6 and 5 bits. The fade starts from the card's own
  // step (the card around it shows that step), so its last row is the card.
  static constexpr int kMax[3] = {31, 63, 31};
  const uint16_t plain = rgb565(card);
  const float from[3] = {static_cast<float>(plain >> 11), static_cast<float>((plain >> 5) & 0x3F),
                         static_cast<float>(plain & 0x1F)};
  const float to[3] = {((color >> 16) & 0xFF) * 31 / 255.0f, ((color >> 8) & 0xFF) * 63 / 255.0f,
                       (color & 0xFF) * 31 / 255.0f};
  for (int32_t y = 0; y < height; ++y) {
    const float alpha = top_opa / 255.0f * (height > 1 ? 1.0f - static_cast<float>(y) / (height - 1) : 1.0f);
    float level[3];
    for (int c = 0; c < 3; ++c) level[c] = from[c] + (to[c] - from[c]) * alpha;
    // The row's eight dithered colors.
    uint16_t fade[8];
    for (int x = 0; x < 8; ++x) {
      const float threshold = (kBayer[y & 7][x] + 0.5f) / 64.0f;
      int step[3];
      for (int c = 0; c < 3; ++c) step[c] = std::min(kMax[c], static_cast<int>(level[c] + threshold));
      fade[x] = static_cast<uint16_t>((step[0] << 11) | (step[1] << 5) | step[2]);
    }
    uint16_t* row = pixels + y * stride_px;
    for (int32_t x = 0; x < width; ++x) row[x] = tops[x] >= 0 && y >= tops[x] ? fade[x & 7] : plain;
  }
}

// The tile's area as an A8 mask, `width` x `height`: opaque below the line,
// transparent above it and in gaps; drawn recolored in the circle's current
// color, so a pressed or tinted card needs no new mask.
inline void render_mask(uint8_t* pixels, uint32_t stride, int32_t width, int32_t height, const int16_t* tops) {
  for (int32_t y = 0; y < height; ++y) {
    uint8_t* row = pixels + y * stride;
    for (int32_t x = 0; x < width; ++x) row[x] = tops[x] >= 0 && y >= tops[x] ? 0xFF : 0x00;
  }
}

}  // namespace area_graph
