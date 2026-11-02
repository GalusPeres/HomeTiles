#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include <lvgl_private.h>

#include "src/core/display/display_flush_gate.h"
#include "src/tiles/runtime/tile_glass.h"

// Frosted glass behind see-through screensaver tiles (user 2026-11-02, test
// builds only for now). The slide's picture is averaged down to a quarter of
// its size per axis with 8 bits per channel (V2 320x200 = 188 KB, S3 120x120
// = 42 KB) and blurred there, once per slide. A see-through tile card writes
// its part straight into LVGL's draw buffer before its own translucent fill
// tints it: scaled back up bilinearly row by row, with the 8x8 Bayer
// dithering of the area graphs (area_graph.h) so the soft gradients show no
// RGB565 bands, and its rounded corners anti-aliased against the picture.
//
// The first version drew the copy through LVGL's scaled image draw: about
// 100 ms more per full V2 frame and visible bands in the gradients (user
// 2026-11-02, b646). The pixels are written like the clock's kept pixels
// (clock_pixel_cache.h): only on a settled RGB565 layer at full opacity;
// otherwise the card shows its plain tint.
#if (defined(HOMETILES_ISSUE38_BETA) || defined(HOMETILES_TEST_BETA)) && !defined(HOMETILES_FROSTED_TILES)
#define HOMETILES_FROSTED_TILES 1
#endif

