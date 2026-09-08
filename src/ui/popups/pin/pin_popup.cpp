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
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"

#include <lvgl.h>

#include <string.h>

namespace {

constexpr uint32_t kErrorColor = 0xFF6B6B;
constexpr uint32_t kAutoCloseMs = 60000;
constexpr uint32_t kLastDigitRevealMs = 600;
constexpr int kKeySize = popup_layout::scale(92);
constexpr int kDigitGridExtent = popup_layout::contentScale(340);
constexpr int kDigitGapRaw = (kDigitGridExtent - (3 * kKeySize)) / 2;
constexpr int kDigitGap = kDigitGapRaw > 0 ? kDigitGapRaw : 0;
constexpr int kCardContentHeight =
    popup_layout::kCardHeight - (2 * popup_layout::kCardPad);
constexpr int kBottomRowTop =
    kCardContentHeight - popup_layout::kNavBottomInset - kKeySize;
constexpr int kKeypadHeight =
    (4 * kKeySize) + (3 * kDigitGap);
constexpr int kKeypadTop = kBottomRowTop - kKeypadHeight + kKeySize;
constexpr int kPinValueY =
    kKeypadTop - kDigitGap - popup_layout::kValueHeight;

constexpr int keypad_row_y(uint8_t row) {
  return row * (kKeySize + kDigitGap);
}

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
  lv_obj_t* value_label = nullptr;
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
  PinKeyData keys[12]{};
};

PinPopupContext* g_ctx = nullptr;

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

void update_value(PinPopupContext* ctx) {
  if (!ctx || !ctx->value_label) return;
  if (ctx->show_error) {
    const auto& tr = i18n::strings(configManager.getConfig().language);
    lv_label_set_text(ctx->value_label, tr.pin_popup_incorrect);
    lv_obj_set_style_text_color(ctx->value_label,
                                lv_color_hex(kErrorColor), 0);
    return;
  }

  char masked[pin_access::kInputMaxDigits + 1] = {};
  for (size_t i = 0; i < ctx->length; ++i) masked[i] = '*';
  if (ctx->reveal_last_digit && ctx->length > 0) {
    masked[ctx->length - 1] = ctx->input[ctx->length - 1];
  }
  lv_label_set_text(ctx->value_label, masked);
  lv_obj_set_style_text_color(ctx->value_label, lv_color_white(), 0);
}

lv_obj_t* create_key(lv_obj_t* parent, const char* text,
                     const lv_font_t* font, PinKeyData* key_data) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, kKeySize, kKeySize);
  lv_obj_set_style_bg_color(button, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_10, 0);
  lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
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
          // cache switch has actually committed. The masked label can remain
          // visible, but the plaintext buffer must be cleared immediately.
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
    lv_obj_set_style_bg_color(g_ctx->card, lv_color_hex(init.bg_color), 0);
    hometiles_title::set(g_ctx->title_label, init.title.c_str());
    lv_label_set_text(g_ctx->icon_label,
                      popup_icon_glyph(init.icon_name).c_str());
    update_value(g_ctx);
    popup_layout::alignHeader(g_ctx->card, g_ctx->title_label, g_ctx->icon_label);
    lv_obj_clear_flag(g_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);

    arm_auto_close_timer(g_ctx);
    show_popup_shell(g_ctx->overlay, g_ctx->card, g_ctx->title_label, g_ctx->icon_label, g_ctx->close_button);
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
  hometiles_title::set(parts.title, init.title.c_str());
  lv_label_set_text(parts.icon, popup_icon_glyph(init.icon_name).c_str());

  ctx->value_label = lv_label_create(ctx->card);
  lv_obj_set_size(ctx->value_label, LV_PCT(100), popup_layout::kValueHeight);
  lv_obj_align(ctx->value_label, LV_ALIGN_TOP_MID, 0,
               kPinValueY);
  lv_obj_set_style_text_align(ctx->value_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ctx->value_label, popup_layout::font40(), 0);

  lv_obj_t* grid = lv_obj_create(ctx->card);
  lv_obj_set_size(grid, kDigitGridExtent, kKeypadHeight);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, kKeypadTop);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  size_t key_index = 0;
  for (uint8_t digit = 1; digit <= 9; ++digit) {
    PinKeyData& key = ctx->keys[key_index++];
    key.popup = ctx;
    key.action = PinKeyAction::Digit;
    key.digit = digit;
    char text[2] = {static_cast<char>('0' + digit), '\0'};
    lv_obj_t* button = create_key(grid, text, popup_layout::font32(), &key);
    const int column = (digit - 1) % 3;
    const int row = (digit - 1) / 3;
    lv_obj_set_pos(button, column * (kKeySize + kDigitGap),
                   keypad_row_y(row));
  }

  PinKeyData& backspace = ctx->keys[key_index++];
  backspace.popup = ctx;
  backspace.action = PinKeyAction::Backspace;
  lv_obj_t* backspace_button = create_key(
      grid, getMdiChar("backspace-outline").c_str(), FONT_MDI_ICONS,
      &backspace);
  lv_obj_set_pos(backspace_button, 0, keypad_row_y(3));

  PinKeyData& zero = ctx->keys[key_index++];
  zero.popup = ctx;
  zero.action = PinKeyAction::Digit;
  zero.digit = 0;
  lv_obj_t* zero_button = create_key(
      grid, "0", popup_layout::font32(), &zero);
  lv_obj_set_pos(zero_button, kKeySize + kDigitGap, keypad_row_y(3));

  PinKeyData& confirm = ctx->keys[key_index++];
  confirm.popup = ctx;
  confirm.action = PinKeyAction::Confirm;
  lv_obj_t* confirm_button = create_key(
      grid, getMdiChar("check-bold").c_str(), FONT_MDI_ICONS, &confirm);
  lv_obj_set_pos(confirm_button, 2 * (kKeySize + kDigitGap),
                 keypad_row_y(3));

  lv_obj_add_event_cb(ctx->overlay, on_delete, LV_EVENT_DELETE, ctx);
  update_value(ctx);
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(close);

  arm_auto_close_timer(ctx);
  show_popup_shell(g_ctx->overlay, g_ctx->card, g_ctx->title_label, g_ctx->icon_label, g_ctx->close_button);
}

void preload_pin_popup() {
  if (g_ctx && g_ctx->overlay && g_ctx->card) return;
  const auto& tr = i18n::strings(configManager.getConfig().language);
  PinPopupInit init;
  init.title = tr.pin_popup_settings_title;
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
