#pragma once

#include <stdint.h>

#include "src/devices/device.h"
#include "src/devices/device_select.h"

// The tile grid the panel shows (layouts, user 2026-10-07/08). There are
// three layouts, each with its own arrangement of the tiles:
//  - Classic: the profile's own grid, as always. Its places are the tiles'
//    stored positions (PackedTileV7).
//  - Bar: a head like the Settings screen's (circle, title, time, gear or X)
//    on top and fewer, larger cells below it, in landscape.
//  - Portrait: the same with the screen upright.
// The places of the bar layouts live in their own file (tile_layouts.h), by
// stable tile ID. The layout is read once at boot; changing it restarts the
// panel. A layout the panel cannot show yet (a turned screen or a half row
// on a panel without upright_ready()) is not switched to: the panel then
// starts with the classic layout.
//
// Stored tiles keep the profile's grid as their space (GRID_COLS, GRID_ROWS,
// TILES_PER_GRID in tile_config.h). The shown grid has at most as many cells,
// but an upright layout can have more rows (kSpaceRows, its places live in
// the layout file).
namespace grid_layout {

enum class Layout : uint8_t { kClassic = 0, kBar = 1, kPortrait = 2 };
constexpr uint8_t kLayoutCount = 3;

struct Shown {
  uint8_t cols;
  uint8_t rows;  // whole rows
  int cell_w;
  int cell_h;
  int pad_left;
  int pad_right;
  int pad_top;
  int pad_bottom;
  bool head_bar;
  // A further half row below the whole ones (upright 1280 x 800: 4 x 6.5).
  bool half_row;
  // The screen as the layout shows it: upright when taller than wide.
  bool portrait;
  uint16_t screen_w;
  uint16_t screen_h;
  Layout layout;
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
  bool half_row;
};

// The grid below the head bar per screen as the layout shows it (user picks
// 2026-10-07, emulator build/ha-dummy-sim/panel/layouts.mjs BAR_PICKS).
// Landscape: 1280 x 800 (Guition V2, Waveshare 8" and 10.1") 6 x 4;
// 1280 x 720 (Tab5, Waveshare 7") and 1024 x 600 (Guition 7", Waveshare 7B)
// 5 x 3; 800 x 480 (Waveshare 4.3", the JC4880 turned) 4 x 3; square panels
// 3 x 3. Upright: 800 x 1280 4 x 6.5; 720 x 1280 and 600 x 1024 3 x 6;
// 480 x 800 (JC4880) 3 x 5.5.
constexpr Size bar_grid(uint16_t width, uint16_t height) {
  if (width == height) return {3, 3, false};
  if (width == 1280 && height == 800) return {6, 4, false};
  if (width == 800 && height == 1280) return {4, 6, true};
  if ((width == 1280 && height == 720) || (width == 1024 && height == 600)) return {5, 3, false};
  if ((width == 720 && height == 1280) || (width == 600 && height == 1024)) return {3, 6, false};
  if (width == 800 && height == 480) return {4, 3, false};
  if (width == 480 && height == 800) return {3, 5, true};
  // Any other screen: the profile's grid less a column and a row, at least
  // 1 x 1, so the cells grow like on the known panels; turned when the
  // screen is turned against the profile.
  const uint8_t cols = static_cast<uint8_t>(Device::kGridCols > 1 ? Device::kGridCols - 1 : 1);
  const uint8_t rows = static_cast<uint8_t>(Device::kGridRows > 1 ? Device::kGridRows - 1 : 1);
  const bool turned = (width > height) != (Device::kScreenWidth > Device::kScreenHeight);
  return turned ? Size{rows, cols, false} : Size{cols, rows, false};
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
          false,
          false,
          Device::kScreenHeight > Device::kScreenWidth,
          Device::kScreenWidth,
          Device::kScreenHeight,
          Layout::kClassic};
}

// The screen is upright by itself (JC4880); square panels have no upright
// layout of their own.
constexpr bool native_portrait() { return Device::kScreenHeight > Device::kScreenWidth; }
constexpr bool square() { return Device::kScreenHeight == Device::kScreenWidth; }

// A head bar grid: one grid gap inside the card margin on every side, the
// head (the X's box) and a gap above the tiles, the cells as large as the
// rest allows (a half row counts half a cell and half a gap); the remaining
// pixels split like the profile's margins.
constexpr Shown bar_grid_layout(uint16_t width, uint16_t height, Layout layout) {
  const Size size = bar_grid(width, height);
  const int gap = Device::kGridGap;
  const int pad = kHeadMargin + gap;
  const int top = kHeadMargin + gap + kHeadClose + gap;
  const int rows2 = size.rows * 2 + (size.half_row ? 1 : 0);
  const int cell_w = (static_cast<int>(width) - 2 * pad - (size.cols - 1) * gap) / size.cols;
  const int cell_h = (2 * (static_cast<int>(height) - top - pad) - (rows2 - 2) * gap) / rows2;
  const int grid_h = (rows2 * cell_h + (rows2 - 2) * gap) / 2;
  const int extra_x = static_cast<int>(width) - (size.cols * cell_w + (size.cols - 1) * gap + 2 * pad);
  const int extra_y = static_cast<int>(height) - (grid_h + top + pad);
  return {size.cols,
          size.rows,
          cell_w,
          cell_h,
          pad + extra_x / 2,
          pad + extra_x - extra_x / 2,
          top + extra_y / 2,
          pad + extra_y - extra_y / 2,
          true,
          size.half_row,
          height > width,
          width,
          height,
          layout};
}

