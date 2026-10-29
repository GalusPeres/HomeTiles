#pragma once

#include <algorithm>
#include <lvgl.h>

#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/climate/layout.h"

namespace tile_graph_box {

// The box under the Switch header (tile_header.h) in card coordinates: the
// Sensor graph tile's area, its looks and the Energy tile's bars. One
// Climate gap below the corner disc, the outer inset from the card's sides
// and bottom.
inline lv_area_t box(const Tile& tile) {
  const int tile_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int tile_h = tile_geometry::extent(tile.row, std::max(1.0f, tile.span_h), GRID_CELL_H, GRID_GAP);
  const int icon_width =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  const int top = climate_layout::content_top(tile_icon_disc::header_diameter(icon_width), tile_icon_disc::inset());
  return {climate_layout::kOuterInset, top, tile_w - climate_layout::kOuterInset - 1,
          tile_h - climate_layout::kOuterInset - 1};
}

}  // namespace tile_graph_box
