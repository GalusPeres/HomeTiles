#pragma once

#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/energy_flow/flow_geometry.h"

// The Energy flow tile's sizes on this device layout, shared by the renderer
// and the Web Admin preview (web_scripts.cpp). The design values are the
// V2's (scale 1); the other layouts scale them like every tile.
namespace energy_flow {

inline Metrics device_metrics() {
  Metrics m;
  // The decided node: a 56 px disc in a 6 px ring, 68 px in all (user
  // 2026-10-24); the device's tile radius is smaller on some layouts and
  // gave smaller circles (b532).
  m.ring = tile_layout::scale(34);
  m.disc = tile_layout::scale(28);
  m.inset_x = tile_layout::scale(24);
  m.inset_y = tile_layout::scale(20);
  m.spacing = tile_layout::scale(18);
  return m;
}

inline int line_width() { return LV_MAX(2, tile_layout::scale(4)); }
inline int dot_radius() { return LV_MAX(3, tile_layout::scale(5)); }
inline const lv_font_t* value_font() { return tile_layout::content_font_20(); }
// A value's distance from its node's ring, and the extra drop of the values
// under grid and home (mockup: their baseline 15 below that distance).
inline int value_gap() { return tile_layout::scale(12); }
inline int value_below() { return tile_layout::scale(15); }
// The values' room ends this far from the tile's edge.
inline int value_edge() { return tile_layout::scale(6); }
// The header title: its right end and its baseline (picture 66).
inline int title_right() { return tile_layout::scale(17); }
inline int title_baseline() { return tile_layout::scale(44); }

}  // namespace energy_flow