namespace frosted_backdrop {

constexpr int kShrink = 4;      // the copy's size: 1/4 per axis
constexpr int kBoxRadius = 5;   // per box pass at the copy's size (about 20 px full size)
constexpr int kPasses = 3;      // three box passes come close to a Gaussian

inline int shrunk(int full) { return (full + kShrink - 1) / kShrink; }

// The blurred copy: 8-bit R, G, B per pixel, w x h.
struct Copy {
  uint8_t* rgb = nullptr;
  int w = 0;
  int h = 0;
};

inline size_t copy_bytes(int w, int h) { return static_cast<size_t>(w) * h * 3; }

// The P4 divides slowly: one division per channel and pixel made the blur
// take 290 ms per slide and the frosted cards 230 ms more per frame (V2
// b647). Every division below is a multiplication by a reciprocal, exact
// for the values that occur (test-frosted-backdrop-lvgl).
constexpr uint32_t kWindow = 2 * kBoxRadius + 1;
constexpr uint32_t kWindowRecip = (65536 + kWindow - 1) / kWindow;

// A window's rounded mean of 8-bit values.
inline uint8_t window_mean(uint32_t sum) {
  return static_cast<uint8_t>(((sum + kWindow / 2) * kWindowRecip) >> 16);
}

// ceil(2^32 / d): num / d = (num * reciprocal(d)) >> 32 while num * d stays
// far below 2^32.
inline uint64_t reciprocal(uint32_t d) { return ((static_cast<uint64_t>(1) << 32) + d - 1) / d; }
inline uint32_t divide(uint32_t num, uint64_t recip) { return static_cast<uint32_t>((num * recip) >> 32); }

// One horizontal box pass over every row of the 8-bit RGB plane, edges clamped.
inline void box_rows(const uint8_t* in, uint8_t* out, int w, int h) {
  auto clamp = [w](int i) { return 3 * (i < 0 ? 0 : (i >= w ? w - 1 : i)); };
  for (int y = 0; y < h; ++y) {
    const uint8_t* src = in + static_cast<size_t>(y) * w * 3;
    uint8_t* dst = out + static_cast<size_t>(y) * w * 3;
    uint32_t s0 = 0, s1 = 0, s2 = 0;
    for (int i = -kBoxRadius; i <= kBoxRadius; ++i) {
      const int k = clamp(i);
      s0 += src[k];
      s1 += src[k + 1];
      s2 += src[k + 2];
    }
    for (int x = 0; x < w; ++x) {
      dst[3 * x] = window_mean(s0);
      dst[3 * x + 1] = window_mean(s1);
      dst[3 * x + 2] = window_mean(s2);
      const int leave = clamp(x - kBoxRadius);
      const int enter = clamp(x + kBoxRadius + 1);
      s0 += src[enter] - src[leave];
      s1 += src[enter + 1] - src[leave + 1];
      s2 += src[enter + 2] - src[leave + 2];
    }
  }
}

// One vertical box pass, edges clamped, walked row by row (PSRAM reads long
// runs) with every column's running sum in `sums` (3 * w words).
inline void box_columns(const uint8_t* in, uint8_t* out, int w, int h, uint32_t* sums) {
  const size_t row_bytes = static_cast<size_t>(w) * 3;
  auto row = [&](int y) { return in + static_cast<size_t>(y < 0 ? 0 : (y >= h ? h - 1 : y)) * row_bytes; };
  memset(sums, 0, row_bytes * sizeof(uint32_t));
  for (int i = -kBoxRadius; i <= kBoxRadius; ++i) {
    const uint8_t* r = row(i);
    for (size_t k = 0; k < row_bytes; ++k) sums[k] += r[k];
  }
  for (int y = 0; y < h; ++y) {
    uint8_t* dst = out + static_cast<size_t>(y) * row_bytes;
    const uint8_t* leave = row(y - kBoxRadius);
    const uint8_t* enter = row(y + kBoxRadius + 1);
    for (size_t k = 0; k < row_bytes; ++k) {
      dst[k] = window_mean(sums[k]);
      sums[k] += enter[k] - leave[k];
    }
  }
}

// One picture row's RGB565 channels added to the blocks' sums.
template <bool kSwapped>
inline void add_row(const uint16_t* row, int w, uint32_t* sums) {
  for (int x = 0; x < w; ++x) {
    const uint16_t raw = row[x];
    const uint16_t p = kSwapped ? static_cast<uint16_t>((raw << 8) | (raw >> 8)) : raw;
    uint32_t* s = sums + 3 * (x / kShrink);
    s[0] += p >> 11;
    s[1] += (p >> 5) & 0x3f;
    s[2] += p & 0x1f;
  }
}

// The picture (RGB565, or big-endian RGB565 when swapped; stride in pixels)
// averaged down by kShrink, reading it row by row (PSRAM likes long runs),
// then blurred with kPasses box passes each way. copy.rgb and scratch hold
// copy_bytes(shrunk(w), shrunk(h)); sums holds 3 * shrunk(w) words.
// A block of n samples becomes sum * 255 / (31 * n) rounded (63 for green).
inline void shrink_and_blur(const uint16_t* src, int w, int h, int stride_px, bool swapped, Copy& copy,
                            uint8_t* scratch, uint32_t* sums) {
  const int sw = shrunk(w);
  const int sh = shrunk(h);
  copy.w = sw;
  copy.h = sh;
  uint32_t n_of = 0;  // the block size the reciprocals are for
  uint64_t recip31 = 0, recip63 = 0;
  for (int sy = 0; sy < sh; ++sy) {
    memset(sums, 0, static_cast<size_t>(sw) * 3 * sizeof(uint32_t));
    const int y0 = sy * kShrink;
    const int y1 = y0 + kShrink < h ? y0 + kShrink : h;
    for (int y = y0; y < y1; ++y) {
      const uint16_t* row = src + static_cast<size_t>(y) * stride_px;
      if (swapped) {
        add_row<true>(row, w, sums);
      } else {
        add_row<false>(row, w, sums);
      }
    }
    uint8_t* out = copy.rgb + static_cast<size_t>(sy) * sw * 3;
    for (int sx = 0; sx < sw; ++sx) {
      const int cols = (sx + 1) * kShrink <= w ? kShrink : w - sx * kShrink;
      const uint32_t n = static_cast<uint32_t>(cols * (y1 - y0));
      if (n != n_of) {
        n_of = n;
        recip31 = reciprocal(31 * n);
        recip63 = reciprocal(63 * n);
      }
      const uint32_t* s = sums + 3 * sx;
      out[3 * sx + 0] = static_cast<uint8_t>(divide(s[0] * 255 + 31 * n / 2, recip31));
      out[3 * sx + 1] = static_cast<uint8_t>(divide(s[1] * 255 + 63 * n / 2, recip63));
      out[3 * sx + 2] = static_cast<uint8_t>(divide(s[2] * 255 + 31 * n / 2, recip31));
    }
  }
  for (int pass = 0; pass < kPasses; ++pass) {
    box_rows(copy.rgb, scratch, sw, sh);
    box_columns(scratch, copy.rgb, sw, sh, sums);
  }
}

// What the tile cards draw: the blurred copy and the picture object it lies
// over (scaled back up from the picture's top left).
struct Backdrop {
  Copy copy;
  lv_obj_t* picture = nullptr;
};

// The Bayer 8x8 thresholds of the area graphs (area_graph.h render_fade).
inline uint8_t bayer(int32_t x, int32_t y) {
  static constexpr uint8_t kBayer[8][8] = {
      {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
      {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
      {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21}};
  return kBayer[y & 7][x & 7];
}

// An 8.8 channel value (0..255 in 8.8) dithered to `max` steps.
inline uint32_t dither(uint32_t value88, uint32_t max, uint32_t threshold88) {
  const uint32_t level88 = (value88 * max * 257) >> 16;  // value * max / 255, 8.8
  const uint32_t step = (level88 + threshold88) >> 8;
  return step > max ? max : step;
}

// Coverage (0..256) of pixel (x, y) by a card with corner radius r: full
// outside the corner squares, else the pixel centre against the corner circle.
inline uint32_t corner_coverage(const lv_area_t& card, int32_t r, int32_t x, int32_t y) {
  if (r <= 0) return 256;
  float cx, cy;
  if (x < card.x1 + r) cx = static_cast<float>(card.x1 + r);
  else if (x > card.x2 - r) cx = static_cast<float>(card.x2 - r + 1);
  else return 256;
  if (y < card.y1 + r) cy = static_cast<float>(card.y1 + r);
  else if (y > card.y2 - r) cy = static_cast<float>(card.y2 - r + 1);
  else return 256;
  const float dx = x + 0.5f - cx;
  const float dy = y + 0.5f - cy;
  const float cover = static_cast<float>(r) + 0.5f - sqrtf(dx * dx + dy * dy);
  if (cover <= 0.0f) return 0;
  if (cover >= 1.0f) return 256;
  return static_cast<uint32_t>(cover * 256.0f);
}

// pixel over dst with coverage 0..256, per RGB565 channel.
inline uint16_t mix565(uint16_t pixel, uint16_t dst, uint32_t coverage) {
  if (coverage >= 256) return pixel;
  if (coverage == 0) return dst;
  const uint32_t keep = 256 - coverage;
  const uint32_t r = (((pixel >> 11) * coverage) + ((dst >> 11) * keep)) >> 8;
  const uint32_t g = ((((pixel >> 5) & 0x3f) * coverage) + (((dst >> 5) & 0x3f) * keep)) >> 8;
  const uint32_t b = (((pixel & 0x1f) * coverage) + ((dst & 0x1f) * keep)) >> 8;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

inline bool usable(const lv_layer_t* layer) {
  return layer && layer->opa >= LV_OPA_MAX && layer->color_format == LV_COLOR_FORMAT_RGB565 &&
         layer->draw_buf && layer->draw_buf->data && layer->draw_buf->header.cf == LV_COLOR_FORMAT_RGB565;
}

// The layer's queued draws finished, so its buffer holds what lies under the
// card (clock_shadow_paint.h layer_settled). A band's draws wait while the
// band before it is still copied to the panel (display_flush_gate.h, about
// 1 ms on the V2): the card waits for that copy, a bounded time. Giving up
// after eight tries left cards without their glass in the bands drawn
// after a popup closed (user 2026-11-02, b647).
constexpr uint32_t kSettleUs = 20000;

inline bool settled(lv_layer_t* layer) {
  const uint32_t started = display_flush_gate::clock_us();
  while (true) {
    bool pending = false;
    for (lv_draw_task_t* t = layer->draw_task_head; t; t = t->next) {
      if (t->state != LV_DRAW_TASK_STATE_FINISHED && t->state != LV_DRAW_TASK_STATE_FAILED) {
        pending = true;
        break;
      }
    }
    if (!pending) return true;
    if (display_flush_gate::clock_us() - started > kSettleUs) return false;
    lv_draw_dispatch();
  }
}

// The card's tint and frame line, read from its current style.
struct CardLook {
  uint8_t tint[3] = {0, 0, 0};
  uint32_t tint_opa = 0;        // 0..255
  uint16_t line = 0;            // RGB565
  uint32_t line_opa = 0;        // 0..255, the card's own opacity at most
};

// Coverage (0..256) of pixel (x, y) by the card's frame line: the 1 px ring
// between the card's rounded edge and the same shape 1 px inside it.
inline uint32_t ring_coverage(const lv_area_t& card, int32_t r, int32_t x, int32_t y) {
  const uint32_t outer = corner_coverage(card, r, x, y);
  if (x == card.x1 || x == card.x2 || y == card.y1 || y == card.y2) return outer;
  const lv_area_t inner = {card.x1 + 1, card.y1 + 1, card.x2 - 1, card.y2 - 1};
  const uint32_t in = corner_coverage(inner, r > 1 ? r - 1 : 0, x, y);
  return outer > in ? outer - in : 0;
}

// Writes the card's part of the copy into the RGB565 buffer `buf` (stride in
// pixels, buf_area its place on the screen) over `area`: bilinear from the
// copy (picture at px, py), the card's tint mixed in, dithered; the corners
// anti-aliased against what lies under them and the frame line on the edge.
inline void paint(const Copy& copy, int32_t px, int32_t py, const lv_area_t& card, int32_t r, const CardLook& look,
                  uint16_t* buf, int32_t stride_px, const lv_area_t& buf_area, const lv_area_t& area) {
  const int32_t first = area.x1 - px;
  const int32_t last = area.x2 - px;
  // Copy columns the row needs (u = 64 * X - 96 in 8.8: pixel centres).
  const int32_t i_first = (64 * first - 96) >> 8;
  const int32_t i_last = ((64 * last - 96) >> 8) + 1;
  const int32_t span = i_last - i_first + 1;
  // One row of the copy interpolated vertically, 8.8 per channel.
  uint16_t stack_row[3 * 96];
  uint16_t* row = span <= 96 ? stack_row : static_cast<uint16_t*>(lv_malloc(static_cast<size_t>(span) * 3 * sizeof(uint16_t)));
  if (!row) return;
  auto clampi = [](int32_t v, int32_t hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
  // The tint over the copy: value * (255 - opa) / 255 + tint * opa / 255, in
  // 8.8; keep_mul is (255 - opa) / 255 in 0.16.
  const uint32_t keep_mul = ((255 - look.tint_opa) * 65536 + 127) / 255;
  uint32_t tint88[3];
  for (int c = 0; c < 3; ++c) tint88[c] = look.tint[c] * look.tint_opa * 256 / 255;
  const bool line = look.line_opa > 0;
  for (int32_t y = area.y1; y <= area.y2; ++y) {
    const int32_t v = 64 * (y - py) - 96;
    const int32_t j0 = clampi(v >> 8, copy.h - 1);
    const int32_t j1 = clampi((v >> 8) + 1, copy.h - 1);
    const uint32_t fy = static_cast<uint32_t>(v & 0xff);
    const uint8_t* a = copy.rgb + static_cast<size_t>(j0) * copy.w * 3;
    const uint8_t* b = copy.rgb + static_cast<size_t>(j1) * copy.w * 3;
    for (int32_t k = 0; k < span; ++k) {
      const int32_t i = clampi(i_first + k, copy.w - 1);
      for (int c = 0; c < 3; ++c) {
        row[3 * k + c] = static_cast<uint16_t>(a[3 * i + c] * (256 - fy) + b[3 * i + c] * fy);
      }
    }
    uint16_t* out = buf + static_cast<size_t>(y - buf_area.y1) * stride_px + (area.x1 - buf_area.x1);
    const bool edge_row = y <= card.y1 + r || y >= card.y2 - r;
    for (int32_t x = area.x1; x <= area.x2; ++x, ++out) {
      const int32_t u = 64 * (x - px) - 96;
      const int32_t k0 = (u >> 8) - i_first;
      const uint32_t fx = static_cast<uint32_t>(u & 0xff);
      const uint16_t* p0 = row + 3 * k0;
      const uint16_t* p1 = p0 + 3;
      const uint32_t threshold = bayer(x, y) * 4 + 2;
      uint32_t value[3];
      for (int c = 0; c < 3; ++c) {
        const uint32_t frosted = (p0[c] * (256 - fx) + p1[c] * fx) >> 8;
        value[c] = ((frosted * keep_mul) >> 16) + tint88[c];
      }
      uint16_t pixel = static_cast<uint16_t>((dither(value[0], 31, threshold) << 11) |
                                             (dither(value[1], 63, threshold) << 5) | dither(value[2], 31, threshold));
      const bool edge = edge_row || x <= card.x1 + r || x >= card.x2 - r;
      if (!edge) {
        *out = pixel;
        continue;
      }
      if (line) {
        const uint32_t ring = ring_coverage(card, r, x, y);
        if (ring) pixel = mix565(look.line, pixel, (ring * look.line_opa) / 255);
      }
      *out = mix565(pixel, *out, corner_coverage(card, r, x, y));
    }
  }
  if (row != stack_row) lv_free(row);
}

// The card's tint (its own fill at its own opacity) and frame line (LVGL's
// 1 px outline on the inner edge, ui_surface_style.cpp, at the card's
// opacity: drawn opaque it stood out on the frosted glass, user 2026-11-02).
inline CardLook look_of(lv_obj_t* card) {
  CardLook look;
  const lv_color_t fill = lv_obj_get_style_bg_color(card, LV_PART_MAIN);
  look.tint[0] = fill.red;
  look.tint[1] = fill.green;
  look.tint[2] = fill.blue;
  look.tint_opa = lv_obj_get_style_bg_opa(card, LV_PART_MAIN);
  if (lv_obj_get_style_outline_width(card, LV_PART_MAIN) > 0 && !lv_obj_get_style_border_post(card, LV_PART_MAIN)) {
    look.line = lv_color_to_u16(lv_obj_get_style_outline_color(card, LV_PART_MAIN));
    const uint32_t opa = lv_obj_get_style_outline_opa(card, LV_PART_MAIN);
    look.line_opa = opa < look.tint_opa ? opa : look.tint_opa;
  }
  return look;
}

// Draw main end of a see-through tile card: LVGL drew its shadow, fill and
// line; the card's area becomes the copy under its tint and line, so the
// shadow stays outside the card (LVGL draws it inside a see-through card
// too, which darkened the frosted edges, user 2026-11-02). The children
// (icon, texts, circles) draw after it.
inline void on_card_draw_end(lv_event_t* e) {
  const Backdrop* b = static_cast<const Backdrop*>(lv_event_get_user_data(e));
  lv_obj_t* card = lv_event_get_current_target_obj(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!b || !b->copy.rgb || !b->picture || !card || !layer) return;
  if (lv_obj_get_style_bg_opa(card, LV_PART_MAIN) >= LV_OPA_COVER) return;
  if (lv_obj_has_flag(b->picture, LV_OBJ_FLAG_HIDDEN)) return;
  lv_area_t card_area;
  lv_obj_get_coords(card, &card_area);
  lv_area_t area;
  if (!lv_area_intersect(&area, &layer->_clip_area, &card_area)) return;
  if (!usable(layer) || !lv_area_intersect(&area, &area, &layer->buf_area)) return;
  if (!settled(layer)) return;
  lv_area_t picture;
  lv_obj_get_coords(b->picture, &picture);
  int32_t r = lv_obj_get_style_radius(card, LV_PART_MAIN);
  const int32_t half = LV_MIN(lv_area_get_width(&card_area), lv_area_get_height(&card_area)) / 2;
  if (r > half) r = half;
  paint(b->copy, picture.x1, picture.y1, card_area, r, look_of(card),
        reinterpret_cast<uint16_t*>(layer->draw_buf->data),
        static_cast<int32_t>(layer->draw_buf->header.stride / sizeof(uint16_t)), layer->buf_area, area);
}

// A see-through card draws the backdrop under its tint from now on; only
// its fill is glass: its circles, controls and graphs keep the colors of
// an opaque card (user 2026-11-02, tile_glass.h).
inline void attach(lv_obj_t* card, const Backdrop* backdrop) {
  if (!card || !backdrop) return;
  tile_glass::mark(card);
  lv_obj_add_event_cb(card, on_card_draw_end, LV_EVENT_DRAW_MAIN_END, const_cast<Backdrop*>(backdrop));
}

}  // namespace frosted_backdrop
