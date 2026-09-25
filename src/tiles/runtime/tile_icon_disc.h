#pragma once

#include <algorithm>

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/ui/shared/ui_surface_style.h"

// One translucent disc behind every tile icon. Half-height tiles introduced
// it; full-size header tiles and centered tiles reuse the identical size,
// inset, radius rule and opacity, so every tile icon sits on the same shape.
// The disc is a child of the tile card: it is created and released with the
// card, so cached grids, folder snapshots and cache restores keep it.
namespace tile_icon_disc {

// Distance between a corner disc and the tile's outer edges.
inline int inset() { return tile_layout::scale_480(4); }
// One half-height tile row; the disc fills it minus the inset on both sides.
inline int row_height() { return (GRID_CELL_H - GRID_GAP) / 2; }
inline int diameter() { return row_height() - inset() * 2; }
// Tile radius baseline minus the inset keeps the disc concentric with the
// tile corner. The shared radius style follows global radius changes live: at
// the largest global radius the disc is a circle, below that a rounded square.
inline int radius_baseline() { return tile_layout::scale_480(22) - inset(); }
// Horizontal room between a corner disc and the header title.
inline int title_gap() { return inset() * 2; }
inline constexpr lv_opa_t kOpa = 38;

enum class Corner : uint8_t { Left, Right };

// Disc objects carry this address as user data, so icon hide/show paths can
// find the disc of an icon without an extra allocation per tile.
inline constexpr char kTag = 0;

inline bool is_disc(lv_obj_t* obj) {
  return obj && lv_obj_get_user_data(obj) == static_cast<const void*>(&kTag);
}

inline lv_obj_t* disc_of(lv_obj_t* icon) {
  lv_obj_t* parent = icon ? lv_obj_get_parent(icon) : nullptr;
  return is_disc(parent) ? parent : nullptr;
}

// The object that represents the icon in its card: the disc when wrapped.
inline lv_obj_t* outer(lv_obj_t* icon) {
  lv_obj_t* disc = disc_of(icon);
  return disc ? disc : icon;
}

// Hides or shows an icon together with its disc; an empty disc never shows.
inline void set_icon_hidden(lv_obj_t* icon, bool hidden) {
  if (!icon) return;
  lv_obj_set_flag(icon, LV_OBJ_FLAG_HIDDEN, hidden);
  if (lv_obj_t* disc = disc_of(icon)) lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, hidden);
}

// Moves `icon` into a new disc on `card` and centers it there. The disc takes
// the icon's stacking position and hidden state. A wrapped icon keeps its disc.
inline lv_obj_t* wrap(lv_obj_t* card, lv_obj_t* icon) {
  if (!card || !icon) return nullptr;
  if (lv_obj_t* existing = disc_of(icon)) return existing;
  lv_obj_t* disc = lv_obj_create(card);
  if (!disc) return nullptr;
  lv_obj_remove_style_all(disc);
  lv_obj_remove_flag(disc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  // Icons that forward presses to their card keep doing so through the disc.
  lv_obj_add_flag(disc, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_user_data(disc, const_cast<char*>(&kTag));
  lv_obj_set_size(disc, diameter(), diameter());
  ui_surface_style::apply_radius(disc, radius_baseline(), 0);
  lv_obj_set_style_bg_color(disc, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(disc, kOpa, 0);
  if (lv_obj_get_parent(icon) == card) lv_obj_move_to_index(disc, lv_obj_get_index(icon));
  lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
  lv_obj_set_parent(icon, disc);
  lv_obj_center(icon);
  return disc;
}

// Places a disc in a top corner, measured from the card's outer edges so the
// card padding does not move it.
inline void place_in_corner(lv_obj_t* card, lv_obj_t* disc, Corner corner) {
  if (!card || !disc) return;
  const int top = inset() - lv_obj_get_style_space_top(card, LV_PART_MAIN);
  if (corner == Corner::Left) {
    lv_obj_align(disc, LV_ALIGN_TOP_LEFT,
                 inset() - lv_obj_get_style_space_left(card, LV_PART_MAIN), top);
  } else {
    lv_obj_align(disc, LV_ALIGN_TOP_RIGHT,
                 lv_obj_get_style_space_right(card, LV_PART_MAIN) - inset(), top);
  }
}

// Header title opposite a corner disc: it keeps its horizontal alignment, is
// centered on the disc's vertical center and never runs under the disc. Two
// line titles are top aligned by hometiles_title and share the same center.
inline void align_title(lv_obj_t* card, lv_obj_t* title, int card_width,
                        bool has_disc, Corner disc_corner) {
  if (!card || !title) return;
  const int line = lv_font_get_line_height(lv_obj_get_style_text_font(title, LV_PART_MAIN));
  const int y = inset() + diameter() / 2 - line / 2 -
                lv_obj_get_style_space_top(card, LV_PART_MAIN);
  const lv_align_t align = lv_obj_get_style_align(title, LV_PART_MAIN);
  const int x = lv_obj_get_style_x(title, LV_PART_MAIN);
  lv_obj_align(title, align, x, y);
  if (!has_disc) return;
  const int reserved = inset() + diameter() + title_gap();
  const int max_width = disc_corner == Corner::Left
      ? card_width - lv_obj_get_style_space_right(card, LV_PART_MAIN) + x - reserved
      : card_width - reserved - lv_obj_get_style_space_left(card, LV_PART_MAIN) - x;
  lv_obj_set_style_max_width(title, std::max(1, max_width), 0);
}

// Full-size header: icon disc in a top corner, title on the opposite side.
inline lv_obj_t* apply_header(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title,
                              const Tile& tile, Corner corner = Corner::Left) {
  lv_obj_t* disc = wrap(card, icon);
  place_in_corner(card, disc, corner);
  align_title(card, title,
              tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP),
              disc != nullptr, corner);
  return disc;
}

// Centered layouts: the disc takes over the icon's existing center-based
// alignment, so the icon stays where it was. Only when a centered title below
// would come closer than the inset (two-line titles on compact layouts) do the
// disc and the title move apart equally, keeping the block centered.
inline lv_obj_t* apply_centered(lv_obj_t* card, lv_obj_t* icon, lv_obj_t* title) {
  if (!card || !icon) return nullptr;
  const lv_align_t align = lv_obj_get_style_align(icon, LV_PART_MAIN);
  const int x = lv_obj_get_style_x(icon, LV_PART_MAIN);
  int y = lv_obj_get_style_y(icon, LV_PART_MAIN);
  lv_obj_t* disc = wrap(card, icon);
  if (!disc) return nullptr;
  if (title && align == LV_ALIGN_CENTER &&
      lv_obj_get_style_align(title, LV_PART_MAIN) == LV_ALIGN_CENTER) {
    const int title_y = lv_obj_get_style_y(title, LV_PART_MAIN);
    int title_height = lv_obj_get_style_height(title, LV_PART_MAIN);
    if (LV_COORD_IS_SPEC(title_height)) {
      title_height = lv_font_get_line_height(lv_obj_get_style_text_font(title, LV_PART_MAIN));
    }
    const int overlap = (y + diameter() / 2 + inset()) - (title_y - title_height / 2);
    if (overlap > 0) {
      y -= overlap / 2;
      lv_obj_align(title, LV_ALIGN_CENTER, lv_obj_get_style_x(title, LV_PART_MAIN),
                   title_y + overlap - overlap / 2);
    }
  }
  lv_obj_align(disc, align, x, y);
  return disc;
}

}  // namespace tile_icon_disc
