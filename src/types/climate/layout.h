#pragma once

#include "src/devices/device_select.h"

namespace climate_layout {

// One shared mini-grid rectangle for every climate tile size.
#if defined(DEVICE_LAYOUT_480X480)
inline constexpr int kCardPaddingHorizontal = 13;
inline constexpr int kCardPaddingVertical = 16;
inline constexpr int kOuterInset = 4;
inline constexpr int kGap = 7;
// The 15 px outer card radius minus the 4 px inset keeps both arcs concentric.
inline constexpr int kControlRadius = 11;
inline constexpr int kContentTop = 46;
#elif defined(DEVICE_LAYOUT_1024X600)
inline constexpr int kCardPaddingHorizontal = 20;
inline constexpr int kCardPaddingVertical = 24;
inline constexpr int kOuterInset = 5;
inline constexpr int kGap = 8;
// Outer card radius 22 minus the 5 px inset keeps both corner arcs concentric.
inline constexpr int kControlRadius = 17;
inline constexpr int kContentTop = 69;
#else
inline constexpr int kCardPaddingHorizontal = 20;
inline constexpr int kCardPaddingVertical = 24;
inline constexpr int kOuterInset = 6;
inline constexpr int kGap = 10;
inline constexpr int kControlRadius = 16;
inline constexpr int kContentTop = 69;
#endif

// Top of the mini-grid in the card: one Climate gap below the corner header
// disc (tile_icon_disc::corner_header), never above kContentTop, the same
// rule as the Switch tile's bar (user 2026-10-01: with the disc's inset as
// the gap the target pill sat 4 px under the disc on the V2). Taller cells
// (Tab5, 4B, S3) have a larger disc. Device and Web preview
// (--climate-slots-top, --switch-bar-height) use the same value.
inline constexpr int content_top(int header_disc, int disc_inset) {
  return disc_inset + header_disc + kGap > kContentTop
             ? disc_inset + header_disc + kGap
             : kContentTop;
}

}  // namespace climate_layout
