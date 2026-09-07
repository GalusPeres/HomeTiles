#pragma once
#include "src/ui/popups/popup_layout.h"

// Shared Settings controls; keep popup editors visually identical to Settings.
namespace ui_control_style {
inline void slider(lv_obj_t* slider) {
  if (!slider) return;
  lv_obj_set_height(slider, popup_layout::scale(20));
  lv_obj_set_style_width(slider, popup_layout::scale(42), LV_PART_KNOB);
  lv_obj_set_style_height(slider, popup_layout::scale(42), LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, popup_layout::scale(20));
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
  lv_obj_clear_flag(slider, LV_OBJ_FLAG_SCROLLABLE);
}

inline void dropdown(lv_obj_t* dd) {
  lv_obj_set_height(dd, popup_layout::scale(52));
  lv_obj_set_style_text_font(dd, popup_layout::font20(), LV_PART_MAIN);
  lv_obj_set_style_text_font(dd, &ui_symbols_20, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(dd, lv_color_hex(0x1E1E1E), 0);
  lv_obj_set_style_text_color(dd, lv_color_white(), 0);
  lv_obj_set_style_text_color(dd, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_radius(dd, popup_layout::scale(10), 0);
  lv_obj_set_style_border_color(dd, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(dd, 1, 0);
  lv_obj_set_style_border_opa(dd, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(dd, popup_layout::scale(12), 0);
  lv_obj_set_style_pad_right(dd, popup_layout::scale(36), 0);
}

inline void dropdownList(lv_obj_t* list) {
  if (!list) return;
  lv_obj_set_style_bg_color(list, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_font(list, popup_layout::font20(), LV_PART_MAIN);
  lv_obj_set_style_text_color(list, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_radius(list, popup_layout::scale(10), LV_PART_MAIN);
  lv_obj_set_style_border_color(list, lv_color_hex(0x555555), LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(list, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, popup_layout::scale(6), LV_PART_MAIN);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x26A69A), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED);
  lv_obj_set_style_text_font(list, popup_layout::font20(), LV_PART_SELECTED);
  lv_obj_set_style_text_color(list, lv_color_white(), LV_PART_SELECTED);
  lv_obj_set_style_radius(list, popup_layout::scale(6), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(list, lv_color_hex(0x4A4A4A), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
}

inline void largeDropdownList(lv_obj_t* list) {
  if (!list) return;
  dropdownList(list);
  lv_obj_set_style_text_font(list, popup_layout::font28(), LV_PART_MAIN);
  lv_obj_set_style_text_font(list, popup_layout::font28(), LV_PART_SELECTED);
  lv_obj_set_style_pad_left(list, popup_layout::scale(20), 0);
  lv_obj_set_style_pad_right(list, popup_layout::scale(10), 0);
  lv_obj_set_style_pad_ver(list, popup_layout::scale(8), 0);
  lv_obj_set_style_clip_corner(list, true, 0);
}

inline void largeDropdown(lv_obj_t* dd) {
  dropdown(dd);
  lv_dropdown_set_symbol(dd, LV_SYMBOL_DOWN);
  lv_obj_set_height(dd, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(dd, popup_layout::scale(18), 0);
  lv_obj_set_style_pad_left(dd, popup_layout::scale(20), 0);
  lv_obj_set_style_radius(dd, popup_layout::scale(18), 0);
  lv_obj_set_style_text_font(dd, popup_layout::font28(), LV_PART_MAIN);
  lv_obj_set_style_text_font(dd, &ui_symbols_24, LV_PART_INDICATOR);
}

// The Media volume control uses the same track with a smaller knob.
inline void mediaSlider(lv_obj_t* obj) {
  slider(obj);
  lv_obj_set_height(obj, popup_layout::scale(16));
  lv_obj_set_style_width(obj, popup_layout::scale(36), LV_PART_KNOB);
  lv_obj_set_style_height(obj, popup_layout::scale(36), LV_PART_KNOB);
}

// Match the existing Climate popup's transparent round step buttons.
inline void climateStepButton(lv_obj_t* button) {
  auto apply = [&](lv_style_selector_t state, lv_opa_t opacity) {
    lv_obj_set_style_bg_color(button, lv_color_white(), state);
    lv_obj_set_style_bg_opa(button, opacity, state);
    lv_obj_set_style_border_width(button, 0, state);
    lv_obj_set_style_outline_width(button, 0, state);
    lv_obj_set_style_shadow_width(button, 0, state);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, state);
    lv_obj_set_style_anim_duration(button, 0, state);
    lv_obj_set_style_transform_width(button, 0, state);
    lv_obj_set_style_transform_height(button, 0, state);
  };
  apply(0, LV_OPA_TRANSP);
  apply(LV_STATE_PRESSED, LV_OPA_20);
}

// Reuse Settings geometry and behavior; only the font and neutral colors differ.
inline void valueDropdown(lv_obj_t* obj) {
  largeDropdown(obj);
  lv_obj_set_style_text_font(obj, popup_layout::headerTitleFont(), LV_PART_MAIN);
  // Match Settings' dark surface using RGB565-neutral gray levels.
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x1B1B1B), LV_PART_MAIN);
  // CHECKED means the list is open, not that a finger is still pressing it.
  const lv_style_selector_t feedback_states[] = {
      LV_STATE_PRESSED, LV_STATE_CHECKED, LV_STATE_PRESSED | LV_STATE_CHECKED,
      LV_STATE_DISABLED};
  for (lv_style_selector_t state : feedback_states) {
    const uint32_t color = (state & LV_STATE_PRESSED) ? 0x232323 : 0x1B1B1B;
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | state);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x555555), LV_PART_MAIN | state);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | state);
    lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
    // LVGL 9.5's light base theme recolors disabled controls separately from
    // color filters. Keep the dark surface and dim only its text and arrow.
    lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_INDICATOR | state);
  }
  lv_obj_set_style_text_opa(obj, LV_OPA_50, LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_set_style_text_opa(obj, LV_OPA_50, LV_PART_INDICATOR | LV_STATE_DISABLED);
}

inline void valueDropdownList(lv_obj_t* obj) {
  if (!obj) return;
  largeDropdownList(obj);
  lv_obj_set_style_text_font(obj, popup_layout::headerTitleFont(), LV_PART_MAIN);
  lv_obj_set_style_text_font(obj, popup_layout::headerTitleFont(), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x1B1B1B), LV_PART_MAIN);
  // Native list rendering uses CHECKED for the selected option and PRESSED
  // for the touched option, independently of the dropdown button's state.
  const lv_style_selector_t option_states[] = {
      LV_STATE_CHECKED, LV_STATE_PRESSED, LV_STATE_CHECKED | LV_STATE_PRESSED};
  for (lv_style_selector_t state : option_states) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x26A69A), LV_PART_SELECTED | state);
    lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, LV_PART_SELECTED | state);
    lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_PART_SELECTED | state);
  }
}
}  // namespace ui_control_style