// A layout's grid. The bar layouts show the screen turned when their
// orientation is not the panel's own.
constexpr Shown layout_grid(Layout layout) {
  const uint16_t wide = Device::kScreenWidth > Device::kScreenHeight ? Device::kScreenWidth : Device::kScreenHeight;
  const uint16_t narrow = Device::kScreenWidth > Device::kScreenHeight ? Device::kScreenHeight : Device::kScreenWidth;
  return layout == Layout::kBar        ? bar_grid_layout(wide, narrow, Layout::kBar)
         : layout == Layout::kPortrait ? bar_grid_layout(narrow, wide, Layout::kPortrait)
                                       : profile_grid();
}

// Square panels have no upright layout.
constexpr bool available(Layout layout) { return layout != Layout::kPortrait || !square(); }
// The screen would have to be turned (the layouts' second step, P4 only).
constexpr bool needs_rotation(Layout layout) {
  return layout != Layout::kClassic && layout_grid(layout).portrait != native_portrait();
}
// The panel shows a turned layout: its driver draws and reads the touch
// upright without the quarter turn (Device::displaySetUpright), and the tile
// grid takes the half row. Hochkant step 2, one panel after the other (user
// 2026-10-08: the Guition V2 first).
constexpr bool upright_ready() {
#if defined(DEVICE_GUITION_JC8012P4A1_V2)
  return true;
#else
  return false;
#endif
}
// The panel can start with it.
constexpr bool switchable(Layout layout) {
  return available(layout) && ((!needs_rotation(layout) && !layout_grid(layout).half_row) || upright_ready());
}

// The room every layout's grid fits in (a half row counts whole): the tile
// grids' arrays and the editor's bounds. An upright layout can have more rows
// than the profile (1280 x 800: 7 x 5, upright 4 x 6.5).
constexpr uint8_t whole_rows(const Shown& grid) { return static_cast<uint8_t>(grid.rows + (grid.half_row ? 1 : 0)); }
constexpr uint8_t larger(uint8_t a, uint8_t b) { return a > b ? a : b; }
constexpr uint8_t kSpaceCols = larger(larger(Device::kGridCols, layout_grid(Layout::kBar).cols),
                                      available(Layout::kPortrait) ? layout_grid(Layout::kPortrait).cols : 0);
constexpr uint8_t kSpaceRows = larger(larger(Device::kGridRows, whole_rows(layout_grid(Layout::kBar))),
                                      available(Layout::kPortrait) ? whole_rows(layout_grid(Layout::kPortrait)) : 0);
constexpr Layout from_index(uint8_t value) {
  return value == 1 ? Layout::kBar : value == 2 ? Layout::kPortrait : Layout::kClassic;
}
// The stable names in the layout file and the Web Admin.
constexpr const char* key(Layout layout) {
  return layout == Layout::kBar ? "bar" : layout == Layout::kPortrait ? "portrait" : "classic";
}

// The shown grid of this boot (apply() before the UI is built).
inline Shown g_shown = profile_grid();

inline const Shown& shown() { return g_shown; }
inline bool head_bar() { return g_shown.head_bar; }
inline Layout active() { return g_shown.layout; }
// The screen as this boot shows it: turned with an upright layout on a
// landscape panel (SCREEN_WIDTH/SCREEN_HEIGHT stay the profile's).
inline int screen_w() { return g_shown.screen_w; }
inline int screen_h() { return g_shown.screen_h; }
inline bool turned() { return g_shown.portrait != native_portrait(); }
// The shown grid's rows with its half row.
inline float shown_rows() { return g_shown.rows + (g_shown.half_row ? 0.5f : 0.0f); }
// The stored layout, or the classic one while it cannot be switched to.
inline Layout apply(uint8_t stored) {
  const Layout layout = switchable(from_index(stored)) ? from_index(stored) : Layout::kClassic;
  g_shown = layout_grid(layout);
  return layout;
}

// True when a place lies wholly inside a grid.
inline bool inside(const Shown& grid, float col, float row, float span_w, float span_h) {
  return col >= 0 && row >= 0 && col + span_w <= grid.cols + 0.001f &&
         row + span_h <= grid.rows + (grid.half_row ? 0.5f : 0.0f) + 0.001f;
}
inline bool inside(float col, float row, float span_w, float span_h) {
  return inside(g_shown, col, row, span_w, span_h);
}

}  // namespace grid_layout
