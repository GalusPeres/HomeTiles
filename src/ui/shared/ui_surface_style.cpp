#include "src/ui/shared/ui_surface_style.h"

#include "src/core/config/config_manager.h"
#include "src/ui/screensaver/image_screensaver.h"
#include <atomic>

namespace ui_surface_style {
namespace {

// USER_1/USER_2 are already used for image-preview states.
constexpr lv_obj_flag_t kGlobalTileBorderFlag = LV_OBJ_FLAG_USER_3;
constexpr lv_obj_flag_t kHiddenTileBorderFlag = LV_OBJ_FLAG_USER_4;
volatile bool g_global_tile_border_refresh_pending = false;
std::atomic<bool> g_radius_refresh_pending{false};
std::atomic<int> g_preview_radius{-1};
bool g_wallpaper_radius_pending = false;
uint32_t g_wallpaper_radius_changed_at = 0;
constexpr int kRadiusStyleCount = 64;
lv_style_t g_radius_styles[kRadiusStyleCount]{};
bool g_radius_style_initialized[kRadiusStyleCount]{};

// One shared opacity style per (opacity, follows global option) pair. Discs
// use at most a handful of opacities; the table is small and static.
struct IconDiscStyle {
  lv_style_t style;
  lv_opa_t opa = 0;
  bool follows_global = false;
  bool initialized = false;
};
constexpr int kIconDiscStyleCount = 6;
IconDiscStyle g_icon_disc_styles[kIconDiscStyleCount]{};
std::atomic<bool> g_icon_disc_refresh_pending{false};

lv_opa_t icon_disc_opa(const IconDiscStyle& entry) {
  return entry.follows_global && !configManager.getConfig().icon_discs
             ? static_cast<lv_opa_t>(LV_OPA_TRANSP)
             : entry.opa;
}

IconDiscStyle* icon_disc_style(lv_opa_t opa, bool follows_global) {
  IconDiscStyle* free_entry = nullptr;
  for (IconDiscStyle& entry : g_icon_disc_styles) {
    if (!entry.initialized) {
      if (!free_entry) free_entry = &entry;
      continue;
    }
    if (entry.opa == opa && entry.follows_global == follows_global) return &entry;
  }
  if (!free_entry) return nullptr;
  lv_style_init(&free_entry->style);
  free_entry->opa = opa;
  free_entry->follows_global = follows_global;
  free_entry->initialized = true;
  lv_style_set_bg_opa(&free_entry->style, icon_disc_opa(*free_entry));
  return free_entry;
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
  // Border width is constant. State-specific copies force a full descendant
  // layout refresh on every press/release, even when they are all zero.
  lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
  for (lv_style_selector_t selector : kSelectors) {
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, selector);
    lv_obj_set_style_outline_width(obj, enabled ? 1 : 0, selector);
    lv_obj_set_style_outline_pad(obj, -1, selector);
    lv_obj_set_style_outline_color(obj, lv_color_white(), selector);
    lv_obj_set_style_outline_opa(
        obj, enabled ? 51 : LV_OPA_TRANSP, selector);  // approximately 20%
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

void apply_global_tile_border(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, kGlobalTileBorderFlag);
  apply_style(obj, configManager.getConfig().tile_borders);
}

void apply_icon_disc_opa(lv_obj_t* obj, lv_opa_t opa, bool follows_global) {
  if (!obj) return;
  IconDiscStyle* target = icon_disc_style(opa, follows_global);
  if (!target) return;
  // A disc carries exactly one of the shared opacity styles.
  for (IconDiscStyle& entry : g_icon_disc_styles) {
    if (entry.initialized && &entry != target) {
      lv_obj_remove_style(obj, &entry.style, 0);
    }
  }
  lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_OPA, 0);
  lv_obj_add_style(obj, &target->style, 0);
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
    for (IconDiscStyle& entry : g_icon_disc_styles) {
      if (!entry.initialized) continue;
      lv_style_set_bg_opa(&entry.style, icon_disc_opa(entry));
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
