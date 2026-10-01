#pragma once

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/core/config/icon_glow.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/tone_color.h"

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
// With glow, a colored icon tints its disc with its hue (tone_color.h: a
// fixed lightness step above the tile); the global Circle strength
// (icon_glow.h) sets the step of every disc, glowing and white. The tile
// border takes only a hint of the icon hue
// (ui_surface_style::set_tile_border_tint).
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

// Tile controls carry one of these addresses as user data, and
// tile_icon_source::refresh_controls gives them the popup control fill:
// [0] a press fill (Media previous and next, Climate - and +), [1] a resting
// surface (the Climate target pill). Their owner keeps deciding with a local
// opacity where a control shows no fill.
inline constexpr char kControlTags[2] = {};
inline void mark_control(lv_obj_t* btn) {
  if (btn) lv_obj_set_user_data(btn, const_cast<char*>(&kControlTags[0]));
}
inline void mark_surface(lv_obj_t* obj) {
  if (obj) lv_obj_set_user_data(obj, const_cast<char*>(&kControlTags[1]));
}
inline bool is_control(lv_obj_t* obj) { return obj && lv_obj_get_user_data(obj) == &kControlTags[0]; }
inline bool is_surface(lv_obj_t* obj) { return obj && lv_obj_get_user_data(obj) == &kControlTags[1]; }

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

// A card's color at rest (`pressed` false) or pressed: the colors the
// renderers and tints set for those states (not a running transition), else
// its current color.
inline uint32_t card_state_color(lv_obj_t* card, bool pressed) {
  if (!card) return 0x000000;
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &value, LV_PART_MAIN | LV_STATE_DEFAULT) ==
      LV_STYLE_RES_FOUND) {
    lv_style_value_t down;
    if (pressed && lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &down, LV_PART_MAIN | LV_STATE_PRESSED) ==
                       LV_STYLE_RES_FOUND) {
      return lv_color_to_u32(down.color) & 0xFFFFFF;
    }
    return lv_color_to_u32(value.color) & 0xFFFFFF;
  }
  return lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
}

// A pressed tile fades to its lighter pressed color with the theme's press
// transition. Circles and resting controls take the card's pressed state
// (follow_card_state) with the same timing (LV_THEME_DEFAULT_TRANSITION_TIME,
// back after the theme's 70 ms delay), so they keep their step above the card
// during the whole fade; an instant recolor ran ahead of the card and flashed.
inline void fade_with_card(lv_obj_t* obj) {
  if (!obj) return;
  static const lv_style_prop_t kProps[] = {LV_STYLE_BG_COLOR, static_cast<lv_style_prop_t>(0)};
  static lv_style_transition_dsc_t down, back;
  static bool ready = false;
  if (!ready) {
    lv_style_transition_dsc_init(&down, kProps, lv_anim_path_linear, LV_THEME_DEFAULT_TRANSITION_TIME, 0, nullptr);
    lv_style_transition_dsc_init(&back, kProps, lv_anim_path_linear, LV_THEME_DEFAULT_TRANSITION_TIME, 70, nullptr);
    ready = true;
  }
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_TRANSITION, &value, LV_PART_MAIN | LV_STATE_PRESSED) !=
      LV_STYLE_RES_FOUND) {
    lv_obj_set_style_transition(obj, &back, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_transition(obj, &down, LV_PART_MAIN | LV_STATE_PRESSED);
  }
}

// Sets the resting and pressed fill of a circle or resting control; only a
// change touches the styles.
inline void set_fill_colors(lv_obj_t* obj, uint32_t rest, uint32_t pressed) {
  const lv_style_selector_t selectors[] = {LV_PART_MAIN | LV_STATE_DEFAULT, LV_PART_MAIN | LV_STATE_PRESSED};
  const uint32_t colors[] = {rest, pressed};
  for (int i = 0; i < 2; ++i) {
    lv_style_value_t value;
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, selectors[i]) != LV_STYLE_RES_FOUND ||
        (lv_color_to_u32(value.color) & 0xFFFFFF) != colors[i]) {
      lv_obj_set_style_bg_color(obj, lv_color_hex(colors[i]), selectors[i]);
    }
  }
}

