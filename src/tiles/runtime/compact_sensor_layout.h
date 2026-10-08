#pragma once

#include <algorithm>

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/ui/shared/ui_surface_style.h"

namespace compact_sensor_layout {
inline int title_size() {
#if defined(DEVICE_LAYOUT_480X480)
  return 14;
#elif defined(DEVICE_LAYOUT_1024X600)
  return 16;
#else
  return 20;
#endif
}
inline int inset() { return tile_icon_disc::inset(); }
inline int text_gap() { return 0; }
inline const lv_font_t* title_font() { return tile_layout::header_title_font(); }
inline int header_height() { return tile_icon_disc::row_height(); }
inline const lv_font_t* step_font(uint8_t step) {
  switch (step) {
    case 1: return tile_layout::content_font_24();
    case 2: return tile_layout::content_font_28();
    default: return tile_layout::content_font_20();
  }
}
// Half-height tiles show the value at the title size by default (20 px on the
// 1280x800 layouts), or at 24 or 28 when chosen (sensor_value_font); 32 and
// 40 use 28 here. Steps: 0 = title size, 1 = 24, 2 = 28. A step whose value
// would end below the half-height row steps down: since the classic grid
// keeps one grid gap to the screen edge (2026-10-08) the 1280x800 panels'
// half tile is 62 px, where 28 sticks out 2 px, so they show 24 (user
// 2026-10-08); larger rows (head bar layouts, other panels) keep 28.
inline uint8_t value_step(uint8_t choice) {
  uint8_t step = choice == 2 ? 1 : choice >= 3 && choice <= SENSOR_VALUE_FONT_MAX ? 2 : 0;
  while (step > 0 && title_font()->line_height + text_gap() + step_font(step)->line_height > header_height()) --step;
  return step;
}
inline int value_size(uint8_t choice = 0) {
#if defined(DEVICE_LAYOUT_480X480)
  static constexpr int kSizes[] = {14, 16, 20};
#elif defined(DEVICE_LAYOUT_1024X600)
  static constexpr int kSizes[] = {16, 20, 24};
#else
  static constexpr int kSizes[] = {20, 24, 28};
#endif
  return kSizes[value_step(choice)];
}
inline const lv_font_t* value_font(uint8_t choice = 0) { return step_font(value_step(choice)); }
// Top of the title line, the title and value block centered on the disc row;
// without a value line (half-height Back) the title alone is centered. The
// value follows one title line and the gap lower. The Web Admin preview takes
// the same tops (--compact-*-top).
inline int text_top(bool with_value, uint8_t value_choice = 0) {
  const int block = title_font()->line_height +
                    (with_value ? value_font(value_choice)->line_height + text_gap() : 0);
  return std::max<int>(0, (header_height() - block) / 2);
}
// Half-height tile content (icon disc, title and value lines) on a card of the
// given width. Overlays that look like a half-height tile use it directly.
inline void apply_content(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title, lv_obj_t* value, int width,
                          uint8_t value_choice = 0) {
  lv_obj_set_style_pad_all(card, 0, 0);
  const int margin = inset();
  const int height = header_height();
  const int text_x = height + margin;
  if (lv_obj_t* disc = tile_icon_disc::wrap(card, icon)) {
    tile_icon_disc::place_in_corner(card, disc);
    lv_obj_move_background(disc);
  }
  auto text = [&](lv_obj_t* label, const lv_font_t* font, int y) {
    if (!label) return;
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_size(label, width - text_x - margin * 2, font->line_height);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, text_x, y);
  };
  if (title) {
    if (auto* state = hometiles_title::state_for(title)) state->single_line = true;
  }
  const int text_y = text_top(value != nullptr, value_choice);
  text(title, title_font(), text_y);
  text(value, value_font(value_choice), text_y + title_font()->line_height + text_gap());
}
inline void apply(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title, lv_obj_t* value, const Tile& tile) {
  apply_fractional_tile_geometry(card, tile);
  apply_content(card, icon, title, value,
                tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP),
                tile.sensor_value_font);
}
}  // namespace compact_sensor_layout
