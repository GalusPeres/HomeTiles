#pragma once

#include <algorithm>

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
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
// Half-height tiles always show the value at the title size (20 px on the
// 1280x800 layouts), so explicit value sizes can never clip.
inline int value_size() { return title_size(); }
inline int inset() { return tile_layout::scale_480(4); }
inline int text_gap() { return 0; }
inline const lv_font_t* title_font() { return tile_layout::header_title_font(); }
inline const lv_font_t* value_font() { return tile_layout::content_font_20(); }
inline int header_height() { return (GRID_CELL_H - GRID_GAP) / 2; }
// Half-height tile content (icon disc, title and value lines) on a card of the
// given width. Overlays that look like a half-height tile use it directly.
inline void apply_content(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title, lv_obj_t* value, int width) {
  lv_obj_set_style_pad_all(card, 0, 0);
  const int margin = inset();
  const int height = header_height();
  const int diameter = height - margin * 2;
  const int text_x = height + margin;
  if (icon) {
    lv_obj_t* disc = lv_obj_create(card);
    lv_obj_remove_style_all(disc);
    lv_obj_remove_flag(disc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_pos(disc, margin, margin);
    lv_obj_set_size(disc, diameter, diameter);
    ui_surface_style::apply_radius(disc, tile_layout::scale_480(22) - margin, 0);
    lv_obj_set_style_bg_color(disc, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(disc, static_cast<lv_opa_t>(38), 0);
    lv_obj_move_background(disc);
    lv_obj_set_parent(icon, disc);
    lv_obj_center(icon);
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
  const lv_font_t* value_face = value_font();
  const int gap = text_gap();
  const int text_y = std::max<int>(0, (height - title_font()->line_height - value_face->line_height - gap) / 2);
  text(title, title_font(), text_y);
  text(value, value_face, text_y + title_font()->line_height + gap);
}
inline void apply(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title, lv_obj_t* value, const Tile& tile) {
  apply_fractional_tile_geometry(card, tile);
  apply_content(card, icon, title, value,
                tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP));
}
}  // namespace compact_sensor_layout
