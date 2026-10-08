#pragma once

#include "src/ui/shared/ui_surface_style.h"
#include "src/core/config/tile_radius.h"

#include "src/ui/shared/title_label.h"

#include "src/core/display/display_manager.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/icons/mdi_icons.h"

namespace popup_layout {

constexpr int scale(int value) {
#if defined(DEVICE_LAYOUT_1024X600)
  return (value * 5 + (value >= 0 ? 3 : -3)) / 6;
#elif defined(DEVICE_LAYOUT_480X480)
  return (value * 2 + (value >= 0 ? 1 : -1)) / 3;
#else
  return value;
#endif
}

constexpr int contentScale(int value) {
#if defined(DEVICE_LAYOUT_1024X600)
  return (value * 3 + (value >= 0 ? 2 : -2)) / 4;
#elif defined(DEVICE_LAYOUT_480X480)
  return (value * 2 + (value >= 0 ? 1 : -1)) / 3;
#else
  return value;
#endif
}

// Scale legacy 720x720 geometry only for the strict 2/3 square layout.
// The separately tuned 1024x600 popup geometry must remain unchanged.
constexpr int scale480(int value) {
#if defined(DEVICE_LAYOUT_480X480)
  return (value * 2 + (value >= 0 ? 1 : -1)) / 3;
#else
  return value;
#endif
}

inline const lv_font_t* font20() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_16;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_14;
#else
  return &ui_font_20;
#endif
}

inline const lv_font_t* font24() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_20;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_16;
#else
  return &ui_font_24;
#endif
}

inline const lv_font_t* font28() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_24;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_20;
#else
  return &ui_font_28;
#endif
}

inline const lv_font_t* font32() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_28;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_20;
#else
  return &ui_font_32;
#endif
}

inline const lv_font_t* font40() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_32;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_28;
#else
  return &ui_font_40;
#endif
}

inline const lv_font_t* font48() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_40;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_32;
#else
  return &ui_font_48;
#endif
}

inline const lv_font_t* font56() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_48;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_40;
#else
  return &ui_font_56;
#endif
}

inline const lv_font_t* font64() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_56;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_40;
#else
  return &ui_font_64;
#endif
}

inline const lv_font_t* font72() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_56;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_48;
#else
  return &ui_font_72;
#endif
}

inline const lv_font_t* font80() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_64;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_56;
#else
  return &ui_font_80;
#endif
}

inline const lv_font_t* font96() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_80;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_64;
#else
  return &ui_font_96;
#endif
}

inline const lv_font_t* headerTitleFont() {
#if defined(DEVICE_LAYOUT_1024X600)
  return &ui_font_16;
#elif defined(DEVICE_LAYOUT_480X480)
  return &ui_font_16;
#else
  return &ui_font_24;
#endif
}

inline void applyIconScale(lv_obj_t* label) {
  // Compact layouts use native-size MDI fonts, so LVGL can center the label
  // directly without a transform pivot or per-frame resampling.
  (void)label;
}

// The header like the Home head bar and the Settings head (home_bar.cpp,
// settings_style.h), with the card as the frame: the X's box one grid gap
// inside the card corner (its pressed shape concentric with the card), the
// header circle mirroring it, circle, title and X on the X's centre line
// (user 2026-10-08: the head and every popup match). The offsets are from the
// card's content box (kCardPad, below).
constexpr int kHeadCardPad = scale(20);
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kHeaderCenterY = Device::kGridGap + 72 / 2;
constexpr int kCloseButtonSize = 72;
constexpr int kCloseButtonRadius = 13;
constexpr int kCloseButtonOffsetX = kHeadCardPad - Device::kGridGap;
constexpr int kCloseButtonOffsetY = -(kHeadCardPad - Device::kGridGap);
constexpr int kCloseButtonClickArea = 7;
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kHeaderCenterY = Device::kGridGap + 64 / 2;
constexpr int kCloseButtonSize = 64;
constexpr int kCloseButtonRadius = 11;
constexpr int kCloseButtonOffsetX = kHeadCardPad - Device::kGridGap;
constexpr int kCloseButtonOffsetY = -(kHeadCardPad - Device::kGridGap);
constexpr int kCloseButtonClickArea = 6;
#else
constexpr int kHeaderCenterY = Device::kGridGap + 96 / 2;
constexpr int kCloseButtonSize = 96;
constexpr int kCloseButtonRadius = 16;
constexpr int kCloseButtonOffsetX = kHeadCardPad - Device::kGridGap;
constexpr int kCloseButtonOffsetY = -(kHeadCardPad - Device::kGridGap);
constexpr int kCloseButtonClickArea = 8;
#endif
// Header icon disc: a translucent circle at the card content's left edge,
// centered on the header line. The header icon label spans the disc width
// with centered text, so its glyph sits in the middle of the circle.
constexpr int kHeaderIconDiscSize = scale(72);
constexpr int kHeaderIconDiscGap = scale(16);
constexpr int kHeaderIconDiscOpa = 38;
// A colored header icon tints its disc like the tile's circle
// (tone_color::fill, popup_shell.cpp header_fill). The card hairline is the
// plain white 20 % tile border.
constexpr int kPopupBorderOpa = 51;
constexpr int kHeaderIconX = Device::kGridGap + (kCloseButtonSize - kHeaderIconDiscSize) / 2 - kHeadCardPad;
constexpr int kHeaderTitleX = kHeaderIconX + kHeaderIconDiscSize + kHeaderIconDiscGap;

