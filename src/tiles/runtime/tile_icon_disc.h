#pragma once

#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/core/config/icon_glow.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/tone_color.h"
#include "src/types/climate/layout.h"
#include "src/ui/shared/ui_theme.h"

// The surface a tile card draws on (user 2026-10-10: "the tile decides"). A
// tile in its own color follows that color's lightness - its text, icons and
// circles take the dark theme's rules on a dark color and the light theme's on
// a light one, in either theme - and a tinted tile in the light theme is
// filled with its icon's color (kJewel: the dark theme's recipe, lighter;
// tone_color::jewel_card).
// tile_renderer::render_tile marks the card; every later color path opens a
// ui_theme::SurfaceScope for it (scope()), so state updates draw like the
// first render.
namespace tile_surface {

// kJewel: a tinted tile in the light theme while it is tinted; it draws with
// ui_theme::kDarkTinted.
enum : uint8_t { kNone = 0, kDark = 1, kLight = 2, kJewel = 3 };

// Kept as a local style value in a state selector the card never takes. Every
// tile card carries it (kNone too), so its scopes know their card.
inline constexpr lv_style_selector_t kStore = LV_PART_MAIN | LV_STATE_USER_1 | LV_STATE_USER_4;

inline bool marked(lv_obj_t* obj) {
  lv_style_value_t value;
  return obj && lv_obj_get_local_style_prop(obj, LV_STYLE_BG_OPA, &value, kStore) == LV_STYLE_RES_FOUND;
}

inline uint8_t get(lv_obj_t* card) {
  lv_style_value_t value;
  if (!card || lv_obj_get_local_style_prop(card, LV_STYLE_BG_OPA, &value, kStore) != LV_STYLE_RES_FOUND) return kNone;
  return static_cast<uint8_t>(value.num);
}

inline void set(lv_obj_t* card, uint8_t surface) {
  if (!card || (marked(card) && get(card) == surface)) return;
  lv_obj_set_style_bg_opa(card, surface, kStore);
}

// The card `obj` sits in (or is): the first one carrying a surface.
inline lv_obj_t* card_of(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 5; ++depth, obj = lv_obj_get_parent(obj)) {
    if (marked(obj)) return obj;
  }
  return nullptr;
}

// The surface marked on `obj` or the card it sits in.
inline uint8_t find(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 5; ++depth, obj = lv_obj_get_parent(obj)) {
    const uint8_t surface = get(obj);
    if (surface != kNone) return surface;
  }
  return kNone;
}

// The theme a surface draws with (ui_theme::g_surface): 0 dark, 1 light,
// 2 dark for a tinted tile, -1 the theme.
inline int8_t theme_of(uint8_t surface) {
  if (surface == kJewel) return ui_theme::kDarkTintedSurface;
  return surface == kLight ? 1 : surface == kDark ? 0 : -1;
}


// Opens the surface of `obj`'s card for the colors computed while it lives;
// a card on the theme opens the theme explicitly, also inside a popup's
// scope (a popup command updating its tile).
inline ui_theme::SurfaceScope scope(lv_obj_t* obj) {
  lv_obj_t* card = card_of(obj);
  const int8_t theme = theme_of(card ? get(card) : find(obj));
  return ui_theme::SurfaceScope(theme >= 0 ? theme : ui_theme::kThemeSurface, card);
}

// The rule of a card whose surface changes while it runs (render_tile): the
// surface it shows without a tint (BORDER_OPA) and its flags (OUTLINE_OPA).
// kFollowTint: filled (kJewel) while tinted. kNeutralIdle: without a tint
// the neutral grey fill (a Media tile, tone_color::jewel_card of white).
inline constexpr uint8_t kFollowTint = 1;
inline constexpr uint8_t kNeutralIdle = 2;

inline void set_rule(lv_obj_t* card, uint8_t base, uint8_t flags) {
  if (!card) return;
  lv_obj_set_style_border_opa(card, base, kStore);
  lv_obj_set_style_outline_opa(card, flags, kStore);
}

inline bool rule_of(lv_obj_t* card, uint8_t& base, uint8_t& flags) {
  lv_style_value_t b, f;
  if (!card || lv_obj_get_local_style_prop(card, LV_STYLE_BORDER_OPA, &b, kStore) != LV_STYLE_RES_FOUND ||
      lv_obj_get_local_style_prop(card, LV_STYLE_OUTLINE_OPA, &f, kStore) != LV_STYLE_RES_FOUND) {
    return false;
  }
  base = static_cast<uint8_t>(b.num);
  flags = static_cast<uint8_t>(f.num);
  return true;
}

