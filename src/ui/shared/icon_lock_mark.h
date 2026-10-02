#pragma once

#include <lvgl.h>

#include <cstdlib>

#include "src/tiles/icons/mdi_bar_icons.h"

// A lock at the bottom right of an MDI icon label: PIN-protected Folder and
// Settings tiles and their PIN popup (user 2026-10-02, mockup
// build/design-mockups/pin-lock-badge/pin-lock-icon.html). It looks like
// mdi:folder-lock, but works for any icon because a Folder can take every
// icon: drawn after the icon with an even rim in the color behind it (the icon
// disc over the card, or the card), so it reads as cut into the icon. It draws
// only when the icon redraws and adds no objects.
namespace icon_lock_mark {

// mdi:lock (0xF033E) in UTF-8; every mdi_bar_icons font holds it.
inline constexpr uint32_t kCodepoint = 0xF033E;
inline constexpr char kGlyph[] = "\xF3\xB0\x8C\xBE";

// About 46 % of the icon, like mdi:folder-lock (user 2026-10-02, variant A
// of build/design-mockups/pin-lock-badge/pin-lock-v2.html): 15 for the 32 px
// icons, 18 for 40, 22 for 48.
inline const lv_font_t* font_for(int32_t icon_line) {
  const lv_font_t* fonts[] = {&mdi_bar_icons_15, &mdi_bar_icons_18, &mdi_bar_icons_22, &mdi_bar_icons_26,
                              &mdi_bar_icons_34};
  const int32_t want = icon_line * 46 / 100;
  const lv_font_t* best = fonts[0];
  for (const lv_font_t* font : fonts) {
    if (std::abs(lv_font_get_line_height(font) - want) < std::abs(lv_font_get_line_height(best) - want)) best = font;
  }
  return best;
}

// The rim around the lock, about 6 % of the icon: 3 px on 48 px icons, 2 px
// on 40 and 32 (user 2026-10-02: 9 % looked too thick).
inline int32_t rim_for(int32_t icon_line) { return LV_MAX(2, (icon_line * 6 + 50) / 100); }

inline int32_t icon_line(lv_obj_t* icon) {
  return lv_font_get_line_height(lv_obj_get_style_text_font(icon, LV_PART_MAIN));
}

// The icon disc over `under`, or `under` alone without a visible disc.
inline lv_color_t behind(lv_obj_t* disc, lv_color_t under) {
  if (!disc || lv_obj_has_flag(disc, LV_OBJ_FLAG_HIDDEN)) return under;
  const lv_opa_t opa = lv_obj_get_style_bg_opa(disc, LV_PART_MAIN);
  return opa > LV_OPA_MIN ? lv_color_mix(lv_obj_get_style_bg_color(disc, LV_PART_MAIN), under, opa) : under;
}

// For LV_EVENT_REFR_EXT_DRAW_SIZE: the rim may reach past the icon's box.
inline void ext_draw_size(lv_event_t* event, lv_obj_t* icon) {
  lv_event_set_ext_draw_size(event, rim_for(icon_line(icon)) + 1);
}

// For LV_EVENT_DRAW_POST of `icon`: the lock in the icon's color on a rim in
// `rim_color`, the lock's right and bottom edge at 0.9 of the icon's box like
// mdi:folder-lock, so its rim leaves the top corner of the icon whole and stays
// inside a round disc (user 2026-10-02: the rim cut the folder corner and
// bulged out of the circle). The lock glyph spans x 0.167-0.833 and y
// 0.042-0.958 of its em box.
inline void draw(lv_layer_t* layer, lv_obj_t* icon, lv_color_t rim_color) {
  if (!layer || !icon) return;
  const int32_t line = icon_line(icon);
  const lv_font_t* font = font_for(line);
  const int32_t lock_line = lv_font_get_line_height(font);
  const int32_t lock_width = static_cast<int32_t>(lv_font_get_glyph_width(font, kCodepoint, 0));
  const int32_t rim = rim_for(line);
  // The glyph's own box: a wider label (the popup header icon) centers it.
  // MDI fonts give every glyph the advance of 0xF0001.
  lv_area_t box;
  lv_obj_get_content_coords(icon, &box);
  lv_point_t at = {0, 0};
  lv_label_get_letter_pos(icon, 0, &at);
  const int32_t glyph_width = static_cast<int32_t>(
      lv_font_get_glyph_width(lv_obj_get_style_text_font(icon, LV_PART_MAIN), 0xF0001, 0));
  const int32_t x = box.x1 + at.x + glyph_width * 90 / 100 - lock_width * 833 / 1000;
  const int32_t y = box.y1 + at.y + line * 90 / 100 - lock_line * 958 / 1000;
  const lv_area_t area = {x, y, x + lock_width - 1, y + lock_line - 1};

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.font = font;
  dsc.text = kGlyph;
  dsc.opa = lv_obj_get_style_text_opa(icon, LV_PART_MAIN);
  dsc.color = rim_color;
  // The rim: the lock shifted around a full circle and half way in, so no
  // gaps open at its corners. Offsets round to the nearest pixel: truncating
  // them toward zero made the rim a diamond with flat, stepped corners
  // instead of a round outline (user 2026-10-02: "kantig").
  static constexpr int8_t kRing[16][2] = {{100, 0},   {92, 38},   {71, 71},   {38, 92},   {0, 100},  {-38, 92},
                                          {-71, 71},  {-92, 38},  {-100, 0},  {-92, -38}, {-71, -71}, {-38, -92},
                                          {0, -100},  {38, -92},  {71, -71},  {92, -38}};
  const int32_t reaches[2] = {rim, rim / 2};
  for (const int32_t reach : reaches) {
    for (size_t i = 0; i < 16; i += reach == rim ? 1 : 2) {
      const auto round_div = [](int32_t value) { return (value >= 0 ? value + 50 : value - 50) / 100; };
      lv_area_t shifted = area;
      lv_area_move(&shifted, round_div(kRing[i][0] * reach), round_div(kRing[i][1] * reach));
      lv_draw_label(layer, &dsc, &shifted);
    }
  }
  dsc.color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);
  lv_draw_label(layer, &dsc, &area);
}

}  // namespace icon_lock_mark
