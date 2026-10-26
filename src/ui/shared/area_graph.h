#pragma once

#include <lvgl.h>

#include <algorithm>
#include <cstdint>

// The area under a Sensor graph's line (an lv_chart with one line series,
// mockup build/design-mockups/sensor-graph-2, issue #131): the tile fills it
// with its circle color, the popup shows a fading picture there. There is no
// polygon mask or extra layer: each line segment is a rect from its lower
// end down plus the triangle between the line and that rect, and the line,
// drawn on top, covers the triangles' slanted edges. The popup paints its
// picture over the whole graph once and covers what lies above the line
// with the same pieces upwards in the card color, so the fade stays one
// picture of a few pixels' width (fill_fade).
namespace area_graph {

// Visits the drawn segments of `series` that reach into `clip`, in screen
// coordinates where lv_chart draws its line (lv_chart_get_point_pos_by_id);
// `last` marks the series' last segment. Missing points
// (LV_CHART_POINT_NONE) leave a gap.
template <typename Fn>
inline void for_each_segment(lv_obj_t* chart, lv_chart_series_t* series, const lv_area_t& clip, Fn&& segment) {
  const uint32_t count = chart && series ? lv_chart_get_point_count(chart) : 0;
  const int32_t* values = count > 1 ? lv_chart_get_series_y_array(chart, series) : nullptr;
  if (!values) return;
  lv_area_t coords;
  lv_obj_get_coords(chart, &coords);
  lv_point_t prev = {0, 0};
  bool have = false;
  for (uint32_t i = 0; i < count; ++i) {
    if (values[i] == LV_CHART_POINT_NONE) {
      have = false;
      continue;
    }
    lv_point_t point;
    lv_chart_get_point_pos_by_id(chart, series, i, &point);
    point.x += coords.x1;
    point.y += coords.y1;
    if (have) {
      if (prev.x > clip.x2) return;
      if (point.x >= clip.x1) segment(prev, point, i + 1 == count);
    }
    prev = point;
    have = true;
  }
}

// The fill below one segment down to `bottom`. Rects never overlap (a
// see-through tile would show the seams twice): each takes its columns up to
// the next segment's first one. The triangle reaches one column past its
// upright edge, which the line covers, so no edge pixel stays half covered.
inline void draw_below(lv_layer_t* layer, const lv_point_t& a, const lv_point_t& b, bool last, int32_t bottom,
                       const lv_draw_rect_dsc_t& rect, lv_draw_triangle_dsc_t& triangle) {
  const int32_t low = std::max(a.y, b.y);
  const int32_t right = last || b.x <= a.x ? b.x : b.x - 1;
  if (low <= bottom) {
    const lv_area_t area = {a.x, low, right, bottom};
    lv_draw_rect(layer, &rect, &area);
  }
  if (a.y == b.y || a.x == b.x) return;
  const bool a_higher = a.y < b.y;
  triangle.p[0].x = a.x - (a_higher ? 1 : 0);
  triangle.p[0].y = a.y;
  triangle.p[1].x = b.x + (a_higher ? 0 : 1);
  triangle.p[1].y = b.y;
  triangle.p[2].x = a_higher ? a.x - 1 : b.x + 1;
  triangle.p[2].y = low;
  lv_draw_triangle(layer, &triangle);
}

// The cover above one segment up to `top` (the popup, on its opaque card).
inline void draw_above(lv_layer_t* layer, const lv_point_t& a, const lv_point_t& b, int32_t top,
                       const lv_draw_rect_dsc_t& rect, lv_draw_triangle_dsc_t& triangle) {
  const int32_t high = std::min(a.y, b.y);
  if (high >= top) {
    const lv_area_t area = {a.x, top, b.x, high};
    lv_draw_rect(layer, &rect, &area);
  }
  if (a.y == b.y || a.x == b.x) return;
  const bool a_higher = a.y < b.y;
  triangle.p[0].x = a.x - (a_higher ? 0 : 1);
  triangle.p[0].y = a.y;
  triangle.p[1].x = b.x + (a_higher ? 1 : 0);
  triangle.p[1].y = b.y;
  triangle.p[2].x = a_higher ? b.x + 1 : a.x - 1;
  triangle.p[2].y = high;
  lv_draw_triangle(layer, &triangle);
}

// The popup's fade (issue #131): `color` over the card `card` from `top_opa`
// in the first row to nothing in the last, `width` x `height` opaque
// ARGB8888 pixels. Ordered dithering (Bayer 8x8) picks for every pixel one of
// the two RGB565 steps around its exact color, so the 16-bit panels show a
// smooth fade instead of bands; each channel is stored as that step's 8-bit
// value, which the panel's conversion keeps unchanged. The pattern repeats
// every 8 pixels, so an 8-pixel-wide strip tiles seamlessly.
inline void fill_fade(uint8_t* pixels, uint32_t stride, int width, int height, uint32_t color, uint32_t card,
                      uint8_t top_opa) {
  static constexpr uint8_t kBayer[8][8] = {
      {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
      {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
      {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21}};
  const float from[3] = {static_cast<float>((card >> 16) & 0xFF), static_cast<float>((card >> 8) & 0xFF),
                         static_cast<float>(card & 0xFF)};
  const float to[3] = {static_cast<float>((color >> 16) & 0xFF), static_cast<float>((color >> 8) & 0xFF),
                       static_cast<float>(color & 0xFF)};
  // Steps per channel: 5, 6 and 5 bits.
  static constexpr int kMax[3] = {31, 63, 31};
  for (int y = 0; y < height; ++y) {
    const float alpha = top_opa / 255.0f * (height > 1 ? 1.0f - static_cast<float>(y) / (height - 1) : 1.0f);
    float level[3];
    for (int c = 0; c < 3; ++c) level[c] = (from[c] + (to[c] - from[c]) * alpha) * kMax[c] / 255.0f;
    lv_color32_t* row = reinterpret_cast<lv_color32_t*>(pixels + y * stride);
    for (int x = 0; x < width; ++x) {
      const float threshold = (kBayer[y & 7][x & 7] + 0.5f) / 64.0f;
      uint8_t out[3];
      for (int c = 0; c < 3; ++c) {
        const int step = std::min(kMax[c], static_cast<int>(level[c] + threshold));
        // The step's 8-bit value with its top bits repeated, like LVGL's
        // RGB565 to 8-bit expansion.
        out[c] = c == 1 ? static_cast<uint8_t>((step << 2) | (step >> 4))
                        : static_cast<uint8_t>((step << 3) | (step >> 2));
      }
      row[x] = lv_color32_make(out[0], out[1], out[2], 0xFF);
    }
  }
}

}  // namespace area_graph