// The surface a card with a rule shows now.
inline uint8_t wanted(uint8_t base, uint8_t flags, bool tinted) {
  return (flags & kFollowTint) && tinted ? kJewel : base;
}

// The tint strength of a filled card (tile color "From icon" percent): its
// card's and circle's chroma (tone_color::jewel_card, jewel_fill).
inline void set_strength(lv_obj_t* card, uint8_t percent) {
  if (card) lv_obj_set_style_shadow_opa(card, percent, kStore);
}
inline uint8_t strength(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 5; ++depth, obj = lv_obj_get_parent(obj)) {
    lv_style_value_t value;
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_SHADOW_OPA, &value, kStore) == LV_STYLE_RES_FOUND) {
      return static_cast<uint8_t>(value.num);
    }
  }
  return 0;
}

}  // namespace tile_surface

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
// The corner circle keeps the same distance to the card edge as the bar
// pill (climate_layout::kOuterInset, user 2026-10-07): it is drawn smaller
// around its own middle by the difference, concentric with the tile corner;
// its box, the icon and the labels beside it stay where they were.
inline int corner_shrink() {
  const int extra = climate_layout::kOuterInset - inset();
  return extra > 0 ? extra : 0;
}
inline void shrink_corner_disc(lv_obj_t* disc) {
  if (!disc || !corner_shrink()) return;
  lv_obj_set_style_transform_width(disc, -corner_shrink(), 0);
  lv_obj_set_style_transform_height(disc, -corner_shrink(), 0);
  ui_surface_style::apply_radius(disc, radius_baseline() - corner_shrink(), 0);
}
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

// A label drawn as a cut-out in its card's color (the Media play glyph on its
// circle): tile_icon_source::refresh_discs gives it the card's current color
// after every tint.
inline constexpr char kCutoutTag[1] = {};
inline void mark_cutout(lv_obj_t* label) {
  if (label) lv_obj_set_user_data(label, const_cast<char*>(&kCutoutTag[0]));
}
inline bool is_cutout(lv_obj_t* obj) { return obj && lv_obj_get_user_data(obj) == &kCutoutTag[0]; }

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

// A pressed tile takes its lighter pressed color the way its card does
// (tile_press.h: at once, back after the theme's delay, at once when a
// swipe took the press over). Circles and resting controls take the card's
// pressed state (follow_card_state) with the very same transitions, so they
// keep their step above the card the whole time; a recolor apart from the
// card flashed.
inline void fade_with_card(lv_obj_t* obj) { tile_press::apply(obj); }

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

// A tile card (a button) over a see-through parent: a screensaver tile over
// its picture. Its circle takes the card's own color; walking past it took
// the overlay's black and drew a dark circle (user 2026-10-10). A see-through
// card on an opaque grid (Transparent Clock/Text) shows the grid there.
inline bool over_picture(lv_obj_t* card) {
  lv_obj_t* parent = lv_obj_get_parent(card);
  return lv_obj_check_type(card, &lv_button_class) && parent &&
         lv_obj_get_style_bg_opa(parent, LV_PART_MAIN) < LV_OPA_50;
}

