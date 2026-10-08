#pragma once

#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <lvgl_private.h>

// The screensaver clock's shadow: nine faint dark copies of one A8 mask of a
// clock line (renderer.cpp). As nine lv_image objects LVGL blended each copy
// over its whole picture, the empty margin of the mask included, with its own
// draw task: about 87 ms of every screensaver frame on the V2 for time and
// date (b308; with the shadow off "above" fell from about 120 to 33 ms). One
// object draws all nine now. On an RGB565 layer it takes each pixel through
// the copies in their order with LVGL's own arithmetic for an A8 image with
// opacity (lv_draw_sw_blend_color_to_rgb565, mask and opacity) and skips the
// mask's empty rows and margins: the same pixels as the nine images (host
// test test-clock-shadow-mask-lvgl). On any other layer, or while LVGL still
// has unfinished work below it, it hands LVGL the nine images' draws.
namespace clock_shadow_paint {

constexpr uint8_t kCopies = 9;

struct Copy {
  int16_t x;
  int16_t y;
  lv_opa_t opa;
};

struct Shadow {
  const lv_draw_buf_t* mask = nullptr;  // A8, owned by the caller
  // The copies' offsets from the painter's top left corner.
  Copy copies[kCopies] = {};
  // Per mask row its first and last non-zero column, -1 for an empty row.
  int16_t* row_first = nullptr;
  int16_t* row_last = nullptr;
  uint32_t rows = 0;
};

inline void release(Shadow& shadow) {
  if (shadow.row_first) lv_free(shadow.row_first);
  if (shadow.row_last) lv_free(shadow.row_last);
  shadow.row_first = nullptr;
  shadow.row_last = nullptr;
  shadow.rows = 0;
  shadow.mask = nullptr;
}

// The painter's place and size: every copy of the mask at its offset
// (`copies` relative to the line, the mask reaching `ext` beyond the text on
// every side, like the nine images at copies[i] - ext).
inline void place(lv_obj_t* painter, Shadow& shadow, const lv_draw_buf_t* mask,
                  const Copy (&copies)[kCopies], int32_t ext) {
  release(shadow);
  if (!painter) return;
  if (!mask || mask->header.cf != LV_COLOR_FORMAT_A8 || !mask->data ||
      mask->header.w == 0 || mask->header.h == 0) {
    lv_obj_add_flag(painter, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const uint32_t w = mask->header.w;
  const uint32_t h = mask->header.h;
  shadow.row_first = static_cast<int16_t*>(lv_malloc(h * sizeof(int16_t)));
  shadow.row_last = static_cast<int16_t*>(lv_malloc(h * sizeof(int16_t)));
  if (!shadow.row_first || !shadow.row_last) {
    release(shadow);
    lv_obj_add_flag(painter, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* row = mask->data + y * mask->header.stride;
    int32_t first = -1;
    int32_t last = -1;
    for (uint32_t x = 0; x < w; ++x) {
      if (!row[x]) continue;
      if (first < 0) first = static_cast<int32_t>(x);
      last = static_cast<int32_t>(x);
    }
    shadow.row_first[y] = static_cast<int16_t>(first);
    shadow.row_last[y] = static_cast<int16_t>(last);
  }
  shadow.rows = h;
  shadow.mask = mask;

  int32_t min_x = copies[0].x, max_x = copies[0].x;
  int32_t min_y = copies[0].y, max_y = copies[0].y;
  for (const Copy& c : copies) {
    min_x = LV_MIN(min_x, c.x);
    max_x = LV_MAX(max_x, c.x);
    min_y = LV_MIN(min_y, c.y);
    max_y = LV_MAX(max_y, c.y);
  }
  for (uint8_t i = 0; i < kCopies; ++i) {
    shadow.copies[i] = {static_cast<int16_t>(copies[i].x - min_x),
                        static_cast<int16_t>(copies[i].y - min_y), copies[i].opa};
  }
  lv_obj_remove_flag(painter, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(painter, min_x - ext, min_y - ext);
  lv_obj_set_size(painter, max_x - min_x + static_cast<int32_t>(w),
                  max_y - min_y + static_cast<int32_t>(h));
  lv_obj_invalidate(painter);  // The same place may hold a new mask.
}

// Every draw LVGL queued on the layer before the painter is done (with
// LV_OS_NONE it draws each task as it is added; a layer task can wait for its
// child layer). False when work is left: the caller hands LVGL the draws.
inline bool layer_settled(lv_layer_t* layer) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    bool pending = false;
    for (lv_draw_task_t* t = layer->draw_task_head; t; t = t->next) {
      if (t->state != LV_DRAW_TASK_STATE_FINISHED && t->state != LV_DRAW_TASK_STATE_FAILED) {
        pending = true;
        break;
      }
    }
    if (!pending) return true;
    lv_draw_dispatch();
  }
  return false;
}

// LVGL's mix of a prepared colour into an RGB565 pixel
// (LV_COLOR_MIX_16_TO_16_PREPARED), mix 1..254.
inline uint16_t mix_rgb565(uint32_t fg_prep, uint16_t bg, uint32_t mix) {
  const uint32_t bg_prep = (static_cast<uint32_t>(bg) | (static_cast<uint32_t>(bg) << 16)) & 0x07E0F81Fu;
  const uint32_t mix5 = (mix + 4) >> 3;
  const uint32_t res = ((((fg_prep - bg_prep) * mix5) >> 5) + bg_prep) & 0x07E0F81Fu;
  return static_cast<uint16_t>((res >> 16) | res);
}

inline void paint_rgb565(lv_layer_t* layer, const lv_area_t& coords, const Shadow& shadow,
                         lv_color_t color, const lv_opa_t (&opa)[kCopies]) {
  lv_area_t clip;
  if (!lv_area_intersect(&clip, &coords, &layer->_clip_area)) return;
  if (!lv_area_intersect(&clip, &clip, &layer->buf_area)) return;
  const lv_draw_buf_t* mask = shadow.mask;
  const int32_t mask_h = static_cast<int32_t>(mask->header.h);
  const uint32_t mask_stride = mask->header.stride;
  const uint16_t color16 = lv_color_to_u16(color);
  const uint32_t fg_prep = (static_cast<uint32_t>(color16) | (static_cast<uint32_t>(color16) << 16)) & 0x07E0F81Fu;
  const uint32_t dest_stride = layer->draw_buf->header.stride;

  int32_t copy_x[kCopies];
  int32_t copy_y[kCopies];
  for (uint8_t i = 0; i < kCopies; ++i) {
    copy_x[i] = coords.x1 + shadow.copies[i].x;
    copy_y[i] = coords.y1 + shadow.copies[i].y;
  }
  for (int32_t y = clip.y1; y <= clip.y2; ++y) {
    // This row of every copy that reaches it: the mask row and its columns
    // with ink, clipped.
    const uint8_t* rows[kCopies];
    int32_t lo[kCopies];
    int32_t hi[kCopies];
    uint8_t active[kCopies];
    uint8_t count = 0;
    int32_t span_lo = clip.x2 + 1;
    int32_t span_hi = clip.x1 - 1;
    for (uint8_t i = 0; i < kCopies; ++i) {
      if (!opa[i]) continue;
      const int32_t r = y - copy_y[i];
      if (r < 0 || r >= mask_h || shadow.row_first[r] < 0) continue;
      lo[i] = LV_MAX(copy_x[i] + shadow.row_first[r], clip.x1);
      hi[i] = LV_MIN(copy_x[i] + shadow.row_last[r], clip.x2);
      if (lo[i] > hi[i]) continue;
      rows[i] = mask->data + static_cast<uint32_t>(r) * mask_stride;
      active[count++] = i;
      span_lo = LV_MIN(span_lo, lo[i]);
      span_hi = LV_MAX(span_hi, hi[i]);
    }
    if (!count) continue;
    uint16_t* dest = reinterpret_cast<uint16_t*>(layer->draw_buf->data +
                                                 static_cast<uint32_t>(y - layer->buf_area.y1) * dest_stride) +
                     (span_lo - layer->buf_area.x1);
    // The row's pixels go through a small buffer on the stack in pieces:
    // read once, the copies blended one after the other like LVGL's passes
    // over the images, written back once (the frame lies in PSRAM).
    constexpr int32_t kPiece = 256;
    uint16_t piece[kPiece];
    for (int32_t from = span_lo; from <= span_hi; from += kPiece) {
      const int32_t to = LV_MIN(from + kPiece - 1, span_hi);
      uint16_t* out = dest + (from - span_lo);
      const int32_t n = to - from + 1;
      memcpy(piece, out, static_cast<size_t>(n) * sizeof(uint16_t));
      // The copies in their order, as LVGL blends one image after the other.
      for (uint8_t k = 0; k < count; ++k) {
        const uint8_t i = active[k];
        const int32_t x1 = LV_MAX(lo[i], from);
        const int32_t x2 = LV_MIN(hi[i], to);
        if (x1 > x2) continue;
        const uint8_t* src = rows[i] + (x1 - copy_x[i]);
        uint16_t* px = piece + (x1 - from);
        const lv_opa_t o = opa[i];
        for (int32_t x = 0; x <= x2 - x1; ++x) {
          const uint32_t a = LV_OPA_MIX2(src[x], o);
          if (a) px[x] = mix_rgb565(fg_prep, px[x], a);
        }
      }
      memcpy(out, piece, static_cast<size_t>(n) * sizeof(uint16_t));
    }
  }
}

inline void on_draw_main(lv_event_t* e) {
  const Shadow* shadow = static_cast<const Shadow*>(lv_event_get_user_data(e));
  lv_obj_t* painter = lv_event_get_current_target_obj(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!shadow || !shadow->mask || !shadow->row_first || !painter || !layer) return;

  // As the nine images drew themselves (lv_image's draw: the painter carries
  // their black recolor, each copy its opacity in the layer's opacity).
  lv_draw_image_dsc_t dsc;
  lv_draw_image_dsc_init(&dsc);
  dsc.base.layer = layer;
  lv_obj_init_draw_image_dsc(painter, LV_PART_MAIN, &dsc);
  lv_opa_t opa[kCopies];
  for (uint8_t i = 0; i < kCopies; ++i) {
    lv_opa_t o = shadow->copies[i].opa;
    if (layer->opa < LV_OPA_MAX) o = LV_OPA_MIX2(o, layer->opa);
    opa[i] = o <= LV_OPA_MIN ? 0 : o;
  }
  lv_area_t coords;
  lv_obj_get_coords(painter, &coords);

  bool direct = layer->color_format == LV_COLOR_FORMAT_RGB565 && layer->draw_buf &&
                layer->draw_buf->data && layer->draw_buf->header.cf == LV_COLOR_FORMAT_RGB565 &&
                dsc.blend_mode == LV_BLEND_MODE_NORMAL && !lv_draw_sw_get_blend_handler(LV_COLOR_FORMAT_RGB565);
  for (uint8_t i = 0; i < kCopies && direct; ++i) direct = opa[i] < LV_OPA_MAX;
  if (direct && layer_settled(layer)) {
    paint_rgb565(layer, coords, *shadow, dsc.recolor, opa);
    return;
  }

  const int32_t w = static_cast<int32_t>(shadow->mask->header.w);
  const int32_t h = static_cast<int32_t>(shadow->mask->header.h);
  for (uint8_t i = 0; i < kCopies; ++i) {
    if (!opa[i]) continue;
    lv_draw_image_dsc_t copy = dsc;
    copy.opa = opa[i];
    copy.src = shadow->mask;
    lv_area_t area;
    area.x1 = coords.x1 + shadow->copies[i].x;
    area.y1 = coords.y1 + shadow->copies[i].y;
    area.x2 = area.x1 + w - 1;
    area.y2 = area.y1 + h - 1;
    copy.image_area = area;
    lv_draw_image(layer, &copy, &area);
  }
}

// A plain object in the line where the nine images were; place() sizes it.
inline lv_obj_t* create(lv_obj_t* line, Shadow* shadow) {
  lv_obj_t* painter = lv_obj_create(line);
  if (!painter) return nullptr;
  lv_obj_remove_style_all(painter);
  lv_obj_remove_flag(painter, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(painter, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_image_recolor(painter, lv_color_black(), 0);
  lv_obj_add_flag(painter, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(painter, on_draw_main, LV_EVENT_DRAW_MAIN, shadow);
  return painter;
}

}  // namespace clock_shadow_paint
