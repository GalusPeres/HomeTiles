#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/pin/pin_popup.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/config/pin_access.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/ui_surface_style.h"

#include <lvgl.h>

#include <string.h>

namespace {

constexpr uint32_t kErrorColor = 0xFF6B6B;
constexpr uint32_t kAutoCloseMs = 60000;
constexpr uint32_t kLastDigitRevealMs = 600;
// The keypad layout, shared by every screen size: all distances are fixed
// shares of the key height (percent), so a small and a large display look
// the same. From top to bottom below the header: the lock glyph, the prompt
// line ("Enter PIN", then one dot per digit), four key rows.
constexpr int kKeyGapPct = 16;        // between keys
constexpr int kLockGapPct = 20;       // lock glyph to the prompt line
constexpr int kPromptGapPct = 26;     // prompt line to the keys
constexpr int kMarginPct = 8;         // at least this above and below the block
constexpr int kKeyWidthPct = 130;     // keys are a bit wider than tall
constexpr int kDotPct = 55;           // dot diameter, share of the prompt line
// Keys are a bit rounder than the close button and follow the global tile
// radius like it (ui_surface_style::apply_radius).
constexpr int kKeyRadius = popup_layout::kCloseButtonRadius + popup_layout::kCloseButtonRadius / 2;
constexpr int kKeyCount = 12;
constexpr int kBackspaceKey = 9;
constexpr int kZeroKey = 10;
constexpr int kConfirmKey = 11;

enum class PinKeyAction : uint8_t {
  Digit,
  Backspace,
  Confirm,
};

struct PinPopupContext;

struct PinKeyData {
  PinPopupContext* popup = nullptr;
  PinKeyAction action = PinKeyAction::Digit;
  uint8_t digit = 0;
};

struct PinPopupContext {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  // Hidden header value ("Locked"), shown by the shared header.
  lv_obj_t* state_label = nullptr;
  lv_obj_t* lock_label = nullptr;
  // The prompt line: the prompt or the error text, else the dots.
  lv_obj_t* prompt_label = nullptr;
  lv_obj_t* dots_row = nullptr;
  lv_obj_t* dots[pin_access::kInputMaxDigits] = {};
  lv_obj_t* reveal_label = nullptr;
  lv_obj_t* key_buttons[kKeyCount] = {};
  char input[pin_access::kInputMaxDigits + 1] = {};
  size_t length = 0;
  bool show_error = false;
  bool hide_on_success = true;
  bool waiting_for_success_completion = false;
  PinPopupVerifyCallback verify = nullptr;
  PinPopupSuccessCallback success = nullptr;
  void* callback_context = nullptr;
  lv_timer_t* auto_close_timer = nullptr;
  lv_timer_t* last_digit_reveal_timer = nullptr;
  bool reveal_last_digit = false;
  PinKeyData keys[kKeyCount]{};
};

PinPopupContext* g_ctx = nullptr;

// Positions below the header, in the card's content coordinates.
struct KeypadGeometry {
  int key_w = 0;
  int key_h = 0;
  int gap = 0;
  int lock_y = 0;
  int prompt_y = 0;
  int prompt_h = 0;
  int keys_x = 0;
  int keys_y = 0;
  int dot = 0;
};

KeypadGeometry keypad_geometry(lv_obj_t* card, const lv_font_t* lock_font, const lv_font_t* prompt_font) {
  KeypadGeometry g;
  const int pad = lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  const int content_w = popup_layout::kContentWidth;
  const int content_h = popup_layout::kCardHeight - 2 * pad;
  const int top = popup_layout::kHeaderCenterY - pad + popup_layout::kHeaderIconDiscSize / 2;
  const int lock_h = lv_font_get_line_height(lock_font);
  g.prompt_h = lv_font_get_line_height(prompt_font);
  const int available = content_h - top;
  // Height: lock, prompt and the key-height shares fill the space below the
  // header; width: three keys and two gaps fit the content width.
  const int height_pct = 400 + 3 * kKeyGapPct + kLockGapPct + kPromptGapPct + 2 * kMarginPct;
  const int width_pct = 3 * kKeyWidthPct + 2 * kKeyGapPct;
  g.key_h = (available - lock_h - g.prompt_h) * 100 / height_pct;
  const int width_limit = content_w * 100 / width_pct;
  if (width_limit < g.key_h) g.key_h = width_limit;
  if (g.key_h < 1) g.key_h = 1;
  g.key_w = g.key_h * kKeyWidthPct / 100;
  g.gap = g.key_h * kKeyGapPct / 100;
  const int lock_gap = g.key_h * kLockGapPct / 100;
  const int prompt_gap = g.key_h * kPromptGapPct / 100;
  const int block = lock_h + lock_gap + g.prompt_h + prompt_gap + 4 * g.key_h + 3 * g.gap;
  g.lock_y = top + (available - block) / 2;
  g.prompt_y = g.lock_y + lock_h + lock_gap;
  g.keys_y = g.prompt_y + g.prompt_h + prompt_gap;
  g.keys_x = (content_w - (3 * g.key_w + 2 * g.gap)) / 2;
  g.dot = g.prompt_h * kDotPct / 100;
  return g;
}

// Digits in a font that fills the key like the design.
const lv_font_t* digit_font(int key_h) {
  if (key_h >= popup_layout::scale(96)) return popup_layout::font40();
  if (key_h >= popup_layout::scale(72)) return popup_layout::font32();
  return popup_layout::font28();
}

void update_value(PinPopupContext* ctx);

void cancel_last_digit_reveal(PinPopupContext* ctx) {
  if (!ctx) return;
  ctx->reveal_last_digit = false;
  if (ctx->last_digit_reveal_timer) {
    lv_timer_pause(ctx->last_digit_reveal_timer);
  }
}

void last_digit_reveal_timer_cb(lv_timer_t* timer) {
  PinPopupContext* ctx = static_cast<PinPopupContext*>(
      lv_timer_get_user_data(timer));
  lv_timer_pause(timer);
  if (!ctx || ctx != g_ctx || timer != ctx->last_digit_reveal_timer) return;
  ctx->reveal_last_digit = false;
  update_value(ctx);
}

void arm_last_digit_reveal_timer(PinPopupContext* ctx) {
  if (!ctx) return;
  ctx->reveal_last_digit = true;
  if (!ctx->last_digit_reveal_timer) {
    ctx->last_digit_reveal_timer = lv_timer_create(
        last_digit_reveal_timer_cb, kLastDigitRevealMs, ctx);
    return;
  }
  lv_timer_set_period(ctx->last_digit_reveal_timer, kLastDigitRevealMs);
  lv_timer_reset(ctx->last_digit_reveal_timer);
  lv_timer_resume(ctx->last_digit_reveal_timer);
}

void auto_close_timer_cb(lv_timer_t* timer) {
  PinPopupContext* ctx = static_cast<PinPopupContext*>(
      lv_timer_get_user_data(timer));
  if (!ctx || ctx != g_ctx) {
    lv_timer_pause(timer);
    return;
  }
  lv_timer_pause(timer);
  if (ctx->waiting_for_success_completion) return;
  hide_pin_popup();
}

void arm_auto_close_timer(PinPopupContext* ctx) {
  if (!ctx) return;
  if (!ctx->auto_close_timer) {
    ctx->auto_close_timer =
        lv_timer_create(auto_close_timer_cb, kAutoCloseMs, ctx);
    return;
  }
  lv_timer_set_period(ctx->auto_close_timer, kAutoCloseMs);
  lv_timer_reset(ctx->auto_close_timer);
  lv_timer_resume(ctx->auto_close_timer);
}

void clear_input(PinPopupContext* ctx) {
  if (!ctx) return;
  cancel_last_digit_reveal(ctx);
  pin_access::secureClear(ctx->input, sizeof(ctx->input));
  ctx->length = 0;
  ctx->show_error = false;
}

// The prompt line: "Enter PIN" while empty, the error after a wrong PIN, else
// one dot per digit, the latest digit shown briefly instead of its dot.
void update_value(PinPopupContext* ctx) {
  if (!ctx || !ctx->prompt_label || !ctx->dots_row) return;
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const bool prompt = ctx->show_error || ctx->length == 0;
  lv_obj_set_flag(ctx->prompt_label, LV_OBJ_FLAG_HIDDEN, !prompt);
  lv_obj_set_flag(ctx->dots_row, LV_OBJ_FLAG_HIDDEN, prompt);
  if (prompt) {
    lv_label_set_text(ctx->prompt_label, ctx->show_error ? tr.pin_popup_incorrect : tr.pin_popup_enter);
    lv_obj_set_style_text_color(ctx->prompt_label,
                                ctx->show_error ? lv_color_hex(kErrorColor) : lv_color_white(), 0);
    return;
  }
  const bool reveal = ctx->reveal_last_digit;
  const size_t dots = reveal ? ctx->length - 1 : ctx->length;
  for (size_t i = 0; i < pin_access::kInputMaxDigits; ++i) {
    lv_obj_set_flag(ctx->dots[i], LV_OBJ_FLAG_HIDDEN, i >= dots);
  }
  char digit[2] = {reveal ? ctx->input[ctx->length - 1] : '\0', '\0'};
  lv_label_set_text(ctx->reveal_label, digit);
  lv_obj_set_flag(ctx->reveal_label, LV_OBJ_FLAG_HIDDEN, !reveal);
}

// Keys take the popup control fill (popup_nav_style.h) of the card and the
// header icon; a pressed key lights up with twice its opacity. Backspace sits
// halfway between the keys and the card; confirm is white with the check in
// the card color, like Play in the Media popup. The lock takes the icon color.
void style_keypad(PinPopupContext* ctx) {
  if (!ctx || !ctx->card || !ctx->icon_label) return;
  const lv_color_t card = lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN);
  const lv_color_t icon = lv_obj_get_style_text_color(ctx->icon_label, LV_PART_MAIN);
  lv_color_t fill;
  lv_opa_t opa;
  popup_nav_style::fill(card, icon, fill, opa);
  const lv_opa_t pressed = opa > LV_OPA_COVER / 2 ? LV_OPA_COVER : static_cast<lv_opa_t>(opa * 2);
  for (int i = 0; i < kKeyCount; ++i) {
    lv_obj_t* key = ctx->key_buttons[i];
    if (!key) continue;
    if (i == kConfirmKey) {
      popup_nav_style::set_bg(key, lv_color_white(), LV_OPA_COVER, LV_PART_MAIN);
      popup_nav_style::set_bg(key, lv_color_hex(0xD8D8D8), LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_t* label = lv_obj_get_child(key, 0);
      if (label && !lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), card)) {
        lv_obj_set_style_text_color(label, card, 0);
      }
      continue;
    }
    const bool backspace = i == kBackspaceKey;
    popup_nav_style::set_bg(key, fill, backspace ? static_cast<lv_opa_t>(opa / 2) : opa, LV_PART_MAIN);
    popup_nav_style::set_bg(key, fill, backspace ? opa : pressed, LV_PART_MAIN | LV_STATE_PRESSED);
  }
  if (ctx->lock_label && !lv_color_eq(lv_obj_get_style_text_color(ctx->lock_label, LV_PART_MAIN), icon)) {
    lv_obj_set_style_text_color(ctx->lock_label, icon, 0);
  }
}

