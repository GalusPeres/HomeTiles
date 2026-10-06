#pragma once

#include <lvgl.h>

#include "src/core/config/tile_radius.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_surface_style.h"

// Sizes, fonts and colors of the Settings screen and the first-start setup.
// The reference is the approved mockup
// (build/design-mockups/settings/settings-menu.html, PROFILES.vars): its
// 1280x800 values, x 5/6 on the 1024x600 class (popup_layout::scale), and
// the mockup's own values on the 480 class. Positions come from the tile
// grid (tile_geometry), so the screen lines up with the Home tiles.
namespace settings_style {

constexpr int pick(int big, int small) {
#if defined(DEVICE_LAYOUT_480X480)
  (void)big;
  return small;
#else
  (void)small;
  return popup_layout::scale(big);
#endif
}

// Section heading above a group.
constexpr int kSectionTop = pick(22, 14);
constexpr int kSectionBottom = pick(10, 7);
constexpr int kSectionLeft = pick(24, 16);
// Group of rows and its rows.
constexpr int kGroupRadius = pick(28, 19);
constexpr int kRowHeight = pick(88, 59);
constexpr int kRowPadLeft = pick(28, 19);
constexpr int kRowPadRight = pick(24, 16);
constexpr int kRowGap = pick(20, 12);
constexpr int kSubTop = pick(4, 2);
// Pill buttons.
constexpr int kButtonHeight = pick(56, 37);
constexpr int kButtonPad = pick(24, 16);
constexpr int kButtonGap = pick(10, 6);
// Segment switch.
constexpr int kSegmentHeight = pick(48, 32);
constexpr int kSegmentInset = pick(4, 3);
constexpr int kSegmentMinWidth = pick(104, 64);
constexpr int kSegmentPad = pick(18, 12);
// Four rotation steps use narrower segments on every panel (mockup rotRow).
constexpr int kQuarterSegmentMinWidth = 56;
constexpr int kQuarterSegmentPad = 8;
// Slider: track height, longest length, thumb width; value column.
constexpr int kSliderHeight = pick(48, 32);
constexpr int kSliderMaxWidth = pick(420, 160);
constexpr int kSliderThumbWidth = pick(4, 3);
constexpr int kValueWidth = pick(118, 64);
// Settings card: padding, the large page title (5-row and portrait panels).
constexpr int kCardPad = popup_layout::kCardPad;
constexpr int kTitleTop = popup_layout::scale(6);
constexpr int kTitleBodyTop = popup_layout::scale(62);
// Category tile: circle, its distance to the edge and to the text.
constexpr int kCategoryDisc = popup_layout::scale(72);
constexpr int kCategoryPad = popup_layout::scale(20);
constexpr int kCategoryTextGap = popup_layout::scale(16);
// Bar: space after the title; the tabs keep a quarter of it to the X.
constexpr int kBarGap = pick(28, 10);

// Text: row text, sub lines and headings, buttons and segments, the bar
// title, the large page title.
inline const lv_font_t* row_font() { return popup_layout::font24(); }
inline const lv_font_t* small_font() { return popup_layout::font20(); }
inline const lv_font_t* bar_title_font() { return popup_layout::font24(); }
inline const lv_font_t* page_title_font() { return popup_layout::font32(); }
// Their pixel sizes: the mockup sets line boxes from them (row text x 1.21,
// sub lines x 1.3, page title x 1.25).
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kRowFontPx = 20;
constexpr int kSmallFontPx = 16;
constexpr int kTitleFontPx = 28;
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kRowFontPx = 16;
constexpr int kSmallFontPx = 14;
constexpr int kTitleFontPx = 20;
#else
constexpr int kRowFontPx = 24;
constexpr int kSmallFontPx = 20;
constexpr int kTitleFontPx = 32;
#endif
// Grey text and icons: white at 60 %.
constexpr lv_opa_t kGreyOpa = 153;
// Row separators: white at 10 %.
constexpr lv_opa_t kSeparatorOpa = 26;
// Selected segment.
constexpr uint32_t kSelectedBg = 0xF5F7F5;
constexpr uint32_t kSelectedText = 0x1A1A1A;

// Category colors (mockup CATS).
constexpr uint32_t kDisplayColor = 0xFFB300;
constexpr uint32_t kWifiColor = 0x42A5F5;
constexpr uint32_t kLocalizationColor = 0xAB47BC;
constexpr uint32_t kSystemColor = 0x26A69A;
// The gear's circle: a neutral grey tile.
constexpr uint32_t kGearColor = 0x9E9E9E;

// A rounding the mockup draws at the default, maximum tile radius. It follows
// the global radius like every popup corner: smaller by as much as the global
// radius is below its maximum (ui_surface_style::radius baselines sit at the
// minimum radius).
inline void apply_radius(lv_obj_t* obj, int at_maximum, lv_style_selector_t selector = 0) {
  int baseline = at_maximum - (tile_radius::kMaximum - tile_radius::kMinimum);
  if (baseline < 0) baseline = 0;
  ui_surface_style::apply_radius(obj, baseline, selector);
}
// The global tile radius itself (tiles, cards, popups).
inline void apply_tile_radius(lv_obj_t* obj) { ui_surface_style::apply_radius(obj, tile_radius::kMinimum, 0); }

// Grid positions relative to the grid's top-left corner (the Settings panel
// carries the grid margins as its padding), exactly like the tiles.
inline int grid_x(float col) { return tile_geometry::edge(col, GRID_CELL_W, GRID_GAP); }
inline int grid_y(float row) { return tile_geometry::edge(row, GRID_CELL_H, GRID_GAP); }
inline int grid_w(float col, float span) { return tile_geometry::extent(col, span, GRID_CELL_W, GRID_GAP); }
inline int grid_h(float row, float span) { return tile_geometry::extent(row, span, GRID_CELL_H, GRID_GAP); }

// The colors of the moment: the card is the global tile color; groups,
// buttons and their pressed step sit one, two and three control steps above
// it (tone_color.h), so the global Circle strength moves them like the tile
// controls.
struct Colors {
  uint32_t card;
  uint32_t group;
  uint32_t button;
  uint32_t pressed;
};

inline float control_step() {
  const float step = ui_surface_style::icon_glow_percent() * tone_color::kStepPerPercent;
  return step < tone_color::kControlMinStep ? tone_color::kControlMinStep : step;
}

inline Colors colors(uint32_t card) {
  card &= 0xFFFFFF;
  const float step = control_step();
  return {card, tone_color::lifted(card, card, false, step), tone_color::lifted(card, card, false, 2 * step),
          tone_color::lifted(card, card, false, 3 * step)};
}

// A colored circle on the card (category tabs, page accents): the circle in
// the color's hue, the icon readable on it.
struct Tone {
  uint32_t disc;
  uint32_t icon;
};

inline Tone tone(uint32_t card, uint32_t color) {
  const bool hue = tile_tint::has_hue(color);
  const tone_color::Fill fill = tone_color::fill(card, color, hue, ui_surface_style::icon_glow_percent());
  return {fill.disc, hue ? tone_color::readable_icon(color) : 0xFFFFFF};
}

}  // namespace settings_style
