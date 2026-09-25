#include "src/ui/shared/ui_surface_style.h"

#include "src/core/config/config_manager.h"
#include "src/core/config/icon_glow.h"
#include "src/ui/screensaver/image_screensaver.h"
#include <atomic>

namespace ui_surface_style {
namespace {

// USER_1/USER_2 are already used for image-preview states.
constexpr lv_obj_flag_t kGlobalTileBorderFlag = LV_OBJ_FLAG_USER_3;
constexpr lv_obj_flag_t kHiddenTileBorderFlag = LV_OBJ_FLAG_USER_4;
// The tile border hairline at about 20 %: white, or the hint of a glowing
// icon (border_hint), so it stays a slightly lighter step of the tile.
constexpr lv_opa_t kTileBorderOpa = 51;
// A glowing icon's border hint is kept as a local style value under a state
// tiles never enter, so border refreshes can restore it.
constexpr lv_style_selector_t kBorderTintStore = LV_PART_MAIN | LV_STATE_USER_4;
volatile bool g_global_tile_border_refresh_pending = false;
std::atomic<bool> g_radius_refresh_pending{false};
std::atomic<int> g_preview_radius{-1};
bool g_wallpaper_radius_pending = false;
uint32_t g_wallpaper_radius_changed_at = 0;
constexpr int kRadiusStyleCount = 64;
lv_style_t g_radius_styles[kRadiusStyleCount]{};
bool g_radius_style_initialized[kRadiusStyleCount]{};

// One shared opacity style per (white or glowing disc, contrast step 0..3,
// follows the global disc option), plus the transparent Off style. The
// opacity is computed from the global Glow strength and the global disc
// option, so a change of either only updates these 17 styles
// (request_icon_disc_refresh) and every disc follows, also in cached grids.
struct IconDiscStyle {
  lv_style_t style;
  bool initialized = false;
};
constexpr uint8_t kIconDiscGlowKey = 8;
constexpr uint8_t kIconDiscOffKey = 16;
constexpr int kIconDiscStyleCount = kIconDiscOffKey + 1;
IconDiscStyle g_icon_disc_styles[kIconDiscStyleCount]{};
std::atomic<bool> g_icon_disc_refresh_pending{false};

lv_opa_t icon_disc_opa(uint8_t key) {
  if (key == kIconDiscOffKey) return LV_OPA_TRANSP;
  const bool follows_global = (key & 1) != 0;
  if (follows_global && !configManager.getConfig().icon_discs) return LV_OPA_TRANSP;
  const uint8_t glow = configManager.getConfig().icon_glow;
  const unsigned full = (key & kIconDiscGlowKey) ? icon_glow::disc_opa(glow) : icon_glow::neutral_opa(glow);
  const unsigned step = (key >> 1) & 3;
  // tile_icon_disc::scaled_opa(): subtler on dark tiles.
  return static_cast<lv_opa_t>((full * (24 + 7 * step) + 22) / 45);
}

void apply_style(lv_obj_t* obj, bool enabled) {
  if (!obj) return;

  enabled = enabled && !lv_obj_has_flag(obj, kHiddenTileBorderFlag);

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
  // A tile whose icon disc glows keeps the icon's border hint
  // (set_tile_border_tint); every other tile uses white.
  lv_color_t color = lv_color_white();
  lv_style_value_t stored{};
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_OUTLINE_COLOR, &stored, kBorderTintStore) ==
      LV_STYLE_RES_FOUND) {
    color = stored.color;
  }
  const lv_opa_t opa = kTileBorderOpa;
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
  const int preview = g_preview_radius.load();
  const int outer = preview < 0 ? configManager.getConfig().tile_radius : preview;
  return tile_radius::inset(tile_radius::clamp(outer),
                            tile_radius::kMinimum - baseline);
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

void request_global_radius_refresh() {
  g_preview_radius.store(-1);
  g_radius_refresh_pending.store(true);
}

void preview_radius(int value) {
  g_preview_radius.store(tile_radius::clamp(value));
  g_radius_refresh_pending.store(true);
}

void disable_tile_border(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, kHiddenTileBorderFlag);
  apply_style(obj, false);
}

void apply_tile_border(lv_obj_t* obj, bool enabled) {
  apply_style(obj, enabled);
}