// Whether a card lets the background through (screensaver tiles below full
// Tile opacity): its circles and controls stay a veil (tone_color.h).
inline bool see_through(lv_obj_t* card) {
  return !card || lv_obj_get_style_bg_opa(card, LV_PART_MAIN) < LV_OPA_COVER;
}

// The nearest opaque background behind `obj` (the tile card).
inline lv_obj_t* card_of(lv_obj_t* obj) {
  lv_obj_t* host = obj ? lv_obj_get_parent(obj) : nullptr;
  for (int depth = 0; host && depth < 3 &&
       lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < LV_OPA_50; ++depth) {
    host = lv_obj_get_parent(host);
  }
  return host;
}

// The resting color of the nearest opaque background behind `obj` (the tile
// card).
inline uint32_t card_color(lv_obj_t* obj) { return card_state_color(card_of(obj), false); }

// An icon too dark to read on its circle is shown lighter in its own hue
// (tone_color::readable_icon, measured against the default settings, so the
// global tile color and the Circle strength never change it). The color it
// was given stays in these unused
// state selectors next to the shown one, so every reader gets the given
// color and a direct write of a new color is recognized.
inline constexpr lv_style_selector_t kIconGiven = LV_PART_MAIN | LV_STATE_USER_1;
inline constexpr lv_style_selector_t kIconShown = LV_PART_MAIN | LV_STATE_USER_1 | LV_STATE_USER_2;

// The color the icon was given, before any readability lift.
inline uint32_t icon_color(lv_obj_t* icon) {
  if (!icon) return 0xFFFFFF;
  const lv_color_t text = lv_obj_get_style_text_color(icon, LV_PART_MAIN);
  lv_style_value_t shown, given;
  if (lv_obj_get_local_style_prop(icon, LV_STYLE_TEXT_COLOR, &shown, kIconShown) == LV_STYLE_RES_FOUND &&
      lv_color_eq(shown.color, text) &&
      lv_obj_get_local_style_prop(icon, LV_STYLE_TEXT_COLOR, &given, kIconGiven) == LV_STYLE_RES_FOUND) {
    return lv_color_to_u32(given.color) & 0xFFFFFF;
  }
  return lv_color_to_u32(text) & 0xFFFFFF;
}

// Shows `given`, lighter only where it would be hard to read.
inline void show_readable(lv_obj_t* icon, uint32_t given) {
  if (!icon) return;
  const uint32_t shown = tone_color::readable_icon(given);
  auto store = [icon](lv_style_selector_t selector, uint32_t rgb, bool keep) {
    lv_style_value_t value;
    const bool found =
        lv_obj_get_local_style_prop(icon, LV_STYLE_TEXT_COLOR, &value, selector) == LV_STYLE_RES_FOUND;
    if (!keep) {
      if (found) lv_obj_remove_local_style_prop(icon, LV_STYLE_TEXT_COLOR, selector);
    } else if (!found || (lv_color_to_u32(value.color) & 0xFFFFFF) != rgb) {
      lv_obj_set_style_text_color(icon, lv_color_hex(rgb), selector);
    }
  };
  store(kIconGiven, given, shown != given);
  store(kIconShown, shown, shown != given);
  if ((lv_color_to_u32(lv_obj_get_style_text_color(icon, LV_PART_MAIN)) & 0xFFFFFF) != shown) {
    lv_obj_set_style_text_color(icon, lv_color_hex(shown), 0);
  }
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

// tile_icon_source keeps a card's tile color "From icon" strength (BG_OPA)
// in this unused selector, and "From cover" (Media) its permission (BG_OPA)
// and the cover color (BG_COLOR) in the next.
inline constexpr lv_style_selector_t kCardTintStore = LV_PART_MAIN | LV_STATE_USER_4;
inline constexpr lv_style_selector_t kCardCoverStore = LV_PART_MAIN | LV_STATE_USER_3;

// Whether a card shows the tile color "From icon" (or "From cover"): it then
// already is the card its circle belongs to.
inline bool card_follows_icon(lv_obj_t* card) {
  for (int depth = 0; card && depth < 3; ++depth, card = lv_obj_get_parent(card)) {
    lv_style_value_t value;
    if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_OPA, &value, kCardTintStore) == LV_STYLE_RES_FOUND) {
      return value.num > 0;
    }
    lv_style_value_t cover;
    if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_OPA, &value, kCardCoverStore) == LV_STYLE_RES_FOUND &&
        value.num > 0 &&
        lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &cover, kCardCoverStore) == LV_STYLE_RES_FOUND &&
        icon_color_tints(lv_color_to_u32(cover.color) & 0xFFFFFF)) {
      return true;
    }
  }
  return false;
}

