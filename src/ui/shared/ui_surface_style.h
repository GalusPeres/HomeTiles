#pragma once

#include <lvgl.h>

namespace ui_surface_style {

// Baseline radii retain their inset from the original outer tile radius.
// Shared styles update cached/hidden objects without rebuilding their trees.
int radius(int baseline);
void apply_radius(lv_obj_t* obj, int baseline, lv_style_selector_t selector = 0);
void request_global_radius_refresh();
void preview_radius(int value);

// The border hairline color for a glowing icon: its hue halfway to white. At
// the hairline's 20 % the border stays mostly the tile, slightly lighter,
// with only a hint of the icon.
inline lv_color_t border_hint(lv_color_t icon) {
  return lv_color_mix(lv_color_white(), icon, 128);
}

void disable_tile_border(lv_obj_t* obj);
void apply_tile_border(lv_obj_t* obj, bool enabled);
void apply_global_tile_border(lv_obj_t* obj);

// Tile icon discs. A disc that follows the global icon disc option shows
// `opa` while the option is on and stays transparent while it is off; other
// discs always use `opa`. Shared styles update cached and hidden discs
// without rebuilding them.
// Glow strength of colored icon discs (global display setting, icon_glow.h):
// the disc tint as a full opacity before the dark-tile contrast scaling.
lv_opa_t icon_glow_opa();
// Gives a tile card's border hairline (found from obj or up to three parents)
// the hint of a glowing icon (border_hint); clear restores the plain white
// hairline. Both keep the 20 % hairline opacity.
void set_tile_border_tint(lv_obj_t* obj, lv_color_t icon);
void clear_tile_border_tint(lv_obj_t* obj);
// The popup card hairline: follows the global Tile borders option, drawn like
// the tile border.
void apply_popup_border(lv_obj_t* obj, lv_color_t color, lv_opa_t opa);
void apply_icon_disc_opa(lv_obj_t* obj, lv_opa_t opa, bool follows_global);

// Safe to call from the Web handler: only sets a flag. Apply the actual
// LVGL update later during the safe UI service pass.
void request_global_tile_border_refresh();
void request_icon_disc_refresh();
void process_pending_updates();

}  // namespace ui_surface_style
