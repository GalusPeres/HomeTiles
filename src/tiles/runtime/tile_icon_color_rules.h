#pragma once

#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/runtime/tile_icon_disc.h"

namespace tile_icon_color_rules {

// Per-tile icon colors (tile_icon_colors.h) on a state update of a Sensor
// family, Binary sensor or Energy tile: the color bar for numeric states or
// the first matching state color, else the tile's fixed icon color, else
// `fallback`, the type's own color for this state. Unavailable or unknown
// states (`known` false) always use `fallback`. The color goes through
// tile_icon_disc::set_icon_color(), so the disc glow follows; unchanged colors
// are skipped and nothing is allocated.
inline void apply(lv_obj_t* icon, const char* record, bool known,
                  const char* state, const char* display, lv_color_t fallback) {
  if (!icon) return;
  uint32_t rgb = 0;
  const lv_color_t color =
      known && tile_icon_colors::resolve(record, state, display, rgb) ? lv_color_hex(rgb)
                                                                       : fallback;
  if (lv_color_eq(lv_obj_get_style_text_color(icon, LV_PART_MAIN), color)) return;
  tile_icon_disc::set_icon_color(icon, color);
}

// Icon-and-title tiles (Scene, Folder, Back, Camera) have no entity state:
// the icon takes the tile's fixed icon color, else stays white. Called once
// when the tile is built; the disc reads the color when its options apply.
inline void apply_fixed(lv_obj_t* icon, const char* record) {
  apply(icon, record, true, "", nullptr, lv_color_white());
}

}  // namespace tile_icon_color_rules
