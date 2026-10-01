#pragma once

#include <lvgl.h>

#include <algorithm>

#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/types/climate/layout.h"
#include "src/types/switch/layout.h"
#include "src/ui/shared/ui_surface_style.h"

// The level bar below a tile's Sensor header (the Switch dimmer, the Cover
// position): the Climate target pill's box, a track in the card's control
// fill and a fill drawn in one DRAW_MAIN pass (switch_layout::Dimmer). Every
// tile that shows a level bar uses these functions, so geometry, drawing and
// touch mapping stay one implementation; the tile keeps its own state and
// commands.
namespace level_bar {

// The bar's box in the card: one Climate gap below the corner disc
// (climate_layout::content_top), as high as in a one-row tile, anchored at
// the card's bottom; taller tiles add a third of their extra height
// (switch_layout::bar_growth).
struct BarBox {
  int width = 0;
  int height = 0;
  int top = 0;
  int base = 0;  // the one-row height
};

inline BarBox box(const Tile& tile) {
  const int tile_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int tile_h = tile_geometry::extent(tile.row, std::max(1.0f, tile.span_h), GRID_CELL_H, GRID_GAP);
  const int icon_width =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  const int top =
      climate_layout::content_top(tile_icon_disc::header_diameter(icon_width), tile_icon_disc::inset());
  BarBox box;
  box.width = tile_w - climate_layout::kOuterInset * 2;
  box.base = GRID_CELL_H - climate_layout::kOuterInset - top;
  box.height = box.base + switch_layout::bar_growth(tile_h, GRID_CELL_H);
  box.top = tile_h - climate_layout::kOuterInset - box.height;
  return box;
}

// LVGL marks a touched object pressed before its own callbacks run. The bar
// is a resting surface that shows the pressed step only with its card
// (tile_icon_source follow_card_press); its own touch left the card
// unpressed, so the track lit up under the finger. Its pressed color is its
// resting color until the release restores the card's pressed step.
inline void press_cb(lv_event_t* e) {
  lv_obj_t* bar = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_style_value_t rest;
  if (lv_obj_get_local_style_prop(bar, LV_STYLE_BG_COLOR, &rest, LV_PART_MAIN | LV_STATE_DEFAULT) !=
      LV_STYLE_RES_FOUND) {
    return;
  }
  const uint32_t rgb = lv_color_to_u32(rest.color) & 0xFFFFFF;
  tile_icon_disc::set_fill_colors(bar, rgb, rgb);
}

inline void release_cb(lv_event_t* e) {
  lv_obj_t* bar = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  tile_icon_source::refresh_controls(lv_obj_get_parent(bar));
}

// The bar object in the card (Sensor paddings: positions are inside the
// card's content box). The caller adds its draw and touch callbacks.
inline lv_obj_t* create(lv_obj_t* card, const Tile& tile) {
  const BarBox bar_box = box(tile);
  const int bar_h = bar_box.height;
  const int bar_w = bar_box.width;
  if (bar_h < 8 || bar_w < 8) return nullptr;

  lv_obj_t* bar = lv_obj_create(card);
  if (!bar) return nullptr;
  // The control fill of the card (tile_icon_source::refresh_controls), like
  // the Climate target pill. Its shared style sets color and opacity; a local
  // bg_opa would outrank it and hide the bar's track.
  tile_icon_disc::mark_surface(bar);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_shadow_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  ui_surface_style::apply_radius(bar, climate_layout::kControlRadius, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  // The bar owns its touches: dragging must not scroll the grid or press
  // the card.
  lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  // Touch area up to the card edges (left, right, bottom) and the same
  // distance up towards the disc (approved touch zones, 2026-10-01): a touch
  // just beside the bar still reaches the bar, never the card's toggle or
  // popup.
  lv_obj_set_ext_click_area(bar, climate_layout::kOuterInset);
  lv_obj_add_event_cb(bar, press_cb, static_cast<lv_event_code_t>(LV_EVENT_PRESSED | LV_EVENT_PREPROCESS), nullptr);
  lv_obj_add_event_cb(bar, release_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(bar, release_cb, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_set_size(bar, bar_w, bar_h);
  lv_obj_set_pos(bar, climate_layout::kOuterInset - tile_layout::scale_480(20),
                 bar_box.top - tile_layout::scale_480(24));
  return bar;
}

// lv_area_intersect is private in LVGL 9.6.
inline bool intersect(const lv_area_t& a, const lv_area_t& b, lv_area_t& out) {
  out.x1 = std::max(a.x1, b.x1);
  out.y1 = std::max(a.y1, b.y1);
  out.x2 = std::min(a.x2, b.x2);
  out.y2 = std::min(a.y2, b.y2);
  return out.x1 <= out.x2 && out.y1 <= out.y2;
}

inline void draw_rect(lv_layer_t* layer, const lv_area_t& area, lv_color_t color, int32_t radius) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = radius;
  lv_draw_rect(layer, &dsc, &area);
}

inline int32_t radius_of(lv_obj_t* bar, int32_t height) {
  return std::min<int32_t>(lv_obj_get_style_radius(bar, LV_PART_MAIN), height / 2);
}

// Draws `level` (1..100; 0 draws nothing) of the bar in `accent` with the
// handle line in `card`. `base`: the one-row bar height (a taller bar keeps
// its end rounding and handle width).
inline void draw_fill(lv_layer_t* layer, lv_obj_t* bar, uint8_t level, int16_t base, lv_color_t accent,
                      lv_color_t card) {
  if (!layer || !bar || level == 0) return;
  lv_area_t area;
  lv_obj_get_coords(bar, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  if (width < 4 || height < 4) return;
  const int32_t radius = radius_of(bar, height);
  switch_layout::Dimmer geometry{width, height, radius, base};
  const int32_t fill = geometry.fill_width(level);
  const int32_t end = geometry.end_radius_for(fill);
  // The fill stays inside the bar's shape without a clip layer: up to where
  // its end rounding starts it is the bar's own rounded rect, clipped to
  // that range; the end is a rect with the end rounding, clipped to the
  // rest. Both pieces meet at a full-height column, so there is no seam
  // (switch_layout::Dimmer keeps the end inside the bar's round end).
  const lv_area_t clip_ori = layer->_clip_area;
  const lv_area_t start_range = {area.x1, area.y1, area.x1 + fill - end - 1, area.y2};
  lv_area_t clip;
  if (intersect(clip_ori, start_range, clip)) {
    layer->_clip_area = clip;
    draw_rect(layer, area, accent, radius);
  }
  const lv_area_t end_range = {area.x1 + fill - end, area.y1, area.x1 + fill - 1, area.y2};
  if (intersect(clip_ori, end_range, clip)) {
    layer->_clip_area = clip;
    const lv_area_t end_piece = {area.x1 + fill - 2 * end, area.y1, area.x1 + fill - 1, area.y2};
    draw_rect(layer, end_piece, accent, end);
  }
  layer->_clip_area = clip_ori;
  const int32_t handle_w = geometry.handle_width();
  const int32_t handle_h = geometry.handle_height();
  const int32_t handle_x = area.x1 + geometry.handle_x(level) - handle_w / 2;
  const int32_t handle_y = area.y1 + (height - handle_h) / 2;
  const lv_area_t handle = {handle_x, handle_y, handle_x + handle_w - 1, handle_y + handle_h - 1};
  draw_rect(layer, handle, card, LV_RADIUS_CIRCLE);
}

// The level under a touch point: the Light popup's brightness logic sideways
// (100 % at the far cap center, 1 % at the near one, 0 only beyond it).
inline uint8_t value_at(lv_obj_t* bar, int16_t base, const lv_point_t& point) {
  lv_area_t area;
  lv_obj_get_coords(bar, &area);
  const int32_t height = lv_area_get_height(&area);
  switch_layout::Dimmer geometry{lv_area_get_width(&area), height, radius_of(bar, height), base};
  return geometry.value_at(point.x - area.x1);
}

// Redraws only the columns a level change touches, like the Light popup's
// invalidate_brightness_change: between the old and new fill end, plus the
// end rounding and the handle line before it. The card corners, the tile
// border and the bar's round start stay untouched while dragging.
inline void invalidate_change(lv_obj_t* bar, int16_t base, uint8_t old_level, uint8_t new_level) {
  lv_area_t area;
  lv_obj_get_coords(bar, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t radius = radius_of(bar, height);
  const switch_layout::Dimmer geometry{width, height, radius, base};
  const int32_t old_fill = geometry.fill_width(old_level);
  const int32_t new_fill = geometry.fill_width(new_level);
  const int32_t reach = std::max<int32_t>(radius, geometry.handle_margin() + geometry.handle_width()) + 1;
  const int32_t from = std::max<int32_t>(0, std::min(old_fill, new_fill) - reach);
  const int32_t to = std::min<int32_t>(width, std::max(old_fill, new_fill) + 1);
  lv_area_t dirty = {area.x1 + from, area.y1, area.x1 + to - 1, area.y2};
  lv_obj_invalidate_area(bar, &dirty);
}

}  // namespace level_bar
