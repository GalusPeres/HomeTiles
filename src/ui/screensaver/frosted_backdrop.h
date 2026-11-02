#pragma once

#include <math.h>
#include <new>
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
// over (scaled back up from the picture's top left). generation changes with
// every new copy: the cards' kept pixels are stale then.
struct Backdrop {
  Copy copy;
  lv_obj_t* picture = nullptr;
  uint32_t generation = 0;
};

// Row y of the Bayer 8x8 thresholds of the area graphs (area_graph.h
// render_fade).
inline const uint8_t* bayer_row(int32_t y) {
  static constexpr uint8_t kBayer[8][8] = {
      {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
      {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
      {15, 47, 7, 39, 13, 45, 5, 37},  {63, 31, 55, 23, 61, 29, 53, 21}};
  return kBayer[y & 7];
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

// pixel (RGB565) over an ARGB8888 dst with coverage 0..256.
inline lv_color32_t mix8888(uint16_t pixel, lv_color32_t dst, uint32_t coverage) {
  const uint32_t r = ((pixel >> 11) << 3) | (pixel >> 13);
  const uint32_t g = (((pixel >> 5) & 0x3f) << 2) | ((pixel >> 9) & 0x03);
  const uint32_t b = ((pixel & 0x1f) << 3) | ((pixel >> 2) & 0x07);
  if (coverage >= 256) return lv_color32_make(r, g, b, LV_OPA_COVER);
  const uint32_t keep = 256 - coverage;
  return lv_color32_make((r * coverage + dst.red * keep) >> 8, (g * coverage + dst.green * keep) >> 8,
                         (b * coverage + dst.blue * keep) >> 8, (255 * coverage + dst.alpha * keep) >> 8);
}

// The panel's RGB565 bands and the screenshot's ARGB8888 snapshot of the top
// layer (web_admin_diagnostics.cpp createUiScreenshot), at full opacity: a
// screenshot showed no glass while only RGB565 was drawn (user 2026-11-02).
inline bool usable(const lv_layer_t* layer) {
  if (!layer || layer->opa < LV_OPA_MAX || !layer->draw_buf || !layer->draw_buf->data) return false;
  const lv_color_format_t cf = static_cast<lv_color_format_t>(layer->draw_buf->header.cf);
  return layer->color_format == cf && (cf == LV_COLOR_FORMAT_RGB565 || cf == LV_COLOR_FORMAT_ARGB8888);
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

// The card's values over columns x1..x2 of rows y1..y2 into `dst` (stride in
// pixels): the copy (picture at px, py) scaled back up bilinearly, the card's
// tint mixed in, dithered to RGB565. The tint and the step size are applied
// to a row's samples of the copy before the horizontal interpolation (both
// are linear), so a pixel takes one multiplication per channel.
inline void fill(const Copy& copy, int32_t px, int32_t py, const CardLook& look, int32_t x1, int32_t x2,
                 int32_t y1, int32_t y2, uint16_t* dst, int32_t dst_stride) {
  // Copy columns the rows need (u = 64 * X - 96 in 8.8: pixel centres).
  const int32_t i_first = (64 * (x1 - px) - 96) >> 8;
  const int32_t i_last = ((64 * (x2 - px) - 96) >> 8) + 1;
  const int32_t span = i_last - i_first + 1;
  // One row's samples in RGB565 steps, 8.8 per channel.
  int32_t stack_row[3 * 96];
  int32_t* row = span <= 96 ? stack_row : static_cast<int32_t*>(lv_malloc(static_cast<size_t>(span) * 3 * sizeof(int32_t)));
  if (!row) return;
  auto clampi = [](int32_t v, int32_t hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
  // The tint over the copy: value * (255 - opa) / 255 + tint * opa / 255, in
  // 8.8; keep_mul is (255 - opa) / 255 in 0.16. Then value * max / 255.
  const uint32_t keep_mul = ((255 - look.tint_opa) * 65536 + 127) / 255;
  uint32_t tint88[3];
  for (int c = 0; c < 3; ++c) tint88[c] = look.tint[c] * look.tint_opa * 256 / 255;
  static constexpr uint32_t kMax[3] = {31, 63, 31};
  for (int32_t y = y1; y <= y2; ++y) {
    const int32_t v = 64 * (y - py) - 96;
    const int32_t j0 = clampi(v >> 8, copy.h - 1);
    const int32_t j1 = clampi((v >> 8) + 1, copy.h - 1);
    const uint32_t fy = static_cast<uint32_t>(v & 0xff);
    const uint8_t* a = copy.rgb + static_cast<size_t>(j0) * copy.w * 3;
    const uint8_t* b = copy.rgb + static_cast<size_t>(j1) * copy.w * 3;
    for (int32_t k = 0; k < span; ++k) {
      const int32_t i = clampi(i_first + k, copy.w - 1);
      for (int c = 0; c < 3; ++c) {
        const uint32_t frosted = a[3 * i + c] * (256 - fy) + b[3 * i + c] * fy;
        const uint32_t value = ((frosted * keep_mul) >> 16) + tint88[c];
        row[3 * k + c] = static_cast<int32_t>((value * kMax[c] * 257) >> 16);
      }
    }
    const uint8_t* thresholds = bayer_row(y);
    uint16_t* out = dst + static_cast<size_t>(y - y1) * dst_stride;
    for (int32_t x = x1; x <= x2; ++x) {
      const int32_t u = 64 * (x - px) - 96;
      const int32_t* p0 = row + 3 * ((u >> 8) - i_first);
      const int32_t fx = u & 0xff;
      const int32_t threshold = thresholds[x & 7] * 4 + 2;
      uint32_t step[3];
      for (int c = 0; c < 3; ++c) {
        const int32_t level = p0[c] + (((p0[c + 3] - p0[c]) * fx) >> 8);
        const uint32_t s = static_cast<uint32_t>(level + threshold) >> 8;
        step[c] = s > kMax[c] ? kMax[c] : s;
      }
      *out++ = static_cast<uint16_t>((step[0] << 11) | (step[1] << 5) | step[2]);
    }
  }
  if (row != stack_row) lv_free(row);
}

// A value over a layer pixel with coverage 0..256, and a run of values
// copied, in the layer's format.
inline void put(uint16_t& out, uint16_t pixel, uint32_t coverage) { out = mix565(pixel, out, coverage); }
inline void put(lv_color32_t& out, uint16_t pixel, uint32_t coverage) { out = mix8888(pixel, out, coverage); }
inline void copy_run(uint16_t* out, const uint16_t* values, int32_t n) {
  memcpy(out, values, static_cast<size_t>(n) * sizeof(uint16_t));
}
inline void copy_run(lv_color32_t* out, const uint16_t* values, int32_t n) {
  for (int32_t i = 0; i < n; ++i) out[i] = mix8888(values[i], out[i], 256);
}

// One row y of the card's values (`values`, values[0] at screen column
// values_x) over columns x1..x2 of `out` (the layer at x1): the inside copied,
// the rounded edge and the frame line mixed with what lies under them.
template <typename Pixel>
inline void compose_row(const uint16_t* values, int32_t values_x, const lv_area_t& card, int32_t r,
                        const CardLook& look, int32_t y, int32_t x1, int32_t x2, Pixel* out) {
  auto edge = [&](int32_t x) {
    uint16_t pixel = values[x - values_x];
    if (look.line_opa) {
      const uint32_t ring = ring_coverage(card, r, x, y);
      if (ring) pixel = mix565(look.line, pixel, (ring * look.line_opa) / 255);
    }
    put(out[x - x1], pixel, corner_coverage(card, r, x, y));
  };
  // The inside of a row between the corner columns; none in a corner row.
  const bool edge_row = y <= card.y1 + r || y >= card.y2 - r;
  const int32_t in1 = edge_row ? x2 + 1 : LV_MAX(x1, card.x1 + r + 1);
  const int32_t in2 = edge_row ? x2 : LV_MIN(x2, card.x2 - r - 1);
  if (in1 > in2) {
    for (int32_t x = x1; x <= x2; ++x) edge(x);
    return;
  }
  for (int32_t x = x1; x < in1; ++x) edge(x);
  copy_run(out + (in1 - x1), values + (in1 - values_x), in2 - in1 + 1);
  for (int32_t x = in2 + 1; x <= x2; ++x) edge(x);
}

// A card's values over its whole area for one look (tint and opacity), at
// one place over one copy: a press, a new value or a closing popup copies
// them instead of computing every pixel again (a 404x320 card took 69 ms
// when a popup closed, V2 b648). Two looks per card: pressed or not, on or
// off.
struct Kept {
  uint16_t* px = nullptr;
  size_t bytes = 0;
  bool valid = false;
  uint32_t generation = 0;
  lv_area_t card{};
  int32_t picture_x = 0;
  int32_t picture_y = 0;
  uint32_t look = 0;  // tint RGB888 << 8 | opacity
  uint32_t used = 0;
};

struct CardGlass {
  const Backdrop* backdrop = nullptr;
  Kept kept[2];
  uint32_t uses = 0;
};

// How often a card's values were computed (host tests).
inline uint32_t g_kept_fills = 0;

inline uint32_t look_key(const CardLook& look) {
  if (look.tint_opa == 0) return 0;  // no tint: its color does not matter
  return (static_cast<uint32_t>(look.tint[0]) << 24) | (static_cast<uint32_t>(look.tint[1]) << 16) |
         (static_cast<uint32_t>(look.tint[2]) << 8) | look.tint_opa;
}

// The card's values for its look, kept or computed now; nullptr without
// memory for them.
inline const uint16_t* kept_values(CardGlass& glass, int32_t px, int32_t py, const lv_area_t& card,
                                   const CardLook& look) {
  const Backdrop& b = *glass.backdrop;
  const uint32_t key = look_key(look);
  ++glass.uses;
  for (Kept& k : glass.kept) {
    if (k.valid && k.generation == b.generation && k.look == key && k.picture_x == px && k.picture_y == py &&
        k.card.x1 == card.x1 && k.card.y1 == card.y1 && k.card.x2 == card.x2 && k.card.y2 == card.y2) {
      k.used = glass.uses;
      return k.px;
    }
  }
  // An empty entry, else the one used longer ago.
  Kept* k = !glass.kept[0].valid ? &glass.kept[0]
            : !glass.kept[1].valid ? &glass.kept[1]
            : (glass.kept[0].used <= glass.kept[1].used ? &glass.kept[0] : &glass.kept[1]);
  const int32_t w = lv_area_get_width(&card);
  const size_t bytes = static_cast<size_t>(w) * lv_area_get_height(&card) * sizeof(uint16_t);
  if (k->bytes != bytes) {
    if (k->px) lv_free(k->px);
    k->px = static_cast<uint16_t*>(lv_malloc(bytes));
    k->bytes = k->px ? bytes : 0;
  }
  k->valid = false;
  if (!k->px) return nullptr;
  fill(b.copy, px, py, look, card.x1, card.x2, card.y1, card.y2, k->px, w);
  ++g_kept_fills;
  k->valid = true;
  k->generation = b.generation;
  k->card = card;
  k->picture_x = px;
  k->picture_y = py;
  k->look = key;
  k->used = glass.uses;
  return k->px;
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

// The glass paints this card on `layer` (on_card_draw_end): see-through,
// over a shown picture, on a layer it can write.
inline bool paints(const CardGlass* glass, lv_obj_t* card, const lv_layer_t* layer) {
  const Backdrop* b = glass ? glass->backdrop : nullptr;
  return b && b->copy.rgb && b->picture && card && !lv_obj_has_flag(b->picture, LV_OBJ_FLAG_HIDDEN) &&
         lv_obj_get_style_bg_opa(card, LV_PART_MAIN) < LV_OPA_COVER && usable(layer);
}

// LVGL's own fill and frame line of a card the glass paints are left out:
// the glass draws both itself, and its rounded edge mixed with them instead
// of the picture under the card (the opaque line showed through the corners,
// frayed, user 2026-11-02 b648). The shadow stays, and a line drawn over the
// children (border_post, Camera tiles).
inline void on_card_task(lv_event_t* e) {
  lv_draw_task_t* task = lv_event_get_draw_task(e);
  lv_obj_t* card = lv_event_get_current_target_obj(e);
  if (!task || !card) return;
  const auto* base = static_cast<const lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
  if (!base || base->obj != card || base->part != LV_PART_MAIN) return;
  const lv_draw_task_type_t type = lv_draw_task_get_type(task);
  const bool fill_task = type == LV_DRAW_TASK_TYPE_FILL;
  const bool line_task = type == LV_DRAW_TASK_TYPE_BORDER && !lv_obj_get_style_border_post(card, LV_PART_MAIN);
  if ((!fill_task && !line_task) || !paints(static_cast<CardGlass*>(lv_event_get_user_data(e)), card, base->layer)) {
    return;
  }
  // Done before it is drawn: LVGL's dispatch takes waiting tasks only and
  // drops finished ones (as for a task no draw unit takes, lv_draw.c).
  task->state = LV_DRAW_TASK_STATE_FINISHED;
}

// Draws the card's rows over `area` of the layer (RGB565 or ARGB8888).
template <typename Pixel>
inline void compose_area(CardGlass& glass, lv_layer_t* layer, const lv_area_t& card_area, const lv_area_t& area,
                         int32_t r, const CardLook& look, int32_t px, int32_t py) {
  Pixel* buf = reinterpret_cast<Pixel*>(layer->draw_buf->data);
  const int32_t stride = static_cast<int32_t>(layer->draw_buf->header.stride / sizeof(Pixel));
  auto layer_row = [&](int32_t y) {
    return buf + static_cast<size_t>(y - layer->buf_area.y1) * stride + (area.x1 - layer->buf_area.x1);
  };
  const int32_t w = lv_area_get_width(&card_area);
  if (const uint16_t* values = kept_values(glass, px, py, card_area, look)) {
    for (int32_t y = area.y1; y <= area.y2; ++y) {
      compose_row(values + static_cast<size_t>(y - card_area.y1) * w, card_area.x1, card_area, r, look, y,
                  area.x1, area.x2, layer_row(y));
    }
    return;
  }
  // Without memory to keep them, this draw's rows are computed as it goes.
  uint16_t* row = static_cast<uint16_t*>(lv_malloc(static_cast<size_t>(lv_area_get_width(&area)) * sizeof(uint16_t)));
  if (!row) return;
  for (int32_t y = area.y1; y <= area.y2; ++y) {
    fill(glass.backdrop->copy, px, py, look, area.x1, area.x2, y, y, row, 0);
    compose_row(row, area.x1, card_area, r, look, y, area.x1, area.x2, layer_row(y));
  }
  lv_free(row);
}

// Draw main end of a see-through tile card: LVGL drew its shadow (its fill
// and line were left out, on_card_task); the card's area becomes the copy
// under its tint and line, so the shadow stays outside the card (LVGL draws
// it inside a see-through card too, which darkened the frosted edges, user
// 2026-11-02). The children (icon, texts, circles) draw after it.
inline void on_card_draw_end(lv_event_t* e) {
  CardGlass* glass = static_cast<CardGlass*>(lv_event_get_user_data(e));
  lv_obj_t* card = lv_event_get_current_target_obj(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!paints(glass, card, layer)) return;
  const Backdrop* b = glass->backdrop;
  lv_area_t card_area;
  lv_obj_get_coords(card, &card_area);
  lv_area_t area;
  if (!lv_area_intersect(&area, &layer->_clip_area, &card_area)) return;
  if (!lv_area_intersect(&area, &area, &layer->buf_area)) return;
  if (!settled(layer)) return;
  lv_area_t picture;
  lv_obj_get_coords(b->picture, &picture);
  int32_t r = lv_obj_get_style_radius(card, LV_PART_MAIN);
  const int32_t w = lv_area_get_width(&card_area);
  const int32_t half = LV_MIN(w, lv_area_get_height(&card_area)) / 2;
  if (r > half) r = half;
  const CardLook look = look_of(card);
  if (layer->draw_buf->header.cf == LV_COLOR_FORMAT_ARGB8888) {
    compose_area<lv_color32_t>(*glass, layer, card_area, area, r, look, picture.x1, picture.y1);
  } else {
    compose_area<uint16_t>(*glass, layer, card_area, area, r, look, picture.x1, picture.y1);
  }
}

inline void on_card_delete(lv_event_t* e) {
  CardGlass* glass = static_cast<CardGlass*>(lv_event_get_user_data(e));
  if (!glass) return;
  for (Kept& k : glass->kept) {
    if (k.px) lv_free(k.px);
  }
  glass->~CardGlass();
  lv_free(glass);
}

// A see-through card draws the backdrop under its tint from now on, with
// its kept values (released with the card); only its fill is glass: its
// circles, controls and graphs keep the colors of an opaque card (user
// 2026-11-02, tile_glass.h).
inline void attach(lv_obj_t* card, const Backdrop* backdrop) {
  if (!card || !backdrop) return;
  void* memory = lv_malloc(sizeof(CardGlass));
  if (!memory) return;
  CardGlass* glass = new (memory) CardGlass();
  glass->backdrop = backdrop;
  tile_glass::mark(card);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
  lv_obj_add_event_cb(card, on_card_task, LV_EVENT_DRAW_TASK_ADDED, glass);
  lv_obj_add_event_cb(card, on_card_draw_end, LV_EVENT_DRAW_MAIN_END, glass);
  lv_obj_add_event_cb(card, on_card_delete, LV_EVENT_DELETE, glass);
}

}  // namespace frosted_backdrop
