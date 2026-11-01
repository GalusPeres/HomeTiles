#pragma once

#include <lvgl.h>

#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/power_flow/flow_geometry.h"

// The Power flow tile's sizes on this device layout, shared by the renderer
// and the Web Admin preview (web_scripts.cpp). The design values are the
// V2's (scale 1); the other layouts scale them like every tile.
namespace power_flow {

// The disc of a 1x1 tile on this layout, as drawn: its corner disc around
// the icon (tile_icon_disc::corner_header), shrunk to keep the bar pill's
// gap to the card edge (shrink_corner_disc).
inline int tile_disc_diameter() {
  const int icon = static_cast<int>(lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0));
  return tile_icon_disc::header_diameter(icon) - 2 * tile_icon_disc::corner_shrink();
}

inline Metrics device_metrics() {
  Metrics m;
  // A node is the disc of a 1x1 tile in a ring as wide as that disc's gap
  // to the card edge (user 2026-10-24: "gleiche Größe wie die ab 1x1
  // Kachel"; b532 took the tile radius, b533 the mockup's 56 px, both
  // smaller than the V2's 60 px tile disc).
  m.disc = tile_disc_diameter() / 2.0f;
  m.ring = m.disc + tile_icon_disc::inset() + tile_icon_disc::corner_shrink();
  // The nodes 6 px further out than the mockup (user 2026-10-26, b545).
  m.inset_x = tile_layout::scale(18);
  m.inset_y = tile_layout::scale(14);
  m.spacing = tile_layout::scale(18);
  return m;
}

inline int line_width() { return LV_MAX(2, tile_layout::scale(4)); }
inline int dot_radius() { return LV_MAX(3, tile_layout::scale(5)); }
inline const lv_font_t* value_font() { return tile_layout::content_font_20(); }
// A value's distance from its node's ring (4 px closer than the mockup, user
// 2026-10-26), and the extra drop of the values under grid and home
// (mockup: their baseline 15 below that distance).
inline int value_gap() { return tile_layout::scale(8); }
inline int value_below() { return tile_layout::scale(15); }
// The values' room ends this far from the tile's edge.
inline int value_edge() { return tile_layout::scale(6); }
// The header title: its right end and its baseline (picture 66).
inline int title_right() { return tile_layout::scale(17); }
inline int title_baseline() { return tile_layout::scale(44); }

}  // namespace power_flow