// The card reaches the screen edge (user 2026-10-08: the 3 or 4 px margin
// gave away 8 px in every dimension; the shadow is cut at the edge anyway).
// The head bar and the Settings head keep the same frame (kHeadMargin,
// settings_style.h), so the X stays over the gear and every corner stays
// concentric.
constexpr int kCardMargin = 0;
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kCardRadius = 15;
#else
constexpr int kCardRadius = 22;
#endif

// The card shadow of every popup: tile popups and the PIN pad (popup_shell),
// the Settings dialogs and the first-start setup, the camera pill. LVGL keeps
// a blurred shadow corner only while blur width + radius stays below
// LV_DRAW_SW_SHADOW_CACHE_SIZE (lv_draw_sw_box_shadow.c, the cache of
// lv_conf.h: S3 48, P4 72); a missed corner is computed again for every
// refresh band. Since b284 the card radius is the tile radius plus a grid
// gap, and the former 28 px blur missed the cache on the V2, 8-inch, 10.1",
// Tab5, 7", 4B and the S3 (V2: 14.6 ms per frame while a popup slider
// moved). LVGL computes the whole corner before it clips it to the screen,
// so a shadow cut at the edge costs the same. The blur is therefore as wide
// as the cache still holds at the object's current radius (it follows the
// tile radius setting), at most the former 28 px (user 2026-10-08: smaller,
// the same on every popup). A card that fills a square screen gets none.
#if defined(DEVICE_ESP32_S3_RGB_480) || defined(DEVICE_GUITION_ESP32_4848S040) || \
    defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) || defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
constexpr int kShadowCacheSize = 48;
#else
constexpr int kShadowCacheSize = 72;
#endif
#if defined(LV_DRAW_SW_SHADOW_CACHE_SIZE) && LV_DRAW_SW_SHADOW_CACHE_SIZE > 0
static_assert(kShadowCacheSize == LV_DRAW_SW_SHADOW_CACHE_SIZE, "the shadow cache of lv_conf.h");
#endif
constexpr int kCardShadowMax = scale480(28);
constexpr int kCardShadowSpread = scale480(2);
// The widest blur LVGL still caches with this corner radius.
inline int shadow_width_for(int radius) {
  const int room = kShadowCacheSize - 1 - radius;
  return room <= 0 ? 0 : room < kCardShadowMax ? room : kCardShadowMax;
}

// The card is a square in the middle of the screen: of the screen height on
// landscape panels, of the screen width on portrait ones (like the 480x480
// panels; a card over the whole portrait height left the square content
// above a large gap, user 2026-10-07).
constexpr int kCardWidth =
    (SCREEN_WIDTH > SCREEN_HEIGHT)
        ? (SCREEN_HEIGHT - (kCardMargin * 2))
        : (SCREEN_WIDTH - (kCardMargin * 2));
constexpr int kCardHeight =
    (SCREEN_HEIGHT > SCREEN_WIDTH) ? kCardWidth : (SCREEN_HEIGHT - (kCardMargin * 2));
// A card that fills a square screen: its shadow would lie outside it.
constexpr bool kCardFillsScreen = kCardWidth == SCREEN_WIDTH && kCardHeight == SCREEN_HEIGHT;
// PIN keypad keys at most this share of the card height, per mille. On the
// panels of 7 inches and more (1024x600 7", 1280x800 8" and 10.1") filling
// the card would make them physically about twice the size of the 4" and 5"
// boards, which fill the space (Tab5 included, 1280x720 at 5").
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_WAVESHARE_TOUCH_LCD_8) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) || defined(DEVICE_GUITION_JC8012P4A1) || \
    defined(DEVICE_GUITION_JC8012P4A1_V2)
