#include "src/ui/shared/ui_surface_style.h"

#include "src/core/config/config_manager.h"
#include "src/core/config/icon_glow.h"
#include "src/tiles/runtime/tile_screen.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_theme.h"

namespace ui_surface_style {
namespace {

// USER_1/USER_2 are already used for image-preview states.
constexpr lv_obj_flag_t kGlobalTileBorderFlag = LV_OBJ_FLAG_USER_3;
constexpr lv_obj_flag_t kHiddenTileBorderFlag = LV_OBJ_FLAG_USER_4;
volatile bool g_global_tile_border_refresh_pending = false;
constexpr int kRadiusStyleCount = 64;
lv_style_t g_radius_styles[kRadiusStyleCount]{};
bool g_radius_style_initialized[kRadiusStyleCount]{};

// The shared opacities of the discs: always shown, Global (shown too since
// the global disc option is gone), Off; each once for opaque cards and once
// for see-through cards, where a disc stays a veil (tone_color.h). Discs
// carry their own color (tone_color::fill).
struct IconDiscStyle {
  lv_style_t style;
  bool initialized = false;
};
constexpr uint8_t kIconDiscShownKey = 0;
constexpr uint8_t kIconDiscGlobalKey = 1;
constexpr uint8_t kIconDiscOffKey = 2;
constexpr uint8_t kIconDiscModes = 3;
// Keys of see-through cards follow the opaque ones.
constexpr int kIconDiscStyleCount = kIconDiscModes * 2;
IconDiscStyle g_icon_disc_styles[kIconDiscStyleCount]{};
// The control opacity of tile controls on opaque and on see-through cards.
IconDiscStyle g_control_styles[2]{};

lv_opa_t icon_disc_opa(uint8_t key) {
  const uint8_t mode = key % kIconDiscModes;
  if (mode == kIconDiscOffKey) return LV_OPA_TRANSP;
  return tone_color::disc_opa(icon_glow_percent(), key >= kIconDiscModes);
}

lv_opa_t control_fill_opa(bool see_through) {
  return tone_color::control_opa(icon_glow_percent(), see_through);
}

// The card's resting color.
uint32_t card_color(lv_obj_t* obj) {
  lv_style_value_t value;
  return (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, LV_PART_MAIN | LV_STATE_DEFAULT) == LV_STYLE_RES_FOUND
              ? lv_color_to_u32(value.color)
              : lv_color_to_u32(lv_obj_get_style_bg_color(obj, LV_PART_MAIN))) & 0xFFFFFF;
}

// The frame line of a tile (user 2026-10-11, like Tessera's theme::LINE and
// theme::outline): a solid line, on the theme's plain card a fixed neutral
// (light #DDDDDD, dark #292929), on any other card (filled, an own color)
// that card a little darker on the light theme's screen and a little lighter
// on the dark one. It moves away from the screen behind it: a lighter line
// on a filled card in the light theme looked like a frayed edge (user
// 2026-10-11, "unsauber").
lv_color_t frame_line(lv_obj_t* obj) { return ui_surface_style::frame_line_for(card_color(obj)); }

// A filled card of the light theme (white words) draws no line (user
// 2026-10-11, b391): its darker line stepped along the rounded corners
// ("ausgefranst", the strong colors of b389 showed it); the color stands off
// the light screen by itself. A picture over its card (a Camera tile,
// border_post) neither (user 2026-10-12, worst on the S3): the light line
// over the dark picture stepped along the picture's own rounded corners. White
// and other light cards keep theirs, the dark theme keeps every line.
bool frame_line_shown(lv_obj_t* obj) {
  if (!ui_theme::theme_light()) return true;
  if (lv_obj_get_style_border_post(obj, LV_PART_MAIN)) return false;
  return tone_color::light_surface(card_color(obj));
}

void apply_style(lv_obj_t* obj, bool enabled) {
  if (!obj) return;
  // The line follows the design of the card's screen (tile_screen.h).
  const ui_theme::SurfaceScope screen = tile_screen::scope(obj);

  enabled = enabled && !lv_obj_has_flag(obj, kHiddenTileBorderFlag) && frame_line_shown(obj);

  // An LVGL border occupies the inner box and reduces usable content space.
  // A 1 px outline with -1 px padding follows the same inset edge
  // without affecting content or padding. Apply the same values to button
  // states; otherwise PRESSED/FOCUSED styles win on touch and the line
  // briefly disappears.
  static constexpr lv_style_selector_t kSelectors[] = {
      LV_PART_MAIN | LV_STATE_DEFAULT,
      LV_PART_MAIN | LV_STATE_PRESSED,
      LV_PART_MAIN | LV_STATE_FOCUSED,
      LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED),
  };
  const lv_color_t color = frame_line(obj);
  const lv_opa_t opa = LV_OPA_COVER;
  // A card whose children cover it (a Camera tile's picture, user
  // 2026-10-11) draws its line over them: a 1 px border drawn after the
  // children (border_post, draw_frame_over_children) instead of the outline
  // below them. Its width stays, so the children never move; only its
  // opacity follows the option.
  if (lv_obj_get_style_border_post(obj, LV_PART_MAIN)) {
    if (lv_obj_get_style_border_width(obj, LV_PART_MAIN) != 1) lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    for (lv_style_selector_t selector : kSelectors) {
      lv_obj_set_style_outline_width(obj, 0, selector);
      lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, selector);
      lv_obj_set_style_border_color(obj, color, selector);
      lv_obj_set_style_border_opa(obj, enabled ? opa : LV_OPA_TRANSP, selector);
    }
    return;
  }
  // Border width is constant. State-specific copies force a full descendant
  // layout refresh on every press/release, even when they are all zero.
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  for (lv_style_selector_t selector : kSelectors) {
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, selector);
    lv_obj_set_style_outline_width(obj, enabled ? 1 : 0, selector);
    lv_obj_set_style_outline_pad(obj, -1, selector);
    lv_obj_set_style_outline_color(obj, color, selector);
    lv_obj_set_style_outline_opa(obj, enabled ? opa : LV_OPA_TRANSP, selector);
  }
}

