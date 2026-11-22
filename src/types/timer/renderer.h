#pragma once

#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer.h"

// The Timer tile: the panel's voice timers (voice_timers.h) in the Sensor
// tile's look, the icon circle top left, the countdown and the name at the
// bottom. The icon follows the timer's name (timer_icons.h), else the
// tile's own icon, else a timer. A tap opens the Timers popup. From two
// cells wide the tile adds controls: presets without a timer, pause/resume,
// +1 min and cancel for one, +1 min and the next two for more.
lv_obj_t* render_timer_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index);

namespace timer_tile {

// The card's padding (the Sensor tile's, a smaller bottom one) and the
// corner circle's place in its content box, shared with the Web Admin
// preview (web_admin_styles.cpp --timer-*).
constexpr int kPadTop = tile_layout::scale_480(24);
constexpr int kPadSide = tile_layout::scale_480(20);
constexpr int kPadBottom = tile_layout::scale_480(14);

struct Geometry {
  int disc = 0;      // the corner circle's side (and the buttons' height)
  int center_y = 0;  // its middle from the content top
  int beside = 0;    // where text beside it starts from the content left
};

inline Geometry geometry() {
  Geometry g;
  const int icon_w = FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0)
                                    : tile_icon_disc::round_diameter();
  g.disc = tile_icon_disc::header_diameter(icon_w);
  g.center_y = tile_icon_disc::inset() - kPadTop + g.disc / 2;
  g.beside = tile_icon_disc::inset() - kPadSide + g.disc + Device::kGridGap / 2;
  return g;
}

// From this card height a one-column tile shows "Tap to start" under the
// name: the corner circle, the countdown and two text lines fit.
inline int hint_min_height() {
  return tile_icon_disc::inset() + geometry().disc + lv_font_get_line_height(tile_layout::content_font_28()) +
         2 * lv_font_get_line_height(tile_layout::header_title_font()) + kPadBottom;
}

// The countdowns every tick, the layout when the list changed (timer_ui's
// loop service). Loop task only.
void refresh();

}  // namespace timer_tile
