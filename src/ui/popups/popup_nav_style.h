#pragma once

#include <lvgl.h>
#include <stdint.h>

// popup_shell.h: the fill of the controls around a popup's header; with
// `tinted` whether it takes the icon's hue.
void popup_shell_control_fill(uint32_t card_rgb, uint32_t icon_rgb, lv_color_t& color, lv_opa_t& opa,
                              bool* tinted);

// Footer controls of the history popups (7D/24H/Today and the date and day
// pills) look like the pressed close button: the selected control has exactly
// the control fill (the circle's color, tone_color::fill), an info pill the
// same, and a press shows that same color. All their text is white.
namespace popup_nav_style {

inline void fill(lv_color_t popup, lv_color_t icon, lv_color_t& color, lv_opa_t& opa, bool* tinted = nullptr) {
  popup_shell_control_fill(lv_color_to_u32(popup) & 0xFFFFFF, lv_color_to_u32(icon) & 0xFFFFFF, color, opa,
                           tinted);
}

// A press shows exactly the control color: no theme darkening. Set once.
inline void no_press_filter(lv_obj_t* obj, lv_style_selector_t selector) {
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_COLOR_FILTER_OPA, &value, selector) != LV_STYLE_RES_FOUND ||
      value.num != LV_OPA_TRANSP) {
    lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, selector);
  }
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
    no_press_filter(btn, selector);
  }
  if (label) {
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
  }
}

// Sets a background only when it changes (the Media popup restyles on every
// state update).
inline void set_bg(lv_obj_t* obj, lv_color_t color, lv_opa_t opa, lv_style_selector_t selector) {
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &value, selector) != LV_STYLE_RES_FOUND ||
      !lv_color_eq(value.color, color)) {
    lv_obj_set_style_bg_color(obj, color, selector);
  }
  if (lv_obj_get_local_style_prop(obj, LV_STYLE_BG_OPA, &value, selector) != LV_STYLE_RES_FOUND ||
      value.num != opa) {
    lv_obj_set_style_bg_opa(obj, opa, selector);
  }
}

// A button with a fill only while pressed (Media previous, next and volume):
// the control fill, like the pressed close button.
inline void style_press(lv_obj_t* btn, lv_color_t popup, lv_color_t icon) {
  if (!btn) return;
  lv_color_t color;
  lv_opa_t opa;
  fill(popup, icon, color, opa);
  set_bg(btn, color, opa, LV_PART_MAIN | LV_STATE_PRESSED);
  no_press_filter(btn, LV_PART_MAIN | LV_STATE_PRESSED);
}

// A slider (Media volume and position): the unused track like an info pill
// (the control fill), the used part at full opacity in the icon color when
// the controls take its hue, else white. The knob stays white.
inline void style_slider(lv_obj_t* slider, lv_color_t popup, lv_color_t icon) {
  if (!slider) return;
  lv_color_t color;
  lv_opa_t opa;
  bool tinted = false;
  fill(popup, icon, color, opa, &tinted);
  set_bg(slider, color, opa, LV_PART_MAIN);
  set_bg(slider, tinted ? icon : lv_color_white(), LV_OPA_COVER, LV_PART_INDICATOR);
}

// An info pill (date range, day title): the control fill like a selected
// toggle, white text.
inline void style_pill(lv_obj_t* pill, lv_obj_t* label, lv_color_t popup, lv_color_t icon) {
  lv_color_t color;
  lv_opa_t opa;
  fill(popup, icon, color, opa);
  if (pill) {
    lv_obj_set_style_bg_color(pill, color, 0);
    lv_obj_set_style_bg_opa(pill, opa, 0);
  }
  if (label) {
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
  }
}

}  // namespace popup_nav_style
