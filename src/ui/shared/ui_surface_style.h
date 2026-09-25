#pragma once

#include <lvgl.h>

namespace ui_surface_style {

// Baseline radii retain their inset from the original outer tile radius.
// Shared styles update cached/hidden objects without rebuilding their trees.
int radius(int baseline);
void apply_radius(lv_obj_t* obj, int baseline, lv_style_selector_t selector = 0);
void request_global_radius_refresh();
void preview_radius(int value);

void disable_tile_border(lv_obj_t* obj);
void apply_tile_border(lv_obj_t* obj, bool enabled);
void apply_global_tile_border(lv_obj_t* obj);

// Tile icon discs. A disc that follows the global icon disc option shows
// `opa` while the option is on and stays transparent while it is off; other
// discs always use `opa`. Shared styles update cached and hidden discs
// without rebuilding them.
void apply_icon_disc_opa(lv_obj_t* obj, lv_opa_t opa, bool follows_global);

// Safe to call from the Web handler: only sets a flag. Apply the actual
// LVGL update later during the safe UI service pass.
void request_global_tile_border_refresh();
void request_icon_disc_refresh();
void process_pending_updates();

}  // namespace ui_surface_style
