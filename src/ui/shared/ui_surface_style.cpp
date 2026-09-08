#include "src/ui/shared/ui_surface_style.h"

#include "src/core/config/config_manager.h"

namespace ui_surface_style {
namespace {

// USER_1/USER_2 are already used for image-preview states.
constexpr lv_obj_flag_t kGlobalTileBorderFlag = LV_OBJ_FLAG_USER_3;
volatile bool g_global_tile_border_refresh_pending = false;

void apply_style(lv_obj_t* obj, bool enabled) {
  if (!obj) return;

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

void apply_tile_border(lv_obj_t* obj, bool enabled) {
  apply_style(obj, enabled);
}

void apply_global_tile_border(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, kGlobalTileBorderFlag);
  apply_style(obj, configManager.getConfig().tile_borders);
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