// "Circle in icon color" always shows the circle of the tile color "From
// icon" (user 2026-10-01): card, circle and icon stay one family of colors.
// On a card in its own Global or Custom color the circle is computed for the
// card "From icon" would give instead of mixing the icon's hue into that
// card's lightness; a card that follows the icon already is that card.
// Untinted circles (white, grey, black icons, option off) and see-through
// screensaver tiles keep their own card.
inline uint32_t circle_card(lv_obj_t* host, uint32_t card, uint32_t rgb, bool tinted, bool pressed,
                            bool see_through_card) {
  if (!tinted || see_through_card || !tone_color::g_from_icon_card || card_follows_icon(host)) return card;
  return tone_color::g_from_icon_card(rgb, pressed, 0);
}

// The fill of a disc for the card behind it (at rest or pressed) and its
// icon's color (tone_color::fill, circle_card); `card` and
// `see_through_card` report that card.
inline tone_color::Fill disc_fill(lv_obj_t* disc, bool pressed, uint32_t& card, bool& see_through_card) {
  lv_obj_t* icon = icon_of(disc);
  const uint32_t rgb = icon ? icon_color(icon) : 0xFFFFFF;
  const bool tinted = glow_of(disc) && icon_color_tints(rgb);
  lv_obj_t* host = card_of(disc);
  card = card_state_color(host, pressed);
  see_through_card = see_through(host);
  return tone_color::fill(circle_card(host, card, rgb, tinted, pressed, see_through_card), rgb, tinted,
                          ui_surface_style::icon_glow_percent(), see_through_card);
}

// Color and opacity of a disc from its mode, glow option, the card (at rest
// and pressed) and the icon's color (tone_color::fill). Global discs follow
// the global option through the shared style, On discs always show, Off
// discs stay transparent. The icon is shown readable (show_readable).
inline void apply_fill(lv_obj_t* disc) {
  if (!is_disc(disc)) return;
  const Mode mode = mode_of(disc);
  lv_obj_t* icon = icon_of(disc);
  const uint32_t rgb = icon ? icon_color(icon) : 0xFFFFFF;
  uint32_t card = 0, card_pressed = 0;
  bool see_through_card = false;
  const tone_color::Fill fill = disc_fill(disc, false, card, see_through_card);
  const tone_color::Fill pressed = disc_fill(disc, true, card_pressed, see_through_card);
  set_fill_colors(disc, fill.disc_color, pressed.disc_color);
  ui_surface_style::apply_icon_disc(disc, mode == Mode::Off, mode == Mode::Global, see_through_card);
  show_readable(icon, rgb);
  if (g_icon_color_hook) g_icon_color_hook(disc);
}

// The card's pressed state reaches its discs (fade_with_card) and, through
// g_card_state_hook (tile_icon_source), its resting controls. Only fills
// change with it; the icon inside a half-height disc keeps its color.
inline void (*g_card_state_hook)(lv_obj_t* card) = nullptr;