constexpr int kKeypadKeyMaxPermille = 125;
#else
constexpr int kKeypadKeyMaxPermille = 1000;
#endif
constexpr int kCardPad = kHeadCardPad;
constexpr int kContentWidth = kCardWidth - (kCardPad * 2);

#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
constexpr int kExtraHeight = 0;
constexpr int kCompactHeightLift = 0;
#else
constexpr int kDesignCardHeight = 720 - (kCardMargin * 2);
constexpr int kExtraHeight = (kCardHeight > kDesignCardHeight)
                                 ? (kCardHeight - kDesignCardHeight)
                                 : 0;
constexpr int kCompactHeightLift = (kExtraHeight == 0) ? 1 : 0;
#endif

constexpr int kExtraGapBase = kExtraHeight / 2;
constexpr int kExtraGapRemainder = kExtraHeight % 2;

constexpr int extra_gap(int index) {
  return kExtraGapBase + ((index >= 0 && index < kExtraGapRemainder) ? 1 : 0);
}

constexpr int extra_gap_before_value() {
  return 0;
}

constexpr int extra_gap_before_body() {
  return extra_gap(0) / 2;
}

constexpr int kHeaderY = 0;
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kCompactValueLiftY = 0;
constexpr int kCompactBodyLiftY = 0;
constexpr int kLargeValueTextOffsetY = 0;
constexpr int kHeaderHeight = scale(96);
constexpr int kValueBaseY = scale(98);
constexpr int kValueHeight = scale(74);
constexpr int kBodyBaseY = scale(178);
// Leave a visible buffer above the bottom navigation on the 600 px layout.
constexpr int kBodyHeight = contentScale(414);
constexpr int kNavHeight = scale(92);
constexpr int kNavBottomInset = scale(6);
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kCompactValueLiftY = 0;
// The strict 2/3 scaling left the body only about 14 px above the bottom
// navigation. Lift it as one unit: the gap below grows while the oversized gap
// between the main value and the body disappears on every compact popup.
constexpr int kCompactBodyLiftY = 12;
constexpr int kLargeValueTextOffsetY = 0;
constexpr int kHeaderHeight = scale(96);
constexpr int kValueBaseY = scale(98);
constexpr int kValueHeight = scale(74);
constexpr int kBodyBaseY = scale(178) - kCompactBodyLiftY;
constexpr int kBodyHeight = contentScale(414);
constexpr int kNavHeight = scale(92);
constexpr int kNavBottomInset = scale(6);
#else
constexpr int kCompactValueLiftY = kCompactHeightLift * 24;
constexpr int kCompactBodyLiftY = kCompactHeightLift * 28;
constexpr int kLargeValueTextOffsetY = kCompactHeightLift * 6;
constexpr int kHeaderHeight = 96;
constexpr int kValueBaseY = 104 - kCompactValueLiftY;
constexpr int kValueHeight = 74;
constexpr int kBodyBaseY = 186 - kCompactBodyLiftY;
constexpr int kBodyHeight = 408;
constexpr int kNavHeight = 92;
constexpr int kNavBottomInset = 6;
#endif
constexpr int kValueY = kValueBaseY + extra_gap_before_value();
constexpr int kBodyY = kBodyBaseY + extra_gap_before_body();
constexpr int kNavY = kCardHeight - kNavBottomInset - kNavHeight;

// Header coordinates depend only on the font and fixed padding. Do not force
// layout of the entire screen to position cached header metadata before opening.
inline void alignHeader(lv_obj_t* card, lv_obj_t* title, lv_obj_t* icon,
                        lv_obj_t* icon_disc = nullptr) {
  if (!card) return;
  const int center = kHeaderCenterY - lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  if (icon_disc) {
    const int y = center - kHeaderIconDiscSize / 2;
    if (lv_obj_get_style_x(icon_disc, LV_PART_MAIN) != kHeaderIconX ||
        lv_obj_get_style_y(icon_disc, LV_PART_MAIN) != y)
      lv_obj_align(icon_disc, LV_ALIGN_TOP_LEFT, kHeaderIconX, y);
  }
  for (auto* label : {title, icon}) {
    if (!label) continue;
    const auto* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    int height = lv_font_get_line_height(font);
    const auto* state = hometiles_title::state_for(label);
    if (label == title && state && !state->top_aligned &&
        state->text.find('\n') != std::string::npos) height *= 2;
    const int x = label == title ? kHeaderTitleX : kHeaderIconX;
    const int y = std::max(0, center - height / 2);
    if (lv_obj_get_style_x(label, LV_PART_MAIN) != x ||
        lv_obj_get_style_y(label, LV_PART_MAIN) != y)
      lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
  }
}

