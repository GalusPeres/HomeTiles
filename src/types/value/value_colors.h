#pragma once

#include <lvgl.h>
#include <initializer_list>
#include "src/ui/shared/ui_theme.h"

// Only the Number, Select and Date/Time popup editors use this palette.
namespace editable_colors {
inline lv_color_t blend(lv_color_t base, lv_color_t tint, unsigned amount) {
  auto channel = [amount](unsigned a, unsigned b) {
    return static_cast<uint8_t>((a * (255 - amount) + b * amount + 127) / 255);
  };
  return lv_color_make(channel(base.red, tint.red), channel(base.green, tint.green),
                       channel(base.blue, tint.blue));
}

// The editors are popup controls (popup_nav_style.h): the number and clock
// pills, the date fields, the Select field and its selected option draw the
// control fill (`fill` at `opa`, the circle's color, tone_color::fill) over
// the card, exactly like a selected 7D/24H; a press shows that same color.
// The open option list is the card with the popup hairline. White text.
struct Palette {
  lv_color_t base, fill, surface, list;
  lv_opa_t opa;
};

inline Palette from(lv_color_t base, lv_color_t fill, lv_opa_t opa) {
  Palette p{};
  p.base = base;
  p.fill = fill;
  p.opa = opa;
  // What the fill shows over the card.
  p.surface = blend(base, fill, opa);
  p.list = base;
  return p;
}

// The popup card hairline (popup_layout::kPopupBorderOpa).
inline constexpr lv_opa_t kHairlineOpa = 51;

inline void surface(lv_obj_t* obj, const Palette& p) {
  if (!obj) return;
  for (lv_style_selector_t state : {LV_STATE_DEFAULT, LV_STATE_DISABLED}) {
    lv_obj_set_style_bg_color(obj, p.fill, LV_PART_MAIN | state);
    lv_obj_set_style_bg_opa(obj, p.opa, LV_PART_MAIN | state);
    lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
    lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
  }
}

// The Select field: the control fill in every state, no outline and no theme
// darkening (the default theme's black recolor on press).
inline void dropdown(lv_obj_t* obj, const Palette& p) {
  for (lv_style_selector_t state : std::initializer_list<lv_style_selector_t>{LV_STATE_DEFAULT, LV_STATE_PRESSED,
       LV_STATE_CHECKED, LV_STATE_PRESSED | LV_STATE_CHECKED, LV_STATE_DISABLED}) {
    lv_obj_set_style_bg_color(obj, p.fill, LV_PART_MAIN | state);
    lv_obj_set_style_bg_opa(obj, p.opa, LV_PART_MAIN | state);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | state);
    if (state & LV_STATE_PRESSED) lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
  }
}

// The open list: the card with the popup hairline drawn above the options.
// LVGL's own selection box spans the full list width, so it stays
// transparent; value_control.cpp draws the selected option inset.
inline void dropdownList(lv_obj_t* obj, const Palette& p) {
  if (!obj) return;
  lv_obj_set_style_bg_color(obj, p.list, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(ui_theme::text()), LV_PART_MAIN);
  lv_obj_set_style_border_opa(obj, kHairlineOpa, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
  lv_obj_set_style_border_post(obj, true, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, p.surface, LV_PART_SCROLLBAR);
  for (lv_style_selector_t state : std::initializer_list<lv_style_selector_t>{LV_STATE_DEFAULT, LV_STATE_CHECKED,
       LV_STATE_PRESSED, LV_STATE_CHECKED | LV_STATE_PRESSED}) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_SELECTED | state);
    lv_obj_set_style_text_color(obj, lv_color_hex(ui_theme::text()), LV_PART_SELECTED | state);
  }
}
}  // namespace editable_colors
