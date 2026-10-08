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

// Heading above a group (the setup's Networks heading; the WiFi entry's
// title keeps its left inset); headings inside a group (the Settings pages)
// sit kHeadingInsideTop below its top edge.
constexpr int kSectionTop = pick(22, 14);
constexpr int kSectionBottom = pick(10, 7);
constexpr int kSectionLeft = pick(24, 16);
constexpr int kHeadingInsideTop = pick(16, 11);
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
// Pair and Allow on System: a little taller, both one width (text + pads + this).
constexpr int kSwitchOnHeight = pick(64, 42);
constexpr int kSwitchOnExtra = pick(24, 16);
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
// Option list of a dropdown row (mockup .ddl): option height, the inset of
// the options in the list (also its gap to the row), six options visible.
constexpr int kOptionHeight = pick(56, 37);
constexpr int kOptionInset = pick(8, 5);
constexpr int kOptionsVisible = 6;
// Cards narrower than this show a row's value under its label.
constexpr int kStackedCardWidth = 520;
// System page: the logo, the head's large name from this card width on, the
// three buttons at the bottom, the download bar.
constexpr int kHeroLogo = pick(72, 44);
constexpr int kWideHeadCard = 720;
constexpr int kSystemButtonMin = pick(60, 44);
constexpr int kProgressWidth = pick(220, 110);
constexpr int kProgressHeight = pick(8, 6);
// Dialog (mockup P.dlg): width, padding, gaps, buttons, the GitHub QR code.
constexpr int kDialogWidth = pick(600, 440);
constexpr int kDialogPadTop = pick(36, 24);
constexpr int kDialogPadSide = pick(32, 20);
constexpr int kDialogPadBottom = pick(28, 18);
constexpr int kDialogGap = pick(16, 10);
constexpr int kDialogButtonsTop = pick(32, 20);
constexpr int kDialogButtonHeight = pick(72, 48);
constexpr int kDialogQr = pick(210, 140);
// Toggle (mockup .tg): track and knob inset.
constexpr int kToggleWidth = pick(88, 58);
constexpr int kToggleHeight = pick(48, 32);
constexpr int kToggleInset = pick(6, 4);
// WiFi page: the hotspot's QR code and the padding around it, the
// Networks heading's right margin.
constexpr int kHotspotQr = pick(168, 112);
constexpr int kHotspotQrPad = pick(16, 11);
constexpr int kHeadingRight = 8;
// WiFi entry (mockup sheetGeo): the 480 class has its own sizes.
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kEntryPad = 13;
constexpr int kEntryClose = 44;
constexpr int kEntryCloseRight = 8;
constexpr int kEntryCloseTop = 8;
constexpr int kEntryFieldTop = 60;
constexpr int kEntryFieldHeight = 44;
constexpr int kEntryFieldGap = 12;
constexpr int kEntryMessageTop = 112;
constexpr int kKeyHeight = 56;
constexpr int kKeyStep = 62;
constexpr int kKeyGap = 6;
constexpr int kKeyRadius = 12;
constexpr int kKeyboardBottom = 13;
// The entry in the setup's popup card: larger fields than the Settings card's.
constexpr int kSetupFieldTop = 76;
constexpr int kSetupFieldHeight = 48;
constexpr int kSetupMessageTop = 132;
#else
constexpr int kEntrySide = popup_layout::scale(20);
constexpr int kEntryColumn = popup_layout::scale(864);
constexpr int kEntryClose = popup_layout::scale(96);
constexpr int kEntryCloseRight = popup_layout::scale(14);
constexpr int kEntryCloseTop = popup_layout::scale(12);
constexpr int kEntryFieldTop = popup_layout::scale(112);
constexpr int kEntryFieldHeight = popup_layout::scale(72);
constexpr int kEntryFieldGap = popup_layout::scale(12);
constexpr int kKeyHeight = popup_layout::scale(84);
constexpr int kKeyStep = popup_layout::scale(94);
constexpr int kKeyGap = popup_layout::scale(10);
constexpr int kKeyRadius = popup_layout::scale(18);
constexpr int kKeyboardBottom = popup_layout::scale(20);
#endif
// First-start setup (mockup wzCard, wzBody, wzFoot): Back and Next in the
// popups' navigation size and place (popup_layout kNavHeight above
// kNavBottomInset, user 2026-10-06), the space under the body for them, the
// step dots, the pairing code's box, the width from which the Web Admin QR
// code stands beside its text.
constexpr int kSetupNavHeight = popup_layout::kNavHeight;
// More room than the Settings pages (user 2026-10-06): from the head to the
// body, per row, between groups.
constexpr int kSetupBodyGap = pick(32, 20);
constexpr int kSetupRowHeight = pick(100, 66);
constexpr int kSetupGroupGap = pick(24, 16);
constexpr int kSetupNavBottom = popup_layout::kCardPad + popup_layout::kNavBottomInset;
constexpr int kSetupFoot = kSetupNavBottom + kSetupNavHeight + pick(24, 14);
constexpr int kSetupDot = 8;
constexpr int kSetupDotCurrent = 24;
constexpr int kSetupCodePadTop = pick(32, 18);
constexpr int kSetupCodePadSide = pick(24, 12);
constexpr int kSetupCodeGap = pick(8, 4);
constexpr int kSetupCodeButtonTop = pick(16, 10);
constexpr int kSetupWideTiles = popup_layout::scale(740);
// Category tile: circle, its distance to the edge and to the text.
constexpr int kCategoryDisc = popup_layout::scale(72);
constexpr int kCategoryPad = popup_layout::scale(20);
constexpr int kCategoryTextGap = popup_layout::scale(16);