namespace {

// The tile card that owns the border: obj or up to three parents (a switch
// tile's disc sits in a content container).
lv_obj_t* border_host(lv_obj_t* obj) {
  lv_obj_t* host = obj;
  for (int depth = 0; host && depth < 3; ++depth) {
    if (lv_obj_has_flag(host, kGlobalTileBorderFlag) || lv_obj_has_flag(host, kHiddenTileBorderFlag))
      return host;
    host = lv_obj_get_parent(host);
  }
  return nullptr;
}

void store_border_color(lv_obj_t* host, lv_color_t color, bool keep) {
  lv_style_value_t stored{};
  const bool found =
      lv_obj_get_local_style_prop(host, LV_STYLE_OUTLINE_COLOR, &stored, kBorderTintStore) ==
      LV_STYLE_RES_FOUND;
  if (keep ? (found && lv_color_eq(stored.color, color)) : !found) return;
  if (keep) {
    lv_obj_set_style_outline_color(host, color, kBorderTintStore);
  } else {
    lv_obj_remove_local_style_prop(host, LV_STYLE_OUTLINE_COLOR, kBorderTintStore);
  }
  apply_style(host, lv_obj_get_style_outline_width(host, LV_PART_MAIN) > 0);
}

}  // namespace

void set_tile_border_tint(lv_obj_t* obj, lv_color_t icon) {
  if (lv_obj_t* host = border_host(obj)) store_border_color(host, border_hint(icon), true);
}

void clear_tile_border_tint(lv_obj_t* obj) {
  if (lv_obj_t* host = border_host(obj)) store_border_color(host, lv_color_white(), false);
}

void apply_global_tile_border(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, kGlobalTileBorderFlag);
  apply_style(obj, configManager.getConfig().tile_borders);
}

lv_opa_t icon_glow_opa() {
  return icon_glow::disc_opa(configManager.getConfig().icon_glow);
}

lv_opa_t icon_neutral_opa() {
  return icon_glow::neutral_opa(configManager.getConfig().icon_glow);
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

void apply_icon_disc(lv_obj_t* obj, bool glow, uint8_t step, bool off, bool follows_global) {
  if (!obj) return;
  const uint8_t key = off ? kIconDiscOffKey
                          : static_cast<uint8_t>((glow ? kIconDiscGlowKey : 0) | ((step & 3) << 1) |
                                                 (follows_global ? 1 : 0));
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

void request_global_tile_border_refresh() {
  g_global_tile_border_refresh_pending = true;
}

void request_icon_disc_refresh() {
  g_icon_disc_refresh_pending.store(true);
}

void process_pending_updates() {
  if (g_radius_refresh_pending.exchange(false)) {
    for (int i = 0; i < kRadiusStyleCount; ++i) {
      if (!g_radius_style_initialized[i]) continue;
      lv_style_set_radius(&g_radius_styles[i], radius(i));
      lv_obj_report_style_change(&g_radius_styles[i]);
    }
    g_wallpaper_radius_pending = true;
    g_wallpaper_radius_changed_at = lv_tick_get();
  }
  // Wallpaper pixels have baked corners. Coalesce slider input before using
  // the existing wallpaper replacement path; do not decode on every step.
  if (g_wallpaper_radius_pending && lv_tick_elaps(g_wallpaper_radius_changed_at) >= 350) {
    g_wallpaper_radius_pending = false;
    image_screensaver_config_changed();
  }
  if (g_icon_disc_refresh_pending.exchange(false)) {
    for (int key = 0; key < kIconDiscStyleCount; ++key) {
      IconDiscStyle& entry = g_icon_disc_styles[key];
      if (!entry.initialized) continue;
      lv_style_set_bg_opa(&entry.style, icon_disc_opa(static_cast<uint8_t>(key)));
      lv_obj_report_style_change(&entry.style);
    }
  }
  if (!g_global_tile_border_refresh_pending) return;
  g_global_tile_border_refresh_pending = false;

  const bool enabled = configManager.getConfig().tile_borders;
  refresh_marked_tree(lv_screen_active(), enabled);
  refresh_marked_tree(lv_layer_top(), enabled);
}

}  // namespace ui_surface_style
