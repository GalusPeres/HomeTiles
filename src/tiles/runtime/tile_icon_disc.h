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
// With glow, a colored icon tints its disc with its hue on a neutral (grey)
// card; the strength is the global Glow setting (ui_surface_style::
// icon_glow_opa, default 25 %). The tile border stays the neutral hairline.
// MDI icon fonts give every glyph this glyph's advance width.
inline constexpr uint32_t kMdiReferenceGlyph = 0xF0001;

enum class Shape : uint8_t { Concentric, Round };

// Per-tile disc override (TileIconDiscMode): follow the global icon disc
// option, always show the disc, or never show it.
enum class Mode : uint8_t { Global = 0, On = 1, Off = 2 };

// Disc objects carry one of these addresses as user data. The address marks
// the object as a disc and holds its mode and glow option (mode * 2 + glow),
// so icon paths find the disc of an icon and its options without an extra
// allocation per tile.
inline constexpr char kTags[6] = {};

inline bool is_disc(lv_obj_t* obj) {
  if (!obj) return false;
  const uintptr_t tag = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj));
  return tag - reinterpret_cast<uintptr_t>(&kTags[0]) < sizeof(kTags);
}

inline uintptr_t tag_index(lv_obj_t* disc) {
  return reinterpret_cast<uintptr_t>(lv_obj_get_user_data(disc)) -
         reinterpret_cast<uintptr_t>(&kTags[0]);
}
inline Mode mode_of(lv_obj_t* disc) { return static_cast<Mode>(tag_index(disc) / 2); }
inline bool glow_of(lv_obj_t* disc) { return (tag_index(disc) % 2) != 0; }

inline void set_tag(lv_obj_t* disc, Mode mode, bool glow) {
  lv_obj_set_user_data(
      disc, const_cast<char*>(&kTags[static_cast<uint8_t>(mode) * 2 + (glow ? 1 : 0)]));
}

// Central disc fill rule, shared with the Web Admin preview (iconDiscTinted):
// with glow on, a colored icon tints its disc with its own hue; white and
// grey icons keep the neutral white disc. Per-tile icon colors and color
// rules (tile_icon_color_rules.h) reach the disc through set_icon_color().
inline bool icon_color_tints(uint32_t rgb) {
  const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return r != g || g != b;
}

// A colored card (own tile color or a rules tint) gets a disc in its own hue
// (ui_surface_style::surface_hue), a lighter step of the card, so disc and
// card never clash. Near-grey cards (channel spread up to kNeutralSpread)
// count as neutral and let a colored icon glow in its hue.
inline constexpr uint8_t kNeutralSpread = 12;
inline bool card_is_neutral(uint32_t rgb) {
  const uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  const uint8_t hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const uint8_t lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
  return hi - lo <= kNeutralSpread;
}

// Discs are subtler on dark tiles: the same step from tile to disc reads much
// stronger on near-black. The opacity scales from 8 % (tile luma <= 0.08) to
// the full value (luma >= 0.25) in four steps, so only a few shared opacity
// styles exist. The Web Admin preview (iconDiscContrastStep) and the popup
// header (popup_layout::headerDiscContrastStep) use the same rule.
inline uint8_t contrast_step_for(uint32_t rgb) {
  const float luma = (0.2126f * ((rgb >> 16) & 0xFF) + 0.7152f * ((rgb >> 8) & 0xFF) +
                      0.0722f * (rgb & 0xFF)) / 255.0f;
  float t = (luma - 0.08f) / 0.17f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return static_cast<uint8_t>(t * 3.0f + 0.5f);
}
// Step 3 keeps `full`; step 0 is 8/15 of it (8 % instead of 15 % for kOpa).
inline lv_opa_t scaled_opa(lv_opa_t full, uint8_t step) {
  return static_cast<lv_opa_t>((full * (24 + 7 * step) + 22) / 45);
}
// The color of the nearest background behind the disc with at least
// `min_opa` (the tile card); false without one. The neutral-card rule uses
// any visible background, so a translucent screensaver tile keeps its own
// color instead of the wallpaper layer behind it.
inline bool card_color(lv_obj_t* disc, uint32_t& rgb, lv_opa_t min_opa = LV_OPA_50) {
  lv_obj_t* host = lv_obj_get_parent(disc);
  for (int depth = 0; host && depth < 3 &&
       lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < min_opa; ++depth) {
    host = lv_obj_get_parent(host);
  }
  if (!host || lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < min_opa) return false;
  rgb = lv_color_to_u32(lv_obj_get_style_bg_color(host, LV_PART_MAIN)) & 0xFFFFFF;
  return true;
}
inline uint8_t contrast_step(lv_obj_t* disc) {
  uint32_t rgb = 0;
  return card_color(disc, rgb) ? contrast_step_for(rgb) : 3;
}

