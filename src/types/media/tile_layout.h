#pragma once

#include <lvgl.h>

#include "src/devices/device_select.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"

// Geometry and fonts of the media tile: renderer.cpp builds it,
// content_layout.cpp places its texts, and the Web Admin preview draws it
// from the same values (web_scripts.cpp).
namespace media_tile {

#if defined(DEVICE_WAVESHARE_4B)
constexpr lv_coord_t kControlButtonSize = 76;
constexpr lv_coord_t kControlSideOffset = 96;
constexpr lv_coord_t kControlBottomOffset = -8;
constexpr lv_coord_t kContentYOffset = 8;
#elif defined(DEVICE_LAYOUT_480X480)
constexpr lv_coord_t kControlButtonSize = 51;
constexpr lv_coord_t kControlSideOffset = 64;
constexpr lv_coord_t kControlBottomOffset = -5;
constexpr lv_coord_t kContentYOffset = 5;
#else
constexpr lv_coord_t kControlButtonSize = tile_layout::scale(56);
constexpr lv_coord_t kControlSideOffset = tile_layout::scale(76);
constexpr lv_coord_t kControlBottomOffset = 0;
constexpr lv_coord_t kContentYOffset = 0;
#endif

// The artwork's top edge in the content area; taller cards leave room for
// the header.
inline lv_coord_t cover_top(bool large) {
  return tile_layout::scale(large ? 58 : 42) + kContentYOffset;
}
constexpr lv_coord_t kCoverLeft = tile_layout::scale(-2);
constexpr lv_coord_t kCoverRadius = tile_layout::scale(12);
// Texts: left edge without artwork, gap after the artwork, right margin, the
// gap between title and subtitle, and the gap above the controls.
constexpr lv_coord_t kTextLeft = tile_layout::scale(20);
constexpr lv_coord_t kTextAfterCover = tile_layout::scale(16);
constexpr lv_coord_t kTextRight = tile_layout::scale(8);
constexpr lv_coord_t kSubtitleGap = tile_layout::scale(6);
constexpr lv_coord_t kFooterGap = tile_layout::scale(12);

inline const lv_font_t* title_font(bool large) {
#if defined(DEVICE_WAVESHARE_4B)
  return large ? &ui_font_28 : &ui_font_24;
#elif defined(DEVICE_LAYOUT_480X480)
  return large ? &ui_font_20 : &ui_font_16;
#else
  return large ? tile_layout::content_font_24() : tile_layout::content_font_20();
#endif
}

inline const lv_font_t* subtitle_font() {
#if defined(DEVICE_WAVESHARE_4B)
  return &ui_font_20;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_14;
#else
  return FONT_SMALL;
#endif
}

}  // namespace media_tile
