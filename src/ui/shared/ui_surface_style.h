#pragma once

#include <lvgl.h>

namespace ui_surface_style {

// Baseline radii retain their inset from the original outer tile radius.
// Shared styles update cached/hidden objects without rebuilding their trees.
int radius(int baseline);
void apply_radius(lv_obj_t* obj, int baseline, lv_style_selector_t selector = 0);
void request_global_radius_refresh();
void preview_radius(int value);

// The hue of a surface at full brightness: the largest channel raised to 255,
// hue and saturation kept; white for greys and black. Tile and popup borders
// and neutral icon discs overlay it, so they are a lighter step of the tile in
// its own hue (grey tiles keep their white overlays).
inline lv_color_t surface_hue(lv_color_t bg) {
  const uint8_t hi = bg.red > bg.green ? (bg.red > bg.blue ? bg.red : bg.blue)
                                       : (bg.green > bg.blue ? bg.green : bg.blue);
  if (hi == 0) return lv_color_white();
  return lv_color_make(static_cast<uint8_t>(bg.red * 255 / hi),
                       static_cast<uint8_t>(bg.green * 255 / hi),
                       static_cast<uint8_t>(bg.blue * 255 / hi));
}

void disable_tile_border(lv_obj_t* obj);
// Re-applies a tile card's border after its background changed at runtime
// (rules tint); obj may be the card or a child up to three levels deep.
void refresh_tile_border(lv_obj_t* obj);
void apply_tile_border(lv_obj_t* obj, bool enabled);
void apply_global_tile_border(lv_obj_t* obj);

// Tile icon discs. A disc that follows the global icon disc option shows
// `opa` while the option is on and stays transparent while it is off; other
// discs always use `opa`. Shared styles update cached and hidden discs
// without rebuilding them.
// Glow strength of colored icon discs (global display setting, icon_glow.h):
// the disc tint as a full opacity before the dark-tile contrast scaling.
lv_opa_t icon_glow_opa();
// The popup card hairline: follows the global Tile borders option, drawn like
// the tile border (color: surface_hue of the card).
void apply_popup_border(lv_obj_t* obj, lv_color_t color, lv_opa_t opa);
void apply_icon_disc_opa(lv_obj_t* obj, lv_opa_t opa, bool follows_global);

// Safe to call from the Web handler: only sets a flag. Apply the actual
// LVGL update later during the safe UI service pass.
void request_global_tile_border_refresh();
void request_icon_disc_refresh();
void process_pending_updates();

}  // namespace ui_surface_style
