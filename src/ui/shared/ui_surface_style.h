#pragma once

#include <lvgl.h>
#include "src/ui/shared/ui_theme.h"

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
  return lv_color_mix(lv_color_hex(ui_theme::text()), icon, 128);
}

void disable_tile_border(lv_obj_t* obj);
void apply_tile_border(lv_obj_t* obj, bool enabled);
void apply_global_tile_border(lv_obj_t* obj);

// Tile icon discs. Every disc and control carries its own color
// (tone_color::fill) and one shared opacity: opaque, or the veil of the
// global Circle strength (icon_glow.h) on see-through cards, so cached and
// hidden discs follow the global options without a rebuild.
uint8_t icon_glow_percent();
// The global icon disc option, which discs in Global mode follow.
bool icon_discs_shown();
// Gives a tile card's border hairline (found from obj or up to three parents)
// the hint of a glowing icon (border_hint); clear restores the plain white
// hairline. Both keep the 20 % hairline opacity.
void set_tile_border_tint(lv_obj_t* obj, lv_color_t icon);
void clear_tile_border_tint(lv_obj_t* obj);
// The popup card hairline: follows the global Tile borders option, drawn like
// the tile border.
void apply_popup_border(lv_obj_t* obj, lv_color_t color, lv_opa_t opa);
// Gives a disc the shared opacity of its mode (tone_color::disc_opa: opaque,
// a veil on a `see_through` card, none at 0 %), transparent for Off discs and
// for Global discs while the global option is off.
void apply_icon_disc(lv_obj_t* obj, bool off, bool follows_global, bool see_through = false);
// Gives a tile control the control fill for `selector` (pressed for buttons,
// the main part for surfaces): `color` (tone_color::Fill::control_color) at
// the shared control opacity (tone_color::control_opa). Local opacities of
// the owner still win.
void apply_control_fill(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector, bool see_through = false);

// Safe to call from the Web handler: only sets a flag. Apply the actual
// LVGL update later during the safe UI service pass.
void request_global_tile_border_refresh();
void request_icon_disc_refresh();
void process_pending_updates();

}  // namespace ui_surface_style
