#pragma once

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/ui/shared/ui_surface_style.h"

// One translucent disc behind every tile icon, all with the same diameter
// and opacity. Half-height tiles hold the icon in a disc that is concentric
// with the tile corner and follows the global radius. Taller tiles keep their
// icon exactly where it is and get a plain circle centered on the icon.
// Discs are children of the tile card: they are created and released with the
// card, so cached grids, folder snapshots and cache restores keep them.
namespace tile_icon_disc {

// Distance between the half-height disc and the tile's outer edges.
inline int inset() { return tile_layout::scale_480(4); }
// One half-height tile row; the disc fills it minus the inset on both sides.
inline int row_height() { return (GRID_CELL_H - GRID_GAP) / 2; }
inline int diameter() { return row_height() - inset() * 2; }
// Tile radius baseline minus the inset keeps the half-height disc concentric
// with the tile corner; the shared radius style follows global radius changes.
inline int radius_baseline() { return tile_layout::scale_480(22) - inset(); }
inline constexpr lv_opa_t kOpa = 38;
// MDI icon fonts give every glyph this glyph's advance width.
inline constexpr uint32_t kMdiReferenceGlyph = 0xF0001;

enum class Shape : uint8_t { Concentric, Round };

// Disc objects carry this address as user data, so icon hide/show paths can
// find the disc of an icon without an extra allocation per tile.
inline constexpr char kTag = 0;

inline bool is_disc(lv_obj_t* obj) {
  return obj && lv_obj_get_user_data(obj) == static_cast<const void*>(&kTag);
}

// A wrapped icon's disc is its parent; a round disc sits directly behind it.
inline lv_obj_t* disc_of(lv_obj_t* icon) {
  if (!icon) return nullptr;
  lv_obj_t* parent = lv_obj_get_parent(icon);
  if (is_disc(parent)) return parent;
  const int32_t index = lv_obj_get_index(icon);
  lv_obj_t* below = parent && index > 0 ? lv_obj_get_child(parent, index - 1) : nullptr;
  return is_disc(below) ? below : nullptr;
}

// Hides or shows an icon together with its disc; an empty disc never shows.
inline void set_icon_hidden(lv_obj_t* icon, bool hidden) {
  if (!icon) return;
  lv_obj_set_flag(icon, LV_OBJ_FLAG_HIDDEN, hidden);
  if (lv_obj_t* disc = disc_of(icon)) lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, hidden);
}

// The single disc implementation. The shape selects the corner rule only.
inline lv_obj_t* create(lv_obj_t* card, Shape shape) {
  lv_obj_t* disc = card ? lv_obj_create(card) : nullptr;
  if (!disc) return nullptr;
  lv_obj_remove_style_all(disc);
  lv_obj_remove_flag(disc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  // Presses on a wrapped icon still reach the card through the disc.
  lv_obj_add_flag(disc, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_user_data(disc, const_cast<char*>(&kTag));
  lv_obj_set_size(disc, diameter(), diameter());
  if (shape == Shape::Concentric) {
    ui_surface_style::apply_radius(disc, radius_baseline(), 0);
  } else {
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  }
  lv_obj_set_style_bg_color(disc, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(disc, kOpa, 0);
  return disc;
}

// Half-height tiles: moves `icon` into a concentric disc on `card` and centers
// it there. The disc takes the icon's hidden state.
inline lv_obj_t* wrap(lv_obj_t* card, lv_obj_t* icon) {
  if (!card || !icon) return nullptr;
  if (lv_obj_t* existing = disc_of(icon)) return existing;
  lv_obj_t* disc = create(card, Shape::Concentric);
  if (!disc) return nullptr;
  lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
  lv_obj_set_parent(icon, disc);
  lv_obj_center(icon);
  return disc;
}

// Places a half-height disc in the top-left corner, measured from the card's
// outer edges so the card padding does not move it.
inline void place_in_corner(lv_obj_t* card, lv_obj_t* disc) {
  if (!card || !disc) return;
  lv_obj_align(disc, LV_ALIGN_TOP_LEFT,
               tile_icon_disc::inset() - lv_obj_get_style_space_left(card, LV_PART_MAIN),
               tile_icon_disc::inset() - lv_obj_get_style_space_top(card, LV_PART_MAIN));
}

// Offset that moves a box of `size` so its center matches a box of
// `icon_size` placed with the same anchor and `offset` (0 start, 1 mid, 2 end).
inline int centered_offset(int anchor, int offset, int icon_size, int size) {
  const int start_shift = (icon_size - size) / 2;
  if (anchor == 0) return offset + start_shift;
  if (anchor == 2) return offset + (size - icon_size) + start_shift;
  return offset - icon_size / 2 + size / 2 + start_shift;
}

// Taller tiles: a round disc directly behind `icon`, centered on the icon's
// current aligned position. The icon itself does not move. Call it once the
// icon's alignment is final; it takes the icon's hidden state.
inline lv_obj_t* add_round(lv_obj_t* card, lv_obj_t* icon) {
  if (!card || !icon || lv_obj_get_parent(icon) != card) return nullptr;
  if (lv_obj_t* existing = disc_of(icon)) return existing;
  // The icon label sizes to its content: one glyph wide, one line high.
  const lv_font_t* font = lv_obj_get_style_text_font(icon, LV_PART_MAIN);
  lv_point_t icon_size{};
  lv_text_get_size(&icon_size, lv_label_get_text(icon), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  if (icon_size.x <= 0) icon_size.x = lv_font_get_glyph_width(font, kMdiReferenceGlyph, 0);
  icon_size.y = lv_font_get_line_height(font);
  lv_obj_t* disc = create(card, Shape::Round);
  if (!disc) return nullptr;
  lv_align_t align = lv_obj_get_style_align(icon, LV_PART_MAIN);
  if (align == LV_ALIGN_DEFAULT) align = LV_ALIGN_TOP_LEFT;
  int horizontal = 1;
  int vertical = 1;
  switch (align) {
    case LV_ALIGN_TOP_LEFT: horizontal = 0; vertical = 0; break;
    case LV_ALIGN_TOP_MID: vertical = 0; break;
    case LV_ALIGN_TOP_RIGHT: horizontal = 2; vertical = 0; break;
    case LV_ALIGN_LEFT_MID: horizontal = 0; break;
    case LV_ALIGN_RIGHT_MID: horizontal = 2; break;
    case LV_ALIGN_BOTTOM_LEFT: horizontal = 0; vertical = 2; break;
    case LV_ALIGN_BOTTOM_MID: vertical = 2; break;
    case LV_ALIGN_BOTTOM_RIGHT: horizontal = 2; vertical = 2; break;
    default: break;
  }
  lv_obj_align(disc, align,
               centered_offset(horizontal, lv_obj_get_style_x(icon, LV_PART_MAIN), icon_size.x, diameter()),
               centered_offset(vertical, lv_obj_get_style_y(icon, LV_PART_MAIN), icon_size.y, diameter()));
  lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
  lv_obj_move_to_index(disc, lv_obj_get_index(icon));
  return disc;
}

}  // namespace tile_icon_disc
