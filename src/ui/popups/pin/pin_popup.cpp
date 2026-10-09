#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/pin/pin_keypad_geometry.h"

#include "src/core/config/config_manager.h"
#include "src/ui/shared/ui_theme.h"
#include "src/core/i18n/i18n.h"
#include "src/core/config/pin_access.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
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

using namespace pin_keypad;

// The error text; the light theme takes its dark tone (settings_style::error_color).
inline uint32_t error_color() { return ui_theme::light() ? 0xD03F45 : 0xFF6B6B; }
constexpr uint32_t kAutoCloseMs = 60000;
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
  // "Enter PIN" or the error text, above the dots line.
  lv_obj_t* prompt_label = nullptr;
  lv_obj_t* dots_row = nullptr;
  lv_obj_t* dots[pin_access::kInputMaxDigits] = {};
  lv_obj_t* key_buttons[kKeyCount] = {};
  char input[pin_access::kInputMaxDigits + 1] = {};
  size_t length = 0;
  bool show_error = false;
  bool hide_on_success = true;
  bool waiting_for_success_completion = false;
  PinPopupVerifyCallback verify = nullptr;
  PinPopupSuccessCallback success = nullptr;
  PinPopupDismissedCallback dismissed = nullptr;
  void* callback_context = nullptr;
  // Device code texts; empty keeps the PIN texts.
  String prompt;
  String error;
  lv_timer_t* auto_close_timer = nullptr;
  PinKeyData keys[kKeyCount]{};
};

PinPopupContext* g_ctx = nullptr;

// Digits in a font that fills the key like the design.
const lv_font_t* digit_font(int key_h) {
  if (key_h >= popup_layout::scale(96)) return popup_layout::font40();
  if (key_h >= popup_layout::scale(72)) return popup_layout::font32();
  return popup_layout::font28();
}

void update_value(PinPopupContext* ctx);

void auto_close_timer_cb(lv_timer_t* timer) {
  PinPopupContext* ctx = static_cast<PinPopupContext*>(
      lv_timer_get_user_data(timer));
  if (!ctx || ctx != g_ctx) {
    lv_timer_pause(timer);
    return;
  }
  lv_timer_pause(timer);
  if (ctx->waiting_for_success_completion) return;
  PinPopupDismissedCallback dismissed = ctx->dismissed;
  void* context = ctx->callback_context;
  hide_pin_popup();
  if (dismissed) dismissed(context);
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
  pin_access::secureClear(ctx->input, sizeof(ctx->input));
  ctx->length = 0;
  ctx->show_error = false;
}

