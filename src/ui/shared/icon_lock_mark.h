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

// About 56 % of the icon: 18 for the 32 px icons, 22 for 40, 26 for 48.
inline const lv_font_t* font_for(int32_t icon_line) {
  const lv_font_t* fonts[] = {&mdi_bar_icons_18, &mdi_bar_icons_22, &mdi_bar_icons_26, &mdi_bar_icons_34};
  const int32_t want = icon_line * 56 / 100;
  const lv_font_t* best = fonts[0];
  for (const lv_font_t* font : fonts) {
    if (std::abs(lv_font_get_line_height(font) - want) < std::abs(lv_font_get_line_height(best) - want)) best = font;
  }
  return best;
}

// The rim around the lock, about 9 % of the icon (more than the first mockup,
// user 2026-10-02).
inline int32_t rim_for(int32_t icon_line) { return LV_MAX(2, (icon_line * 9 + 50) / 100); }

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
// `rim_color`, its right edge on the icon's box like mdi:folder-lock (the lock
// glyph spans x 0.17-0.83 and y 0.04-0.96 of its em box).
inline void draw(lv_layer_t* layer, lv_obj_t* icon, lv_color_t rim_color) {
  if (!layer || !icon) return;
  const int32_t line = icon_line(icon);
  const lv_font_t* font = font_for(line);
  const int32_t lock_line = lv_font_get_line_height(font);
  const int32_t lock_width = static_cast<int32_t>(lv_font_get_glyph_width(font, kCodepoint, 0));
  lv_area_t box;
  lv_obj_get_content_coords(icon, &box);
  const int32_t x = box.x2 + 1 - lock_width * 83 / 100;
  const int32_t y = box.y1 + line * 95 / 100 - lock_line * 96 / 100;
  const lv_area_t area = {x, y, x + lock_width - 1, y + lock_line - 1};

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.font = font;
  dsc.text = kGlyph;
  dsc.opa = lv_obj_get_style_text_opa(icon, LV_PART_MAIN);
  dsc.color = rim_color;
  // The rim: the lock shifted around a full circle and half way in, so no
  // gaps open at its corners.
  static constexpr int8_t kRing[16][2] = {{100, 0},   {92, 38},   {71, 71},   {38, 92},   {0, 100},  {-38, 92},
                                          {-71, 71},  {-92, 38},  {-100, 0},  {-92, -38}, {-71, -71}, {-38, -92},
                                          {0, -100},  {38, -92},  {71, -71},  {92, -38}};
  const int32_t rim = rim_for(line);
  const int32_t reaches[2] = {rim, rim / 2};
  for (const int32_t reach : reaches) {
    for (size_t i = 0; i < 16; i += reach == rim ? 1 : 2) {
      lv_area_t shifted = area;
      lv_area_move(&shifted, kRing[i][0] * reach / 100, kRing[i][1] * reach / 100);
      lv_draw_label(layer, &dsc, &shifted);
    }
  }
  dsc.color = lv_obj_get_style_text_color(icon, LV_PART_MAIN);
  lv_draw_label(layer, &dsc, &area);
}

}  // namespace icon_lock_mark
