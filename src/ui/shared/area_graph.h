#pragma once

#include <lvgl.h>
#include <lvgl_private.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <initializer_list>

// The pictures of a Sensor graph (an lv_chart with one line series, mockup
// build/design-mockups/sensor-graph-2, issue #131): the area under the line
// (the tile in its circle's color, the popup the icon color fading to
// nothing), the line itself and the popup's dashed max/min (and zero)
// lines. They are computed once from the line and drawn as images, computed
// again only when the line, the colors or the size change (picture_key).
// LVGL's own line costs about 90 ms per popup frame on the P4 (a 4 px
// anti-aliased polyline of 288 points, b559 log; 1.1 ms against 0.09 ms for
// the area picture on the host), the area built from a rect and a triangle
// per segment about 150 ms (b558).
namespace area_graph {

// A point of the line in the chart's coordinates (lv_chart's own
// positions); a gap (LV_CHART_POINT_NONE) has y == kGap.
struct Point {
  int16_t x;
  int16_t y;
};
inline constexpr int16_t kGap = INT16_MIN;

// The series' points relative to the chart's top left corner (padding
// included), up to `capacity`; the count, 0 without two points in a row.
inline int32_t chart_points(lv_obj_t* chart, lv_chart_series_t* series, Point* out, int32_t capacity) {
  const uint32_t count = chart && series ? lv_chart_get_point_count(chart) : 0;
  const int32_t* values = count > 1 ? lv_chart_get_series_y_array(chart, series) : nullptr;
  if (!values || static_cast<int32_t>(count) > capacity) return 0;
  bool pair = false;
  for (uint32_t i = 0; i < count; ++i) {
    // A gap keeps its column, so the points stay ordered left to right.
    lv_point_t point;
    lv_chart_get_point_pos_by_id(chart, series, i, &point);
    const bool gap = values[i] == LV_CHART_POINT_NONE;
    out[i] = {static_cast<int16_t>(point.x), gap ? kGap : static_cast<int16_t>(point.y)};
    pair = pair || (!gap && i > 0 && out[i - 1].y != kGap);
  }
  return pair ? static_cast<int32_t>(count) : 0;
}

// A key of what a picture shows: the points, its size and colors.
inline uint32_t picture_key(const Point* points, int32_t count, std::initializer_list<uint32_t> extra) {
  uint32_t hash = 2166136261u;
  auto mix = [&hash](uint32_t value) { hash = (hash ^ value) * 16777619u; };
  for (int32_t i = 0; i < count; ++i) mix((static_cast<uint32_t>(static_cast<uint16_t>(points[i].x)) << 16) |
                                          static_cast<uint16_t>(points[i].y));
  for (const uint32_t value : extra) mix(value);
  return hash;
}

// The line's top in every column of a picture `width` wide (rows of the
// chart, linear between the points as lv_chart draws its line); -1 where a
// gap leaves no line.
inline void column_tops(const Point* points, int32_t count, int16_t* tops, int32_t width) {
  std::fill(tops, tops + width, static_cast<int16_t>(-1));
  for (int32_t i = 1; i < count; ++i) {
    const Point a = points[i - 1], b = points[i];
    if (a.y == kGap || b.y == kGap) continue;
    const int32_t from = std::max<int32_t>(0, a.x), to = std::min<int32_t>(width - 1, b.x);
    for (int32_t x = from; x <= to; ++x) {
      const int32_t y = b.x == a.x ? std::min(a.y, b.y) : a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
      const int16_t top = static_cast<int16_t>(std::max<int32_t>(0, y));
      tops[x] = tops[x] < 0 ? top : std::min(tops[x], top);
    }
  }
}

// The rows a tile's masks use in a picture `height` high: the line's
// (its points' rows and the line's reach around them) and the area's (from
// the line's highest point down). Drawing only them keeps a flat line cheap.
struct Rows {
  int32_t line_top = 0;
  int32_t line_rows = 0;
  int32_t area_top = 0;
};
inline Rows rows_used(const Point* points, int32_t count, int32_t height, float width_px) {
  int32_t top = INT32_MAX, bottom = INT32_MIN;
  for (int32_t i = 0; i < count; ++i) {
    if (points[i].y == kGap) continue;
    top = std::min<int32_t>(top, points[i].y);
    bottom = std::max<int32_t>(bottom, points[i].y);
  }
  Rows rows;
  if (top > bottom) return rows;
  const int32_t reach = static_cast<int32_t>(std::ceil(width_px * 0.5f + 1.0f));
  rows.line_top = std::max<int32_t>(0, top - reach);
  rows.line_rows = std::min<int32_t>(height, bottom + reach + 1) - rows.line_top;
  rows.area_top = std::max<int32_t>(0, std::min<int32_t>(height - 1, top));
  return rows;
}

// The 16-bit value LVGL shows for an 8-bit color (lv_color_to_u16).
inline uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb & 0xFF) >> 3));
}
inline uint32_t rgb888(uint16_t pixel) {
  const uint32_t r = pixel >> 11, g = (pixel >> 5) & 0x3F, b = pixel & 0x1F;
  return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
// `top` over `pixel` with `opa` (0..255).
inline uint16_t blend565(uint16_t pixel, uint32_t top, uint32_t opa) {
  const uint32_t under = rgb888(pixel);
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const uint32_t a = (top >> shift) & 0xFF, b = (under >> shift) & 0xFF;
    out |= ((a * opa + b * (255 - opa) + 127) / 255) << shift;
  }
  return rgb565(out);
}

