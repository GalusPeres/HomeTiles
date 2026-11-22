#pragma once

#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <lvgl_private.h>

#include "src/types/clock/clock_shadow_paint.h"

// The screensaver clock's finished pixels (user 2026-10-31: closing a popup
// over the screensaver redrew the clock in ~46 of ~128 ms, its nine shadow
// copies blended pixel by pixel in every band, b640 CloseDraw). A shadowed
// line's pixels follow from the pixels under it alone, pixel by pixel: the
// copies and the white text each mix one colour into the pixel below. So a
// line keeps, for the pixels it drew last, what lay under them and what it
// made of them. Where the pixels under it are the same again (the same
// picture, the same tiles), it copies its own result instead of drawing,
// exactly the same pixels; anything else under it (a new slide, a changed
// tile) draws it as before and keeps the new result. A new text, width or
// place starts over. Only on an RGB565 layer at full opacity.
namespace clock_pixel_cache {

struct Key {
  lv_area_t rect{0, 0, -1, -1};  // the line and its drawn margin
  lv_area_t painter{0, 0, -1, -1};
  lv_area_t label{0, 0, -1, -1};
};

struct Cache {
  Key key;
  uint16_t* under = nullptr;  // the pixels under the line, rect-sized
  uint16_t* drawn = nullptr;  // what the line made of them
  int16_t* lo = nullptr;      // per row the kept columns (lo > hi: none)
  int16_t* hi = nullptr;
  int32_t w = 0, h = 0;
  lv_obj_t* painter = nullptr;
  lv_obj_t* label = nullptr;
  // A capture between the line's draw and its end (the band's columns).
  bool pending = false;
  lv_area_t pending_area{0, 0, -1, -1};
};

inline bool same_area(const lv_area_t& a, const lv_area_t& b) {
  return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

inline void forget(Cache& c) {
  for (int32_t y = 0; y < c.h; ++y) {
    c.lo[y] = INT16_MAX;
    c.hi[y] = INT16_MIN;
  }
  c.pending = false;
}

inline void release(Cache& c) {
  if (c.under) lv_free(c.under);
  if (c.drawn) lv_free(c.drawn);
  if (c.lo) lv_free(c.lo);
  if (c.hi) lv_free(c.hi);
  c.under = c.drawn = nullptr;
  c.lo = c.hi = nullptr;
  c.w = c.h = 0;
  c.pending = false;
  c.key = Key{};
}

// A new text, width or alignment: the kept pixels no longer match.
inline void invalidate(Cache& c) {
  if (c.lo) forget(c);
}

inline Key key_of(lv_obj_t* line, const Cache& c) {
  Key k;
  lv_obj_get_coords(line, &k.rect);
  const int32_t ext = lv_obj_get_ext_draw_size(line);
  lv_area_increase(&k.rect, ext, ext);
  if (c.painter && !lv_obj_has_flag(c.painter, LV_OBJ_FLAG_HIDDEN)) lv_obj_get_coords(c.painter, &k.painter);
  if (c.label) lv_obj_get_coords(c.label, &k.label);
  return k;
}

inline bool fit(Cache& c, const Key& k) {
  const int32_t w = lv_area_get_width(&k.rect);
  const int32_t h = lv_area_get_height(&k.rect);
  if (w <= 0 || h <= 0) return false;
  if (w != c.w || h != c.h || !c.under) {
    release(c);
    const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
    c.under = static_cast<uint16_t*>(lv_malloc(px * sizeof(uint16_t)));
    c.drawn = static_cast<uint16_t*>(lv_malloc(px * sizeof(uint16_t)));
    c.lo = static_cast<int16_t*>(lv_malloc(static_cast<size_t>(h) * sizeof(int16_t)));
    c.hi = static_cast<int16_t*>(lv_malloc(static_cast<size_t>(h) * sizeof(int16_t)));
    if (!c.under || !c.drawn || !c.lo || !c.hi) {
      release(c);
      return false;
    }
    c.w = w;
    c.h = h;
  }
  c.key = k;
  forget(c);
  return true;
}

inline bool usable(const lv_layer_t* layer) {
  return layer && layer->opa >= LV_OPA_MAX && layer->color_format == LV_COLOR_FORMAT_RGB565 &&
         layer->draw_buf && layer->draw_buf->data && layer->draw_buf->header.cf == LV_COLOR_FORMAT_RGB565;
}

inline uint16_t* layer_px(lv_layer_t* layer, int32_t x, int32_t y) {
  return reinterpret_cast<uint16_t*>(layer->draw_buf->data +
                                     static_cast<uint32_t>(y - layer->buf_area.y1) * layer->draw_buf->header.stride) +
         (x - layer->buf_area.x1);
}

inline uint16_t* kept_px(uint16_t* plane, const Cache& c, int32_t x, int32_t y) {
  return plane + static_cast<size_t>(y - c.key.rect.y1) * static_cast<size_t>(c.w) + (x - c.key.rect.x1);
}

// Draw main begin of the line: the band's part of it from the kept pixels
// when the pixels under it are unchanged (the line then draws nothing: an
// empty clip leaves its children out), else note what lies under it.
inline void on_draw_begin(lv_event_t* e) {
  Cache* c = static_cast<Cache*>(lv_event_get_user_data(e));
  lv_obj_t* line = lv_event_get_current_target_obj(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!c || !line) return;
  // A capture that never reached its end left a part half kept.
  if (c->pending) invalidate(*c);
  if (!usable(layer)) return;
  const Key k = key_of(line, *c);
  if (!c->under || !same_area(k.rect, c->key.rect) || !same_area(k.painter, c->key.painter) ||
      !same_area(k.label, c->key.label)) {
    if (!fit(*c, k)) return;
  }
  lv_area_t r;
  if (!lv_area_intersect(&r, &layer->_clip_area, &c->key.rect)) return;
  if (!lv_area_intersect(&r, &r, &layer->buf_area)) return;
  if (!clock_shadow_paint::layer_settled(layer)) return;
  const size_t n = static_cast<size_t>(lv_area_get_width(&r)) * sizeof(uint16_t);
  bool kept = true;
  for (int32_t y = r.y1; y <= r.y2 && kept; ++y) {
    const int32_t row = y - c->key.rect.y1;
    kept = c->lo[row] <= r.x1 && c->hi[row] >= r.x2 &&
           memcmp(layer_px(layer, r.x1, y), kept_px(c->under, *c, r.x1, y), n) == 0;
  }
  if (kept) {
    for (int32_t y = r.y1; y <= r.y2; ++y) memcpy(layer_px(layer, r.x1, y), kept_px(c->drawn, *c, r.x1, y), n);
    layer->_clip_area.x2 = layer->_clip_area.x1 - 1;
    return;
  }
  for (int32_t y = r.y1; y <= r.y2; ++y) memcpy(kept_px(c->under, *c, r.x1, y), layer_px(layer, r.x1, y), n);
  c->pending = true;
  c->pending_area = r;
}

// Draw post end of the line: what it made of the noted pixels. A row keeps
// the new columns with the old ones when they touch, else only the new.
inline void on_draw_post_end(lv_event_t* e) {
  Cache* c = static_cast<Cache*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!c || !c->pending) return;
  c->pending = false;
  const lv_area_t r = c->pending_area;
  if (!usable(layer) || !clock_shadow_paint::layer_settled(layer)) {
    invalidate(*c);
    return;
  }
  const size_t n = static_cast<size_t>(lv_area_get_width(&r)) * sizeof(uint16_t);
  for (int32_t y = r.y1; y <= r.y2; ++y) {
    memcpy(kept_px(c->drawn, *c, r.x1, y), layer_px(layer, r.x1, y), n);
    const int32_t row = y - c->key.rect.y1;
    const bool touch = c->lo[row] <= c->hi[row] && c->lo[row] <= r.x2 + 1 && c->hi[row] + 1 >= r.x1;
    c->lo[row] = static_cast<int16_t>(touch ? LV_MIN(c->lo[row], r.x1) : r.x1);
    c->hi[row] = static_cast<int16_t>(touch ? LV_MAX(c->hi[row], r.x2) : r.x2);
  }
}

// The line keeps its pixels; painter and label are its drawn children.
inline void attach(lv_obj_t* line, Cache* c, lv_obj_t* painter, lv_obj_t* label) {
  if (!line || !c) return;
  c->painter = painter;
  c->label = label;
  lv_obj_add_event_cb(line, on_draw_begin, LV_EVENT_DRAW_MAIN_BEGIN, c);
  lv_obj_add_event_cb(line, on_draw_post_end, LV_EVENT_DRAW_POST_END, c);
}

}  // namespace clock_pixel_cache
