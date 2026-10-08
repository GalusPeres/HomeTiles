#pragma once

#include <lvgl.h>
// lv_image_cache_drop(): the firmware's LVGL include root is its src folder,
// the host test's is the library folder.
#if __has_include(<misc/cache/instance/lv_image_cache.h>)
#include <misc/cache/instance/lv_image_cache.h>
#elif __has_include(<src/misc/cache/instance/lv_image_cache.h>)
#include <src/misc/cache/instance/lv_image_cache.h>
#endif

// The screensaver clock's soft shadow is nine faint dark copies of each line
// (clock/renderer.cpp). Drawing all of them in every frame cost about 100 ms
// of each screensaver opening and slide on the V2 (b304: composite snapshot
// 290 ms with the shadow, 192 ms without). The copies now sit in a hidden
// group that is drawn once into an ARGB8888 picture whenever the line's text
// or width changes (once a minute); frames draw that picture behind the
// white text. Same pixels: black copies over a transparent picture, then the
// picture over the background (host test).
namespace clock_shadow_cache {

// Draws the hidden `group` into a new picture: the normal refresh skips a
// hidden object, a snapshot draws it with its visible children. nullptr when
// LVGL memory is short.
inline lv_draw_buf_t* render(lv_obj_t* group) {
  if (!group) return nullptr;
  return lv_snapshot_take(group, LV_COLOR_FORMAT_ARGB8888);
}

// Shows `next` in `image` and frees the picture it replaces.
inline void show(lv_obj_t* image, lv_draw_buf_t*& current, lv_draw_buf_t* next) {
  if (!image) return;
  lv_image_set_src(image, next);
  if (current && current != next) {
    lv_image_cache_drop(current);
    lv_draw_buf_destroy(current);
  }
  current = next;
}

// Frees the picture; the image shows nothing until the next show().
inline void release(lv_obj_t* image, lv_draw_buf_t*& current) {
  if (image) lv_image_set_src(image, nullptr);
  if (!current) return;
  lv_image_cache_drop(current);
  lv_draw_buf_destroy(current);
  current = nullptr;
}

}  // namespace clock_shadow_cache