// Text: row text, sub lines and headings, buttons and segments, the large
// name on the System page and the WiFi entry's title.
inline const lv_font_t* row_font() { return popup_layout::font24(); }
inline const lv_font_t* small_font() { return popup_layout::font20(); }
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
// The MDI icon size of the class (FONT_MDI_ICONS).
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kIconPx = 40;
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kIconPx = 32;
#else
constexpr int kIconPx = 48;
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
// States (mockup GOOD, WARN), errors, the danger button, text on accent
// buttons.
constexpr uint32_t kGoodColor = 0x51CF66;
constexpr uint32_t kWarnColor = 0xFFC04D;
constexpr uint32_t kErrorColor = 0xFF6B6B;
constexpr uint32_t kDangerColor = 0xE5534B;
constexpr uint32_t kAccentText = 0x1A1A1A;
// Product names stay as they are in every language.
constexpr const char* kProductName = "HomeTiles";
constexpr const char* kHomeAssistant = "Home Assistant";
constexpr const char* kGitHub = "GitHub";
constexpr const char* kWebAdmin = "Web Admin";

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

// The half-height tiles' circle and its distance to the tile edges, as
// tile_icon_disc::inset() and diameter() compute them (the portrait
// category tiles' circle).
inline int tile_inset() { return popup_layout::scale480(4); }
// Settings keeps the profile's grid (grid_layout.h) whatever the tile grid
// shows: its look does not change with the head bar layout.
constexpr grid_layout::Shown kGrid = grid_layout::profile_grid();
inline int half_tile_disc() { return (kGrid.cell_h - GRID_GAP) / 2 - 2 * tile_inset(); }

// The frame and head (mockup head=popup/row): Settings is a popup card at
// full size. The popup card margin (kCardMargin) frames the screen; inside
// it everything keeps one grid gap to the frame, like the tiles keep it to
// each other. The head is the popup head: the X (kCloseButtonSize) in the top
// right corner of that inner box, the circle (kHeaderIconDiscSize) mirroring
// it on the left (as far from the top as from the side), the title beside the
// circle, all on the X's centre line; the body starts one grid gap below the
// X. Screen coordinates (the panel's padding is subtracted where they are used).
inline int inner_left() { return popup_layout::kCardMargin + GRID_GAP; }
inline int inner_right() { return grid_layout::screen_w() - popup_layout::kCardMargin - GRID_GAP; }
inline int inner_bottom() { return grid_layout::screen_h() - popup_layout::kCardMargin - GRID_GAP; }
inline int head_center_y() { return popup_layout::kCardMargin + GRID_GAP + popup_layout::kCloseButtonSize / 2; }
inline int head_disc_x() {
  return inner_left() + (popup_layout::kCloseButtonSize - popup_layout::kHeaderIconDiscSize) / 2;
}
inline int body_top() { return popup_layout::kCardMargin + 2 * GRID_GAP + popup_layout::kCloseButtonSize; }
// The X's pressed shape: halfway between the head circle and the full X,
// centred on the X, with the tile radius (user 2026-10-07 "Mittelweg"); the
// touch area keeps the X's size.
constexpr int kClosePressed = (popup_layout::kCloseButtonSize + popup_layout::kHeaderIconDiscSize) / 2;
// Its corner is concentric with the corner around the X (a frame or card of
// the tile radius + the grid gap, the X one gap inside it): the tile radius
// minus the pressed shape's inset in the X (user 2026-10-07: every head
// button the same size, every one concentric).
constexpr int kClosePressedBaseline = tile_radius::kMinimum - (popup_layout::kCloseButtonSize - kClosePressed) / 2;

// The width of `span` grid columns from `col`, exactly like the tiles (the
// landscape category column: two columns).
inline int grid_w(float col, float span) { return tile_geometry::extent(col, span, kGrid.cell_w, GRID_GAP); }

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

// The card the tile color "From icon" gives a color (default strength), the
// selected category tile; without the registered rule the plain tint.
inline uint32_t from_icon_card(uint32_t card, uint32_t color, bool pressed = false) {
  if (tone_color::g_from_icon_card) return tone_color::g_from_icon_card(color & 0xFFFFFF, pressed, 0) & 0xFFFFFF;
  const uint32_t tile = tile_tint::background(card, color, tone_color::kReferenceTint);
  return pressed ? tone_color::lifted(tile, tile, false, control_step()) : tile;
}

// A colored circle, control or slider track: like the popups' and tiles'
// "Circle in icon color" (popup_shell.cpp header_fill, the Light popup's
// brightness track), computed for the card "From icon" gives the color, so
// card, circle and icon stay one family; a neutral color on the card itself.
// The icon stays readable on the circle.
struct Tone {
  uint32_t disc;
  uint32_t control;
  uint32_t icon;
};

inline Tone tone(uint32_t card, uint32_t color) {
  const bool hue = tile_tint::has_hue(color);
  const uint32_t circle_card = hue ? from_icon_card(card, color) : card;
  const tone_color::Fill fill = tone_color::fill(circle_card, color, hue, ui_surface_style::icon_glow_percent());
  return {fill.disc, fill.control_color, hue ? tone_color::readable_icon(color) : 0xFFFFFF};
}

}  // namespace settings_style
