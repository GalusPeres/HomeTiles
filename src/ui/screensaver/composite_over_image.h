#pragma once

#include <string.h>

#include <lvgl.h>
#include <lvgl_private.h>

// The screensaver frame: the wallpaper fills it under the tiles and the
// clock. LVGL's image draw copied it with LVGL's own memcpy (130-143 ms for
// 1280x800 on the V2, b304/b305, about twice the C library's copy). Here the
// frame takes the picture with one copy and LVGL draws only what lies above
// it, the way lv_snapshot_take_to_draw_buf draws from its top object: the
// image's younger siblings, then each parent's younger siblings up to `top`,
// with the parents' post draw events. Only when LVGL itself would start from
// the image (it covers the frame, opaque) and would draw it as a plain copy
// (native RGB565 of the frame's size and stride, not scaled, rotated or
// recolored); otherwise false, `refused` names the failed check and the
// caller takes the snapshot.
namespace composite_over_image {

// Optional timing for the caller's log: the picture's copy and LVGL's drawing
// above it, measured with the caller's clock.
struct Timing {
  uint32_t (*now_ms)() = nullptr;
  uint32_t copy_ms = 0;
  uint32_t draw_ms = 0;
};

inline bool render(lv_display_t* display, lv_obj_t* top, lv_obj_t* image,
                   const lv_image_dsc_t* picture, lv_draw_buf_t* buf,
                   const char** refused = nullptr, Timing* timing = nullptr) {
  auto refuse = [refused](const char* why) {
    if (refused) *refused = why;
    return false;
  };
  if (!display || !top || !image || !picture || !buf || !picture->data) return refuse("missing");
  if (lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN)) return refuse("hidden");
  if (lv_image_get_src(image) != picture) return refuse("source");
  const uint32_t w = buf->header.w;
  const uint32_t h = buf->header.h;
  const size_t bytes = static_cast<size_t>(buf->header.stride) * h;
  if (buf->header.cf != LV_COLOR_FORMAT_RGB565 || picture->header.cf != LV_COLOR_FORMAT_RGB565) {
    return refuse("format");
  }
  if (picture->header.w != w || picture->header.h != h ||
      picture->header.stride != buf->header.stride || picture->data_size < bytes) {
    return refuse("size");
  }
  if (lv_image_get_scale(image) != LV_SCALE_NONE || lv_image_get_rotation(image) != 0 ||
      lv_obj_get_style_image_recolor_opa(image, LV_PART_MAIN) != LV_OPA_TRANSP) {
    return refuse("transform");
  }
  lv_obj_update_layout(top);
  lv_area_t frame;
  lv_obj_get_coords(top, &frame);
  if (lv_area_get_width(&frame) != static_cast<int32_t>(w) ||
      lv_area_get_height(&frame) != static_cast<int32_t>(h)) {
    return refuse("frame");
  }
  lv_area_t coords;
  lv_obj_get_coords(image, &coords);
  if (!lv_area_is_equal(&coords, &frame)) return refuse("position");
  // The image drawn as nothing but its picture, opaque.
  if (lv_obj_get_style_image_opa(image, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_opa(image, LV_PART_MAIN) < LV_OPA_MAX ||
      lv_obj_get_layer_type(image) != LV_LAYER_TYPE_NONE ||
      lv_obj_get_style_blend_mode(image, LV_PART_MAIN) != LV_BLEND_MODE_NORMAL ||
      lv_image_get_bitmap_map_src(image)) {
    return refuse("opacity");
  }
  if (lv_obj_get_style_bg_opa(image, LV_PART_MAIN) != LV_OPA_TRANSP ||
      lv_obj_get_style_border_width(image, LV_PART_MAIN) != 0 ||
      lv_obj_get_style_outline_width(image, LV_PART_MAIN) != 0 ||
      lv_obj_get_style_shadow_width(image, LV_PART_MAIN) != 0 ||
      lv_obj_get_style_radius(image, LV_PART_MAIN) != 0) {
    return refuse("style");
  }
  // Where LVGL starts the frame: the image itself, or (an image with a
  // transparent background never answers its cover check with "cover") its
  // opaque parent. The picture covers the whole frame opaquely, so whatever
  // the parent and the older siblings draw first is hidden under it. A
  // parent with rounded corners does not cover the frame, and LVGL starts
  // from the bottom.
  lv_obj_t* start = lv_refr_get_top_obj(&frame, top);
  if (start != image && start != lv_obj_get_parent(image)) return refuse("start");

  const bool timed = timing && timing->now_ms;
  const uint32_t copy_started = timed ? timing->now_ms() : 0;
  memcpy(buf->data, picture->data, bytes);
  const uint32_t draw_started = timed ? timing->now_ms() : 0;

  lv_layer_t layer;
  lv_layer_init(&layer);
  layer.draw_buf = buf;
  layer.buf_area = frame;
  layer.color_format = LV_COLOR_FORMAT_RGB565;
  layer._clip_area = frame;
  layer.phy_clip_area = frame;
  lv_draw_unit_send_event(nullptr, LV_EVENT_CHILD_CREATED, &layer);
  lv_display_t* display_old = lv_refr_get_disp_refreshing();
  lv_layer_t* layer_old = display->layer_head;
  display->layer_head = &layer;
  lv_refr_set_disp_refreshing(display);

  lv_obj_t* parent = lv_obj_get_parent(image);
  lv_obj_t* border = image;
  while (parent && border != top) {
    bool above = false;
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; ++i) {
      lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
      if (!above) {
        above = child == border;
      } else {
        lv_obj_refr(&layer, child);
      }
    }
    lv_obj_send_event(parent, LV_EVENT_DRAW_POST_BEGIN, &layer);
    lv_obj_send_event(parent, LV_EVENT_DRAW_POST, &layer);
    lv_obj_send_event(parent, LV_EVENT_DRAW_POST_END, &layer);
    border = parent;
    parent = lv_obj_get_parent(parent);
  }
  layer.all_tasks_added = true;
  while (layer.draw_task_head) {
    lv_draw_dispatch_wait_for_request();
    lv_draw_dispatch();
  }
  display->layer_head = layer_old;
  lv_refr_set_disp_refreshing(display_old);
  lv_draw_unit_send_event(nullptr, LV_EVENT_SCREEN_LOAD_START, &layer);
  if (timed) {
    timing->copy_ms = draw_started - copy_started;
    timing->draw_ms = timing->now_ms() - draw_started;
  }
  if (refused) *refused = nullptr;
  return true;
}

}  // namespace composite_over_image
