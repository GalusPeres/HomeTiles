#pragma once

#include <lvgl.h>

#include <algorithm>
#include <cstring>

#include "src/fonts/ui_fonts.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_theme.h"

// The header of tiles with a state line below the corner disc (the Switch
// header layouts, the Cover with its position bar, the Climate Layout "with
// value"): one row high, title and state left-aligned beside the corner disc
// like the half-height tiles (compact_sensor_layout: two insets from the
// disc, the block centered on it, no gap between the lines), the state at the
// half-height Sensor value size. From 1.5 rows (`tall`) the Sensor tile's
// title top right and the state at a Sensor value size centered between the
// disc and the bar below. A longer state steps its font down instead of being
// shortened. Labels sit at explicit positions in the card's content box (its
// paddings), so the corner disc (add_round) does not move them; a half-height
// tile moves them with compact_sensor_layout::apply.
namespace tile_header {

struct Header {
  lv_obj_t* title = nullptr;
  lv_obj_t* state = nullptr;
  // The chosen state size, the largest the state steps down from.
  const lv_font_t* state_font = nullptr;
  // Tall tiles: the state's vertical center in the content box, kept when the
  // font steps down; -1 beside the disc.
  int16_t state_center = -1;
  // Width the state may use before its font steps down.
  int16_t state_width = 0;
};

// The largest font up to `largest` that fits `text` into `width` (12 and
// 14 px exist only in the 480x480 layout, ui_fonts.h).
inline const lv_font_t* fitting_font(const char* text, int width, const lv_font_t* largest) {
  static const lv_font_t* const kSizes[] = {&ui_font_40, &ui_font_32, &ui_font_28, &ui_font_24,
                                            &ui_font_20, &ui_font_16,
#if defined(DEVICE_LAYOUT_480X480)
                                            &ui_font_14, &ui_font_12,
#endif
  };
  const int32_t start = lv_font_get_line_height(largest);
  const lv_font_t* last = kSizes[sizeof(kSizes) / sizeof(kSizes[0]) - 1];
  for (const lv_font_t* font : kSizes) {
    if (lv_font_get_line_height(font) > start) continue;
    last = font;
    if (width <= 0) return font;
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= width) return font;
  }
  return last;
}

// Sets a state line. Translated states never shorten: with `largest` the font
// steps down until the text fits `width` (null keeps the font, half-height
// tiles); a tall state keeps its vertical `center`. Only a change touches the
// label.
inline void set_state(lv_obj_t* label, const char* text, const lv_font_t* largest, int width, int center) {
  if (!label || !text) return;
  if (strcmp(lv_label_get_text(label), text) == 0) return;
  if (largest) {
    const lv_font_t* font = fitting_font(text, width, largest);
    if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != font) {
      lv_obj_set_style_text_font(label, font, 0);
      if (center >= 0) lv_obj_set_y(label, center - lv_font_get_line_height(font) / 2);
    }
  }
  lv_label_set_text(label, text);
}

// Creates the title (if the tile has one) and the state label. `bar_top`: the
// top of the bar below (level_bar::box), used by tall tiles.
inline Header create(lv_obj_t* card, const Tile& tile, bool tall, int bar_top) {
  Header header;
  const int card_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int inset = tile_icon_disc::inset();
  const int icon_width =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  const int disc = tile_icon_disc::header_diameter(icon_width);
  const int text_x = inset + disc + 2 * inset;
  const int text_w = std::max(1, card_w - text_x - 2 * inset);
  const bool has_title = tile.title.length() > 0;
  const int title_h = lv_font_get_line_height(compact_sensor_layout::title_font());
  const lv_font_t* state_font = tall ? tile_layout::value_font_for_choice(tile.sensor_value_font, FONT_VALUE)
                                     : compact_sensor_layout::value_font(tile.sensor_value_font);
  header.state_font = state_font;
  const int state_h = lv_font_get_line_height(state_font);
  const int block = (has_title ? title_h : 0) + state_h;
  const int text_y = inset + disc / 2 - block / 2;
  // Label positions are inside the card's content box.
  const int pad_x = lv_obj_get_style_pad_left(card, LV_PART_MAIN);
  const int pad_y = lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  if (tall) {
    if (has_title) {
      header.title = lv_label_create(card);
      set_label_style(header.title, lv_color_hex(ui_theme::text()), tile_layout::header_title_font());
      lv_label_set_long_mode(header.title, LV_LABEL_LONG_DOT);
      lv_obj_set_width(header.title, LV_PCT(70));
      lv_obj_set_style_text_align(header.title, LV_TEXT_ALIGN_RIGHT, 0);
      hometiles_title::tile(header.title, tile.title.c_str(), true);
      lv_obj_align(header.title, LV_ALIGN_TOP_RIGHT, tile_layout::scale_480(4), tile_layout::scale_480(4));
    }
    const int content_w = std::max(1, card_w - 2 * pad_x);
    header.state_center = static_cast<int16_t>((inset + disc + bar_top) / 2 - pad_y);
    header.state = lv_label_create(card);
    set_label_style(header.state, lv_color_hex(ui_theme::text()), state_font);
    lv_label_set_long_mode(header.state, LV_LABEL_LONG_DOT);
    lv_obj_set_width(header.state, content_w);
    lv_obj_set_style_text_align(header.state, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(header.state, "--");
    lv_obj_set_pos(header.state, 0, header.state_center - state_h / 2);
    lv_obj_clear_flag(header.state, LV_OBJ_FLAG_CLICKABLE);
    header.state_width = static_cast<int16_t>(content_w);
    return header;
  }
  if (has_title) {
    header.title = lv_label_create(card);
    set_label_style(header.title, lv_color_hex(ui_theme::text()), compact_sensor_layout::title_font());
    lv_label_set_long_mode(header.title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(header.title, text_w);
    hometiles_title::tile(header.title, tile.title.c_str(), true);
    // One title line: the state line takes the second.
    if (auto* title_state = hometiles_title::state_for(header.title)) title_state->single_line = true;
    lv_obj_set_style_text_align(header.title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(header.title, text_x - pad_x, text_y - pad_y);
  }
  header.state = lv_label_create(card);
  set_label_style(header.state, lv_color_hex(ui_theme::text()), state_font);
  lv_label_set_long_mode(header.state, LV_LABEL_LONG_DOT);
  lv_obj_set_width(header.state, text_w);
  lv_obj_set_style_text_align(header.state, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(header.state, "--");
  lv_obj_set_pos(header.state, text_x - pad_x, text_y + (has_title ? title_h : 0) - pad_y);
  lv_obj_clear_flag(header.state, LV_OBJ_FLAG_CLICKABLE);
  header.state_width = static_cast<int16_t>(text_w);
  return header;
}

}  // namespace tile_header