void refresh_marked_tree(lv_obj_t* root, bool enabled) {
  if (!root) return;
  if (lv_obj_has_flag(root, kGlobalTileBorderFlag)) {
    apply_style(root, enabled);
  }

  const uint32_t child_count = lv_obj_get_child_count(root);
  for (uint32_t i = 0; i < child_count; ++i) {
    refresh_marked_tree(lv_obj_get_child(root, static_cast<int32_t>(i)), enabled);
  }
}

}  // namespace

int radius(int baseline) {
  return tile_radius::inset(tile_radius::kDefault, tile_radius::kMinimum - baseline);
}

void apply_radius(lv_obj_t* obj, int baseline, lv_style_selector_t selector) {
  if (!obj || baseline < 0 || baseline >= kRadiusStyleCount) return;
  auto* style = &g_radius_styles[baseline];
  if (!g_radius_style_initialized[baseline]) {
    lv_style_init(style);
    lv_style_set_radius(style, radius(baseline));
    g_radius_style_initialized[baseline] = true;
  }
  lv_obj_remove_local_style_prop(obj, LV_STYLE_RADIUS, selector);
  // LVGL replaces an existing identical style/selector when adding it again.
  lv_obj_add_style(obj, style, selector);
}

void disable_tile_border(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, kHiddenTileBorderFlag);
  apply_style(obj, false);
}


void apply_tile_border(lv_obj_t* obj, bool enabled) {
  apply_style(obj, enabled);
}

void draw_frame_over_children(lv_obj_t* obj) {
  if (!obj || lv_obj_get_style_border_post(obj, LV_PART_MAIN)) return;
  // Keeps the line's current state, whichever option set it.
  const bool enabled = lv_obj_get_style_outline_width(obj, LV_PART_MAIN) > 0 &&
                       lv_obj_get_style_outline_opa(obj, LV_PART_MAIN) > LV_OPA_TRANSP;
  lv_obj_set_style_border_post(obj, true, LV_PART_MAIN);
  apply_style(obj, enabled);
}

void refresh_tile_border(lv_obj_t* obj) {
  if (obj && lv_obj_has_flag(obj, kGlobalTileBorderFlag)) apply_style(obj, configManager.getConfig().tile_borders);
}

void apply_global_tile_border(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, kGlobalTileBorderFlag);
  apply_style(obj, configManager.getConfig().tile_borders);
}

// Circles are always shown at the fixed strength (user 2026-10-11: no global
// circle settings).
uint8_t icon_glow_percent() { return icon_glow::kDefault; }

bool icon_discs_shown() { return true; }

lv_color_t frame_line_for(uint32_t card) {
  // The plain card of either theme (a screensaver tile in the other design
  // too) keeps that theme's fixed line.
  if (card == ui_theme::kLight.card) return lv_color_hex(0xDDDDDD);
  if (card == ui_theme::kDark.card) return lv_color_hex(0x292929);
  return ui_theme::theme_light() ? lv_color_mix(lv_color_black(), lv_color_hex(card), 36)
                                 : lv_color_mix(lv_color_white(), lv_color_hex(card), 23);
}