// The prompt: "Enter PIN", or the error in red after a wrong PIN. Below it
// empty circles that fill one per typed digit (at least as many as the
// shortest PIN); a digit is never shown.
void update_value(PinPopupContext* ctx) {
  if (!ctx || !ctx->prompt_label || !ctx->dots_row) return;
  const auto& tr = i18n::strings(configManager.getConfig().language);
  const char* prompt = ctx->prompt.length() ? ctx->prompt.c_str() : tr.pin_popup_enter;
  const char* error = ctx->error.length() ? ctx->error.c_str() : tr.pin_popup_incorrect;
  lv_label_set_text(ctx->prompt_label, ctx->show_error ? error : prompt);
  lv_obj_set_style_text_color(ctx->prompt_label,
                              ctx->show_error ? lv_color_hex(error_color()) : lv_color_hex(ui_theme::text()), 0);
  const size_t circles = ctx->length > pin_access::kUserPinMinDigits ? ctx->length : pin_access::kUserPinMinDigits;
  for (size_t i = 0; i < pin_access::kInputMaxDigits; ++i) {
    lv_obj_set_flag(ctx->dots[i], LV_OBJ_FLAG_HIDDEN, i >= circles);
    lv_obj_set_style_bg_opa(ctx->dots[i], i < ctx->length ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

// Keys take the popup control fill (popup_nav_style.h) of the card and the
// header icon; a pressed key lights up one more control step (the raised
// fill). Backspace sits halfway between the keys and the card and lights up
// to a key; confirm is white with the check in the card color, like Play in
// the Media popup. A press shows exactly these colors (no theme darkening).
void style_keypad(PinPopupContext* ctx) {
  if (!ctx || !ctx->card || !ctx->icon_label) return;
  const lv_color_t card = lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN);
  const lv_color_t icon = lv_obj_get_style_text_color(ctx->icon_label, LV_PART_MAIN);
  lv_color_t fill;
  lv_opa_t opa;
  popup_nav_style::fill(card, icon, fill, opa);
  lv_color_t raised;
  lv_opa_t pressed;
  popup_nav_style::fill_raised(card, icon, raised, pressed);
  for (int i = 0; i < kKeyCount; ++i) {
    lv_obj_t* key = ctx->key_buttons[i];
    if (!key) continue;
    if (i == kConfirmKey) {
      popup_nav_style::set_bg(key, lv_color_hex(ui_theme::icon()), LV_OPA_COVER, LV_PART_MAIN);
      popup_nav_style::set_bg(key, lv_color_hex(ui_theme::text_soft()), LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
      popup_nav_style::no_press_filter(key, LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_t* label = lv_obj_get_child(key, 0);
      if (label && !lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), card)) {
        lv_obj_set_style_text_color(label, card, 0);
      }
      continue;
    }
    const bool backspace = i == kBackspaceKey;
    popup_nav_style::set_bg(key, fill, backspace ? static_cast<lv_opa_t>(opa / 2) : opa, LV_PART_MAIN);
    if (backspace) popup_nav_style::set_bg(key, fill, opa, LV_PART_MAIN | LV_STATE_PRESSED);
    else popup_nav_style::set_bg(key, raised, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    popup_nav_style::no_press_filter(key, LV_PART_MAIN | LV_STATE_PRESSED);
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
  lv_obj_set_style_text_color(label, lv_color_hex(ui_theme::text()), 0);
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
          }
          update_value(ctx);
          return;
        }
        if (key->action == PinKeyAction::Backspace) {
          if (ctx->length > 0) {
            ctx->input[--ctx->length] = '\0';
          }
          update_value(ctx);
          return;
        }

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
  PinPopupDismissedCallback dismissed = ctx ? ctx->dismissed : nullptr;
  void* context = ctx ? ctx->callback_context : nullptr;
  hide_pin_popup();
  if (dismissed) dismissed(context);
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
  clear_input(ctx);
  delete ctx;
}

bool preload_verify(const char*, void*) { return false; }

String popup_icon_glyph(const String& icon_name) {
  String glyph = getMdiChar(icon_name);
  if (!glyph.length()) glyph = getMdiChar("lock-outline");
  return glyph;
}

// Builds the prompt, the dots line and the 3 x 4 keypad below the header.
void build_keypad(PinPopupContext* ctx) {
  lv_obj_t* card = ctx->card;
  const lv_font_t* prompt_font = popup_layout::font24();
  const KeypadGeometry g = keypad_geometry(card, prompt_font);

  ctx->prompt_label = lv_label_create(card);
  lv_obj_set_style_text_font(ctx->prompt_label, prompt_font, 0);
  lv_obj_set_style_text_align(ctx->prompt_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(ctx->prompt_label, LV_PCT(100));
  lv_label_set_long_mode(ctx->prompt_label, LV_LABEL_LONG_DOT);
  lv_obj_align(ctx->prompt_label, LV_ALIGN_TOP_MID, 0, g.prompt_y);

  ctx->dots_row = lv_obj_create(card);
  lv_obj_remove_style_all(ctx->dots_row);
  lv_obj_set_size(ctx->dots_row, LV_PCT(100), g.prompt_h);
  lv_obj_align(ctx->dots_row, LV_ALIGN_TOP_MID, 0, g.dots_y);
  lv_obj_set_flex_flow(ctx->dots_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctx->dots_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctx->dots_row, g.dot, 0);
  lv_obj_remove_flag(ctx->dots_row, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  for (size_t i = 0; i < pin_access::kInputMaxDigits; ++i) {
    lv_obj_t* dot = lv_obj_create(ctx->dots_row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, g.dot, g.dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(ui_theme::icon()), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(ui_theme::text()), 0);
    lv_obj_set_style_border_width(dot, g.dot / 9 > 2 ? g.dot / 9 : 2, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    ctx->dots[i] = dot;
  }

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
  lv_label_set_text(ctx->state_label, init.state.length() ? init.state.c_str() : tr.pin_popup_locked);
  // A code asked from a popup goes back to it (arrow), any other closes (X).
  if (lv_obj_t* glyph = lv_obj_get_child(ctx->close_button, 0)) {
    lv_label_set_text(glyph, getMdiChar(init.back ? "arrow-left" : "window-close").c_str());
  }
  ctx->prompt = init.prompt;
  ctx->error = init.error;
  ctx->dismissed = init.dismissed;
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
}

}  // namespace

void show_pin_popup(const PinPopupInit& init) {
  hide_device_popup();
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
    popup_shell_icon_lock(g_ctx->card, init.lock_mark);
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
  popup_shell_icon_lock(g_ctx->card, init.lock_mark);
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
  g_ctx->dismissed = nullptr;
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
  popup_shell_pulse_icon(g_ctx->card, false);
  clear_input(g_ctx);
  update_value(g_ctx);
  arm_auto_close_timer(g_ctx);
}

void pin_popup_set_error(const char* text, bool show) {
  if (!g_ctx || !g_ctx->card || !text) return;
  g_ctx->error = text;
  g_ctx->show_error = show;
  update_value(g_ctx);
}

bool is_pin_popup_for(const void* context) {
  return context && is_pin_popup_visible() && g_ctx->callback_context == context;
}

void pin_popup_set_state(const String& icon_name, uint32_t icon_color, const String& state) {
  if (!g_ctx || !g_ctx->card || !g_ctx->icon_label || !g_ctx->state_label) return;
  lv_label_set_text(g_ctx->icon_label, popup_icon_glyph(icon_name).c_str());
  lv_obj_set_style_text_color(g_ctx->icon_label, lv_color_hex(icon_color), 0);
  lv_label_set_text(g_ctx->state_label, state.c_str());
  style_keypad(g_ctx);
  sync_popup_shell();
}

void pin_popup_pulse_icon(bool on) {
  if (g_ctx && g_ctx->card) popup_shell_pulse_icon(g_ctx->card, on);
}

bool is_pin_popup_visible() {
  return g_ctx && g_ctx->card &&
         !lv_obj_has_flag(g_ctx->card, LV_OBJ_FLAG_HIDDEN);
}
