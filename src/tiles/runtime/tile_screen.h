#pragma once

#include <lvgl.h>

#include "src/ui/shared/ui_theme.h"

// The design of the screen a tile lies on (ui_theme::g_screen, user
// 2026-11-02): the screensaver marks its tile grid with its own Dark or Light
// (ScreensaverConfigData::theme), so a tile's render and every later color
// path (tile_surface::scope, its frame line, the popups it opens) follow the
// pages' rules in that design. A tile under no mark lies on the panel's
// theme (-1).
namespace tile_screen {

// A state selector the grid never takes (as tile_surface::kStore), with a
// property of its own.
inline constexpr lv_style_selector_t kStore = LV_PART_MAIN | LV_STATE_USER_1 | LV_STATE_USER_4;

// The design marked on this object; -1 none.
inline int8_t on(lv_obj_t* obj) {
  lv_style_value_t value;
  if (!obj || lv_obj_get_local_style_prop(obj, LV_STYLE_LINE_OPA, &value, kStore) != LV_STYLE_RES_FOUND) return -1;
  return static_cast<int8_t>(value.num);
}

// Marks `grid` (the screensaver's tile grid) with a design, -1 none; only a
// change touches its styles.
inline void set(lv_obj_t* grid, int8_t screen) {
  if (!grid || on(grid) == screen) return;
  if (screen < 0) {
    lv_obj_remove_local_style_prop(grid, LV_STYLE_LINE_OPA, kStore);
  } else {
    lv_obj_set_style_line_opa(grid, static_cast<lv_opa_t>(screen), kStore);
  }
}

// The design marked on `obj` or the first object it sits in that carries
// one (a tile's grid).
inline int8_t of(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 8; ++depth, obj = lv_obj_get_parent(obj)) {
    const int8_t screen = on(obj);
    if (screen >= 0) return screen;
  }
  return -1;
}

// Opens the design of `obj`'s screen, keeping the open surface.
inline ui_theme::SurfaceScope scope(lv_obj_t* obj) { return ui_theme::SurfaceScope(-1, nullptr, of(obj)); }

}  // namespace tile_screen