void apply_popup_border(lv_obj_t* obj, lv_color_t color, lv_opa_t opa) {
  if (!obj) return;
  // Same hairline as the tiles and the same global Tile borders option.
  // Called on every popup sync: only a change touches the styles.
  const bool enabled = configManager.getConfig().tile_borders;
  const int32_t width = enabled ? 1 : 0;
  const lv_opa_t target = enabled ? opa : static_cast<lv_opa_t>(LV_OPA_TRANSP);
  if (lv_obj_get_style_outline_width(obj, LV_PART_MAIN) != width)
    lv_obj_set_style_outline_width(obj, width, 0);
  if (lv_obj_get_style_outline_pad(obj, LV_PART_MAIN) != -1)
    lv_obj_set_style_outline_pad(obj, -1, 0);
  if (!lv_color_eq(lv_obj_get_style_outline_color(obj, LV_PART_MAIN), color))
    lv_obj_set_style_outline_color(obj, color, 0);
  if (lv_obj_get_style_outline_opa(obj, LV_PART_MAIN) != target)
    lv_obj_set_style_outline_opa(obj, target, 0);
}

void apply_icon_disc(lv_obj_t* obj, bool off, bool follows_global, bool see_through) {
  if (!obj) return;
  const uint8_t mode = off ? kIconDiscOffKey : follows_global ? kIconDiscGlobalKey : kIconDiscShownKey;
  const uint8_t key = see_through ? mode + kIconDiscModes : mode;
  IconDiscStyle& target = g_icon_disc_styles[key];
  if (!target.initialized) {
    lv_style_init(&target.style);
    lv_style_set_bg_opa(&target.style, icon_disc_opa(key));
    target.initialized = true;
  }
  // A disc carries exactly one of the shared opacity styles.
  for (IconDiscStyle& entry : g_icon_disc_styles) {
    if (entry.initialized && &entry != &target) {
      lv_obj_remove_style(obj, &entry.style, 0);
    }
  }
  lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_OPA, 0);
  lv_obj_add_style(obj, &target.style, 0);
}

void apply_control_fill(lv_obj_t* obj, lv_color_t color, lv_style_selector_t selector, bool see_through) {
  if (!obj) return;
  IconDiscStyle& target = g_control_styles[see_through ? 1 : 0];
  if (!target.initialized) {
    lv_style_init(&target.style);
    lv_style_set_bg_opa(&target.style, control_fill_opa(see_through));
    target.initialized = true;
  }
  IconDiscStyle& other = g_control_styles[see_through ? 0 : 1];
  if (other.initialized) lv_obj_remove_style(obj, &other.style, selector);
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, selector) != LV_STYLE_RES_FOUND ||
      !lv_color_eq(value.color, color)) {
    lv_obj_set_style_bg_color(obj, color, selector);
  }
  if (selector & LV_STATE_PRESSED) {
    // A pressed button rests in the same color (transparent), so the theme's
    // press fade runs from the card to its fill, not through the theme or
    // black resting color.
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, LV_PART_MAIN) != LV_STYLE_RES_FOUND ||
        !lv_color_eq(value.color, color)) {
      lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    }
    // The pressed fill is exactly the control color: no theme darkening (a
    // black recolor in the default theme, a color filter in older ones).
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_COLOR_FILTER_OPA, &value, selector) != LV_STYLE_RES_FOUND ||
        value.num != LV_OPA_TRANSP) {
      lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, selector);
    }
    if (lv_obj_get_local_style_prop(obj, LV_STYLE_RECOLOR_OPA, &value, selector) != LV_STYLE_RES_FOUND ||
        value.num != LV_OPA_TRANSP) {
      lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, selector);
    }
  }
  // LVGL replaces an existing identical style/selector when adding it again.
  lv_obj_add_style(obj, &target.style, selector);
}

void request_global_tile_border_refresh() {
  g_global_tile_border_refresh_pending = true;
}

void process_pending_updates() {
  if (!g_global_tile_border_refresh_pending) return;
  g_global_tile_border_refresh_pending = false;

  const bool enabled = configManager.getConfig().tile_borders;
  refresh_marked_tree(lv_screen_active(), enabled);
  refresh_marked_tree(lv_layer_top(), enabled);
}

}  // namespace ui_surface_style