// The popup's area picture, RGB565, `width` x `height`: below the line
// `color` over the card `card` fading from `top_opa` at chart row
// `fade_top` to nothing at row `fade_bottom`, above the line and in gaps the
// card. Ordered dithering (Bayer 8x8) picks for every pixel one of the two
// RGB565 steps around its exact color, so the 16-bit panels show a smooth
// fade instead of bands; the fade starts from the card's own step (the card
// around it shows that step).
inline void render_fade(uint16_t* pixels, uint32_t stride_px, int32_t width, int32_t height, const int16_t* tops,
                        int32_t fade_top, int32_t fade_bottom, uint32_t color, uint32_t card, uint8_t top_opa) {
  static constexpr uint8_t kBayer[8][8] = {
      {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
      {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
      {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21}};
  // Steps per channel: 5, 6 and 5 bits.
  static constexpr int kMax[3] = {31, 63, 31};
  const uint16_t plain = rgb565(card);
  const float from[3] = {static_cast<float>(plain >> 11), static_cast<float>((plain >> 5) & 0x3F),
                         static_cast<float>(plain & 0x1F)};
  const float to[3] = {((color >> 16) & 0xFF) * 31 / 255.0f, ((color >> 8) & 0xFF) * 63 / 255.0f,
                       (color & 0xFF) * 31 / 255.0f};
  const float span = static_cast<float>(std::max<int32_t>(1, fade_bottom - fade_top));
  for (int32_t y = 0; y < height; ++y) {
    const float t = std::min(1.0f, std::max(0.0f, (y - fade_top) / span));
    const float alpha = top_opa / 255.0f * (1.0f - t);
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

// The line `width_px` wide through the points, anti-aliased with round
// joins, inside a picture `width` x `height` (its ends cut flush at the
// picture's sides): `plot(x, y, coverage 1..255)` once per covered pixel,
// with the nearest segment's distance, so joints are never covered twice.
template <typename Plot>
inline void trace_line(const Point* points, int32_t count, int32_t width, int32_t height, float width_px, Plot&& plot) {
  const float half = width_px * 0.5f;
  const int32_t reach = static_cast<int32_t>(std::ceil(half + 1.0f));
  int32_t first = 1;
  for (int32_t x = 0; x < width; ++x) {
    // Segments reaching this column: they run left to right.
    while (first < count && std::max(points[first - 1].x, points[first].x) < x - reach) ++first;
    int32_t top = INT32_MAX, bottom = INT32_MIN;
    int32_t last = first;
    for (int32_t i = first; i < count && std::min(points[i - 1].x, points[i].x) <= x + reach; ++i) {
      last = i + 1;
      if (points[i - 1].y == kGap || points[i].y == kGap) continue;
      top = std::min<int32_t>(top, std::min(points[i - 1].y, points[i].y) - reach);
      bottom = std::max<int32_t>(bottom, std::max(points[i - 1].y, points[i].y) + reach);
    }
    if (top > bottom) continue;
    top = std::max<int32_t>(0, top);
    bottom = std::min<int32_t>(height - 1, bottom);
    for (int32_t y = top; y <= bottom; ++y) {
      float nearest = 1e9f;
      for (int32_t i = first; i < last; ++i) {
        const Point a = points[i - 1], b = points[i];
        if (a.y == kGap || b.y == kGap) continue;
        const float dx = static_cast<float>(b.x - a.x), dy = static_cast<float>(b.y - a.y);
        const float px = static_cast<float>(x - a.x), py = static_cast<float>(y - a.y);
        const float length = dx * dx + dy * dy;
        const float t = length > 0 ? std::min(1.0f, std::max(0.0f, (px * dx + py * dy) / length)) : 0.0f;
        const float ex = px - t * dx, ey = py - t * dy;
        nearest = std::min(nearest, ex * ex + ey * ey);
      }
      const float coverage = half + 0.5f - std::sqrt(nearest);
      if (coverage <= 0.0f) continue;
      plot(x, y, coverage >= 1.0f ? 255u : static_cast<uint32_t>(coverage * 255.0f + 0.5f));
    }
  }
}

// The line into an RGB565 picture in `color`.
inline void draw_line(uint16_t* pixels, uint32_t stride_px, int32_t width, int32_t height, const Point* points,
                      int32_t count, float width_px, uint32_t color) {
  const uint16_t solid = rgb565(color);
  trace_line(points, count, width, height, width_px, [&](int32_t x, int32_t y, uint32_t coverage) {
    uint16_t& pixel = pixels[y * stride_px + x];
    pixel = coverage >= 255 ? solid : blend565(pixel, color, coverage);
  });
}

// The line as an A8 mask (cleared first), drawn recolored in the icon's
// current color.
inline void line_mask(uint8_t* pixels, uint32_t stride, int32_t width, int32_t height, const Point* points,
                      int32_t count, float width_px) {
  for (int32_t y = 0; y < height; ++y) std::fill(pixels + y * stride, pixels + y * stride + width, 0);
  trace_line(points, count, width, height, width_px, [&](int32_t x, int32_t y, uint32_t coverage) {
    pixels[y * stride + x] = static_cast<uint8_t>(coverage);
  });
}

// A dashed line `rows` high from row `top`: `dash` pixels on, `dash` off,
// `color` with `opa` over the picture.
inline void draw_dashes(uint16_t* pixels, uint32_t stride_px, int32_t width, int32_t height, int32_t top, int32_t rows,
                        int32_t dash, uint32_t color, uint8_t opa) {
  for (int32_t y = std::max<int32_t>(0, top); y < std::min<int32_t>(height, top + rows); ++y) {
    for (int32_t x = 0; x < width; ++x) {
      if ((x / std::max<int32_t>(1, dash)) % 2) continue;
      uint16_t& pixel = pixels[y * stride_px + x];
      pixel = blend565(pixel, color, opa);
    }
  }
}

// The row of the value zero in a picture over the chart's box, as lv_chart
// maps its values (`top`: the content's top row, the maximum; `bottom` one
// row below the content, the minimum): -1 unless the y range runs from below
// zero to above it and the row keeps `min_gap` from both ends.
inline int32_t zero_row(lv_obj_t* chart, int32_t top, int32_t bottom, int32_t min_gap) {
  const auto* data = reinterpret_cast<const lv_chart_t*>(chart);
  const int32_t low = data->ymin[LV_CHART_AXIS_PRIMARY_Y], high = data->ymax[LV_CHART_AXIS_PRIMARY_Y];
  if (low >= 0 || high <= 0) return -1;
  const int32_t row = bottom - lv_map(0, low, high, 0, bottom - top);
  return row - top >= min_gap && bottom - row >= min_gap ? row : -1;
}

}  // namespace area_graph
