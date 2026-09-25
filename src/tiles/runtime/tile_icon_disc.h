#pragma once

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/ui/shared/ui_surface_style.h"

// One translucent disc behind every tile icon. Half-height tiles hold the icon
// in a disc that is concentric with the tile corner. Taller tiles keep their
// icon exactly where it is and get a slightly larger disc centered on the
// icon. Both follow the global radius with the half-height radius rule.
// Discs are children of the tile card: they are created and released with the
// card, so cached grids, folder snapshots and cache restores keep them.
namespace tile_icon_disc {

// Distance between the half-height disc and the tile's outer edges.
inline int inset() { return tile_layout::scale_480(4); }
// One half-height tile row; the disc fills it minus the inset on both sides.
inline int row_height() { return (GRID_CELL_H - GRID_GAP) / 2; }
inline int diameter() { return row_height() - inset() * 2; }
// Taller tiles have room for a slightly larger disc around the unchanged icon.
inline int round_diameter() { return diameter() + inset(); }
// Tile radius baseline minus the inset keeps the half-height disc concentric
// with the tile corner; the shared radius style follows global radius changes.
inline int radius_baseline() { return tile_layout::scale_480(22) - inset(); }
inline constexpr lv_opa_t kOpa = 38;
// MDI icon fonts give every glyph this glyph's advance width.
inline constexpr uint32_t kMdiReferenceGlyph = 0xF0001;

enum class Shape : uint8_t { Concentric, Round };

// Per-tile disc override (TileIconDiscMode): follow the global icon disc
// option, always show the disc, or never show it.
enum class Mode : uint8_t { Global = 0, On = 1, Off = 2 };

// Disc objects carry one of these addresses as user data. The address marks
// the object as a disc and holds its mode, so icon paths find the disc of an
// icon and its options without an extra allocation per tile.
inline constexpr char kTags[3] = {};

inline bool is_disc(lv_obj_t* obj) {
  if (!obj) return false;
  const uintptr_t tag = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj));
  return tag - reinterpret_cast<uintptr_t>(&kTags[0]) < sizeof(kTags);
}

inline Mode mode_of(lv_obj_t* disc) {
  return static_cast<Mode>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(disc)) -
                           reinterpret_cast<uintptr_t>(&kTags[0]));
}

inline void set_tag(lv_obj_t* disc, Mode mode) {
  lv_obj_set_user_data(disc, const_cast<char*>(&kTags[static_cast<uint8_t>(mode)]));
}

// Opacity of a disc for its mode: Global discs follow the global option
// through the shared style, On discs always show, Off discs stay transparent.
inline void apply_fill(lv_obj_t* disc) {
  if (!is_disc(disc)) return;
  const Mode mode = mode_of(disc);
  ui_surface_style::apply_icon_disc_opa(
      disc, mode == Mode::Off ? static_cast<lv_opa_t>(LV_OPA_TRANSP) : kOpa,
      mode == Mode::Global);
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
  set_tag(disc, Mode::Global);
  const int size = shape == Shape::Round ? round_diameter() : diameter();
  lv_obj_set_size(disc, size, size);
  // Both shapes follow the global radius with the half-height rule. The shape
  // decides the placement (corner or behind the icon) and the diameter.
  ui_surface_style::apply_radius(disc, radius_baseline(), 0);
  lv_obj_set_style_bg_color(disc, lv_color_white(), 0);
  // New discs follow the global option until the tile's own mode is applied.
  ui_surface_style::apply_icon_disc_opa(disc, kOpa, true);
  return disc;
}

// Applies the tile's persisted disc mode to the discs of a rendered card.
// render_tile() calls it once for every tile type after rendering.
inline void apply_tile_mode(lv_obj_t* card, uint8_t mode) {
  if (!card) return;
  const Mode disc_mode = mode <= static_cast<uint8_t>(Mode::Off)
                             ? static_cast<Mode>(mode)
                             : Mode::Global;
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (!is_disc(child)) continue;
    set_tag(child, disc_mode);
    apply_fill(child);
  }
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

// Taller tiles: a disc of round_diameter() directly behind `icon`, centered on
// the icon's current aligned position. The icon itself does not move. Call it
// once the icon's alignment is final; it takes the icon's hidden state.
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
  const int size = round_diameter();
  lv_obj_align(disc, align,
               centered_offset(horizontal, lv_obj_get_style_x(icon, LV_PART_MAIN), icon_size.x, size),
               centered_offset(vertical, lv_obj_get_style_y(icon, LV_PART_MAIN), icon_size.y, size));
  lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
  lv_obj_move_to_index(disc, lv_obj_get_index(icon));
  return disc;
}

}  // namespace tile_icon_disc