// The nearest opaque background behind `obj` (the tile card).
inline lv_obj_t* card_of(lv_obj_t* obj) {
  lv_obj_t* host = obj ? lv_obj_get_parent(obj) : nullptr;
  for (int depth = 0; host && depth < 3 && lv_obj_get_style_bg_opa(host, LV_PART_MAIN) < LV_OPA_50 &&
       !over_picture(host); ++depth) {
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
// tile_icon_source registers it so "Fill" follows the icon. Stays null in
// host tests.
inline void (*g_icon_color_hook)(lv_obj_t* disc) = nullptr;

// tile_icon_source keeps a card's "Fill" strength (BG_OPA) in this unused
// selector, and "From cover" (Media) its icon permission (BORDER_OPA) and the
// cover color (BG_COLOR) in the next.
inline constexpr lv_style_selector_t kCardTintStore = LV_PART_MAIN | LV_STATE_USER_4;
inline constexpr lv_style_selector_t kCardCoverStore = LV_PART_MAIN | LV_STATE_USER_3;

// The fill the matching rule row decides ("Fuellen" per row, user
// 2026-10-11; tile_icon_colors RowFill), kept beside the "Fill" strength:
// none (the row follows "Ausfuellen"), off or on. A card whose rules were
// read keeps the value, also none, so a fill a row gave is cleared again.
enum : uint8_t { kRuleFillNone = 0, kRuleFillOff = 1, kRuleFillOn = 2 };
// The strength a row fills a tile without "Ausfuellen" at
// (tile_icon_colors::kTintDefault, checked in tile_icon_source.cpp).
inline constexpr uint8_t kRuleFillStrength = 20;

inline bool has_rule_fill(lv_obj_t* card, uint8_t& value) {
  lv_style_value_t stored;
  if (!card || lv_obj_get_local_style_prop(card, LV_STYLE_BORDER_OPA, &stored, kCardTintStore) != LV_STYLE_RES_FOUND) {
    return false;
  }
  value = static_cast<uint8_t>(stored.num);
  return true;
}

inline uint8_t rule_fill(lv_obj_t* card) {
  uint8_t value = kRuleFillNone;
  return has_rule_fill(card, value) ? value : kRuleFillNone;
}

// Stores the rule row's fill; true when it changed.
inline bool set_rule_fill(lv_obj_t* card, uint8_t value) {
  uint8_t current = kRuleFillNone;
  if (!card || (has_rule_fill(card, current) && current == value)) return false;
  lv_obj_set_style_border_opa(card, value, kCardTintStore);
  return true;
}

// The card `obj` belongs to that carries "Fill" or a rule row's fill (`obj`
// or up to two of its parents), else null.
inline lv_obj_t* fill_card_of(lv_obj_t* obj) {
  for (int depth = 0; obj && depth < 3; ++depth, obj = lv_obj_get_parent(obj)) {
    lv_style_value_t value;
    uint8_t rule = kRuleFillNone;
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_OPA, &value, kCardTintStore) == LV_STYLE_RES_FOUND ||
        has_rule_fill(obj, rule)) {
      return obj;
    }
  }
  return nullptr;
}

// The fill strength a card shows now: the matching rule row decides when it
// has its own mark (on: "Fill" or the default strength, off: none), else
// "Ausfuellen" (0 without it).
inline uint8_t shown_fill(lv_obj_t* card) {
  if (!card) return 0;
  lv_style_value_t value;
  const uint8_t marker =
      lv_obj_get_local_style_prop(card, LV_STYLE_BG_OPA, &value, kCardTintStore) == LV_STYLE_RES_FOUND
          ? static_cast<uint8_t>(value.num)
          : 0;
  const uint8_t rule = rule_fill(card);
  if (rule == kRuleFillOff) return 0;
  if (rule == kRuleFillOn) return marker ? marker : kRuleFillStrength;
  return marker;
}

// Whether a card is filled now ("Fill" or a rule row): it then already is
// the card its circle belongs to.
inline bool card_follows_icon(lv_obj_t* card) { return shown_fill(fill_card_of(card)) > 0; }

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

// The fill of a tile's circle and controls over `card` (at rest or pressed):
// in the light theme the shown icon color over the card itself
// (tone_color::tile_fill, lab 2026-10-10, like Home Assistant; a colored icon
// without "Circle in icon color" takes the neutral icon grey), else the steps
// of the "From icon" circle (circle_card, tone_color::fill). See-through
// screensaver tiles keep the steps' veil.
inline tone_color::Fill tile_fill(lv_obj_t* host, uint32_t card, uint32_t rgb, bool tinted, bool pressed,
                                  bool see_through_card, uint8_t percent) {
  // A filled tile in the light theme: one step above its card in the icon's
  // hue (tone_color::jewel_fill).
  if (tinted && !see_through_card && tile_surface::find(host) == tile_surface::kJewel) {
    return tone_color::jewel_fill(card, rgb, percent, tile_surface::strength(host));
  }
  if (ui_theme::light() && !see_through_card) {
    const uint32_t shown = tone_color::readable_icon(rgb);
    const uint32_t over = tone_color::circle_source(tinted || !icon_color_tints(shown) ? shown : ui_theme::icon());
    return tone_color::tile_fill(card, over, tinted, percent);
  }
  return tone_color::fill(circle_card(host, card, rgb, tinted, pressed, see_through_card), rgb, tinted, percent,
                          see_through_card);
}

// The color an icon was given, read on the surface that is open: a role of
// the theme (the icon grey, an off grey, set by an update outside the card's
// surface) is that role of this surface (user 2026-10-10: a dark icon on a
// grey tile). Other colors stay.
inline uint32_t surface_icon_color(lv_obj_t* icon) {
  const uint32_t rgb = icon ? icon_color(icon) : ui_theme::text();
  return ui_theme::map_role(rgb, ui_theme::roles_of(ui_theme::kThemeSurface), ui_theme::roles(), true);
}