lv_obj_t* create_key(lv_obj_t* parent, const char* text, const lv_font_t* font,
                     const KeypadGeometry& geometry, PinKeyData* key_data) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, geometry.key_w, geometry.key_h);
  ui_surface_style::apply_radius(button, kKeyRadius, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_outline_width(button, 0, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_anim_time(button, 0, 0);
  lv_obj_set_style_transform_width(button, 0, 0);
  lv_obj_set_style_transform_height(button, 0, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(button);

  lv_obj_t* label = lv_label_create(button);
  lv_obj_set_style_text_font(label, font, 0);
  popup_layout::applyIconScale(label);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_add_event_cb(
      button,
      [](lv_event_t* event) {
        if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
        PinKeyData* key = static_cast<PinKeyData*>(
            lv_event_get_user_data(event));
        PinPopupContext* ctx = key ? key->popup : nullptr;
        if (!ctx || ctx->waiting_for_success_completion) return;
        arm_auto_close_timer(ctx);

        if (ctx->show_error) {
          clear_input(ctx);
        }
        if (key->action == PinKeyAction::Digit) {
          if (ctx->length < pin_access::kInputMaxDigits) {
            ctx->input[ctx->length++] = static_cast<char>('0' + key->digit);
            ctx->input[ctx->length] = '\0';
            arm_last_digit_reveal_timer(ctx);
          }
          update_value(ctx);
          return;
        }
        if (key->action == PinKeyAction::Backspace) {
          cancel_last_digit_reveal(ctx);
          if (ctx->length > 0) {
            ctx->input[--ctx->length] = '\0';
          }
          update_value(ctx);
          return;
        }

        cancel_last_digit_reveal(ctx);
        update_value(ctx);
        const bool accepted = ctx->verify &&
                              ctx->verify(ctx->input,
                                          ctx->callback_context);
        if (!accepted) {
          pin_access::secureClear(ctx->input, sizeof(ctx->input));
          ctx->length = 0;
          ctx->show_error = true;
          update_value(ctx);
          return;
        }

        PinPopupSuccessCallback success = ctx->success;
        void* callback_context = ctx->callback_context;
        const bool hide_on_success = ctx->hide_on_success;
        if (hide_on_success) {
          hide_pin_popup();
        } else {
          // Keep the protected content covered until the asynchronous folder
          // cache switch has actually committed. The dots can remain visible,
          // but the plaintext buffer must be cleared immediately.
          pin_access::secureClear(ctx->input, sizeof(ctx->input));
          ctx->waiting_for_success_completion = true;
          if (ctx->auto_close_timer) lv_timer_pause(ctx->auto_close_timer);
        }
        if (success) success(callback_context);
        if (!success && !hide_on_success) {
          ctx->waiting_for_success_completion = false;
          clear_input(ctx);
          update_value(ctx);
          arm_auto_close_timer(ctx);
        }
      },
      LV_EVENT_CLICKED, key_data);
  return button;
}

void on_close(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  PinPopupContext* ctx = static_cast<PinPopupContext*>(
      lv_event_get_user_data(event));
  if (ctx && ctx->waiting_for_success_completion) return;
  hide_pin_popup();
}

void on_delete(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
  PinPopupContext* ctx = static_cast<PinPopupContext*>(
      lv_event_get_user_data(event));
  if (g_ctx == ctx) g_ctx = nullptr;
  if (ctx && ctx->auto_close_timer) {
    lv_timer_delete(ctx->auto_close_timer);
    ctx->auto_close_timer = nullptr;
  }
  if (ctx && ctx->last_digit_reveal_timer) {
    lv_timer_delete(ctx->last_digit_reveal_timer);
    ctx->last_digit_reveal_timer = nullptr;
  }
  clear_input(ctx);
  delete ctx;
}

bool preload_verify(const char*, void*) { return false; }

String popup_icon_glyph(const String& icon_name) {
  String glyph = getMdiChar(icon_name);
  if (!glyph.length()) glyph = getMdiChar("lock-outline");
  return glyph;
}

// Builds the lock, the prompt line and the 3 x 4 keypad below the header.
void build_keypad(PinPopupContext* ctx) {
  lv_obj_t* card = ctx->card;
  const lv_font_t* prompt_font = popup_layout::font24();
  const KeypadGeometry g = keypad_geometry(card, FONT_MDI_ICONS, prompt_font);

  ctx->lock_label = lv_label_create(card);
  lv_obj_set_style_text_font(ctx->lock_label, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(ctx->lock_label);
  lv_label_set_text(ctx->lock_label, getMdiChar("lock").c_str());
  lv_obj_align(ctx->lock_label, LV_ALIGN_TOP_MID, 0, g.lock_y);

  ctx->prompt_label = lv_label_create(card);
  lv_obj_set_style_text_font(ctx->prompt_label, prompt_font, 0);
  lv_obj_set_style_text_align(ctx->prompt_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(ctx->prompt_label, LV_PCT(100));
  lv_label_set_long_mode(ctx->prompt_label, LV_LABEL_LONG_DOT);
  lv_obj_align(ctx->prompt_label, LV_ALIGN_TOP_MID, 0, g.prompt_y);

  ctx->dots_row = lv_obj_create(card);
  lv_obj_remove_style_all(ctx->dots_row);
  lv_obj_set_size(ctx->dots_row, LV_PCT(100), g.prompt_h);
  lv_obj_align(ctx->dots_row, LV_ALIGN_TOP_MID, 0, g.prompt_y);
  lv_obj_set_flex_flow(ctx->dots_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctx->dots_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctx->dots_row, g.dot, 0);
  lv_obj_remove_flag(ctx->dots_row, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  for (size_t i = 0; i < pin_access::kInputMaxDigits; ++i) {
    lv_obj_t* dot = lv_obj_create(ctx->dots_row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, g.dot, g.dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    ctx->dots[i] = dot;
  }
  // The latest digit, shown briefly after the dots (flex order).
  ctx->reveal_label = lv_label_create(ctx->dots_row);
  lv_obj_set_style_text_font(ctx->reveal_label, prompt_font, 0);
  lv_obj_set_style_text_color(ctx->reveal_label, lv_color_white(), 0);
  lv_label_set_text(ctx->reveal_label, "");

  lv_obj_t* grid = lv_obj_create(card);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, 3 * g.key_w + 2 * g.gap, 4 * g.key_h + 3 * g.gap);
  lv_obj_set_pos(grid, g.keys_x, g.keys_y);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  const lv_font_t* digits = digit_font(g.key_h);
  auto place = [&](lv_obj_t* button, int col, int row) {
    lv_obj_set_pos(button, col * (g.key_w + g.gap), row * (g.key_h + g.gap));
  };
  for (uint8_t digit = 1; digit <= 9; ++digit) {
    PinKeyData& key = ctx->keys[digit - 1];
    key.popup = ctx;
    key.action = PinKeyAction::Digit;
    key.digit = digit;
    char text[2] = {static_cast<char>('0' + digit), '\0'};
    ctx->key_buttons[digit - 1] = create_key(grid, text, digits, g, &key);
    place(ctx->key_buttons[digit - 1], (digit - 1) % 3, (digit - 1) / 3);
  }

  PinKeyData& backspace = ctx->keys[kBackspaceKey];
  backspace.popup = ctx;
  backspace.action = PinKeyAction::Backspace;
  ctx->key_buttons[kBackspaceKey] = create_key(grid, getMdiChar("backspace").c_str(), FONT_MDI_ICONS, g, &backspace);
  place(ctx->key_buttons[kBackspaceKey], 0, 3);

  PinKeyData& zero = ctx->keys[kZeroKey];
  zero.popup = ctx;
  zero.action = PinKeyAction::Digit;
  zero.digit = 0;
  ctx->key_buttons[kZeroKey] = create_key(grid, "0", digits, g, &zero);
  place(ctx->key_buttons[kZeroKey], 1, 3);

  PinKeyData& confirm = ctx->keys[kConfirmKey];
  confirm.popup = ctx;
  confirm.action = PinKeyAction::Confirm;
  ctx->key_buttons[kConfirmKey] = create_key(grid, getMdiChar("check-bold").c_str(), FONT_MDI_ICONS, g, &confirm);
  place(ctx->key_buttons[kConfirmKey], 2, 3);
}

// Header of the protected tile: its icon and name, the state "Locked".
void apply_header(PinPopupContext* ctx, const PinPopupInit& init) {
  const auto& tr = i18n::strings(configManager.getConfig().language);
  lv_obj_set_style_bg_color(ctx->card, lv_color_hex(init.bg_color), 0);
  hometiles_title::set(ctx->title_label, init.title.c_str());
  lv_label_set_text(ctx->icon_label, popup_icon_glyph(init.icon_name).c_str());
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(init.icon_color), 0);
  lv_label_set_text(ctx->state_label, tr.pin_popup_locked);
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
}

}  // namespace

void show_pin_popup(const PinPopupInit& init) {
  hide_light_popup();
  hide_climate_popup();
  hide_cover_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_media_popup();
  hide_camera_popup();

  if (g_ctx && g_ctx->overlay && g_ctx->card) {
    clear_input(g_ctx);
    g_ctx->hide_on_success = init.hide_on_success;
    g_ctx->waiting_for_success_completion = false;
    g_ctx->verify = init.verify;
    g_ctx->success = init.success;
    g_ctx->callback_context = init.context;
    apply_header(g_ctx, init);
    style_keypad(g_ctx);
    update_value(g_ctx);
    lv_obj_clear_flag(g_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);

    arm_auto_close_timer(g_ctx);
    show_popup_shell(g_ctx->overlay, g_ctx->card, g_ctx->title_label, g_ctx->icon_label, g_ctx->close_button,
                     nullptr, g_ctx->state_label);
    return;
  }

  PinPopupContext* ctx = new PinPopupContext();
  if (!ctx) return;
  g_ctx = ctx;
  ctx->hide_on_success = init.hide_on_success;
  ctx->verify = init.verify;
  ctx->success = init.success;
  ctx->callback_context = init.context;

  const auto parts = create_popup_body(on_close, ctx, init.bg_color);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* close = parts.close;
  disable_pressed_button_animation(parts.close);
  // The shared header reads the state from this hidden label.
  ctx->state_label = lv_label_create(ctx->card);
  lv_obj_add_flag(ctx->state_label, LV_OBJ_FLAG_HIDDEN);

  build_keypad(ctx);
  apply_header(ctx, init);
  style_keypad(ctx);

  lv_obj_add_event_cb(ctx->overlay, on_delete, LV_EVENT_DELETE, ctx);
  update_value(ctx);
  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(close);

  arm_auto_close_timer(ctx);
  show_popup_shell(g_ctx->overlay, g_ctx->card, g_ctx->title_label, g_ctx->icon_label, g_ctx->close_button,
                   nullptr, g_ctx->state_label);
}

void preload_pin_popup() {
  if (g_ctx && g_ctx->overlay && g_ctx->card) return;
  const auto& tr = i18n::strings(configManager.getConfig().language);
  PinPopupInit init;
  init.title = tr.tile_type_settings;
  init.icon_name = "lock-outline";
  init.verify = preload_verify;
  show_pin_popup(init);
  hide_pin_popup();
}

void hide_pin_popup() {
  if (!g_ctx || !g_ctx->card || !g_ctx->overlay) return;
  if (g_ctx->auto_close_timer) lv_timer_pause(g_ctx->auto_close_timer);
  g_ctx->waiting_for_success_completion = false;
  clear_input(g_ctx);
  g_ctx->verify = nullptr;
  g_ctx->success = nullptr;
  g_ctx->callback_context = nullptr;
  update_value(g_ctx);
  hide_popup_shell(g_ctx->card);
  cancel_popup_open(g_ctx->card);
  lv_obj_add_flag(g_ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void resume_pin_popup_after_failed_success() {
  if (!g_ctx || !g_ctx->card || !g_ctx->overlay ||
      !g_ctx->waiting_for_success_completion) {
    return;
  }
  g_ctx->waiting_for_success_completion = false;
  clear_input(g_ctx);
  update_value(g_ctx);
  arm_auto_close_timer(g_ctx);
}

bool is_pin_popup_visible() {
  return g_ctx && g_ctx->card &&
         !lv_obj_has_flag(g_ctx->card, LV_OBJ_FLAG_HIDDEN);
}
