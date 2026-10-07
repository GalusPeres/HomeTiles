#pragma once

#include <stdint.h>

#include "src/devices/device.h"
#include "src/devices/device_select.h"

// The tile grid the panel shows (layouts, user 2026-10-07). Without the head
// bar it is the profile's own grid. With the head bar the top of the screen
// carries a head like the Settings screen's (circle, title, time, gear or X)
// and the tiles get fewer, larger cells below it. The layout is read once at
// boot; changing it restarts the panel.
//
// Stored tiles keep the profile's grid as their space (GRID_COLS, GRID_ROWS,
// TILES_PER_GRID in tile_config.h): positions are never rewritten when the
// bar goes on or off. A tile outside the shown grid is not drawn; the Web
// Admin marks it so the user can move it (nothing is ever deleted).
namespace grid_layout {

struct Shown {
  uint8_t cols;
  uint8_t rows;
  int cell_w;
  int cell_h;
  int pad_left;
  int pad_right;
  int pad_top;
  int pad_bottom;
  bool head_bar;
};

// The head's frame like the Settings head and the popups: the card margin
// and the X's box (popup_layout::kCardMargin and kCloseButtonSize; home_bar.cpp
// checks they agree).
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kHeadMargin = 3;
constexpr int kHeadClose = 64;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kHeadMargin = 4;
constexpr int kHeadClose = 72;
#else
constexpr int kHeadMargin = 4;
constexpr int kHeadClose = 96;
#endif

struct Size {
  uint8_t cols;
  uint8_t rows;
};

// The grid below the head bar per screen (user picks 2026-10-07, emulator
// build/ha-dummy-sim/panel/layouts.mjs BAR_PICKS): 1280 x 800 (Guition V2,
// Waveshare 8" and 10.1") 6 x 4; 1280 x 720 (Tab5, Waveshare 7") and
// 1024 x 600 (Guition 7", Waveshare 7B) 5 x 3; 800 x 480 (Waveshare 4.3")
// 4 x 3; square panels 3 x 3. The upright 480 x 800 panel (JC4880) takes
// 3 x 5 until half rows are part of the shown grid (the emulator's 3 x 5.5).
constexpr Size bar_grid(uint16_t width, uint16_t height) {
  if (width == height) return {3, 3};
  if (width == 1280 && height == 800) return {6, 4};
  if ((width == 1280 && height == 720) || (width == 1024 && height == 600)) return {5, 3};
  if (width == 800 && height == 480) return {4, 3};
  if (width == 480 && height == 800) return {3, 5};
  // Any other screen: the profile's grid less a column and a row, at least
  // 1 x 1, so the cells grow like on the known panels.
  return {static_cast<uint8_t>(Device::kGridCols > 1 ? Device::kGridCols - 1 : 1),
          static_cast<uint8_t>(Device::kGridRows > 1 ? Device::kGridRows - 1 : 1)};
}

// The profile's grid: fixed tracks, the rest of the screen split between
// both outer margins, the top and left ones taking the smaller half (the
// former tile_config.h GRID_PAD_* constants).
constexpr Shown profile_grid() {
  const int extra_x = static_cast<int>(Device::kScreenWidth) -
                      (Device::kGridCols * Device::kGridCellW + (Device::kGridCols - 1) * Device::kGridGap +
                       2 * Device::kGridPad);
  const int extra_y = static_cast<int>(Device::kScreenHeight) -
                      (Device::kGridRows * Device::kGridCellH + (Device::kGridRows - 1) * Device::kGridGap +
                       2 * Device::kGridPad);
  return {Device::kGridCols,
          Device::kGridRows,
          Device::kGridCellW,
          Device::kGridCellH,
          Device::kGridPad + extra_x / 2,
          Device::kGridPad + extra_x - extra_x / 2,
          Device::kGridPad + extra_y / 2,
          Device::kGridPad + extra_y - extra_y / 2,
          false};
}

// The head bar's grid: one grid gap inside the card margin on every side,
// the head (the X's box) and a gap above the tiles, the cells as large as
// the rest allows; the remaining pixels split like the profile's margins.
constexpr Shown bar_grid_layout() {
  const Size size = bar_grid(Device::kScreenWidth, Device::kScreenHeight);
  const int gap = Device::kGridGap;
  const int pad = kHeadMargin + gap;
  const int top = kHeadMargin + gap + kHeadClose + gap;
  const int cell_w = (static_cast<int>(Device::kScreenWidth) - 2 * pad - (size.cols - 1) * gap) / size.cols;
  const int cell_h = (static_cast<int>(Device::kScreenHeight) - top - pad - (size.rows - 1) * gap) / size.rows;
  const int extra_x = static_cast<int>(Device::kScreenWidth) - (size.cols * cell_w + (size.cols - 1) * gap + 2 * pad);
  const int extra_y =
      static_cast<int>(Device::kScreenHeight) - (size.rows * cell_h + (size.rows - 1) * gap + top + pad);
  return {size.cols,
          size.rows,
          cell_w,
          cell_h,
          pad + extra_x / 2,
          pad + extra_x - extra_x / 2,
          top + extra_y / 2,
          pad + extra_y - extra_y / 2,
          true};
}

// The shown grid of this boot (apply() before the UI is built).
inline Shown g_shown = profile_grid();

inline const Shown& shown() { return g_shown; }
inline bool head_bar() { return g_shown.head_bar; }
inline void apply(bool with_head_bar) { g_shown = with_head_bar ? bar_grid_layout() : profile_grid(); }

// True when a stored tile lies wholly inside the shown grid.
inline bool inside(float col, float row, float span_w, float span_h) {
  return col >= 0 && row >= 0 && col + span_w <= g_shown.cols + 0.001f && row + span_h <= g_shown.rows + 0.001f;
}

}  // namespace grid_layout