inline void follow_card_state(lv_event_t* event) {
  lv_obj_t* card = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (!card) return;
  const bool pressed = lv_obj_has_state(card, LV_STATE_PRESSED);
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (is_disc(child) && lv_obj_has_state(child, LV_STATE_PRESSED) != pressed) {
      lv_obj_set_state(child, LV_STATE_PRESSED, pressed);
    }
  }
  if (g_card_state_hook) g_card_state_hook(card);
}

// Refills the discs of a card whose background changed outside the color
// paths (screensaver tiles take their Tile opacity after rendering).
inline void refresh_fills(lv_obj_t* card) {
  const uint32_t count = card ? lv_obj_get_child_count(card) : 0;
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (is_disc(child)) apply_fill(child);
  }
}

inline void follow_card_states(lv_obj_t* card) {
  const uint32_t count = lv_obj_get_event_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(card, i)) == follow_card_state) return;
  }
  lv_obj_add_event_cb(card, follow_card_state, LV_EVENT_STATE_CHANGED, nullptr);
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
    lv_obj_set_style_text_color(icon, lv_color_hex(icon_color(icon)), kIconRequested);
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
  ui_surface_style::apply_icon_disc(disc, false, true);
  fade_with_card(disc);
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
    follow_card_states(card);
    set_tag(child, disc_mode, glow);
    apply_fill(child);
    // The tile border takes a hint of a glowing icon's hue when the tile is
    // built, only on a card in the tile color "From icon": a Global or
    // Custom card keeps the neutral hairline (user 2026-10-01). Icon color
    // changes do not touch it: a border change redraws the whole tile and
    // any popup above it, which made a dragged Light color stutter.
    lv_obj_t* icon = icon_of(child);
    const uint32_t rgb = icon ? icon_color(icon) : 0xFFFFFF;
    if (glow && disc_mode != Mode::Off && icon_color_tints(rgb) && card_follows_icon(card)) {
      ui_surface_style::set_tile_border_tint(card, lv_color_hex(rgb));
    } else {
      ui_surface_style::clear_tile_border_tint(card);
    }
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

// Taller tiles with the icon in the top-left corner: the disc grows around
// the unchanged icon until its gap to the tile's left edge equals the
// half-height disc's inset (the header lift makes the top gap match), so it
// sits in the corner like the half-height disc. The icon sits `offset_side`
// from a card padding of `pad_side`. The Web Admin header CSS uses the same
// value (--icon-disc-corner).
// The disc of taller tiles: the size the tuned 8/10-inch header gives around
// its icon (card padding 20, icon offset -8 at the 480 scale, the half-height
// inset to the edges), but never smaller than the half-height disc. Where the
// cells are taller than the header needs (Tab5, 4B, S3), it is the
// half-height disc itself, the same circle at the largest tile radius. The
// Web Admin uses it as --icon-disc-corner.
inline int header_diameter(int icon_width) {
  const int around_icon =
      icon_width + 2 * (tile_layout::scale_480(20) + tile_layout::scale_480(-8) - inset());
  return around_icon > diameter() ? around_icon : diameter();
}

// Taller tiles with the icon in the top-left corner place the disc like the
// half-height tiles: the half-height inset from the tile's left and top
// edges, so it is concentric with the tile corner on every device. The icon
// sits centered in it; the header labels beside the icon move with it
// vertically. On the 8-inch and 10-inch layouts, where the header was tuned,
// nothing moves. `pad_*` are the card paddings, `offset_top` the icon's
// current y inside them. The Web Admin header CSS uses the same values.
struct CornerHeader {
  int disc = 0;       // disc side
  int icon_side = 0;  // icon x inside the side padding
  int icon_top = 0;   // icon y inside the top padding
  int shift = 0;      // vertical move of the icon and the header labels
};

inline CornerHeader corner_header(int pad_top, int pad_side, int offset_top, int icon_width,
                                  int icon_height) {
  CornerHeader header;
  header.disc = header_diameter(icon_width);
  header.icon_side = inset() - pad_side + (header.disc - icon_width) / 2;
  header.icon_top = inset() - pad_top + (header.disc - icon_height) / 2;
  header.shift = header.icon_top - offset_top;
  return header;
}

