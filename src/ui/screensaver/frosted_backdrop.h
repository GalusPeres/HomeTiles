#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <lvgl.h>
#include <lvgl_private.h>

// Frosted glass behind see-through screensaver tiles (user 2026-11-02, test
// builds only for now). The slide's picture is blurred once per slide at a
// quarter of its size per axis: a blurred picture has no detail to lose, so
// the copy stays small (V2 1280x800 -> 320x200 = 125 KB, S3 480x480 -> 120x120
// = 28 KB). Each see-through tile card draws its part of that copy scaled
// back up (bilinear) before its own translucent fill, which tints it. Per
// redraw a card draws scaled image parts; the blur runs only when the slide
// changes.
//
// LVGL 9.6 clips an image to clip_radius only when it is not transformed, so
// the scaled copy fills the card without its four corner squares, and each
// corner square is a small full-size piece (radius x radius) sampled from the
// copy and drawn unscaled with the card's radius. The pieces are kept per card
// and sampled again when the slide or the card's place changes.
#if (defined(HOMETILES_ISSUE38_BETA) || defined(HOMETILES_TEST_BETA)) && !defined(HOMETILES_FROSTED_TILES)
#define HOMETILES_FROSTED_TILES 1
#endif

namespace frosted_backdrop {

constexpr int kShrink = 4;      // the copy's size: 1/4 per axis
constexpr int kBoxRadius = 3;   // per box pass at the copy's size (about 12 px full size)
constexpr int kPasses = 2;      // two box passes come close to a Gaussian

inline int shrunk(int full) { return (full + kShrink - 1) / kShrink; }

inline uint16_t pack(uint32_t r, uint32_t g, uint32_t b) {
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// One box pass over an RGB565 plane, horizontal or vertical, edges clamped.
inline void box_pass(const uint16_t* in, uint16_t* out, int w, int h, bool vertical) {
  const int len = vertical ? h : w;
  const int lines = vertical ? w : h;
  const int window = 2 * kBoxRadius + 1;
  for (int line = 0; line < lines; ++line) {
    auto at = [&](int i) -> uint16_t {
      if (i < 0) i = 0;
      if (i >= len) i = len - 1;
      return vertical ? in[i * w + line] : in[line * w + i];
    };
    uint32_t r = 0, g = 0, b = 0;
    for (int i = -kBoxRadius; i <= kBoxRadius; ++i) {
      const uint16_t p = at(i);
      r += p >> 11;
      g += (p >> 5) & 0x3f;
      b += p & 0x1f;
    }
    for (int i = 0; i < len; ++i) {
      const uint16_t value = pack((r + window / 2) / window, (g + window / 2) / window, (b + window / 2) / window);
      if (vertical) {
        out[i * w + line] = value;
      } else {
        out[line * w + i] = value;
      }
      const uint16_t leaving = at(i - kBoxRadius);
      const uint16_t entering = at(i + kBoxRadius + 1);
      r += (entering >> 11) - (leaving >> 11);
      g += ((entering >> 5) & 0x3f) - ((leaving >> 5) & 0x3f);
      b += (entering & 0x1f) - (leaving & 0x1f);
    }
  }
}

// The picture (RGB565, or big-endian RGB565 when swapped; stride in pixels)
// averaged down by kShrink, then blurred with kPasses box passes each way.
// dst (native RGB565) and scratch hold shrunk(w) x shrunk(h) pixels.
inline void shrink_and_blur(const uint16_t* src, int w, int h, int stride_px, bool swapped,
                            uint16_t* dst, uint16_t* scratch) {
  const int sw = shrunk(w);
  const int sh = shrunk(h);
  for (int sy = 0; sy < sh; ++sy) {
    const int y0 = sy * kShrink;
    const int y1 = y0 + kShrink < h ? y0 + kShrink : h;
    for (int sx = 0; sx < sw; ++sx) {
      const int x0 = sx * kShrink;
      const int x1 = x0 + kShrink < w ? x0 + kShrink : w;
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (int y = y0; y < y1; ++y) {
        const uint16_t* row = src + static_cast<size_t>(y) * stride_px;
        for (int x = x0; x < x1; ++x) {
          const uint16_t raw = row[x];
          const uint16_t p = swapped ? static_cast<uint16_t>((raw << 8) | (raw >> 8)) : raw;
          r += p >> 11;
          g += (p >> 5) & 0x3f;
          b += p & 0x1f;
          ++n;
        }
      }
      dst[sy * sw + sx] = pack((r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n);
    }
  }
  for (int pass = 0; pass < kPasses; ++pass) {
    box_pass(dst, scratch, sw, sh, false);
    box_pass(scratch, dst, sw, sh, true);
  }
}

// The copy at a full-size point (x, y from the picture's top left), bilinear
// like LVGL's scaled draw.
inline uint16_t sample(const lv_image_dsc_t* copy, int32_t x, int32_t y) {
  const int sw = static_cast<int>(copy->header.w);
  const int sh = static_cast<int>(copy->header.h);
  const uint16_t* px = reinterpret_cast<const uint16_t*>(copy->data);
  const int32_t fx = x * 256 / kShrink;
  const int32_t fy = y * 256 / kShrink;
  int x0 = fx >> 8, y0 = fy >> 8;
  const uint32_t ax = fx & 0xff, ay = fy & 0xff;
  auto clampx = [&](int v) { return v < 0 ? 0 : (v >= sw ? sw - 1 : v); };
  auto clampy = [&](int v) { return v < 0 ? 0 : (v >= sh ? sh - 1 : v); };
  const int x1 = clampx(x0 + 1), y1 = clampy(y0 + 1);
  x0 = clampx(x0);
  y0 = clampy(y0);
  const uint16_t p00 = px[y0 * sw + x0], p10 = px[y0 * sw + x1];
  const uint16_t p01 = px[y1 * sw + x0], p11 = px[y1 * sw + x1];
  auto mix = [&](int shift, uint32_t mask) {
    const uint32_t c00 = (p00 >> shift) & mask, c10 = (p10 >> shift) & mask;
    const uint32_t c01 = (p01 >> shift) & mask, c11 = (p11 >> shift) & mask;
    const uint32_t top = c00 * (256 - ax) + c10 * ax;
    const uint32_t bottom = c01 * (256 - ax) + c11 * ax;
    return (top * (256 - ay) + bottom * ay + 32768) >> 16;
  };
  return pack(mix(11, 0x1f), mix(5, 0x3f), mix(0, 0x1f));
}

// What the tile cards draw: the blurred copy, the picture object it belongs
// to (the copy lies over the picture, scaled back) and a generation that
// changes whenever the copy's pixels do.
struct Backdrop {
  const lv_image_dsc_t* image = nullptr;
  lv_obj_t* picture = nullptr;
  uint32_t generation = 0;
};

// Per card: the four corner pieces and what they were sampled for.
struct CardFrost {
  const Backdrop* backdrop = nullptr;
  uint32_t generation = 0;
  lv_area_t card{0, 0, -1, -1};
  lv_area_t picture{0, 0, -1, -1};
  int32_t radius = 0;
  uint16_t* pixels = nullptr;
  lv_image_dsc_t corner[4] = {};
};

inline bool same_area(const lv_area_t& a, const lv_area_t& b) {
  return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

// The corner squares of a card with radius r: top left, top right, bottom
// left, bottom right.
inline lv_area_t corner_area(const lv_area_t& card, int32_t r, int i) {
  const int32_t x = (i & 1) ? card.x2 - r + 1 : card.x1;
  const int32_t y = (i & 2) ? card.y2 - r + 1 : card.y1;
  return lv_area_t{x, y, x + r - 1, y + r - 1};
}

inline bool ensure_corners(CardFrost* frost, const lv_area_t& card, const lv_area_t& picture, int32_t r) {
  const Backdrop* b = frost->backdrop;
  if (frost->pixels && frost->generation == b->generation && frost->radius == r &&
      same_area(frost->card, card) && same_area(frost->picture, picture)) {
    return true;
  }
  if (!frost->pixels || frost->radius != r) {
    free(frost->pixels);
    frost->pixels = static_cast<uint16_t*>(malloc(static_cast<size_t>(4) * r * r * sizeof(uint16_t)));
    if (!frost->pixels) return false;
  }
  for (int i = 0; i < 4; ++i) {
    lv_image_dsc_t& dsc = frost->corner[i];
    lv_image_cache_drop(&dsc);
    uint16_t* out = frost->pixels + static_cast<size_t>(i) * r * r;
    const lv_area_t at = corner_area(card, r, i);
    for (int32_t y = 0; y < r; ++y) {
      for (int32_t x = 0; x < r; ++x) {
        out[y * r + x] = sample(b->image, at.x1 + x - picture.x1, at.y1 + y - picture.y1);
      }
    }
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = r;
    dsc.header.h = r;
    dsc.header.stride = r * sizeof(uint16_t);
    dsc.data_size = static_cast<uint32_t>(r) * r * sizeof(uint16_t);
    dsc.data = reinterpret_cast<const uint8_t*>(out);
  }
  frost->generation = b->generation;
  frost->radius = r;
  frost->card = card;
  frost->picture = picture;
  return true;
}

// The scaled copy inside one rectangle of the card (clipped to the band).
inline void draw_scaled(lv_layer_t* layer, const Backdrop* b, const lv_area_t& picture,
                        const lv_area_t& clip, const lv_area_t& part) {
  lv_area_t area;
  if (part.x2 < part.x1 || part.y2 < part.y1 || !lv_area_intersect(&area, &clip, &part)) return;
  lv_draw_image_dsc_t dsc;
  lv_draw_image_dsc_init(&dsc);
  dsc.src = b->image;
  dsc.scale_x = LV_SCALE_NONE * kShrink;
  dsc.scale_y = LV_SCALE_NONE * kShrink;
  dsc.pivot.x = 0;
  dsc.pivot.y = 0;
  dsc.antialias = 1;
  const lv_area_t coords = {picture.x1, picture.y1,
                            picture.x1 + static_cast<int32_t>(b->image->header.w) - 1,
                            picture.y1 + static_cast<int32_t>(b->image->header.h) - 1};
  const lv_area_t saved = layer->_clip_area;
  layer->_clip_area = area;
  lv_draw_image(layer, &dsc, &coords);
  layer->_clip_area = saved;
}

// Draw main begin of a see-through tile card: its part of the blurred copy in
// the card's rounded shape, under the card's own translucent fill.
inline void on_card_draw_begin(lv_event_t* e) {
  CardFrost* frost = static_cast<CardFrost*>(lv_event_get_user_data(e));
  lv_obj_t* card = lv_event_get_current_target_obj(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  const Backdrop* b = frost ? frost->backdrop : nullptr;
  if (!b || !b->image || !b->picture || !card || !layer) return;
  if (lv_obj_get_style_bg_opa(card, LV_PART_MAIN) >= LV_OPA_COVER) return;
  if (lv_obj_has_flag(b->picture, LV_OBJ_FLAG_HIDDEN)) return;
  lv_area_t card_area;
  lv_obj_get_coords(card, &card_area);
  lv_area_t clip;
  if (!lv_area_intersect(&clip, &layer->_clip_area, &card_area)) return;
  lv_area_t picture;
  lv_obj_get_coords(b->picture, &picture);
  int32_t r = lv_obj_get_style_radius(card, LV_PART_MAIN);
  const int32_t half = LV_MIN(lv_area_get_width(&card_area), lv_area_get_height(&card_area)) / 2;
  if (r > half) r = half;
  if (r <= 0 || !ensure_corners(frost, card_area, picture, r)) {
    draw_scaled(layer, b, picture, clip, card_area);
    return;
  }
  // The card without its corner squares: a middle band and two strips.
  draw_scaled(layer, b, picture, clip, {card_area.x1, card_area.y1 + r, card_area.x2, card_area.y2 - r});
  draw_scaled(layer, b, picture, clip, {card_area.x1 + r, card_area.y1, card_area.x2 - r, card_area.y1 + r - 1});
  draw_scaled(layer, b, picture, clip, {card_area.x1 + r, card_area.y2 - r + 1, card_area.x2 - r, card_area.y2});
  // The corner pieces, unscaled, in the card's rounded shape.
  for (int i = 0; i < 4; ++i) {
    const lv_area_t at = corner_area(card_area, r, i);
    lv_area_t area;
    if (!lv_area_intersect(&area, &clip, &at)) continue;
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &frost->corner[i];
    dsc.clip_radius = r;
    dsc.image_area = card_area;
    const lv_area_t saved = layer->_clip_area;
    layer->_clip_area = area;
    lv_draw_image(layer, &dsc, &at);
    layer->_clip_area = saved;
  }
}

inline void on_card_delete(lv_event_t* e) {
  CardFrost* frost = static_cast<CardFrost*>(lv_event_get_user_data(e));
  if (!frost) return;
  for (lv_image_dsc_t& dsc : frost->corner) lv_image_cache_drop(&dsc);
  free(frost->pixels);
  delete frost;
}

// A see-through card draws the backdrop under its fill from now on.
inline void attach(lv_obj_t* card, const Backdrop* backdrop) {
  if (!card || !backdrop) return;
  CardFrost* frost = new CardFrost();
  frost->backdrop = backdrop;
  lv_obj_add_event_cb(card, on_card_draw_begin, LV_EVENT_DRAW_MAIN_BEGIN, frost);
  lv_obj_add_event_cb(card, on_card_delete, LV_EVENT_DELETE, frost);
}

}  // namespace frosted_backdrop