// The icon a disc belongs to: its child (half-height) or its next sibling.
inline lv_obj_t* icon_of(lv_obj_t* disc) {
  if (lv_obj_get_child_count(disc) > 0) return lv_obj_get_child(disc, 0);
  lv_obj_t* parent = lv_obj_get_parent(disc);
  return parent ? lv_obj_get_child(parent, lv_obj_get_index(disc) + 1) : nullptr;
}

// Called at the end of apply_fill(), which every icon color change reaches:
// tile_icon_source registers it so the "Tint tile" option of the icon color
// follows the icon. Stays null in host tests.
inline void (*g_icon_color_hook)(lv_obj_t* disc) = nullptr;

// Color and opacity of a disc from its mode, glow option and the icon's
// current color. Global discs follow the global option through the shared
// style, On discs always show, Off discs stay transparent.
inline void apply_fill(lv_obj_t* disc) {
  if (!is_disc(disc)) return;
  const Mode mode = mode_of(disc);
  lv_obj_t* icon = icon_of(disc);
  const uint32_t rgb =
      icon ? lv_color_to_u32(lv_obj_get_style_text_color(icon, LV_PART_MAIN)) & 0xFFFFFF
           : 0xFFFFFF;
  uint32_t card = 0;
  const bool known_card = card_color(disc, card, 1);
  const bool neutral_card = !known_card || card_is_neutral(card);
  const bool tinted = glow_of(disc) && icon_color_tints(rgb) && neutral_card;
  const lv_color_t color = tinted ? lv_color_hex(rgb)
                           : known_card ? ui_surface_style::surface_hue(lv_color_hex(card))
                                        : lv_color_white();
  if (!lv_color_eq(lv_obj_get_style_bg_color(disc, LV_PART_MAIN), color)) {
    lv_obj_set_style_bg_color(disc, color, 0);
  }
  const uint8_t step = contrast_step(disc);
  const lv_opa_t opa = mode == Mode::Off ? static_cast<lv_opa_t>(LV_OPA_TRANSP)
                                         : scaled_opa(tinted ? ui_surface_style::icon_glow_opa() : kOpa, step);
  ui_surface_style::apply_icon_disc_opa(disc, opa, mode == Mode::Global);
  if (g_icon_color_hook) g_icon_color_hook(disc);
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

// Rules (tile_icon_source.cpp) can force an icon color over the color the
// tile type sets from its state. The forced color and the type's latest
// color live in unused state selectors, so releasing the rule returns to the
// type's color without a rebuild.
inline constexpr lv_style_selector_t kIconForced = LV_PART_MAIN | LV_STATE_USER_3;
inline constexpr lv_style_selector_t kIconRequested = LV_PART_MAIN | LV_STATE_USER_2;

inline bool forced_color(lv_obj_t* icon, lv_color_t& out) {
  lv_style_value_t value;
  if (!icon || lv_obj_get_local_style_prop(icon, LV_STYLE_TEXT_COLOR, &value, kIconForced) != LV_STYLE_RES_FOUND) {
    return false;
  }
  out = value.color;
  return true;
}

// The one path for runtime icon color changes: the disc follows the new
// color without a rebuild and without per-frame work. A forced rule color
// wins; the requested color is kept for when the rule releases the icon.
inline void set_icon_color(lv_obj_t* icon, lv_color_t color) {
  if (!icon) return;
  lv_color_t forced;
  if (forced_color(icon, forced)) {
    lv_obj_set_style_text_color(icon, color, kIconRequested);
    color = forced;
  }
  lv_obj_set_style_text_color(icon, color, 0);
  if (lv_obj_t* disc = disc_of(icon)) apply_fill(disc);
}

inline void force_icon_color(lv_obj_t* icon, lv_color_t color) {
  if (!icon) return;
  lv_color_t forced;
  if (!forced_color(icon, forced)) {
    lv_obj_set_style_text_color(icon, lv_obj_get_style_text_color(icon, LV_PART_MAIN), kIconRequested);
  } else if (lv_color_eq(forced, color)) {
    return;
  }
  lv_obj_set_style_text_color(icon, color, kIconForced);
  lv_obj_set_style_text_color(icon, color, 0);
  if (lv_obj_t* disc = disc_of(icon)) apply_fill(disc);
}

inline void release_icon_color(lv_obj_t* icon) {
  lv_color_t forced;
  if (!forced_color(icon, forced)) return;
  lv_style_value_t value;
  lv_color_t requested = lv_color_white();
  if (lv_obj_get_local_style_prop(icon, LV_STYLE_TEXT_COLOR, &value, kIconRequested) == LV_STYLE_RES_FOUND) {
    requested = value.color;
  }
  lv_obj_remove_local_style_prop(icon, LV_STYLE_TEXT_COLOR, kIconForced);
  lv_obj_remove_local_style_prop(icon, LV_STYLE_TEXT_COLOR, kIconRequested);
  lv_obj_set_style_text_color(icon, requested, 0);
  if (lv_obj_t* disc = disc_of(icon)) apply_fill(disc);
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
  set_tag(disc, Mode::Global, false);
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

// Applies the tile's persisted disc mode and glow option to the discs of a
// rendered card. render_tile() calls it once for every tile type.
inline void apply_tile_options(lv_obj_t* card, uint8_t mode, bool glow) {
  if (!card) return;
  const Mode disc_mode = mode <= static_cast<uint8_t>(Mode::Off)
                             ? static_cast<Mode>(mode)
                             : Mode::Global;
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (!is_disc(child)) continue;
    set_tag(child, disc_mode, glow);
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

// How far a corner header (disc, icon and header labels) moves up so the
// round disc's top gap matches its side gap. The icon sits at (offset_side,
// offset_top) inside a card with these paddings (offsets measured towards the
// card's inside). The Web Admin header CSS uses the same value.
inline int corner_lift(int pad_top, int pad_side, int offset_side, int offset_top,
                       int icon_width, int icon_height) {
  const int size = round_diameter();
  const int top_gap = pad_top + centered_offset(0, offset_top, icon_height, size);
  const int side_gap = pad_side + centered_offset(0, offset_side, icon_width, size);
  return top_gap > side_gap ? top_gap - side_gap : 0;
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
  // Header icons in a top corner: the disc sits closer to the side edge than
  // to the top edge. Move the whole header (disc, icon and the header labels
  // beside it) up until both gaps match; never down, never sideways.
  if (vertical == 0 && horizontal != 1) {
    const int icon_x = lv_obj_get_style_x(icon, LV_PART_MAIN);
    const int shift = corner_lift(
        lv_obj_get_style_pad_top(card, LV_PART_MAIN),
        horizontal == 0 ? lv_obj_get_style_pad_left(card, LV_PART_MAIN)
                        : lv_obj_get_style_pad_right(card, LV_PART_MAIN),
        horizontal == 0 ? icon_x : -icon_x, lv_obj_get_style_y(icon, LV_PART_MAIN),
        icon_size.x, icon_size.y);
    if (shift > 0) {
      const int header_bottom = lv_obj_get_style_y(icon, LV_PART_MAIN) + icon_size.y;
      const uint32_t count = lv_obj_get_child_count(card);
      for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(card, i);
        const lv_align_t child_align = lv_obj_get_style_align(child, LV_PART_MAIN);
        if (child_align != LV_ALIGN_TOP_LEFT && child_align != LV_ALIGN_TOP_MID &&
            child_align != LV_ALIGN_TOP_RIGHT) {
          continue;
        }
        const int y = lv_obj_get_style_y(child, LV_PART_MAIN);
        if (child != disc && child != icon && y >= header_bottom) continue;
        lv_obj_set_y(child, y - shift);
      }
    }
  }
  lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
  lv_obj_move_to_index(disc, lv_obj_get_index(icon));
  return disc;
}

}  // namespace tile_icon_disc