// How far a corner header (disc, icon and header labels) moves up so the
// disc of `size` has the same top gap as side gap. The icon sits at
// (offset_side, offset_top) inside a card with these paddings (offsets
// measured towards the card's inside). The Web Admin header CSS uses the
// same value.
inline int corner_lift(int pad_top, int pad_side, int offset_side, int offset_top,
                       int icon_width, int icon_height, int size) {
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
  // Colored weather icons carry recolor commands that take no space.
  lv_text_get_size(&icon_size, lv_label_get_text(icon), font, 0, 0, LV_COORD_MAX,
                   lv_label_get_recolor(icon) ? LV_TEXT_FLAG_RECOLOR : LV_TEXT_FLAG_NONE);
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
  // Top-left corner header: the disc sits in the corner like the half-height
  // disc, the icon centered in it, the header labels move with the icon
  // (corner_header).
  if (vertical == 0 && horizontal == 0) {
    const int pad_top = lv_obj_get_style_pad_top(card, LV_PART_MAIN);
    const int pad_left = lv_obj_get_style_pad_left(card, LV_PART_MAIN);
    const int icon_y = lv_obj_get_style_y(icon, LV_PART_MAIN);
    const CornerHeader header = corner_header(pad_top, pad_left, icon_y, icon_size.x, icon_size.y);
    lv_obj_set_size(disc, header.disc, header.disc);
    lv_obj_align(disc, LV_ALIGN_TOP_LEFT, inset() - pad_left, inset() - pad_top);
    if (header.shift != 0) {
      const int header_bottom = icon_y + icon_size.y;
      const uint32_t count = lv_obj_get_child_count(card);
      for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(card, i);
        if (child == disc) continue;
        const lv_align_t child_align = lv_obj_get_style_align(child, LV_PART_MAIN);
        if (child_align != LV_ALIGN_TOP_LEFT && child_align != LV_ALIGN_TOP_MID &&
            child_align != LV_ALIGN_TOP_RIGHT) {
          continue;
        }
        const int y = lv_obj_get_style_y(child, LV_PART_MAIN);
        if (child != icon && y >= header_bottom) continue;
        lv_obj_set_y(child, y + header.shift);
      }
    }
    lv_obj_set_x(icon, header.icon_side);
    lv_obj_set_flag(disc, LV_OBJ_FLAG_HIDDEN, lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN));
    lv_obj_move_to_index(disc, lv_obj_get_index(icon));
    return disc;
  }
  // Centered icons (icon above a title: Folder, Scene, Camera, Switch) take
  // the corner header's disc size, so every taller tile shows one disc;
  // other icons keep the round disc.
  const int size = vertical == 1 && horizontal == 1 ? header_diameter(icon_size.x) : round_diameter();
  if (size != round_diameter()) lv_obj_set_size(disc, size, size);
  lv_obj_align(disc, align,
               centered_offset(horizontal, lv_obj_get_style_x(icon, LV_PART_MAIN), icon_size.x, size),
               centered_offset(vertical, lv_obj_get_style_y(icon, LV_PART_MAIN), icon_size.y, size));
  // Header icons in the top-right corner: the disc sits closer to the side
  // edge than to the top edge. Move the whole header (disc, icon and the
  // header labels beside it) up until both gaps match; never down, never
  // sideways.
  if (vertical == 0 && horizontal == 2) {
    const int icon_x = lv_obj_get_style_x(icon, LV_PART_MAIN);
    const int shift = corner_lift(
        lv_obj_get_style_pad_top(card, LV_PART_MAIN),
        horizontal == 0 ? lv_obj_get_style_pad_left(card, LV_PART_MAIN)
                        : lv_obj_get_style_pad_right(card, LV_PART_MAIN),
        horizontal == 0 ? icon_x : -icon_x, lv_obj_get_style_y(icon, LV_PART_MAIN),
        icon_size.x, icon_size.y, size);
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
