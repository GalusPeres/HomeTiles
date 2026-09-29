#pragma once

#include <lvgl.h>
#include <stdint.h>

// popup_shell.h: the fill of the controls around a popup's header.
void popup_shell_control_fill(uint32_t card_rgb, uint32_t icon_rgb, lv_color_t& color, lv_opa_t& opa);

// Footer controls of the history popups (7D/24H/Today and the date and day
// pills) look like the pressed close button: the selected control has exactly
// the control fill (the icon color with tile color "From icon" and "Circle in
// icon color", else white, at the Glow strength), an info pill half of it.
// All their text is white.
namespace popup_nav_style {

inline void fill(lv_color_t popup, lv_color_t icon, lv_color_t& color, lv_opa_t& opa) {
  popup_shell_control_fill(lv_color_to_u32(popup) & 0xFFFFFF, lv_color_to_u32(icon) & 0xFFFFFF, color, opa);
}

// A toggle (7D, 24H, Today): the selected one has the disc fill, the others
// only their white label; pressing shows the disc fill.
inline void style_toggle(lv_obj_t* btn, lv_obj_t* label, lv_color_t popup, lv_color_t icon, bool selected) {
  if (!btn) return;
  lv_color_t color;
  lv_opa_t opa;
  fill(popup, icon, color, opa);
  const lv_style_selector_t selectors[] = {0, LV_STATE_PRESSED};
  for (const lv_style_selector_t selector : selectors) {
    const bool pressed = selector == LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(btn, color, selector);
    lv_obj_set_style_bg_opa(btn, selected || pressed ? opa : LV_OPA_TRANSP, selector);
    lv_obj_set_style_border_width(btn, 0, selector);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_transform_width(btn, 0, selector);
    lv_obj_set_style_transform_height(btn, 0, selector);
    lv_obj_set_style_translate_y(btn, 0, selector);
  }
  if (label) {
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
  }
}

// An info pill (date range, day title): half the disc fill, white text.
inline void style_pill(lv_obj_t* pill, lv_obj_t* label, lv_color_t popup, lv_color_t icon) {
  lv_color_t color;
  lv_opa_t opa;
  fill(popup, icon, color, opa);
  if (pill) {
    lv_obj_set_style_bg_color(pill, color, 0);
    lv_obj_set_style_bg_opa(pill, static_cast<lv_opa_t>(opa / 2), 0);
  }
  if (label) {
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
  }
}

}  // namespace popup_nav_style
