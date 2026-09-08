#pragma once

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

#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kHeaderCenterY = 50;
constexpr int kHeaderTitleX = 62;
constexpr int kCloseButtonSize = 72;
constexpr int kCloseButtonRadius = 13;
constexpr int kCloseButtonOffsetX = 3;
constexpr int kCloseButtonOffsetY = -3;
constexpr int kCloseButtonClickArea = 7;
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kHeaderCenterY = 40;
constexpr int kHeaderTitleX = 52;
constexpr int kCloseButtonSize = 64;
constexpr int kCloseButtonRadius = 11;
constexpr int kCloseButtonOffsetX = 4;
constexpr int kCloseButtonOffsetY = -4;
constexpr int kCloseButtonClickArea = 6;
#else
constexpr int kHeaderCenterY = 60;
constexpr int kHeaderTitleX = 78;
constexpr int kCloseButtonSize = 96;
constexpr int kCloseButtonRadius = 16;
constexpr int kCloseButtonOffsetX = 6;
constexpr int kCloseButtonOffsetY = -6;
constexpr int kCloseButtonClickArea = 8;
#endif
constexpr int kHeaderIconX = scale(8);

#if defined(DEVICE_LAYOUT_480X480)
constexpr int kCardMargin = 3;
constexpr int kCardRadius = 15;
#else
constexpr int kCardMargin = 4;
constexpr int kCardRadius = 22;
#endif
constexpr int kCardWidth =
    (SCREEN_WIDTH > SCREEN_HEIGHT)
        ? (SCREEN_HEIGHT - (kCardMargin * 2))
        : (SCREEN_WIDTH - (kCardMargin * 2));
constexpr int kCardHeight = SCREEN_HEIGHT - (kCardMargin * 2);
constexpr int kCardPad = scale(20);
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
inline void alignHeader(lv_obj_t* card, lv_obj_t* title, lv_obj_t* icon) {
  if (!card) return;
  const int center = kHeaderCenterY - lv_obj_get_style_pad_top(card, LV_PART_MAIN);
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
  lv_obj_t* close_btn = lv_button_create(card);
  lv_obj_set_size(close_btn, kCloseButtonSize, kCloseButtonSize);
  lv_obj_set_style_bg_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(close_btn, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(close_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(close_btn, kCloseButtonRadius, 0);
  lv_obj_set_style_pad_all(close_btn, 0, 0);
  lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, kCloseButtonOffsetX,
               kCloseButtonOffsetY);
  lv_obj_set_ext_click_area(close_btn, kCloseButtonClickArea);
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

}  // namespace popup_layout
