#pragma once

#include <lvgl.h>
#include <math.h>
#include <initializer_list>

// Only the Number, Select and Date/Time popup editors use this palette.
namespace editable_colors {
inline lv_color_t blend(lv_color_t base, lv_color_t tint, unsigned amount) {
  auto channel = [amount](unsigned a, unsigned b) {
    return static_cast<uint8_t>((a * (255 - amount) + b * amount + 127) / 255);
  };
  return lv_color_make(channel(base.red, tint.red), channel(base.green, tint.green),
                       channel(base.blue, tint.blue));
}

inline float luminance(lv_color_t color) {
  auto linear = [](uint8_t channel) {
    const float value = channel / 255.0f;
    return value <= 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * linear(color.red) + 0.7152f * linear(color.green) +
         0.0722f * linear(color.blue);
}

inline lv_color_t readable_surface(lv_color_t color) {
  // Preserve the existing white control text even for very light tile colors.
  // Only the surface darkens; its hue and the typography remain unchanged.
  for (unsigned i = 0; i < 32 && luminance(color) > 0.175f; ++i)
    color = blend(color, lv_color_black(), 12);
  return color;
}

struct Palette {
  lv_color_t base, surface, pressed, raised, field, border;
};

inline Palette from(lv_color_t base) {
  Palette p{};
  p.base = base;
  p.surface = readable_surface(blend(base, lv_color_black(), 91));
  p.pressed = readable_surface(blend(p.surface, lv_color_white(), 9));
  p.raised = readable_surface(blend(base, lv_color_white(), 19));
  p.field = readable_surface(blend(base, lv_color_white(), 11));
  p.border = blend(p.surface, lv_color_white(), 65);
  return p;
}

inline void surface(lv_obj_t* obj, lv_color_t background) {
  if (!obj) return;
  for (lv_style_selector_t state : {LV_STATE_DEFAULT, LV_STATE_DISABLED}) {
    lv_obj_set_style_bg_color(obj, background, LV_PART_MAIN | state);
    lv_obj_set_style_color_filter_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
    lv_obj_set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | state);
  }
}

inline void dropdown(lv_obj_t* obj, const Palette& p) {
  for (lv_style_selector_t state : std::initializer_list<lv_style_selector_t>{LV_STATE_DEFAULT, LV_STATE_PRESSED,
       LV_STATE_CHECKED, LV_STATE_PRESSED | LV_STATE_CHECKED, LV_STATE_DISABLED}) {
    lv_obj_set_style_bg_color(obj, state & LV_STATE_PRESSED ? p.pressed : p.surface,
                              LV_PART_MAIN | state);
    lv_obj_set_style_border_color(obj, p.border, LV_PART_MAIN | state);
  }
}

inline void dropdownList(lv_obj_t* obj, const Palette& p) {
  surface(obj, p.surface);
  lv_obj_set_style_border_color(obj, p.border, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, p.border, LV_PART_SCROLLBAR);
  for (lv_style_selector_t state : std::initializer_list<lv_style_selector_t>{LV_STATE_DEFAULT, LV_STATE_CHECKED,
       LV_STATE_PRESSED, LV_STATE_CHECKED | LV_STATE_PRESSED}) {
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_SELECTED | state);
    lv_obj_set_style_text_color(obj, p.surface, LV_PART_SELECTED | state);
  }
}
}  // namespace editable_colors