// Standard popup close button. Every popup shares the same geometry, pressed
// feedback, touch area and icon, so the header stays consistent and a change to
// the close control does not have to be repeated per popup. `handler` is
// registered for CLICKED and RELEASED, which is the established press-on-release
// behavior; `user_data` reaches it unchanged.
inline lv_obj_t* createCloseButton(lv_obj_t* card, lv_event_cb_t handler,
                                   void* user_data) {
  // The pressed shape of every head button (the Settings X and gear, the
  // head bar's): halfway between the header circle and the full X, centred
  // where the X always was, its corner the tile radius minus its inset
  // (concentric), without the theme's grow on press (user 2026-10-07: every
  // head button the same size, every one concentric). The touch area keeps
  // the full X box.
  constexpr int kPressed = (kCloseButtonSize + kHeaderIconDiscSize) / 2;
  constexpr int kInset = (kCloseButtonSize - kPressed) / 2;
  lv_obj_t* close_btn = lv_button_create(card);
  lv_obj_set_size(close_btn, kPressed, kPressed);
  lv_obj_set_style_transform_width(close_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(close_btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(close_btn, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(close_btn, LV_OPA_TRANSP, 0);
  ui_surface_style::apply_radius(close_btn, tile_radius::kMinimum - kInset, 0);
  lv_obj_set_style_pad_all(close_btn, 0, 0);
  lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, kCloseButtonOffsetX - kInset,
               kCloseButtonOffsetY + kInset);
  lv_obj_set_ext_click_area(close_btn, kCloseButtonClickArea + kInset);
  lv_obj_add_flag(close_btn, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(close_btn, handler, LV_EVENT_CLICKED, user_data);
  lv_obj_add_event_cb(close_btn, handler, LV_EVENT_RELEASED, user_data);

  lv_obj_t* close_label = lv_label_create(close_btn);
  lv_obj_set_style_text_font(close_label, FONT_MDI_ICONS, 0);
  applyIconScale(close_label);
  lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
  lv_label_set_text(close_label, getMdiChar("window-close").c_str());
  lv_obj_center(close_label);
  return close_btn;
}

// The popups' card shadow (kCardShadowMax, shadow_width_for() above).
// After the object's radius is set (apply_radius()).
inline void apply_card_shadow(lv_obj_t* obj) {
  lv_obj_set_style_shadow_width(obj, shadow_width_for(lv_obj_get_style_radius(obj, LV_PART_MAIN)), 0);
  lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(obj, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(obj, kCardShadowSpread, 0);
}

// Header icon label: as wide as the icon disc with centered text, so the
// glyph is centered in the disc without measuring it or forcing a layout.
inline void styleHeaderIcon(lv_obj_t* icon) {
  lv_obj_set_width(icon, kHeaderIconDiscSize);
  lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
}

// Translucent circle behind the visible header icon. It is a plain circle,
// independent of the global tile radius.
inline lv_obj_t* createHeaderIconDisc(lv_obj_t* card) {
  lv_obj_t* disc = lv_obj_create(card);
  lv_obj_remove_style_all(disc);
  lv_obj_remove_flag(disc, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_size(disc, kHeaderIconDiscSize, kHeaderIconDiscSize);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(disc, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(disc, static_cast<lv_opa_t>(kHeaderIconDiscOpa), 0);
  return disc;
}

// Headers that show the entity's current value (Sensor, Number, Select,
// Date/Time, Binary Sensor, Energy): a smaller title above the value, both
// white, stacked as one block centered on the icon disc.
inline const lv_font_t* headerCompactTitleFont() { return font20(); }
inline const lv_font_t* headerValueFont() { return font28(); }

inline void alignHeaderWithValue(lv_obj_t* card, lv_obj_t* title, lv_obj_t* value,
                                 lv_obj_t* icon, lv_obj_t* icon_disc = nullptr) {
  alignHeader(card, nullptr, icon, icon_disc);
  if (!card || !title || !value) return;
  const int center = kHeaderCenterY - lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  const int title_height =
      lv_font_get_line_height(lv_obj_get_style_text_font(title, LV_PART_MAIN));
  const int value_height =
      lv_font_get_line_height(lv_obj_get_style_text_font(value, LV_PART_MAIN));
  const int top = std::max(0, center - (title_height + value_height) / 2);
  for (auto* label : {title, value}) {
    const int y = label == title ? top : top + title_height;
    if (lv_obj_get_style_x(label, LV_PART_MAIN) != kHeaderTitleX ||
        lv_obj_get_style_y(label, LV_PART_MAIN) != y)
      lv_obj_align(label, LV_ALIGN_TOP_LEFT, kHeaderTitleX, y);
  }
}

}  // namespace popup_layout