// The fill of a disc for the card behind it (at rest or pressed) and its
// icon's color (tile_fill); `card` and `see_through_card` report that card.
inline tone_color::Fill disc_fill(lv_obj_t* disc, bool pressed, uint32_t& card, bool& see_through_card) {
  lv_obj_t* icon = icon_of(disc);
  const uint32_t rgb = surface_icon_color(icon);
  const bool tinted = glow_of(disc) && icon_color_tints(rgb);
  lv_obj_t* host = card_of(disc);
  card = card_state_color(host, pressed);
  see_through_card = see_through(host);
  return tile_fill(host, card, rgb, tinted, pressed, see_through_card, ui_surface_style::icon_glow_percent());
}

// The cut-outs of a card take its current resting color (mark_cutout).
inline void refresh_cutouts(lv_obj_t* card) {
  if (!card) return;
  const lv_color_t color = lv_color_hex(card_state_color(card, false));
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    const uint32_t inner = lv_obj_get_child_count(child);
    for (uint32_t j = 0; j < inner; ++j) {
      lv_obj_t* label = lv_obj_get_child(child, static_cast<int32_t>(j));
      if (is_cutout(label) && !lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), color)) {
        lv_obj_set_style_text_color(label, color, 0);
      }
    }
  }
}

// Color and opacity of a disc from its mode, glow option, the card (at rest
// and pressed) and the icon's color (tone_color::fill). Global discs follow
// the global option through the shared style, On discs always show, Off
// discs stay transparent. The icon is shown readable (show_readable).
inline void apply_fill(lv_obj_t* disc) {
  if (!is_disc(disc)) return;
  // The circle and its icon follow the card's surface (tile_surface, tile_icon_disc.h).
  const ui_theme::SurfaceScope surface = tile_surface::scope(disc);
  const Mode mode = mode_of(disc);
  lv_obj_t* icon = icon_of(disc);
  const uint32_t rgb = surface_icon_color(icon);
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
// A wrapping (concentric) disc holds its icon, so the object after it is not
// its icon: a half-height tile without title has its value label right after
// the corner disc, which made the screensaver treat the value as an icon (no
// own text color, hidden with the icon).
inline lv_obj_t* disc_of(lv_obj_t* icon) {
  if (!icon) return nullptr;
  lv_obj_t* parent = lv_obj_get_parent(icon);
  if (is_disc(parent)) return parent;
  const int32_t index = lv_obj_get_index(icon);
  lv_obj_t* below = parent && index > 0 ? lv_obj_get_child(parent, index - 1) : nullptr;
  return is_disc(below) && lv_obj_get_child_count(below) == 0 ? below : nullptr;
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
  lv_color_t requested = lv_color_hex(ui_theme::text());
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
  lv_obj_set_style_bg_color(disc, lv_color_hex(ui_theme::icon()), 0);
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
  shrink_corner_disc(disc);
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

// An icon over its title (Folder, Settings, Back, Scene, Camera, the Switch
// icon button): the disc, a gap of 30 % of the disc and the title's capitals
// are one group in the card's middle; descenders hang below it (user
// 2026-10-12: the title sat high and close under the icon, the gap 6 to
// 20 px by the panel: fixed offsets of -20 / +35 while the disc follows the
// cell height). The offsets from the card's middle (LV_ALIGN_CENTER) of the
// icon (the disc's middle) and of a one-line title. The preview reads them as
// --icon-title-icon-dy / --icon-title-title-dy (web_admin_styles.cpp).
struct IconTitleOffsets {
  int icon = 0;
  int title = 0;
};
inline IconTitleOffsets icon_title_offsets(const lv_font_t* icon_font, const lv_font_t* title_font) {
  IconTitleOffsets out;
  if (!icon_font || !title_font) return out;
  const int disc = header_diameter(lv_font_get_glyph_width(icon_font, kMdiReferenceGlyph, 0));
  const int gap = (disc * 3 + 5) / 10;
  lv_font_glyph_dsc_t capital{};
  const int caps = lv_font_get_glyph_dsc(title_font, &capital, 'H', 0) ? capital.box_h : 0;
  const int group = disc + gap + caps;
  const int top = -(group / 2);
  out.icon = top + disc / 2;
  // The title's baseline at the group's bottom: its label middle lies half
  // a line above the baseline plus the font's base line.
  const int line = lv_font_get_line_height(title_font);
  out.title = top + group - line / 2 + title_font->base_line;
  return out;
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
    shrink_corner_disc(disc);
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
