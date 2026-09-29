#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "src/tiles/config/tile_tint.h"

// popup_shell.h: whether the opening tile has "Circle in icon color".
bool popup_shell_tinted_controls();

// Footer controls of the history popups (7D/24H/Today and the date and day
// pills). Without the opening tile's "Circle in icon color" they are glass:
// the popup color with white mixed in, like the neutral icon disc. With it
// they keep the popup hue and only get lighter, like the tinted disc. A
// selected control is brighter than an info pill; text stays white. Solid
// fills, so drawing costs nothing extra.
namespace popup_nav_style {

enum class Fill : uint8_t { Info, Selected };

// The popup color with white mixed in (glass).
inline uint32_t glass(uint32_t base, Fill fill) {
  return tile_tint::mix(base & 0xFFFFFF, 0xFFFFFF, fill == Fill::Selected ? 30 : 14);
}

// The popup hue, lighter: HSL lightness +9 (info) or +24 (selected) points.
inline uint32_t hue_lighter(uint32_t base, Fill fill) {
  const float r = static_cast<float>((base >> 16) & 0xFF) / 255.0f;
  const float g = static_cast<float>((base >> 8) & 0xFF) / 255.0f;
  const float b = static_cast<float>(base & 0xFF) / 255.0f;
  const float high = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const float low = r < g ? (r < b ? r : b) : (g < b ? g : b);
  float h = 0.0f, s = 0.0f, l = (high + low) / 2.0f;
  if (high > low) {
    const float d = high - low;
    s = l > 0.5f ? d / (2.0f - high - low) : d / (high + low);
    if (high == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (high == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h /= 6.0f;
  }
  l += fill == Fill::Selected ? 0.24f : 0.09f;
  if (l > 0.9f) l = 0.9f;
  s *= 1.05f;
  if (s > 1.0f) s = 1.0f;
  auto channel = [](float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
  };
  float out_r = l, out_g = l, out_b = l;
  if (s > 0.0f) {
    const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    const float p = 2.0f * l - q;
    out_r = channel(p, q, h + 1.0f / 3.0f);
    out_g = channel(p, q, h);
    out_b = channel(p, q, h - 1.0f / 3.0f);
  }
  auto byte = [](float v) { return static_cast<uint32_t>(v * 255.0f + 0.5f) & 0xFF; };
  return (byte(out_r) << 16) | (byte(out_g) << 8) | byte(out_b);
}

// White text keeps a contrast of at least 3:1 on light popup colors.
inline uint32_t readable(uint32_t rgb) {
  for (int i = 0; i < 40 && tile_tint::white_contrast(rgb) < 3.0; ++i) rgb = tile_tint::scale(rgb, 95);
  return rgb;
}

inline uint32_t fill_rgb(uint32_t popup_rgb, bool tinted, Fill fill) {
  return readable(tinted ? hue_lighter(popup_rgb, fill) : glass(popup_rgb, fill));
}

inline lv_color_t fill(lv_color_t popup, Fill kind) {
  return lv_color_hex(fill_rgb(lv_color_to_u32(popup) & 0xFFFFFF, popup_shell_tinted_controls(), kind));
}

// A toggle (7D, 24H, Today): the selected one is a filled pill, the others
// only their white label; pressing shows the info fill.
inline void style_toggle(lv_obj_t* btn, lv_obj_t* label, lv_color_t popup, bool selected) {
  if (!btn) return;
  const lv_color_t info = fill(popup, Fill::Info);
  const lv_color_t chosen = fill(popup, Fill::Selected);
  const lv_style_selector_t selectors[] = {0, LV_STATE_PRESSED};
  for (const lv_style_selector_t selector : selectors) {
    const bool pressed = selector == LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(btn, selected ? chosen : info, selector);
    lv_obj_set_style_bg_opa(btn, selected || pressed ? LV_OPA_COVER : LV_OPA_TRANSP, selector);
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
    lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_PRESSED);
  }
}

// An info pill (date range, day title): the info fill with white text.
inline void style_pill(lv_obj_t* pill, lv_obj_t* label, lv_color_t popup) {
  if (pill) {
    lv_obj_set_style_bg_color(pill, fill(popup, Fill::Info), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  }
  if (label) lv_obj_set_style_text_color(label, lv_color_white(), 0);
}

}  // namespace popup_nav_style
