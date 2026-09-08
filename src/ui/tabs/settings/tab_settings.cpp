#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/shared/ui_control_style.h"
#include <lvgl.h>
#include <WiFi.h>
#include <cstring>
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/core/config/config_manager.h"
#include "src/core/hardware/board_hal.h"
#include "src/core/power/power_manager.h"
#include "src/network/network_manager.h"
#include "src/network/transport/network_transport.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/ui_manager.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/fonts/ui_fonts.h"
#include "src/core/firmware/firmware_version.h"
#include "src/core/firmware/github_update.h"
#include "src/devices/device.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/clock/clock_format.h"
#include "src/web/setup/web_config.h"
#include "src/ui/shared/ui_keyboard.h"
#include "src/ui/startup/hometiles_logo.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/popups/weather/weather_popup.h"

static lv_obj_t *brightness_label = nullptr;
static lv_obj_t *screensaver_brightness_value_label = nullptr;
static lv_obj_t *display_rotate_btn = nullptr;
static lv_obj_t *display_rotate_label = nullptr;
static lv_obj_t *display_rotate_text_label = nullptr;
static lv_obj_t *display_rotate_sub_label = nullptr;
static bool display_rotated_180 = false;
static uint8_t display_rotation_quarters = Device::kRotationDefault;
static uint8_t display_rotation_mode = kDisplayRotationNormal;
static lv_obj_t *mains_wake_btn = nullptr;
static lv_obj_t *mains_wake_label = nullptr;
static lv_obj_t *mains_wake_sub_label = nullptr;
// Battery wake removed (no battery on Waveshare)
static lv_obj_t *battery_wake_btn = nullptr;
static lv_obj_t *battery_wake_label = nullptr;
static lv_obj_t *battery_wake_sub_label = nullptr;
static uint8_t wake_mode_mains = kWakeModeTouch;
static uint8_t wake_mode_battery = kWakeModeTouch;
static hotspot_callback_t g_hotspot_callback = nullptr;
static wifi_reconnect_callback_t g_wifi_reconnect_callback = nullptr;
static fw_check_callback_t g_fw_check_callback = nullptr;
static fw_install_callback_t g_fw_install_callback = nullptr;
static system_reboot_callback_t g_system_reboot_callback = nullptr;
static wifi_disconnect_callback_t g_wifi_disconnect_callback = nullptr;
static ha_pair_callback_t g_ha_pair_callback = nullptr;

enum class SettingsPopupKind : uint8_t {
  Display,
  Wifi,
  Localization,
  Firmware,
};

static lv_obj_t *settings_tile_display_title = nullptr;
static lv_obj_t *settings_tile_display_summary = nullptr;
static lv_obj_t *settings_tile_wifi_title = nullptr;
static lv_obj_t *settings_tile_wifi_summary = nullptr;
static lv_obj_t *settings_tile_locale_title = nullptr;
static lv_obj_t *settings_tile_locale_summary = nullptr;
static lv_obj_t *settings_tile_firmware_title = nullptr;
static lv_obj_t *settings_tile_firmware_summary = nullptr;

static lv_obj_t *settings_popup_overlay = nullptr;
static lv_obj_t *settings_popup_card = nullptr;
static lv_obj_t *settings_popup_title = nullptr;
static lv_obj_t *settings_popup_content = nullptr;
static lv_obj_t *settings_popup_keyboard = nullptr;
static lv_obj_t *settings_popup_active_ta = nullptr;
// The upper-right close-button icon becomes a back arrow in the Wi-Fi
// entry view; see on_settings_popup_close_clicked.
static lv_obj_t *settings_popup_close_icon = nullptr;
// Reserve space in the content column for the freely positioned keyboard.
// Hide this placeholder in the Wi-Fi list view so the network list can
// use the full height.
static lv_obj_t *settings_popup_kb_spacer = nullptr;
static SettingsPopupKind settings_popup_kind = SettingsPopupKind::Display;

static lv_obj_t *wifi_ssid_ta = nullptr;
static lv_obj_t *wifi_pass_ta = nullptr;
static lv_obj_t *wifi_pass_eye_icon = nullptr;
static lv_obj_t *wifi_list_view = nullptr;
static lv_obj_t *wifi_entry_view = nullptr;
static lv_obj_t *wifi_conn_status_label = nullptr;
static lv_obj_t *wifi_scan_status_label = nullptr;
static lv_obj_t *wifi_list_container = nullptr;
static lv_obj_t *wifi_manual_row = nullptr;
static lv_obj_t *wifi_manual_gap = nullptr;
static lv_obj_t *wifi_ap_qr = nullptr;
static lv_obj_t *wifi_info_box = nullptr;
// AP information uses aligned label/value rows for credentials.
// Size the QR code once to the available space, measured using the
// list-view spacer.
static lv_obj_t *wifi_ap_info_rows = nullptr;
static lv_obj_t *wifi_ap_ssid_val = nullptr;
static lv_obj_t *wifi_ap_pw_val = nullptr;
static lv_obj_t *wifi_ap_ip_val = nullptr;
static lv_obj_t *wifi_list_spacer = nullptr;
static bool wifi_ap_qr_sized = false;
static lv_timer_t *wifi_scan_timer = nullptr;
struct WifiScanEntry { char ssid[33]; int16_t rssi; bool open; };
static WifiScanEntry wifi_scan_results[24];
static size_t wifi_scan_result_count = 0;
static char wifi_selected_ssid[33] = {};
static bool wifi_selected_open = false;
static bool wifi_manual_mode = false;
static char wifi_known_fallback_ssid[33] = {};
// Pause scans briefly after requesting a connection/reconnection;
// a running scan would interfere with WiFi.begin() in the main loop.
static uint32_t wifi_scan_block_until = 0;

static lv_obj_t *locale_language_dd = nullptr;
static lv_obj_t *locale_timezone_dd = nullptr;
static lv_obj_t *locale_time_format_dd = nullptr;
static lv_obj_t *locale_date_format_dd = nullptr;
static lv_obj_t *locale_keyboard_dd = nullptr;

// System popup: version/device, GitHub QR, update check and OTA install
static lv_obj_t *system_info_rows = nullptr;
static lv_obj_t *system_status_label = nullptr;
static lv_obj_t *system_progress_bar = nullptr;
static lv_obj_t *system_check_btn = nullptr;
static lv_obj_t *system_check_btn_label = nullptr;
static lv_obj_t *system_github_btn = nullptr;
static lv_obj_t *system_reboot_btn = nullptr;
static lv_obj_t *system_pair_btn = nullptr;
static lv_obj_t *system_action_row = nullptr;  // Restart and pairing row
static lv_obj_t *system_qr = nullptr;
static lv_obj_t *system_spacer = nullptr;
static bool system_qr_sized = false;
static bool system_check_running = false;
static bool system_install_running = false;
// Deliberately survive the popup so a known update-check result appears
// immediately when reopened, in both the button and status line.
static char system_latest_tag[24] = {};
static bool system_update_available = false;

static const i18n::Strings& tr() {
  return i18n::strings(configManager.getConfig().language);
}

#if LV_USE_QRCODE
static void style_qr_code(lv_obj_t* qr) {
  if (!qr) return;
  lv_obj_set_style_radius(qr, popup_layout::scale(14), 0);
  lv_obj_set_style_clip_corner(qr, true, 0);
  lv_obj_set_style_border_width(qr, 0, 0);
  lv_obj_set_style_bg_color(qr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qr, LV_OPA_COVER, 0);
}
#endif

static lv_obj_t *ap_mode_btn = nullptr;
static lv_obj_t *ap_mode_btn_label = nullptr;
static lv_obj_t *wifi_disconnect_btn = nullptr;
// Network-mode switch, visible only in Ethernet-capable builds
static lv_obj_t *net_mode_btn = nullptr;
static lv_obj_t *net_mode_btn_label = nullptr;
static lv_obj_t *net_mode_hint_label = nullptr;
static lv_obj_t *ip_mode_hint_label = nullptr;
static lv_obj_t *net_mode_row = nullptr;
static lv_obj_t *ethernet_dhcp_btn = nullptr;
static lv_obj_t *ethernet_dhcp_btn_label = nullptr;
static lv_obj_t *ap_confirm_row = nullptr;
static lv_obj_t *ap_confirm_yes_btn = nullptr;
static lv_obj_t *ap_confirm_no_btn = nullptr;
static lv_obj_t *display_section_label = nullptr;
static lv_obj_t *brightness_title_label = nullptr;
static lv_obj_t *screensaver_brightness_title_label = nullptr;
static lv_obj_t *wifi_section_label = nullptr;
static lv_obj_t *ap_yes_label_obj = nullptr;
static lv_obj_t *ap_no_label_obj = nullptr;
static lv_obj_t *sleep_section_label = nullptr;
static lv_timer_t *ap_confirm_timer = nullptr;
// Wi-Fi popup cooldown after toggling AP mode, preventing double taps
static lv_timer_t *ap_btn_cooldown_timer = nullptr;
static bool ap_mode_confirm_pending = false;
static bool ap_mode_active = false;
static uint32_t ap_mode_click_block_until = 0;

// Sleep Settings
static lv_obj_t *sleep_slider = nullptr;
static lv_obj_t *sleep_time_label = nullptr;
static lv_obj_t *sleep_label = nullptr;
static lv_obj_t *screensaver_slider = nullptr;
static lv_obj_t *screensaver_time_label = nullptr;
static lv_obj_t *screensaver_label = nullptr;
static lv_obj_t *screensaver_brightness_slider = nullptr;
static lv_timer_t *screensaver_brightness_preview_timer = nullptr;

// Power Status Labels (stubs -> no battery display)
static lv_obj_t *power_status_label = nullptr;
static lv_obj_t *power_level_label = nullptr;
static lv_obj_t *battery_icon_label = nullptr;
static lv_obj_t *battery_percent_label = nullptr;

// Compact layout constants for the 720x720, 4x4 grid
static const int kSettingsColLeftPct = 15;
static const int kSettingsColRightPct = 85;
static const int kSettingsColGap = popup_layout::scale(8);
static const int kSettingsColRowGap = popup_layout::scale(4);
static const int kSettingsBtnHeight = popup_layout::scale(80);
static const int kSettingsButtonWidthPct = 90;
static const int kSettingsSliderLabelWidth = popup_layout::scale(160);
static const int kSettingsSectionTitlePct = 20;
static const int kSettingsSectionContentPct = 50;
static const int kSettingsSectionActionPct = 30;
static const int kSettingsDisplayValueWidth = popup_layout::scale(56);
static const int kSettingsInlineLabelWidth = popup_layout::scale(98);
static const int kSettingsInlineSliderWidth = popup_layout::scale(116);
static const int kSettingsBrightnessPctMin =
    Device::kConfiguredBrightnessPercentMin;
static const int kSettingsBrightnessPctMax = 100;
static const int kSettingsSliderValueWidth = popup_layout::scale(70);
static const uint8_t kSettingsCardColStart = 1;

// Forward declarations
void settings_update_ap_mode(bool running);
void settings_refresh_language();
static void update_settings_tile_summaries();
static void open_settings_popup(SettingsPopupKind kind);
static void wifi_stop_scan_timer();
static void wifi_show_list_view();
static void wifi_update_conn_status_label();
static void build_localization_popup(lv_obj_t* parent);

static void style_settings_button(lv_obj_t *btn, uint32_t base_color) {
  if (!btn) return;
  uint32_t pressed_color = brighten_rgb_color(base_color, 0x10);
  lv_obj_set_style_anim_duration(btn, 0, 0);
  disable_pressed_button_animation(btn);
  lv_obj_set_style_transform_width(btn, 0, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_transform_height(btn, 0, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_translate_x(btn, 0, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_translate_y(btn, 0, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_bg_color(btn, lv_color_hex(base_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, lv_color_hex(base_color), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(base_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(base_color), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
  lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | (LV_STATE_FOCUSED | LV_STATE_PRESSED));
}
static uint16_t sleep_seconds_from_index(int32_t index) {
  if (index < 0) {
    index = 0;
  } else if (index >= static_cast<int32_t>(kSleepOptionsSecCount)) {
    index = static_cast<int32_t>(kSleepOptionsSecCount) - 1;
  }
  return kSleepOptionsSec[index];
}

static int32_t sleep_index_from_seconds(uint16_t seconds) {
  uint16_t closest = kSleepOptionsSec[0];
  int32_t closest_index = 0;
  uint16_t best_diff = (seconds > closest) ? (seconds - closest) : (closest - seconds);
  for (size_t i = 1; i < kSleepOptionsSecCount; ++i) {
    uint16_t option = kSleepOptionsSec[i];
    uint16_t diff = (seconds > option) ? (seconds - option) : (option - seconds);
    if (diff < best_diff) {
      best_diff = diff;
      closest = option;
      closest_index = static_cast<int32_t>(i);
    }
  }
  return closest_index;
}

static void format_sleep_label(char* buf, size_t len, uint16_t seconds) {
  if (seconds <= 60) {
    snprintf(buf, len, "%u s", static_cast<unsigned>(seconds));
  } else {
    snprintf(buf, len, "%u min", static_cast<unsigned>(seconds / 60));
  }
}

static int32_t sleep_slider_max_index() {
  return static_cast<int32_t>(kSleepOptionsSecCount);
}

static bool sleep_index_is_never(int32_t index) {
  return index >= static_cast<int32_t>(kSleepOptionsSecCount);
}

static void format_sleep_popup_value_for_index(char* buf, size_t len, int32_t index) {
  if (sleep_index_is_never(index)) {
    snprintf(buf, len, "%s", tr().sleep_never);
    return;
  }
  char value[16];
  format_sleep_label(value, sizeof(value), sleep_seconds_from_index(index));
  snprintf(buf, len, "%s %s", tr().sleep_after, value);
}

static void update_display_rotate_label() {
  if (!display_rotate_label) return;
  String icon_char = getMdiChar(String("phone-rotate-landscape"));
  lv_label_set_text(display_rotate_label, icon_char.c_str());
  if (display_rotate_sub_label) {
    static char buf[24];
    snprintf(buf, sizeof(buf), "%u%c", static_cast<unsigned>(display_rotation_quarters * 90), 176);
    lv_label_set_text(display_rotate_sub_label, buf);
  }
}

static void sync_display_rotation_state(uint8_t rotation_quarters) {
  display_rotation_quarters = Device::normalizeRotationQuarterTurns(rotation_quarters);
  display_rotated_180 = (display_rotation_quarters == Device::kRotationFlipped);
  display_rotation_mode = display_rotated_180 ? kDisplayRotationFlipped : kDisplayRotationNormal;
  configManager.setRuntimeDisplayRotationQuarters(display_rotation_quarters);
}

static int brightness_pct_from_raw(int raw) {
  if (raw < 0) raw = 0;
  if (raw > 255) raw = 255;
  return Device::backlightPercentFromRaw(static_cast<uint8_t>(raw));
}

static uint8_t brightness_raw_from_pct(int pct) {
  if (pct < kSettingsBrightnessPctMin) pct = kSettingsBrightnessPctMin;
  if (pct > kSettingsBrightnessPctMax) pct = kSettingsBrightnessPctMax;
  return Device::backlightRawFromPercent(static_cast<uint8_t>(pct));
}

static void restore_normal_brightness_after_preview() {
  // A real screensaver owns the backlight until its exit frame has been
  // presented. The settings preview only runs over the normal UI.
  if (is_image_screensaver_visible()) return;
  powerManager.setDisplayBrightness(
      configManager.getConfig().display_brightness);
}

static void on_screensaver_brightness_preview_timeout(lv_timer_t* timer) {
  if (timer != screensaver_brightness_preview_timer) return;
  screensaver_brightness_preview_timer = nullptr;
  restore_normal_brightness_after_preview();
}

static void schedule_screensaver_brightness_preview_restore() {
  constexpr uint32_t kPreviewDurationMs = 1000;
  if (!screensaver_brightness_preview_timer) {
    screensaver_brightness_preview_timer = lv_timer_create(
        on_screensaver_brightness_preview_timeout, kPreviewDurationMs,
        nullptr);
    if (screensaver_brightness_preview_timer) {
      lv_timer_set_repeat_count(screensaver_brightness_preview_timer, 1);
    }
    return;
  }
  lv_timer_set_period(screensaver_brightness_preview_timer,
                      kPreviewDurationMs);
  lv_timer_reset(screensaver_brightness_preview_timer);
}

static void cancel_screensaver_brightness_preview() {
  if (!screensaver_brightness_preview_timer) return;
  lv_timer_delete(screensaver_brightness_preview_timer);
  screensaver_brightness_preview_timer = nullptr;
}

static const char* wake_mode_text(uint8_t mode) {
  (void)mode;
  return tr().touch_label;
}

static void update_wake_button(lv_obj_t *main_label, lv_obj_t *sub_label, uint8_t mode) {
  (void)mode;
  if (!main_label) return;
  wake_mode_mains = kWakeModeTouch;
  wake_mode_battery = kWakeModeTouch;
  lv_label_set_text(main_label, wake_mode_text(kWakeModeTouch));
  if (sub_label) {
    lv_label_set_text(sub_label, tr().no_imu_hint);
  }
}

void settings_sync_display_rotation(bool rotated) {
  sync_display_rotation_state(rotated ? Device::kRotationFlipped : Device::kRotationDefault);
  displayManager.setRotation(display_rotation_quarters);
  update_display_rotate_label();
  lv_obj_invalidate(lv_scr_act());
  lv_display_t* disp = lv_display_get_default();
  if (disp) {
    lv_refr_now(disp);
  }
  update_settings_tile_summaries();
}

static void on_display_rotate_clicked(lv_event_t *e) {
  (void)e;
  uint8_t next_rotation = displayManager.getRotation();
  if (Device::supportsQuarterTurnRotation()) {
    next_rotation = (next_rotation + 1) & 0x03;
  } else {
    next_rotation = (next_rotation == Device::kRotationFlipped)
                        ? Device::kRotationDefault
                        : Device::kRotationFlipped;
  }
  displayManager.setRotation(next_rotation);
  sync_display_rotation_state(next_rotation);
  const DeviceConfig& cfg = configManager.getConfig();
  configManager.saveDisplaySettings(
      cfg.display_brightness,
      cfg.auto_sleep_enabled,
      cfg.auto_sleep_seconds,
      cfg.auto_sleep_battery_enabled,
      cfg.auto_sleep_battery_seconds,
      display_rotation_mode,
      display_rotated_180,
      display_rotation_quarters,
      wake_mode_mains,
      wake_mode_battery);
  mqttPublishDeviceSettings();
  update_display_rotate_label();
  lv_obj_invalidate(lv_scr_act());
  lv_display_t* disp = lv_display_get_default();
  if (disp) {
    lv_refr_now(disp);
  }
}

static void on_mains_wake_clicked(lv_event_t *e) {
  (void)e;
  update_wake_button(mains_wake_label, mains_wake_sub_label, kWakeModeTouch);
}

static void on_brightness(lv_event_t *e) {
  lv_obj_t *slider = (lv_obj_t*)lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  int32_t pct = lv_slider_get_value(slider);
  uint8_t raw = brightness_raw_from_pct(pct);

  BoardHAL::setBrightness(raw);

  static char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", (int)pct);
  if (brightness_label) lv_label_set_text(brightness_label, buf);

  if (code == LV_EVENT_RELEASED) {
    const DeviceConfig& cfg = configManager.getConfig();
    configManager.saveDisplaySettings(
        raw,
        cfg.auto_sleep_enabled,
        cfg.auto_sleep_seconds,
        cfg.auto_sleep_battery_enabled,
        cfg.auto_sleep_battery_seconds,
        display_rotation_mode,
        display_rotated_180,
        display_rotation_quarters,
        wake_mode_mains,
        wake_mode_battery);
    mqttPublishDeviceSettings();
    update_settings_tile_summaries();
  }
}

static void on_screensaver_brightness(lv_event_t *e) {
  lv_obj_t *slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const lv_event_code_t code = lv_event_get_code(e);
  int32_t pct = lv_slider_get_value(slider);
  if (pct < Device::kConfiguredBrightnessPercentMin) {
    pct = Device::kConfiguredBrightnessPercentMin;
  }
  if (pct > kScreensaverBrightnessPctMax) pct = kScreensaverBrightnessPctMax;

  static char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(pct));
  if (screensaver_brightness_value_label) {
    lv_label_set_text(screensaver_brightness_value_label, buf);
  }

  if (code == LV_EVENT_PRESSED || code == LV_EVENT_VALUE_CHANGED ||
      code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    // A simple tap or drag at the 1%/100% endpoint must also preview the
    // selected level; LVGL sends no VALUE_CHANGED there.
    powerManager.setDisplayBrightness(
        Device::backlightRawFromPercent(static_cast<uint8_t>(pct)));
    // Every movement extends the preview. One second after the final event,
    // the normal UI returns to its configured brightness; the persisted
    // screensaver value stays unchanged.
    schedule_screensaver_brightness_preview_restore();
  }

  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    // On touch devices, PRESS_LOST is a normal drag end when the finger is
    // released outside the slider. Commit the visible final value just as
    // for RELEASED so the displayed and persisted values remain consistent.
    if (configManager.saveScreensaverBrightness(static_cast<uint8_t>(pct))) {
      image_screensaver_brightness_changed();
      mqttPublishDeviceSettings();
    }
  }
}

static lv_obj_t *create_settings_column(lv_obj_t *parent, lv_coord_t width_pct,
                                        lv_flex_align_t main_align, lv_flex_align_t cross_align) {
  lv_obj_t *col = lv_obj_create(parent);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(col, LV_PCT(width_pct));
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, kSettingsColRowGap, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, main_align, cross_align, cross_align);
  return col;
}

static lv_obj_t* create_icon_block(lv_obj_t *parent, const char *icon_name, const char *label_text) {
  lv_obj_t *icon = lv_label_create(parent);
  String icon_char = getMdiChar(String(icon_name));
  lv_label_set_text(icon, icon_char.c_str());
  if (FONT_MDI_ICONS) {
    lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  }
  popup_layout::applyIconScale(icon);
  lv_obj_set_width(icon, LV_PCT(100));
  lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);

  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, label_text);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_margin_top(label, 4, 0);
  return label;
}

static void on_settings_back_clicked(lv_event_t *e) {
  (void)e;
  uiManager.switchToTab(0);
}

static void create_settings_back_button(lv_obj_t *parent) {
  lv_obj_t *btn = lv_button_create(parent);
  // Keep the navigation controls visually aligned with the regular tiles.
  // The 480x480 layout is a strict 2/3 scale of the 720x720 geometry.
  lv_obj_set_style_radius(btn, popup_layout::scale480(22), 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_cell(btn,
      LV_GRID_ALIGN_STRETCH, 0, 1,
      LV_GRID_ALIGN_STRETCH, 0, 1);

  uint32_t btn_color = 0x2A2A2A;
  style_settings_button(btn, btn_color);
  ui_surface_style::apply_global_tile_border(btn);

  lv_obj_add_event_cb(btn, on_settings_back_clicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *icon = lv_label_create(btn);
  String icon_char = getMdiChar(String("arrow-left"));
  lv_label_set_text(icon, icon_char.c_str());
  if (FONT_MDI_ICONS) {
    lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  }
  popup_layout::applyIconScale(icon);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_obj_center(icon);
}

static void on_sleep_slider(lv_event_t *e) {
  lv_obj_t *slider = (lv_obj_t*)lv_event_get_target(e);
  lv_event_code_t code = lv_event_get_code(e);
  int32_t index = lv_slider_get_value(slider);

  static char buf[32];
  format_sleep_popup_value_for_index(buf, sizeof(buf), index);
  if (sleep_time_label) lv_label_set_text(sleep_time_label, buf);

  if (code == LV_EVENT_RELEASED) {
    const DeviceConfig& cfg = configManager.getConfig();
    bool enabled = !sleep_index_is_never(index);
    uint16_t seconds = enabled ? sleep_seconds_from_index(index) : cfg.auto_sleep_seconds;
    configManager.saveDisplaySettings(
        cfg.display_brightness,
        enabled,
        seconds,
        cfg.auto_sleep_battery_enabled,
        cfg.auto_sleep_battery_seconds,
        display_rotation_mode,
        display_rotated_180,
        display_rotation_quarters,
        wake_mode_mains,
        wake_mode_battery);
    mqttPublishDeviceSettings();
    update_settings_tile_summaries();
  }
}

static void on_screensaver_slider(lv_event_t *e) {
  lv_obj_t *slider = (lv_obj_t*)lv_event_get_target(e);
  const lv_event_code_t code = lv_event_get_code(e);
  const int32_t index = lv_slider_get_value(slider);

  static char buf[32];
  format_sleep_popup_value_for_index(buf, sizeof(buf), index);
  if (screensaver_time_label) lv_label_set_text(screensaver_time_label, buf);

  if (code == LV_EVENT_RELEASED) {
    const DeviceConfig& cfg = configManager.getConfig();
    const bool enabled = !sleep_index_is_never(index);
    const uint16_t seconds = enabled
                                 ? sleep_seconds_from_index(index)
                                 : cfg.auto_screensaver_seconds;
    configManager.saveScreensaverTimeout(enabled, seconds);
    update_settings_tile_summaries();
  }
}

// Power Status Update (stub -> no battery on Waveshare)
void settings_update_power_status() {
  // No battery to monitor -> nothing to update
}

static void clear_ap_confirm_timer() {
  if (ap_confirm_timer) {
    lv_timer_del(ap_confirm_timer);
    ap_confirm_timer = nullptr;
  }
}

static void hide_ap_confirm_row() {
  if (ap_confirm_row) {
    lv_obj_add_flag(ap_confirm_row, LV_OBJ_FLAG_HIDDEN);
  }
  clear_ap_confirm_timer();
  ap_mode_confirm_pending = false;
  if (ap_mode_btn) {
    lv_obj_clear_flag(ap_mode_btn, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_ap_confirm_timeout(lv_timer_t *timer) {
  if (timer) {
    lv_timer_del(timer);
  }
  ap_confirm_timer = nullptr;
  hide_ap_confirm_row();
}

static void on_confirm_yes_clicked(lv_event_t *e) {
  if (g_hotspot_callback) {
    g_hotspot_callback(true);
  }
  settings_update_ap_mode(true);
  ap_mode_click_block_until = millis() + 400;
  hide_ap_confirm_row();
}

static void on_confirm_no_clicked(lv_event_t *e) {
  ap_mode_click_block_until = millis() + 400;
  hide_ap_confirm_row();
}

static void on_ap_mode_clicked(lv_event_t *e) {
  if (ap_mode_click_block_until != 0 &&
      (int32_t)(millis() - ap_mode_click_block_until) < 0) {
    return;
  }
  lv_obj_t *btn = (lv_obj_t*)lv_event_get_current_target(e);
  if (btn) {
    ap_mode_btn = btn;
    ap_mode_btn_label = lv_obj_get_child(btn, 0);
  }
  if (ap_mode_active) {
    if (g_hotspot_callback) {
      g_hotspot_callback(false);
    }
    return;
  }
  if (ap_confirm_row && !ap_mode_confirm_pending) {
    ap_mode_confirm_pending = true;
    lv_obj_clear_flag(ap_confirm_row, LV_OBJ_FLAG_HIDDEN);
    clear_ap_confirm_timer();
    ap_confirm_timer = lv_timer_create(on_ap_confirm_timeout, 10000, nullptr);
    if (btn) {
      lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (ap_mode_btn) {
      lv_obj_add_flag(ap_mode_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static lv_obj_t *create_settings_card(lv_obj_t *parent, uint8_t col, uint8_t row) {
  lv_obj_t *card = lv_obj_create(parent);
  uint8_t span = (col < GRID_COLS) ? (GRID_COLS - col) : 1;
  lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_STRETCH, row, 1);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_border_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(card, popup_layout::scale(22), 0);
  lv_obj_set_style_pad_hor(card, popup_layout::scale(12), 0);
  lv_obj_set_style_pad_ver(card, popup_layout::scale(10), 0);
  lv_obj_set_style_pad_row(card, popup_layout::scale(4), 0);
  lv_obj_set_style_pad_column(card, kSettingsColGap, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return card;
}

static lv_obj_t *create_card_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, popup_layout::scale(8), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

static lv_obj_t *create_slider_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, popup_layout::scale(2), 0);
  lv_obj_set_style_pad_right(row, popup_layout::scale(12), 0);
  lv_obj_set_style_pad_column(row, popup_layout::scale(8), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

static void style_settings_slider(lv_obj_t* slider) {
  ui_control_style::slider(slider);
}

// Timezone codes and display names come from the central i18n catalog
// (i18n::timezone_option and LocaleProfile::timezone_labels), keeping
// the device and Web Admin lists identical.
static String settings_timezone_options_text;

static uint32_t settings_timezone_index(const char* code) {
  const char* selected = (code && code[0]) ? code : "berlin";
  for (uint32_t i = 0; i < i18n::kTimezoneOptionCount; ++i) {
    if (strcmp(i18n::timezone_option(i).code, selected) == 0) return i;
  }
  return 2;  // berlin
}

static const char* selected_timezone_code(uint32_t index) {
  if (index >= i18n::kTimezoneOptionCount) index = 2;  // berlin
  return i18n::timezone_option(index).code;
}

static void build_timezone_dropdown_options() {
  settings_timezone_options_text = "";
  settings_timezone_options_text.reserve(900);
  const auto& profile = i18n::locale(configManager.getConfig().language);
  for (uint32_t i = 0; i < i18n::kTimezoneOptionCount; ++i) {
    if (i > 0) settings_timezone_options_text += '\n';
    settings_timezone_options_text += profile.timezone_labels[i];
  }
}

static void style_plain_container(lv_obj_t* obj) {
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
  // border_opa=TRANSP only hides a border. LVGL still subtracts its width
  // from lv_obj_get_content_width/height; see lv_obj_get_style_space_left/right
  // in lv_obj_style.h. Without zeroing the width, nested content such as
  // settings_popup_content -> spacer becomes narrower than its parent.
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
}

static void style_popup_textarea(lv_obj_t* ta) {
  lv_obj_set_height(ta, popup_layout::scale(48));
  lv_textarea_set_one_line(ta, true);
  lv_obj_set_style_text_font(ta, popup_layout::font20(), 0);
  lv_obj_set_style_bg_color(ta, lv_color_hex(0x1E1E1E), 0);
  lv_obj_set_style_text_color(ta, lv_color_white(), 0);
  lv_obj_set_style_radius(ta, popup_layout::scale(10), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_border_opa(ta, LV_OPA_COVER, 0);
  // Focus changes only color and an externally drawn outline. Keep border
  // width fixed at 1 px; changing it makes LV_SIZE_CONTENT fields grow by
  // 2 px on focus and pushes everything below downward.
  lv_obj_set_style_border_color(ta, lv_color_hex(0x26A69A), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(ta, 3, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_pad(ta, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_color(ta, lv_color_hex(0x26A69A), LV_STATE_FOCUSED);
  lv_obj_set_style_outline_opa(ta, LV_OPA_40, LV_STATE_FOCUSED);
  lv_obj_set_style_border_color(ta, lv_color_white(), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(ta, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
}

static void style_popup_dropdown(lv_obj_t* dd) {
  ui_control_style::dropdown(dd);
}

static void style_popup_dropdown_list(lv_obj_t* list) {
  ui_control_style::dropdownList(list);
}

static lv_obj_t* create_popup_button(lv_obj_t* parent, const char* text, uint32_t color,
                                     lv_event_cb_t cb) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_height(btn, popup_layout::scale(64));
  style_settings_button(btn, color);
  lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(btn, popup_layout::scale(20), 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);
  return btn;
}

static lv_obj_t* create_form_area(lv_obj_t* parent) {
  lv_obj_t* form = lv_obj_create(parent);
  lv_obj_set_width(form, LV_PCT(100));
  lv_obj_set_flex_grow(form, 1);
  lv_obj_set_style_bg_opa(form, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(form, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(form, 0, 0);
  lv_obj_set_style_pad_row(form, popup_layout::scale(8), 0);
  lv_obj_set_scrollbar_mode(form, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(form, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return form;
}

static lv_obj_t* create_form_row(lv_obj_t* parent) {
  lv_obj_t* row = lv_obj_create(parent);
  style_plain_container(row);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, popup_layout::scale(76));
  lv_obj_set_style_pad_column(row, popup_layout::scale(10), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}

static lv_obj_t* create_display_control_row(lv_obj_t* parent) {
  lv_obj_t* row = create_form_row(parent);
  lv_obj_set_height(row, popup_layout::scale(84));
  lv_obj_set_style_pad_column(row, popup_layout::scale(18), 0);
  return row;
}

// Use the same white color and 24 px size for labels and values.
// Muted gray labels beside white values looked like a separate
// hierarchy level in user feedback.
static lv_obj_t* create_display_row_label(lv_obj_t* parent, const char* text,
                                          lv_coord_t width,
                                          lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  return label;
}

static lv_obj_t* create_flex_spacer(lv_obj_t* parent) {
  lv_obj_t* spacer = lv_obj_create(parent);
  style_plain_container(spacer);
  lv_obj_set_width(spacer, 1);
  lv_obj_set_height(spacer, 1);
  lv_obj_set_flex_grow(spacer, 1);
  return spacer;
}

// Show the keyboard when a field is tapped instead of keeping it visible.
// Numeric/IP fields use LVGL's built-in number pad rather than the full
// QWERTZ keyboard.
static void on_popup_textarea_focused(lv_event_t* e) {
  lv_obj_t* ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!settings_popup_keyboard || !ta) return;
  const bool numeric = lv_event_get_user_data(e) != nullptr;
  lv_keyboard_set_mode(settings_popup_keyboard,
                       numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
  ui_keyboard_set_target(settings_popup_keyboard, ta, settings_popup_active_ta);
  settings_popup_active_ta = ta;
  lv_obj_clear_flag(settings_popup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// The Wi-Fi entry view includes the keyboard as a permanent part of the
// screen, with the connect button directly above. Neither the collapse
// key nor a tap outside a field may hide it there.
static bool wifi_keyboard_locked() {
  return settings_popup_kind == SettingsPopupKind::Wifi && wifi_entry_view &&
         !lv_obj_has_flag(wifi_entry_view, LV_OBJ_FLAG_HIDDEN);
}

// Tapping outside a field, including a checkbox/save/close button that
// is click-focusable by default, sends DEFOCUSED before LVGL processes
// the click. Hide the keyboard here so every button need not do so
// separately.
static void on_popup_textarea_defocused(lv_event_t*) {
  if (wifi_keyboard_locked()) return;
  if (settings_popup_keyboard) lv_obj_add_flag(settings_popup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void reset_popup_refs() {
  settings_popup_card = nullptr;
  settings_popup_title = nullptr;
  settings_popup_content = nullptr;
  settings_popup_keyboard = nullptr;
  settings_popup_active_ta = nullptr;
  settings_popup_close_icon = nullptr;
  settings_popup_kb_spacer = nullptr;

  brightness_label = nullptr;
  screensaver_brightness_value_label = nullptr;
  // Set by build_display_popup; clear it here to prevent a dangling pointer
  // after the display popup closes. Previously, settings_refresh_language()
  // after a language change called lv_label_set_text on freed memory:
  // a store access fault in lv_label_revert_dots (Tab5 crash, 2026-07-05).
  brightness_title_label = nullptr;
  screensaver_brightness_title_label = nullptr;
  display_rotate_btn = nullptr;
  display_rotate_label = nullptr;
  display_rotate_text_label = nullptr;
  display_rotate_sub_label = nullptr;
  sleep_slider = nullptr;
  sleep_time_label = nullptr;
  sleep_label = nullptr;
  screensaver_slider = nullptr;
  screensaver_time_label = nullptr;
  screensaver_label = nullptr;
  screensaver_brightness_slider = nullptr;
  ap_mode_btn = nullptr;
  ap_mode_btn_label = nullptr;
  wifi_disconnect_btn = nullptr;
  net_mode_btn = nullptr;
  net_mode_btn_label = nullptr;
  net_mode_hint_label = nullptr;
  ip_mode_hint_label = nullptr;
  net_mode_row = nullptr;
  ethernet_dhcp_btn = nullptr;
  ethernet_dhcp_btn_label = nullptr;

  wifi_ssid_ta = nullptr;
  wifi_pass_ta = nullptr;
  wifi_pass_eye_icon = nullptr;
  wifi_list_view = nullptr;
  wifi_entry_view = nullptr;
  wifi_conn_status_label = nullptr;
  wifi_scan_status_label = nullptr;
  wifi_list_container = nullptr;
  wifi_manual_row = nullptr;
  wifi_manual_gap = nullptr;
  wifi_ap_qr = nullptr;
  wifi_info_box = nullptr;
  wifi_ap_info_rows = nullptr;
  wifi_ap_ssid_val = nullptr;
  wifi_ap_pw_val = nullptr;
  wifi_ap_ip_val = nullptr;
  wifi_list_spacer = nullptr;
  wifi_ap_qr_sized = false;
  wifi_manual_mode = false;

  locale_language_dd = nullptr;
  locale_timezone_dd = nullptr;
  locale_time_format_dd = nullptr;
  locale_date_format_dd = nullptr;
  locale_keyboard_dd = nullptr;

  system_info_rows = nullptr;
  system_status_label = nullptr;
  system_progress_bar = nullptr;
  system_check_btn = nullptr;
  system_check_btn_label = nullptr;
  system_github_btn = nullptr;
  system_reboot_btn = nullptr;
  system_pair_btn = nullptr;
  system_action_row = nullptr;
  system_qr = nullptr;
  system_spacer = nullptr;
  system_qr_sized = false;
  system_check_running = false;
  system_install_running = false;
  // Deliberately retain system_latest_tag/system_update_available.
}

static void close_settings_popup() {
  hide_popup_shell(settings_popup_card);
  cancel_popup_open(settings_popup_card);
  wifi_stop_scan_timer();
  if (ap_btn_cooldown_timer) {
    lv_timer_del(ap_btn_cooldown_timer);
    ap_btn_cooldown_timer = nullptr;
  }
  if (networkTransport.isWifiDriverActive()) WiFi.scanDelete();
  // If the screensaver brightness control closes during a preview,
  // always restore normal brightness.
  if (settings_popup_kind == SettingsPopupKind::Display) {
    cancel_screensaver_brightness_preview();
    restore_normal_brightness_after_preview();
  }
  if (settings_popup_overlay) {
    lv_obj_del(settings_popup_overlay);
  }
  settings_popup_overlay = nullptr;
  reset_popup_refs();
}

void hide_settings_popup() {
  if (settings_popup_overlay) close_settings_popup();
}

static void on_settings_popup_close_clicked(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  // In the Wi-Fi entry view, the header button is a back arrow, replaced
  // by wifi_show_entry_view, and returns to the list. Only from the list
  // does it actually close the popup.
  if (settings_popup_kind == SettingsPopupKind::Wifi) {
    const bool entry_visible =
        wifi_entry_view &&
        !lv_obj_has_flag(wifi_entry_view, LV_OBJ_FLAG_HIDDEN);
    if (entry_visible) {
      wifi_show_list_view();
      return;
    }
  }
  close_settings_popup();
}

// Wi-Fi list/entry state machine ported from the formerly unused
// wifi_setup_popup.cpp. Scan/list/connect behavior is preserved, but it
// uses the repaired create_popup_keyboard helper instead of a separate
// keyboard instance with the old border problems.
static void wifi_show_entry_view(bool manual, const char* ssid, bool open_network);
static void wifi_show_list_view();

static void wifi_stop_scan_timer() {
  if (wifi_scan_timer) {
    lv_timer_del(wifi_scan_timer);
    wifi_scan_timer = nullptr;
  }
}

// Show the searching hint only while the list is entirely empty, such as
// the first scan with no known network. Once rows exist, further scans
// run without a loading hint. Never clear the container before scanning;
// see wifi_on_scan_timer. This avoids briefly flashing an empty list.
static bool wifi_list_is_empty() {
  return !wifi_list_container || lv_obj_get_child_count(wifi_list_container) == 0;
}

static void wifi_show_scanning_state() {
  if (!wifi_scan_status_label) return;
  if (wifi_list_is_empty()) {
    lv_label_set_text(wifi_scan_status_label, tr().wifi_scan_searching);
    lv_obj_clear_flag(wifi_scan_status_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(wifi_scan_status_label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void wifi_show_scan_finished_state() {
  if (!wifi_scan_status_label) return;
  if (wifi_list_is_empty()) {
    lv_label_set_text(wifi_scan_status_label, tr().wifi_scan_none);
    lv_obj_clear_flag(wifi_scan_status_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(wifi_scan_status_label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_wifi_manual_clicked(lv_event_t*) {
  wifi_show_entry_view(true, nullptr, false);
}

static void on_wifi_network_row_clicked(lv_event_t* e) {
  const size_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (index >= wifi_scan_result_count) return;
  wifi_show_entry_view(false, wifi_scan_results[index].ssid, wifi_scan_results[index].open);
}

static void on_wifi_known_row_clicked(lv_event_t*) {
  wifi_show_entry_view(false, wifi_known_fallback_ssid, false);
}

static lv_obj_t* wifi_create_row(lv_obj_t* parent, const char* name_text, bool show_check,
                                 bool show_lock) {
  lv_obj_t* row = lv_button_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, popup_layout::scale(72));
  style_settings_button(row, 0x3A3A3A);
  lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(row, popup_layout::scale(20), 0);
  lv_obj_set_style_pad_hor(row, popup_layout::scale(20), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, popup_layout::scale(12), 0);

  if (show_check) {
    lv_obj_t* check = lv_label_create(row);
    lv_label_set_text(check, getMdiChar("check").c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(check, FONT_MDI_ICONS, 0);
    popup_layout::applyIconScale(check);
    lv_obj_set_style_text_color(check, lv_color_hex(0x26A69A), 0);
  }

  lv_obj_t* name = lv_label_create(row);
  lv_label_set_text(name, name_text);
  lv_obj_set_style_text_font(name, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  lv_obj_set_flex_grow(name, 1);
  lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);

  if (show_lock) {
    lv_obj_t* lock = lv_label_create(row);
    lv_label_set_text(lock, getMdiChar("lock").c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(lock, FONT_MDI_ICONS, 0);
    popup_layout::applyIconScale(lock);
    lv_obj_set_style_text_color(lock, lv_color_hex(0x888888), 0);
  }
  return row;
}

// Always show the known/connected network, even before a scan finds it.
// WiFi.scanNetworks() does not list hidden SSIDs by default; otherwise
// a manually connected network would disappear at the next scan.
static void wifi_populate_list() {
  if (!wifi_list_container) return;
  lv_obj_clean(wifi_list_container);

  const bool sta_connected =
      !ap_mode_active &&
      networkTransport.activeKind() == NetworkTransportKind::Wifi &&
      networkTransport.isWifiConnected();
  const String current_ssid =
      String(configManager.getConfig().wifi_ssid);

  bool current_in_results = false;
  for (size_t i = 0; i < wifi_scan_result_count; ++i) {
    const bool is_current = current_ssid.length() && current_ssid == wifi_scan_results[i].ssid;
    if (is_current) current_in_results = true;
    lv_obj_t* row = wifi_create_row(wifi_list_container, wifi_scan_results[i].ssid,
                                    is_current && sta_connected, !wifi_scan_results[i].open);
    lv_obj_add_event_cb(row, on_wifi_network_row_clicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }

  if (current_ssid.length() && !current_in_results) {
    strncpy(wifi_known_fallback_ssid, current_ssid.c_str(), sizeof(wifi_known_fallback_ssid) - 1);
    wifi_known_fallback_ssid[sizeof(wifi_known_fallback_ssid) - 1] = '\0';
    lv_obj_t* row = wifi_create_row(wifi_list_container, wifi_known_fallback_ssid,
                                    sta_connected, true);
    lv_obj_add_event_cb(row, on_wifi_known_row_clicked, LV_EVENT_CLICKED, nullptr);
  }
}

static void wifi_on_scan_timer(lv_timer_t*) {
  int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  wifi_stop_scan_timer();

  wifi_scan_result_count = 0;
  for (int16_t i = 0; i < n && wifi_scan_result_count < 24; ++i) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    const int16_t rssi = static_cast<int16_t>(WiFi.RSSI(i));
    const bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    bool duplicate = false;
    for (size_t k = 0; k < wifi_scan_result_count; ++k) {
      if (strcmp(wifi_scan_results[k].ssid, ssid.c_str()) == 0) {
        duplicate = true;
        if (rssi > wifi_scan_results[k].rssi) {
          wifi_scan_results[k].rssi = rssi;
          wifi_scan_results[k].open = open;
        }
        break;
      }
    }
    if (duplicate) continue;

    WifiScanEntry& entry = wifi_scan_results[wifi_scan_result_count++];
    strncpy(entry.ssid, ssid.c_str(), sizeof(entry.ssid) - 1);
    entry.ssid[sizeof(entry.ssid) - 1] = '\0';
    entry.rssi = rssi;
    entry.open = open;
  }
  WiFi.scanDelete();

  // Sort by signal strength.
  for (size_t i = 1; i < wifi_scan_result_count; ++i) {
    WifiScanEntry key = wifi_scan_results[i];
    size_t j = i;
    while (j > 0 && wifi_scan_results[j - 1].rssi < key.rssi) {
      wifi_scan_results[j] = wifi_scan_results[j - 1];
      --j;
    }
    wifi_scan_results[j] = key;
  }

  wifi_populate_list();
  wifi_show_scan_finished_state();
}

// Retain old results, including the count, until the next scan completes;
// see wifi_show_scanning_state. This avoids briefly flashing an empty
// list between scans.
static void wifi_start_scan() {
  wifi_stop_scan_timer();
  // Populate the list first so the known network appears immediately,
  // then decide whether the searching hint is needed.
  wifi_populate_list();
  // Do not scan in AP mode. WiFi.scanNetworks() also enables STA, and a
  // still-running scan can later disrupt WiFi.begin() or portal operation.
  // This previously prevented automatic connection after stopping the AP.
  if (ap_mode_active) {
    wifi_show_scan_finished_state();
    return;
  }
  // Ethernet and STA WiFi are exclusive. Merely opening the WiFi popup must
  // not restart the hosted P4 WiFi driver behind the transport manager.
  if (!networkTransport.isWifiDriverActive()) {
    wifi_show_scan_finished_state();
    return;
  }
  // A newly requested connection/reconnection must not compete with a scan.
  if (wifi_scan_block_until != 0 &&
      (int32_t)(millis() - wifi_scan_block_until) < 0) {
    wifi_show_scan_finished_state();
    return;
  }
  // scanNetworks() performs multiple ESP-Hosted RPCs internally:
  // GetMode/GetMode/SetMode/ScanStart. If C6 is already stuck, these can
  // block for about 5 s each. Probe the channel with one real mode RPC
  // first, triggering central recovery on its timeout.
  if (!networkManager.probeWifiDriverHealth("WLAN-Scan in Settings")) {
    wifi_show_scan_finished_state();
    return;
  }
  wifi_show_scanning_state();
  WiFi.scanDelete();
  const int16_t r = WiFi.scanNetworks(/*async=*/true);
  if (r == WIFI_SCAN_FAILED) {
    wifi_show_scan_finished_state();
    return;
  }
  wifi_scan_timer = lv_timer_create(wifi_on_scan_timer, 500, nullptr);
}

static void wifi_update_conn_status_label() {
  if (!wifi_conn_status_label) return;
  static char buf[96];

  // Rebuild rows when connection state changes, such as after a live
  // connect request. Scans are paused while connecting, so without this
  // refresh the newly connected network would lack its green check mark.
  const bool sta_connected =
      !ap_mode_active &&
      networkTransport.activeKind() == NetworkTransportKind::Wifi &&
      networkTransport.isWifiConnected();
  static bool prev_sta_connected = false;
  if (sta_connected != prev_sta_connected) {
    prev_sta_connected = sta_connected;
    if (wifi_list_container) wifi_populate_list();
  }

  // Show disconnect only while a STA connection exists. In AP mode or
  // offline, the AP button occupies the full row.
  if (wifi_disconnect_btn) {
    if (sta_connected) lv_obj_clear_flag(wifi_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(wifi_disconnect_btn, LV_OBJ_FLAG_HIDDEN);
  }

  if (wifi_list_spacer) {
    lv_obj_clear_flag(wifi_list_spacer, LV_OBJ_FLAG_HIDDEN);
  }
  if (wifi_info_box) lv_obj_clear_flag(wifi_info_box, LV_OBJ_FLAG_HIDDEN);

  // In AP mode, replace the unused scan/network list with the large
  // credentials/QR information box. Scans are disabled during portal
  // operation; see wifi_start_scan.
  const bool ap = ap_mode_active;
  if (wifi_list_container) {
    if (ap) lv_obj_add_flag(wifi_list_container, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(wifi_list_container, LV_OBJ_FLAG_HIDDEN);
  }
  if (wifi_manual_gap) {
    if (ap) lv_obj_add_flag(wifi_manual_gap, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(wifi_manual_gap, LV_OBJ_FLAG_HIDDEN);
  }
  if (wifi_manual_row) {
    if (ap) lv_obj_add_flag(wifi_manual_row, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(wifi_manual_row, LV_OBJ_FLAG_HIDDEN);
  }
  if (ap && wifi_scan_status_label) lv_obj_add_flag(wifi_scan_status_label, LV_OBJ_FLAG_HIDDEN);

  if (ap) {
    // Use a heading and aligned SSID/password/IP rows instead of inline
    // credential text.
    lv_label_set_text(wifi_conn_status_label, tr().wifi_ap_active);
    lv_obj_set_style_text_font(wifi_conn_status_label, popup_layout::font28(), 0);
    lv_obj_set_style_text_color(wifi_conn_status_label, lv_color_hex(0xFFC04D), 0);
    // softAPIP() can still return "0.0.0.0" immediately after activation;
    // show the default AP address during that interval.
    String ap_ip = WiFi.softAPIP().toString();
    const bool ip_valid = ap_ip.length() && ap_ip != "0.0.0.0";
    if (wifi_ap_info_rows) {
      lv_obj_clear_flag(wifi_ap_info_rows, LV_OBJ_FLAG_HIDDEN);
      if (wifi_ap_ssid_val) lv_label_set_text(wifi_ap_ssid_val, webConfigApSsid());
      if (wifi_ap_pw_val) lv_label_set_text(wifi_ap_pw_val, webConfigApPassword());
      if (wifi_ap_ip_val) {
        lv_label_set_text(wifi_ap_ip_val, ip_valid ? ap_ip.c_str() : "192.168.4.1");
      }
    }
#if LV_USE_QRCODE
    if (wifi_ap_qr) {
      if (!wifi_ap_qr_sized) {
        // Measure once per popup; the main loop calls here on every AP-mode pass.
        // Lay out with the QR code hidden so the flex-grow spacer measures its
        // remaining space, minus the information box's pad_row and a small gap.
        if (wifi_list_view) lv_obj_update_layout(wifi_list_view);
        int target = popup_layout::scale(320);
        if (wifi_list_spacer) {
          target = lv_obj_get_height(wifi_list_spacer) -
                   popup_layout::scale(24);
        }
        const int max_w = lv_obj_get_content_width(lv_obj_get_parent(wifi_ap_qr));
        if (target > max_w) target = max_w;
        if (target < popup_layout::scale(240)) {
          target = popup_layout::scale(240);
        }
        lv_qrcode_set_size(wifi_ap_qr, target);
        wifi_ap_qr_sized = true;
      }
      // Phone cameras can use this code to connect directly to the hotspot.
      static char qr_buf[128];
      static char last_qr_buf[128] = {};
      static lv_obj_t* last_qr_obj = nullptr;
      snprintf(qr_buf, sizeof(qr_buf), "WIFI:T:WPA;S:%s;P:%s;;",
               webConfigApSsid(), webConfigApPassword());
      if (last_qr_obj != wifi_ap_qr || strcmp(last_qr_buf, qr_buf) != 0) {
        lv_qrcode_update(wifi_ap_qr, qr_buf, strlen(qr_buf));
        last_qr_obj = wifi_ap_qr;
        strncpy(last_qr_buf, qr_buf, sizeof(last_qr_buf) - 1);
        last_qr_buf[sizeof(last_qr_buf) - 1] = '\0';
      }
      lv_obj_clear_flag(wifi_ap_qr, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    return;
  }

#if LV_USE_QRCODE
  if (wifi_ap_qr) lv_obj_add_flag(wifi_ap_qr, LV_OBJ_FLAG_HIDDEN);
#endif
  if (wifi_ap_info_rows) lv_obj_add_flag(wifi_ap_info_rows, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(wifi_conn_status_label, popup_layout::font24(), 0);
  if (networkTransport.isConnected()) {
    const String network_name =
        networkTransport.activeKind() == NetworkTransportKind::Wifi
            ? String(configManager.getConfig().wifi_ssid)
            : String(networkTransport.activeName());
    String ip = networkTransport.localIP().toString();
    snprintf(buf, sizeof(buf), "%s: %s (%s)", tr().wifi_connected,
             network_name.length() ? network_name.c_str() : "---", ip.c_str());
    lv_label_set_text(wifi_conn_status_label, buf);
    lv_obj_set_style_text_color(wifi_conn_status_label, lv_color_hex(0x51CF66), 0);
  } else {
    lv_label_set_text(wifi_conn_status_label, tr().wifi_offline);
    lv_obj_set_style_text_color(wifi_conn_status_label, lv_color_hex(0xFF6B6B), 0);
  }
}

// Wi-Fi and Ethernet share IP mode and address values. This switch
// changes only the shared mode; retain the values so they can be
// reactivated before restart.
static bool selected_static_addressing_saved() {
  return configManager.getConfig().wifi_static_enabled;
}

static bool valid_static_ip_value(const char* value) {
  if (!value || !value[0]) return false;
  IPAddress parsed;
  String text(value);
  text.trim();
  return text.length() && parsed.fromString(text);
}

static bool selected_static_addressing_available() {
  const DeviceConfig& cfg = configManager.getConfig();
  return valid_static_ip_value(cfg.wifi_static_ip) &&
         valid_static_ip_value(cfg.wifi_gateway) &&
         valid_static_ip_value(cfg.wifi_subnet);
}

static void update_ethernet_dhcp_button_ui() {
  if (!ethernet_dhcp_btn) return;
  const bool static_enabled = selected_static_addressing_saved();
  const bool boot_static_enabled =
      configManager.bootStaticAddressingEnabled();
  // Hide this row during normal DHCP operation. It provides recovery while
  // a static address is active, and a one-time undo after switching to
  // DHCP until restart.
  const bool show_toggle =
      boot_static_enabled &&
      (static_enabled || selected_static_addressing_available());
  if (show_toggle) {
    lv_obj_clear_flag(ethernet_dhcp_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ethernet_dhcp_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (net_mode_row) {
    if (net_mode_btn || show_toggle) {
      lv_obj_clear_flag(net_mode_row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(net_mode_row, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ethernet_dhcp_btn_label) {
    lv_label_set_text(
        ethernet_dhcp_btn_label,
        static_enabled ? tr().ethernet_dhcp_reset
                       : tr().ethernet_static_restore);
  }
  style_settings_button(ethernet_dhcp_btn, 0x424242);
}

static void net_mode_update_ui() {
  const DeviceConfig& cfg = configManager.getConfig();
  const bool eth_saved = cfg.ethernet_enabled;
  if (net_mode_btn_label) {
    lv_label_set_text(net_mode_btn_label,
                      eth_saved ? tr().net_mode_to_wifi : tr().net_mode_to_ethernet);
  }
  if (net_mode_hint_label) {
    if (eth_saved != networkTransport.isEthernetMode()) {
      lv_label_set_text(net_mode_hint_label, tr().net_mode_restart_note);
      lv_obj_clear_flag(net_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(net_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ip_mode_hint_label) {
    const bool static_enabled = cfg.wifi_static_enabled;
    const bool boot_static_enabled =
        configManager.bootStaticAddressingEnabled();
    if (static_enabled != boot_static_enabled) {
      lv_label_set_text(
          ip_mode_hint_label,
          static_enabled ? tr().ethernet_static_selected
                         : tr().ethernet_dhcp_selected);
      lv_obj_clear_flag(ip_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ip_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
  update_ethernet_dhcp_button_ui();
}

static void on_net_mode_clicked(lv_event_t*) {
  const bool to_ethernet = !configManager.getConfig().ethernet_enabled;
  if (!configManager.saveEthernetEnabled(to_ethernet)) return;
  Serial.printf("[Settings] Network mode saved: %s (applies after restart)\n",
                to_ethernet ? "Ethernet" : "Wi-Fi");
  net_mode_update_ui();
}

static void on_ethernet_dhcp_clicked(lv_event_t*) {
  const bool enable_static = !selected_static_addressing_saved();
  if (enable_static && !selected_static_addressing_available()) return;
  if (!configManager.saveStaticAddressingEnabled(enable_static)) return;
  Serial.printf(
      "[Settings] Shared IP mode saved: %s (applies after restart)\n",
      enable_static ? "static" : "DHCP");
  net_mode_update_ui();
}

// Start a fresh scan on initial opening and when returning from entry
// to the list. This replaces the separate rescan icon button, which
// user feedback found unnecessary.
static void wifi_show_list_view() {
  if (wifi_list_view) lv_obj_clear_flag(wifi_list_view, LV_OBJ_FLAG_HIDDEN);
  if (wifi_entry_view) lv_obj_add_flag(wifi_entry_view, LV_OBJ_FLAG_HIDDEN);
  if (settings_popup_keyboard) lv_obj_add_flag(settings_popup_keyboard, LV_OBJ_FLAG_HIDDEN);
  // Without a keyboard, hide its reserved space so the list uses the
  // full popup height.
  if (settings_popup_kb_spacer) lv_obj_add_flag(settings_popup_kb_spacer, LV_OBJ_FLAG_HIDDEN);
  if (settings_popup_close_icon) {
    lv_label_set_text(settings_popup_close_icon, getMdiChar("window-close").c_str());
  }
  settings_popup_active_ta = nullptr;
  wifi_update_conn_status_label();
  wifi_start_scan();
}

static void wifi_show_entry_view(bool manual, const char* ssid, bool open_network) {
  wifi_manual_mode = manual;
  wifi_selected_open = open_network;
  if (ssid) {
    strncpy(wifi_selected_ssid, ssid, sizeof(wifi_selected_ssid) - 1);
    wifi_selected_ssid[sizeof(wifi_selected_ssid) - 1] = '\0';
  } else {
    wifi_selected_ssid[0] = '\0';
  }
  const bool hide_password = !manual && open_network;

  // Always show the SSID field. From the list it is prefilled and read-only;
  // only manual entry allows editing. Subdued colors indicate the
  // read-only state.
  if (wifi_ssid_ta) {
    lv_textarea_set_text(wifi_ssid_ta, manual ? "" : wifi_selected_ssid);
    if (manual) {
      lv_obj_add_flag(wifi_ssid_ta, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_text_color(wifi_ssid_ta, lv_color_white(), 0);
      lv_obj_set_style_bg_color(wifi_ssid_ta, lv_color_hex(0x1E1E1E), 0);
    } else {
      lv_obj_clear_flag(wifi_ssid_ta, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_text_color(wifi_ssid_ta, lv_color_hex(0xB8B8B8), 0);
      lv_obj_set_style_bg_color(wifi_ssid_ta, lv_color_hex(0x2A2A2A), 0);
    }
  }

  if (wifi_pass_ta) {
    // Prefill the stored password when the selected network is the configured
    // one. Otherwise reopening that network incorrectly appears to have
    // no password saved.
    const DeviceConfig& known_cfg = configManager.getConfig();
    const bool is_known_ssid = !manual && wifi_selected_ssid[0] &&
                              strcmp(wifi_selected_ssid, known_cfg.wifi_ssid) == 0;
    lv_textarea_set_text(wifi_pass_ta, is_known_ssid ? known_cfg.wifi_pass : "");
    lv_textarea_set_password_mode(wifi_pass_ta, true);
    if (wifi_pass_eye_icon) lv_label_set_text(wifi_pass_eye_icon, getMdiChar("eye").c_str());
    lv_obj_t* pass_box = lv_obj_get_parent(wifi_pass_ta);
    if (pass_box) {
      if (hide_password) lv_obj_add_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_clear_flag(pass_box, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (wifi_list_view) lv_obj_add_flag(wifi_list_view, LV_OBJ_FLAG_HIDDEN);
  if (wifi_entry_view) lv_obj_clear_flag(wifi_entry_view, LV_OBJ_FLAG_HIDDEN);

  // The header button becomes a back arrow instead of adding a body link;
  // see on_settings_popup_close_clicked for click handling.
  if (settings_popup_close_icon) {
    lv_label_set_text(settings_popup_close_icon, getMdiChar("arrow-left").c_str());
  }

  // Show the keyboard immediately in this view because it is primarily
  // for text entry. Move its layout placeholder at the same time so the
  // connect button does not end up underneath it.
  if (settings_popup_keyboard) {
    if (hide_password) {
      lv_obj_add_flag(settings_popup_keyboard, LV_OBJ_FLAG_HIDDEN);
      if (settings_popup_kb_spacer) lv_obj_add_flag(settings_popup_kb_spacer, LV_OBJ_FLAG_HIDDEN);
      settings_popup_active_ta = nullptr;
    } else {
      lv_keyboard_set_mode(settings_popup_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
      lv_obj_t* first = manual ? wifi_ssid_ta : wifi_pass_ta;
      lv_obj_t* second = manual ? wifi_pass_ta : wifi_ssid_ta;
      ui_keyboard_set_target(settings_popup_keyboard, first, second);
      settings_popup_active_ta = first;
      if (settings_popup_kb_spacer) lv_obj_clear_flag(settings_popup_kb_spacer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(settings_popup_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void on_wifi_pass_eye_clicked(lv_event_t* e) {
  lv_obj_t* icon = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_obj_t* ta = icon ? lv_obj_get_parent(icon) : nullptr;
  if (!ta) return;
  const bool currently_masked = lv_textarea_get_password_mode(ta);
  lv_textarea_set_password_mode(ta, !currently_masked);
  lv_label_set_text(icon, getMdiChar(currently_masked ? "eye-off" : "eye").c_str());
}

// Connect live: save credentials and request main-loop reconnection
// using WiFi.disconnect and connectWifi with the new values. Return to
// the list, where settings_update_wifi_status feeds live connection
// progress into the information box.
static void wifi_do_connect() {
  const char* ssid = wifi_manual_mode && wifi_ssid_ta ? lv_textarea_get_text(wifi_ssid_ta)
                                                      : wifi_selected_ssid;
  const char* pass = wifi_pass_ta ? lv_textarea_get_text(wifi_pass_ta) : "";
  if (!ssid || !ssid[0]) return;
  // Do not save a secured network without a password; it cannot connect.
  if (!wifi_manual_mode && !wifi_selected_open && (!pass || !pass[0])) return;

  const DeviceConfig& cur = configManager.getConfig();
  const bool creds_changed = strcmp(cur.wifi_ssid, ssid) != 0 ||
                             strcmp(cur.wifi_pass, pass ? pass : "") != 0;

  if (creds_changed) {
    DeviceConfig cfg = configManager.getConfig();
    strncpy(cfg.wifi_ssid, ssid, CONFIG_WIFI_SSID_MAX - 1);
    cfg.wifi_ssid[CONFIG_WIFI_SSID_MAX - 1] = '\0';
    strncpy(cfg.wifi_pass, pass ? pass : "", CONFIG_WIFI_PASS_MAX - 1);
    cfg.wifi_pass[CONFIG_WIFI_PASS_MAX - 1] = '\0';
    // Changing networks resets IP mode to DHCP so an old static address
    // does not make the device unreachable on the new network. Static IP
    // configuration is available in Web Admin.
    cfg.wifi_static_enabled = false;
    cfg.wifi_static_ip[0] = '\0';
    cfg.wifi_gateway[0] = '\0';
    cfg.wifi_subnet[0] = '\0';
    cfg.wifi_dns[0] = '\0';
    if (!configManager.save(cfg)) {
      if (settings_popup_title) lv_label_set_text(settings_popup_title, tr().wifi_save_failed);
      return;
    }
  }

  wifi_stop_scan_timer();
  if (networkTransport.isWifiDriverActive()) WiFi.scanDelete();
  // Do not start scans while connection setup is in progress.
  wifi_scan_block_until = millis() + 10000UL;

  if (ap_mode_active) {
    // Turn AP mode off; apply_hotspot_mode in the main loop then connects
    // using the possibly just-saved credentials.
    if (g_hotspot_callback) g_hotspot_callback(false);
  } else if (creds_changed || !networkTransport.isWifiConnected()) {
    if (g_wifi_reconnect_callback) g_wifi_reconnect_callback();
  }

  wifi_show_list_view();
}

// Keep the same header: icon, Wi-Fi title, save and close buttons; see
// open_settings_popup. Save connects only from the entry view and has
// no effect from the list view.
static void save_wifi_popup() {
  if (wifi_entry_view && !lv_obj_has_flag(wifi_entry_view, LV_OBJ_FLAG_HIDDEN)) {
    wifi_do_connect();
  }
}

static void save_localization_popup() {
  DeviceConfig cfg = configManager.getConfig();
  const uint32_t language_index = locale_language_dd ? lv_dropdown_get_selected(locale_language_dd) : 0;
  strncpy(cfg.language, i18n::language_code_at(language_index), sizeof(cfg.language) - 1);
  cfg.language[sizeof(cfg.language) - 1] = '\0';

  const uint32_t timezone_index = locale_timezone_dd ? lv_dropdown_get_selected(locale_timezone_dd) : 2;
  strncpy(cfg.timezone, selected_timezone_code(timezone_index), sizeof(cfg.timezone) - 1);
  cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';

  cfg.global_time_format = clock_tile::normalize_time_format(
      locale_time_format_dd ? static_cast<int>(lv_dropdown_get_selected(locale_time_format_dd)) : 0);
  cfg.global_date_format = clock_tile::normalize_date_format(
      locale_date_format_dd ? static_cast<int>(lv_dropdown_get_selected(locale_date_format_dd)) : 0);

  uint8_t keyboard_layout = locale_keyboard_dd
      ? static_cast<uint8_t>(lv_dropdown_get_selected(locale_keyboard_dd)) : 0;
  cfg.keyboard_layout = (keyboard_layout > 2) ? 0 : keyboard_layout;

  if (!configManager.save(cfg)) {
    if (settings_popup_title) lv_label_set_text(settings_popup_title, tr().save_failed);
    return;
  }

  settings_refresh_language();
  uiManager.scheduleNtpSync(0);
  tiles_request_reload_all();
  // Keep the popup open after saving. Rebuild its content so row labels
  // and options immediately reflect a changed language. lv_obj_clean also
  // deletes the save button processing this event, matching the close-button
  // pattern in close_settings_popup; LVGL cleans up events on deleted targets.
  if (settings_popup_content) {
    locale_language_dd = nullptr;
    locale_timezone_dd = nullptr;
    locale_time_format_dd = nullptr;
    locale_date_format_dd = nullptr;
    locale_keyboard_dd = nullptr;
    lv_obj_clean(settings_popup_content);
    build_localization_popup(settings_popup_content);
  }
}

static void save_settings_popup() {
  switch (settings_popup_kind) {
    case SettingsPopupKind::Wifi:
      save_wifi_popup();
      break;
    case SettingsPopupKind::Localization:
      save_localization_popup();
      break;
    default:
      break;
  }
}

static void on_settings_popup_save_clicked(lv_event_t*) {
  save_settings_popup();
}

static void hide_popup_keyboard() {
  if (settings_popup_active_ta) {
    lv_obj_remove_state(settings_popup_active_ta, LV_STATE_FOCUSED);
    settings_popup_active_ta = nullptr;
  }
  if (settings_popup_keyboard) lv_obj_add_flag(settings_popup_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void on_popup_keyboard_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    save_settings_popup();
  } else if (code == LV_EVENT_CANCEL) {
    // The lower-left keyboard key only collapses the keyboard. Closing the
    // entire popup here previously discarded unsaved changes. In the Wi-Fi
    // entry view, the keyboard remains visible.
    if (wifi_keyboard_locked()) return;
    hide_popup_keyboard();
  }
}

// After toggling AP mode, the button immediately changes its label in
// the same position. A cooldown prevents a rapid double tap from
// immediately toggling the AP back.
static void on_ap_btn_cooldown_timer(lv_timer_t*) {
  ap_btn_cooldown_timer = nullptr;  // repeat_count=1 lets LVGL delete the timer itself.
  if (ap_mode_btn) {
    lv_obj_set_style_opa(ap_mode_btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(ap_mode_btn, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void on_wifi_disconnect_clicked(lv_event_t*) {
  if (!g_wifi_disconnect_callback) return;
  // Stop the running scan; disconnect asynchronously in the main loop.
  wifi_stop_scan_timer();
  if (networkTransport.isWifiDriverActive()) WiFi.scanDelete();
  g_wifi_disconnect_callback();
  // Wait for the real disconnect before updating status and button
  // visibility through wifi_update_conn_status_label.
}

static void on_popup_ap_mode_clicked(lv_event_t*) {
  if (ap_btn_cooldown_timer) return;
  if (ap_mode_click_block_until != 0 &&
      (int32_t)(millis() - ap_mode_click_block_until) < 0) {
    return;
  }
  // Stop the running scan immediately. The actual mode change runs
  // asynchronously in the main loop and must not overlap a scan.
  wifi_stop_scan_timer();
  if (networkTransport.isWifiDriverActive()) WiFi.scanDelete();
  if (g_hotspot_callback) {
    g_hotspot_callback(!ap_mode_active);
  }
  // Change the label/color only when the main loop completes the mode
  // switch: apply_hotspot_mode calls settings_update_ap_mode. Until then,
  // the button is dimmed and inactive.
  ap_mode_click_block_until = millis() + 2500;
  if (ap_mode_btn) {
    lv_obj_set_style_opa(ap_mode_btn, LV_OPA_50, 0);
    lv_obj_clear_flag(ap_mode_btn, LV_OBJ_FLAG_CLICKABLE);
  }
  ap_btn_cooldown_timer = lv_timer_create(on_ap_btn_cooldown_timer, 2500, nullptr);
  lv_timer_set_repeat_count(ap_btn_cooldown_timer, 1);
}

static void build_display_popup(lv_obj_t* parent) {
  const DeviceConfig& cfg = configManager.getConfig();
  lv_obj_t* form = create_form_area(parent);
  lv_obj_clear_flag(form, LV_OBJ_FLAG_SCROLLABLE);
  // Five rows with a fixed top offset and enough spacing between them:
  // neither flush against the top nor fully centered, following user
  // feedback.
  lv_obj_set_style_pad_top(form, popup_layout::scale(32), 0);
  lv_obj_set_style_pad_row(form, popup_layout::scale(18), 0);

  // Narrower label/value columns than the former 210/150 widths leave
  // more space for the slider between them.
  lv_obj_t* brightness_row = create_display_control_row(form);
  brightness_title_label =
      create_display_row_label(brightness_row, tr().brightness_label,
                               popup_layout::scale(170));

  lv_obj_t* brightness_slider = lv_slider_create(brightness_row);
  style_settings_slider(brightness_slider);
  lv_obj_set_width(brightness_slider, 1);
  lv_obj_set_flex_grow(brightness_slider, 1);
  lv_slider_set_range(brightness_slider, kSettingsBrightnessPctMin, kSettingsBrightnessPctMax);
  const int current_brightness_pct = brightness_pct_from_raw(BoardHAL::getBrightness());
  lv_slider_set_value(brightness_slider, current_brightness_pct, LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness_slider, on_brightness, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(brightness_slider, on_brightness, LV_EVENT_RELEASED, nullptr);

  static char bright_buf[16];
  snprintf(bright_buf, sizeof(bright_buf), "%d%%", current_brightness_pct);
  // Match the sleep slider's value-column width so both sliders have
  // exactly the same length.
  brightness_label =
      create_display_row_label(brightness_row, bright_buf,
                               popup_layout::scale(130),
                               LV_TEXT_ALIGN_RIGHT);

  lv_obj_t* sleep_row = create_display_control_row(form);
  sleep_label = create_display_row_label(
      sleep_row, tr().sleep_label, popup_layout::scale(170));

  sleep_slider = lv_slider_create(sleep_row);
  style_settings_slider(sleep_slider);
  lv_obj_set_width(sleep_slider, 1);
  lv_obj_set_flex_grow(sleep_slider, 1);
  lv_slider_set_range(sleep_slider, 0, sleep_slider_max_index());
  int32_t sleep_index = cfg.auto_sleep_enabled
                            ? sleep_index_from_seconds(cfg.auto_sleep_seconds)
                            : sleep_slider_max_index();
  lv_slider_set_value(sleep_slider, sleep_index, LV_ANIM_OFF);
  lv_obj_add_event_cb(sleep_slider, on_sleep_slider, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sleep_slider, on_sleep_slider, LV_EVENT_RELEASED, nullptr);

  static char sleep_buf[32];
  format_sleep_popup_value_for_index(sleep_buf, sizeof(sleep_buf), sleep_index);
  sleep_time_label =
      create_display_row_label(sleep_row, sleep_buf,
                               popup_layout::scale(130),
                               LV_TEXT_ALIGN_RIGHT);

  lv_obj_t* screensaver_row = create_display_control_row(form);
  screensaver_label =
      create_display_row_label(screensaver_row, tr().screensaver_label,
                               popup_layout::scale(170));

  screensaver_slider = lv_slider_create(screensaver_row);
  style_settings_slider(screensaver_slider);
  lv_obj_set_width(screensaver_slider, 1);
  lv_obj_set_flex_grow(screensaver_slider, 1);
  lv_slider_set_range(screensaver_slider, 0, sleep_slider_max_index());
  const int32_t screensaver_index = cfg.auto_screensaver_enabled
                                        ? sleep_index_from_seconds(cfg.auto_screensaver_seconds)
                                        : sleep_slider_max_index();
  lv_slider_set_value(screensaver_slider, screensaver_index, LV_ANIM_OFF);
  lv_obj_add_event_cb(screensaver_slider, on_screensaver_slider,
                      LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(screensaver_slider, on_screensaver_slider,
                      LV_EVENT_RELEASED, nullptr);

  static char screensaver_buf[32];
  format_sleep_popup_value_for_index(screensaver_buf, sizeof(screensaver_buf),
                                     screensaver_index);
  screensaver_time_label = create_display_row_label(
      screensaver_row, screensaver_buf, popup_layout::scale(130),
      LV_TEXT_ALIGN_RIGHT);

  lv_obj_t* screensaver_brightness_row = create_display_control_row(form);
  screensaver_brightness_title_label = create_display_row_label(
      screensaver_brightness_row, tr().screensaver_brightness_label,
      popup_layout::scale(170));
  lv_label_set_long_mode(screensaver_brightness_title_label,
                         LV_LABEL_LONG_WRAP);

  screensaver_brightness_slider =
      lv_slider_create(screensaver_brightness_row);
  style_settings_slider(screensaver_brightness_slider);
  lv_obj_set_width(screensaver_brightness_slider, 1);
  lv_obj_set_flex_grow(screensaver_brightness_slider, 1);
  lv_slider_set_range(screensaver_brightness_slider,
                      Device::kConfiguredBrightnessPercentMin,
                      kScreensaverBrightnessPctMax);
  lv_slider_set_value(screensaver_brightness_slider,
                      cfg.screensaver_brightness_pct, LV_ANIM_OFF);
  lv_obj_add_event_cb(screensaver_brightness_slider,
                      on_screensaver_brightness, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(screensaver_brightness_slider,
                      on_screensaver_brightness, LV_EVENT_VALUE_CHANGED,
                      nullptr);
  lv_obj_add_event_cb(screensaver_brightness_slider,
                      on_screensaver_brightness, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(screensaver_brightness_slider,
                      on_screensaver_brightness, LV_EVENT_PRESS_LOST,
                      nullptr);

  static char screensaver_brightness_buf[16];
  snprintf(screensaver_brightness_buf, sizeof(screensaver_brightness_buf),
           "%u%%",
           static_cast<unsigned>(cfg.screensaver_brightness_pct));
  screensaver_brightness_value_label = create_display_row_label(
      screensaver_brightness_row, screensaver_brightness_buf,
      popup_layout::scale(130), LV_TEXT_ALIGN_RIGHT);

  // Full-width rotation button with its icon and label inside.
  display_rotate_btn = create_popup_button(form, "", 0x26A69A, on_display_rotate_clicked);
  lv_obj_set_width(display_rotate_btn, LV_PCT(100));
  lv_obj_set_height(display_rotate_btn, popup_layout::scale(76));
  lv_obj_set_flex_flow(display_rotate_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(display_rotate_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(display_rotate_btn, popup_layout::scale(14), 0);
  // Reuse create_popup_button's label for the icon; the flex layout
  // overrides its lv_obj_center alignment.
  display_rotate_label = lv_obj_get_child(display_rotate_btn, 0);
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(display_rotate_label, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(display_rotate_label);
  display_rotate_text_label = lv_label_create(display_rotate_btn);
  lv_label_set_text(display_rotate_text_label, tr().display_rotate_btn_text);
  lv_obj_set_style_text_font(display_rotate_text_label,
                             popup_layout::font28(), 0);
  lv_obj_set_style_text_color(display_rotate_text_label, lv_color_white(), 0);
  update_display_rotate_label();
}

// The keyboard sits closer to the card edge than the form: use
// kKeyboardInset on the left/right/bottom instead of normal 20 px padding.
// Attach it directly to settings_popup_card with ignore-layout, as for
// save/close buttons. settings_popup_content would otherwise clip
// content extending beyond its own box, even with zero padding.
static constexpr int kPopupCardPad = popup_layout::scale(20);
static constexpr int kKeyboardInset = popup_layout::scale(11);
static constexpr int kKeyboardBleed = kPopupCardPad - kKeyboardInset;

// content_parent is settings_popup_content. Measure its dimensions at
// runtime; scaling from SCREEN_WIDTH/HEIGHT did not match the actual
// card width and left no right margin.
static void create_popup_keyboard(lv_obj_t* content_parent) {
  // Place a placeholder in the normal flex column, beside form under
  // content_parent. It makes the form shrink as before, keeping its bottom
  // fields/buttons clear of the freely positioned keyboard.
  lv_obj_t* spacer = lv_obj_create(content_parent);
  style_plain_container(spacer);
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(spacer, LV_PCT(100));
  // Use 48% instead of 42% for larger keys. The two entry rows and connect
  // button (~280 px) still fit above it on 720 px screens.
  lv_obj_set_height(spacer, LV_PCT(48));
  lv_obj_update_layout(content_parent);
  // Measure width from the card, not the spacer. Content is capped at a
  // readable max_width, while the keyboard still needs the full card width.
  const int reserved_w = lv_obj_get_content_width(settings_popup_card);
  const int reserved_h = lv_obj_get_height(spacer);
  // Record and make the spacer hideable only after measurement; keyboard
  // size depends on the spacer's visible height.
  settings_popup_kb_spacer = spacer;

  lv_obj_t* kb = ui_keyboard_create(settings_popup_card);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_IGNORE_LAYOUT);
  const int kb_w = reserved_w + (kKeyboardBleed * 2);
  const int kb_h = reserved_h + kKeyboardBleed;
  lv_obj_set_size(kb, kb_w, kb_h);
  // lv_obj_align always includes parent padding, even with TOP_LEFT:
  // lv_obj_move_to adds the parent's space_left/top unconditionally.
  // Use BOTTOM_LEFT with a negative offset: card_pad(20) - bleed gives
  // the left inset; ph-h+bleed gives the bottom inset; reserved_w sets
  // the right inset. style_plain_container must also zero border_width,
  // not just border_opa, or LVGL's invisible theme border adds to padding
  // and makes reserved_w narrower than the actual card. See that helper's
  // comment for the box-model details.
  lv_obj_align(kb, LV_ALIGN_BOTTOM_LEFT, -kKeyboardBleed, kKeyboardBleed);
  lv_obj_add_event_cb(kb, on_popup_keyboard_event, LV_EVENT_ALL, nullptr);

  // Show only after a field receives focus (on_popup_textarea_focused);
  // do not focus a field automatically when opening.
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  settings_popup_keyboard = kb;
}

// Keep label and textarea in one row so nothing is clipped on the
// 4-inch B4. LV_SIZE_CONTENT with vertical padding, rather than a fixed
// pixel height, centers text and cursor by construction.
// A label/value row with a fixed label column aligns values in the AP
// information box and system popup. Return the value label for the
// caller to populate.
static lv_obj_t* create_info_value_row(lv_obj_t* parent, const char* label_text) {
  lv_obj_t* row = lv_obj_create(parent);
  style_plain_container(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text_fmt(label, "%s:", label_text);
  lv_obj_set_width(label, popup_layout::scale(160));
  lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xC8C8C8), 0);

  lv_obj_t* value = lv_label_create(row);
  lv_label_set_text(value, "-");
  lv_obj_set_style_text_font(value, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(value, lv_color_white(), 0);
  return value;
}

static lv_obj_t* wifi_create_entry_row(lv_obj_t* parent, const char* label_text,
                                       lv_obj_t** ta_out, uint16_t max_len,
                                       bool password) {
  lv_obj_t* row = lv_obj_create(parent);
  style_plain_container(row);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, popup_layout::scale(14), 0);

  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text_fmt(label, "%s:", label_text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(label, popup_layout::scale(160));
  lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xC8C8C8), 0);

  lv_obj_t* ta = lv_textarea_create(row);
  style_popup_textarea(ta);
  // In a row parent, flex_grow takes remaining width along the main axis.
  lv_obj_set_flex_grow(ta, 1);
  lv_textarea_set_max_length(ta, max_len);
  lv_textarea_set_password_mode(ta, password);
  lv_textarea_set_placeholder_text(ta, "");
  lv_textarea_set_text(ta, "");
  // Also handle PRESSED: FOCUSED depends on LVGL's internal last_pressed
  // tracking and is unreliable in some sequences, while PRESSED responds
  // to touch immediately.
  lv_obj_add_event_cb(ta, on_popup_textarea_focused, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(ta, on_popup_textarea_focused, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, on_popup_textarea_defocused, LV_EVENT_DEFOCUSED, nullptr);

  lv_obj_set_height(ta, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(ta, popup_layout::scale(20), 0);
  lv_obj_set_style_pad_left(ta, popup_layout::scale(20), 0);
  lv_obj_set_style_radius(ta, popup_layout::scale(18), 0);
  lv_obj_set_style_text_font(ta, popup_layout::font28(), 0);
  // lv_textarea scrolls internally; suppress the scrollbar at the field edge.
  lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);

  *ta_out = ta;
  return row;
}

static void build_wifi_popup(lv_obj_t* parent) {
  // List view: compact results at the top, manual entry directly below;
  // information box and AP button form the bottom footer.
  wifi_list_view = lv_obj_create(parent);
  style_plain_container(wifi_list_view);
  lv_obj_set_width(wifi_list_view, LV_PCT(100));
  lv_obj_set_flex_grow(wifi_list_view, 1);
  lv_obj_clear_flag(wifi_list_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(wifi_list_view, LV_FLEX_FLOW_COLUMN);
  // Cross-axis CENTER centers the narrower AP button without affecting
  // full-width children.
  lv_obj_set_flex_align(wifi_list_view, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(wifi_list_view, popup_layout::scale(10), 0);

  wifi_scan_status_label = lv_label_create(wifi_list_view);
  lv_label_set_text(wifi_scan_status_label, tr().wifi_scan_searching);
  lv_obj_set_style_text_font(wifi_scan_status_label,
                             popup_layout::font20(), 0);
  lv_obj_set_style_text_color(wifi_scan_status_label, lv_color_hex(0xA0A0A0), 0);

  // Size the list to its content, capped just below half the view before
  // scrolling. Manual entry then sits directly below the results instead
  // of sticking to the bottom edge.
  wifi_list_container = lv_obj_create(wifi_list_view);
  style_plain_container(wifi_list_container);
  lv_obj_set_width(wifi_list_container, LV_PCT(100));
  lv_obj_set_height(wifi_list_container, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(wifi_list_container, LV_PCT(44), 0);
  lv_obj_add_flag(wifi_list_container, LV_OBJ_FLAG_SCROLLABLE);
  // Retain touch scrolling; hide only the scrollbar.
  lv_obj_set_scrollbar_mode(wifi_list_container, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(wifi_list_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(wifi_list_container, popup_layout::scale(8), 0);

  // Keep outside the scroll container, fixed below the list and separated
  // by a gap. Rescans need not recreate it with the results.
  wifi_manual_gap = lv_obj_create(wifi_list_view);
  style_plain_container(wifi_manual_gap);
  lv_obj_clear_flag(wifi_manual_gap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(wifi_manual_gap, LV_PCT(100));
  lv_obj_set_height(wifi_manual_gap, popup_layout::scale(6));

  wifi_manual_row = wifi_create_row(wifi_list_view, tr().wifi_manual_entry, false, false);
  lv_obj_add_event_cb(wifi_manual_row, on_wifi_manual_clicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* manual_chevron = lv_label_create(wifi_manual_row);
  lv_label_set_text(manual_chevron, getMdiChar("chevron-right").c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(manual_chevron, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(manual_chevron);
  lv_obj_set_style_text_color(manual_chevron, lv_color_hex(0x888888), 0);

  // The spacer pushes the information box/AP button to the bottom.
  // In AP mode, wifi_update_conn_status_label uses its height to size
  // the QR code.
  wifi_list_spacer = create_flex_spacer(wifi_list_view);

  // Distinct information card for connection status; AP mode also includes
  // a QR code to connect directly to the hotspot.
  wifi_info_box = lv_obj_create(wifi_list_view);
  style_plain_container(wifi_info_box);
  lv_obj_set_width(wifi_info_box, LV_PCT(100));
  lv_obj_set_height(wifi_info_box, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(wifi_info_box, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(wifi_info_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(wifi_info_box, popup_layout::scale(20), 0);
  lv_obj_set_style_pad_all(wifi_info_box, popup_layout::scale(18), 0);
  lv_obj_set_style_pad_row(wifi_info_box, popup_layout::scale(16), 0);
  lv_obj_set_flex_flow(wifi_info_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wifi_info_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  wifi_conn_status_label = lv_label_create(wifi_info_box);
  lv_obj_set_width(wifi_conn_status_label, LV_PCT(100));
  lv_label_set_long_mode(wifi_conn_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(wifi_conn_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(wifi_conn_status_label,
                             popup_layout::font24(), 0);

  // After a mode change, show the restart hint beside the current connection
  // status in the information box instead of below the buttons.
  if (NetworkTransportManager::deviceSupportsEthernet()) {
    net_mode_hint_label = lv_label_create(wifi_info_box);
    lv_obj_set_width(net_mode_hint_label, LV_PCT(100));
    lv_label_set_long_mode(net_mode_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(net_mode_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(net_mode_hint_label,
                               popup_layout::font20(), 0);
    lv_obj_set_style_text_color(net_mode_hint_label, lv_color_hex(0xFFC04D), 0);
    lv_label_set_text(net_mode_hint_label, tr().net_mode_restart_note);
    lv_obj_add_flag(net_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
  }

  // Give IP mode its own third status line so a pending network change
  // and DHCP/static mode can both remain visible.
  ip_mode_hint_label = lv_label_create(wifi_info_box);
  lv_obj_set_width(ip_mode_hint_label, LV_PCT(100));
  lv_label_set_long_mode(ip_mode_hint_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(ip_mode_hint_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ip_mode_hint_label, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(
      ip_mode_hint_label, lv_color_hex(0xFFC04D), 0);
  lv_label_set_text(ip_mode_hint_label, tr().ethernet_dhcp_selected);
  lv_obj_add_flag(ip_mode_hint_label, LV_OBJ_FLAG_HIDDEN);

  // AP credentials use a fixed label column so SSID/password/IP values
  // align. The block uses SIZE_CONTENT and is centered by the information
  // box.
  wifi_ap_info_rows = lv_obj_create(wifi_info_box);
  style_plain_container(wifi_ap_info_rows);
  lv_obj_set_size(wifi_ap_info_rows, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wifi_ap_info_rows, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(wifi_ap_info_rows, popup_layout::scale(8), 0);
  lv_obj_add_flag(wifi_ap_info_rows, LV_OBJ_FLAG_HIDDEN);
  wifi_ap_ssid_val = create_info_value_row(wifi_ap_info_rows, tr().ssid_label);
  wifi_ap_pw_val = create_info_value_row(wifi_ap_info_rows, tr().wifi_password_label);
  wifi_ap_ip_val = create_info_value_row(wifi_ap_info_rows, tr().ip_label);

#if LV_USE_QRCODE
  wifi_ap_qr = lv_qrcode_create(wifi_info_box);
  lv_qrcode_set_size(wifi_ap_qr, popup_layout::scale(320));
  lv_qrcode_set_dark_color(wifi_ap_qr, lv_color_black());
  lv_qrcode_set_light_color(wifi_ap_qr, lv_color_white());
  lv_qrcode_set_quiet_zone(wifi_ap_qr, true);
  style_qr_code(wifi_ap_qr);
  lv_obj_add_flag(wifi_ap_qr, LV_OBJ_FLAG_HIDDEN);
#endif

  // Full-width footer row: disconnect, visible only for an active STA
  // connection (see wifi_update_conn_status_label), and AP toggle.
  // Both use flex_grow, so hiding disconnect lets AP fill the row;
  // flex ignores hidden children.
  lv_obj_t* wifi_btn_row = lv_obj_create(wifi_list_view);
  style_plain_container(wifi_btn_row);
  lv_obj_clear_flag(wifi_btn_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(wifi_btn_row, LV_PCT(100));
  lv_obj_set_height(wifi_btn_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(wifi_btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(wifi_btn_row, popup_layout::scale(12), 0);

  wifi_disconnect_btn = create_popup_button(wifi_btn_row, tr().wifi_disconnect_btn,
                                            0xC14444, on_wifi_disconnect_clicked);
  lv_obj_set_flex_grow(wifi_disconnect_btn, 1);
  lv_obj_set_height(wifi_disconnect_btn, popup_layout::scale(76));
  lv_obj_t* disconnect_label = lv_obj_get_child(wifi_disconnect_btn, 0);
  if (disconnect_label) {
    lv_obj_set_style_text_font(disconnect_label,
                               popup_layout::font28(), 0);
  }
  lv_obj_add_flag(wifi_disconnect_btn, LV_OBJ_FLAG_HIDDEN);

  ap_mode_btn = create_popup_button(wifi_btn_row, ap_mode_active ? tr().ap_disable : tr().ap_enable,
                                    ap_mode_active ? 0xC62828 : 0xFF9800,
                                    on_popup_ap_mode_clicked);
  lv_obj_set_flex_grow(ap_mode_btn, 1);
  lv_obj_set_height(ap_mode_btn, popup_layout::scale(76));
  ap_mode_btn_label = lv_obj_get_child(ap_mode_btn, 0);
  if (ap_mode_btn_label) {
    lv_obj_set_style_text_font(ap_mode_btn_label,
                               popup_layout::font28(), 0);
  }

  // DHCP reset applies to the selected network profile and is available
  // on Wi-Fi-only devices too. Add the Ethernet switch beside it only
  // in builds with Ethernet support.
  net_mode_row = lv_obj_create(wifi_list_view);
  style_plain_container(net_mode_row);
  lv_obj_clear_flag(net_mode_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(net_mode_row, LV_PCT(100));
  lv_obj_set_height(net_mode_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(net_mode_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(net_mode_row, popup_layout::scale(12), 0);

  if (NetworkTransportManager::deviceSupportsEthernet()) {
    net_mode_btn = create_popup_button(net_mode_row, "", 0x424242,
                                       on_net_mode_clicked);
    lv_obj_set_width(net_mode_btn, 1);
    lv_obj_set_flex_grow(net_mode_btn, 1);
    lv_obj_set_height(net_mode_btn, popup_layout::scale(76));
    net_mode_btn_label = lv_obj_get_child(net_mode_btn, 0);
    if (net_mode_btn_label) {
      lv_obj_set_style_text_font(net_mode_btn_label,
                                 popup_layout::font28(), 0);
    }
  }

  ethernet_dhcp_btn = create_popup_button(
      net_mode_row, "", 0x424242, on_ethernet_dhcp_clicked);
  lv_obj_set_width(ethernet_dhcp_btn, 1);
  lv_obj_set_flex_grow(ethernet_dhcp_btn, 1);
  lv_obj_set_height(ethernet_dhcp_btn, popup_layout::scale(76));
  ethernet_dhcp_btn_label = lv_obj_get_child(ethernet_dhcp_btn, 0);
  if (ethernet_dhcp_btn_label) {
    lv_obj_set_width(ethernet_dhcp_btn_label, LV_PCT(94));
    lv_label_set_long_mode(ethernet_dhcp_btn_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(
        ethernet_dhcp_btn_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(
        ethernet_dhcp_btn_label, popup_layout::font28(), 0);
  }

  net_mode_update_ui();

  // Entry view: SSID/password rows and a connect button underneath
  wifi_entry_view = lv_obj_create(parent);
  style_plain_container(wifi_entry_view);
  lv_obj_set_width(wifi_entry_view, LV_PCT(100));
  lv_obj_set_flex_grow(wifi_entry_view, 1);
  lv_obj_clear_flag(wifi_entry_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(wifi_entry_view, LV_FLEX_FLOW_COLUMN);
  // Cross-axis CENTER centers the compact connect button while input
  // rows remain full-width.
  lv_obj_set_flex_align(wifi_entry_view, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  // Tighter spacing than other forms lets two rows and connect fit above
  // the 48%-height keyboard on 720 px displays.
  lv_obj_set_style_pad_row(wifi_entry_view, popup_layout::scale(10), 0);
  lv_obj_add_flag(wifi_entry_view, LV_OBJ_FLAG_HIDDEN);

  wifi_create_entry_row(wifi_entry_view, tr().ssid_label, &wifi_ssid_ta,
                        CONFIG_WIFI_SSID_MAX - 1, false);
  wifi_create_entry_row(wifi_entry_view, tr().wifi_password_label, &wifi_pass_ta,
                        CONFIG_WIFI_PASS_MAX - 1, true);
  lv_obj_set_style_pad_right(wifi_pass_ta, popup_layout::scale(64), 0);
  wifi_pass_eye_icon = lv_label_create(wifi_pass_ta);
  lv_label_set_text(wifi_pass_eye_icon, getMdiChar("eye").c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(wifi_pass_eye_icon, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(wifi_pass_eye_icon);
  lv_obj_set_style_text_color(wifi_pass_eye_icon, lv_color_hex(0x888888), 0);
  // Use FLOATING so the icon does not contribute to the field's
  // LV_SIZE_CONTENT height. lv_obj_align includes parent padding; see
  // create_popup_keyboard. The positive offset compensates pad_right(64),
  // placing the icon 18 px from the field edge rather than beside its text.
  lv_obj_add_flag(wifi_pass_eye_icon, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(wifi_pass_eye_icon, LV_ALIGN_RIGHT_MID,
               popup_layout::scale(64 - 18), 0);
  lv_obj_add_flag(wifi_pass_eye_icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wifi_pass_eye_icon, on_wifi_pass_eye_clicked, LV_EVENT_CLICKED, nullptr);

  // Small gap between the password field and connect button
  lv_obj_t* connect_gap = lv_obj_create(wifi_entry_view);
  style_plain_container(connect_gap);
  lv_obj_clear_flag(connect_gap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(connect_gap, LV_PCT(100), popup_layout::scale(6));

  // Dedicated connect button with an explicit label. Use the same save
  // dispatch as keyboard Enter: on_settings_popup_save_clicked ->
  // save_wifi_popup. Place it directly below the fields, at the same
  // full content width as the AP button.
  lv_obj_t* wifi_connect_btn = create_popup_button(wifi_entry_view, tr().wifi_connect_btn, 0x2E7D32,
                                                   on_settings_popup_save_clicked);
  lv_obj_set_width(wifi_connect_btn, LV_PCT(100));
  lv_obj_set_height(wifi_connect_btn, popup_layout::scale(76));
  lv_obj_t* connect_label = lv_obj_get_child(wifi_connect_btn, 0);
  if (connect_label) {
    lv_obj_set_style_text_font(connect_label,
                               popup_layout::font28(), 0);
  }

  // Remaining space between button and keyboard
  create_flex_spacer(wifi_entry_view);

  // Create the keyboard and its placeholder first, then activate the list
  // view, which immediately hides the placeholder.
  create_popup_keyboard(parent);
  wifi_show_list_view();
}

static String format_options_text(bool time_format) {
  String opts;
  opts.reserve(80);
  opts += tr().format_auto_language;
  opts += '\n';
  if (time_format) {
    opts += tr().format_24_hour;
    opts += '\n';
    opts += tr().format_12_hour;
  } else {
    opts += "DD.MM.YYYY\nMM/DD/YYYY\nYYYY/MM/DD";
  }
  return opts;
}

// Match the expanded list to the closed field: the same 28 px font,
// entries aligned with field text using pad_left 20 instead of 6,
// and clip_corner to keep the first/last blue selection inside the
// rounded corners.
static void style_locale_dropdown_list(lv_obj_t* list) {
  ui_control_style::largeDropdownList(list);
}

// Restyle the list on every opening (READY event); otherwise LVGL's
// theme restores compact defaults.
static void on_locale_dropdown_ready(lv_event_t* e) {
  lv_obj_t* dd = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  if (!dd) return;
  style_locale_dropdown_list(lv_dropdown_get_list(dd));
}

// A localization row uses the same label/dropdown pattern, fonts, radius
// and padding as wifi_create_entry_row. Size rows to content so all five
// rows and the save button fit without scrolling on 720 px displays.
static lv_obj_t* create_locale_dropdown_row(lv_obj_t* form, const char* label_text,
                                            const char* options, uint32_t selected) {
  lv_obj_t* row = lv_obj_create(form);
  style_plain_container(row);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, popup_layout::scale(14), 0);

  // Wider than Wi-Fi's 160 px label column to fit the date-format label;
  // one shared width keeps all dropdowns aligned.
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, label_text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(label, popup_layout::scale(210));
  lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);

  lv_obj_t* dd = lv_dropdown_create(row);
  style_popup_dropdown(dd);
  // In a row parent, flex_grow takes remaining width along the main axis.
  lv_obj_set_flex_grow(dd, 1);
  lv_dropdown_set_symbol(dd, LV_SYMBOL_DOWN);
  lv_dropdown_set_options(dd, options);
  lv_dropdown_set_selected(dd, selected);

  // Content height plus symmetric padding centers the text exactly.
  lv_obj_set_height(dd, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(dd, popup_layout::scale(18), 0);
  lv_obj_set_style_pad_left(dd, popup_layout::scale(20), 0);
  lv_obj_set_style_radius(dd, popup_layout::scale(18), 0);
  lv_obj_set_style_text_font(dd, popup_layout::font28(), LV_PART_MAIN);
  lv_obj_set_style_text_font(dd, &ui_symbols_24, LV_PART_INDICATOR);
  style_locale_dropdown_list(lv_dropdown_get_list(dd));
  lv_obj_add_event_cb(dd, on_locale_dropdown_ready, LV_EVENT_READY, nullptr);
  return dd;
}

static void build_localization_popup(lv_obj_t* parent) {
  const DeviceConfig& cfg = configManager.getConfig();
  lv_obj_t* form = create_form_area(parent);
  // Use the same row gap as the Wi-Fi entry view.
  lv_obj_set_style_pad_row(form, popup_layout::scale(14), 0);

  build_timezone_dropdown_options();
  const String language_options = i18n::build_language_dropdown_options();
  const String time_options = format_options_text(true);
  const String date_options = format_options_text(false);
  const String keyboard_options =
      String(tr().format_auto_language) + "\nDeutsch (QWERTZ)\nEnglish (QWERTY)";

  locale_language_dd = create_locale_dropdown_row(form, tr().language_label,
                                                  language_options.c_str(),
                                                  i18n::language_index(cfg.language));
  locale_timezone_dd = create_locale_dropdown_row(form, tr().timezone_label,
                                                  settings_timezone_options_text.c_str(),
                                                  settings_timezone_index(cfg.timezone));
  locale_time_format_dd = create_locale_dropdown_row(form, tr().time_format_label,
                                                     time_options.c_str(),
                                                     clock_tile::normalize_time_format(cfg.global_time_format));
  locale_date_format_dd = create_locale_dropdown_row(form, tr().date_format_label,
                                                     date_options.c_str(),
                                                     clock_tile::normalize_date_format(cfg.global_date_format));
  locale_keyboard_dd = create_locale_dropdown_row(form, tr().keyboard_layout_label,
                                                  keyboard_options.c_str(),
                                                  cfg.keyboard_layout > 2 ? 0 : cfg.keyboard_layout);

  // Full-width save button at the bottom, matching AP/connect; the form
  // above receives the remaining height through flex_grow.
  lv_obj_t* save_btn = create_popup_button(parent, tr().save, 0x2E7D32,
                                           on_settings_popup_save_clicked);
  lv_obj_set_width(save_btn, LV_PCT(100));
  lv_obj_set_height(save_btn, popup_layout::scale(76));
  lv_obj_t* save_btn_label = lv_obj_get_child(save_btn, 0);
  if (save_btn_label) {
    lv_obj_set_style_text_font(save_btn_label,
                               popup_layout::font28(), 0);
  }
}

// System popup: version/device, GitHub QR, update check and OTA install

static void system_set_buttons_enabled(bool enabled) {
  lv_obj_t* btns[] = {system_check_btn, system_github_btn, system_reboot_btn,
                      system_pair_btn};
  for (lv_obj_t* btn : btns) {
    if (!btn) continue;
    lv_obj_set_style_opa(btn, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    if (enabled) {
      lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
  }
}

static void system_show_status(const char* text, uint32_t color) {
  if (!system_status_label) return;
  lv_label_set_text(system_status_label, text);
  lv_obj_set_style_text_color(system_status_label, lv_color_hex(color), 0);
  lv_obj_clear_flag(system_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void system_show_update_available_status(bool show_restart_note) {
  if (!system_status_label || !system_latest_tag[0]) return;
  char headline[64];
  snprintf(headline, sizeof(headline), tr().system_update_available_fmt,
           system_latest_tag);
  if (show_restart_note) {
    String text = String(headline) + "\n" + tr().system_update_restart_note;
    system_show_status(text.c_str(), 0x51CF66);
  } else {
    system_show_status(headline, 0x51CF66);
  }
}

// The green primary button has two stages: check for updates first,
// then offer installation of vX.Y.Z after finding an update.
static void system_update_check_btn_text() {
  if (!system_check_btn_label) return;
  if (system_update_available && system_latest_tag[0]) {
    char buf[48];
    snprintf(buf, sizeof(buf), tr().system_install_btn_fmt, system_latest_tag);
    lv_label_set_text(system_check_btn_label, buf);
  } else {
    lv_label_set_text(system_check_btn_label, tr().system_check_updates_btn);
  }
}

// While the GitHub QR code is visible, hide the information rows,
// status and normal actions. Measure available space on first display,
// using the same spacer method as the AP QR code.
static void system_show_qr(bool show) {
#if LV_USE_QRCODE
  if (!system_qr) return;
  if (show) {
    if (system_info_rows) lv_obj_add_flag(system_info_rows, LV_OBJ_FLAG_HIDDEN);
    if (system_status_label) lv_obj_add_flag(system_status_label, LV_OBJ_FLAG_HIDDEN);
    if (system_check_btn) lv_obj_add_flag(system_check_btn, LV_OBJ_FLAG_HIDDEN);
    if (system_action_row) lv_obj_add_flag(system_action_row, LV_OBJ_FLAG_HIDDEN);
    if (!system_qr_sized) {
      if (settings_popup_content) lv_obj_update_layout(settings_popup_content);
      int target = popup_layout::scale(280);
      if (system_spacer) {
        target = lv_obj_get_height(system_spacer) -
                 popup_layout::scale(26);
      }
      const int max_w = lv_obj_get_content_width(lv_obj_get_parent(system_qr));
      if (target > max_w) target = max_w;
      if (target < popup_layout::scale(240)) {
        target = popup_layout::scale(240);
      }
      lv_qrcode_set_size(system_qr, target);
      lv_qrcode_update(system_qr, GithubUpdate::kRepoUrl, strlen(GithubUpdate::kRepoUrl));
      system_qr_sized = true;
    }
    lv_obj_clear_flag(system_qr, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(system_qr, LV_OBJ_FLAG_HIDDEN);
    if (system_info_rows) lv_obj_clear_flag(system_info_rows, LV_OBJ_FLAG_HIDDEN);
    if (system_status_label) lv_obj_clear_flag(system_status_label, LV_OBJ_FLAG_HIDDEN);
    if (system_check_btn) lv_obj_clear_flag(system_check_btn, LV_OBJ_FLAG_HIDDEN);
    if (system_action_row) lv_obj_clear_flag(system_action_row, LV_OBJ_FLAG_HIDDEN);
  }
#else
  (void)show;
#endif
}

static void on_system_github_clicked(lv_event_t*) {
  if (system_install_running) return;
#if LV_USE_QRCODE
  if (!system_qr) return;
  system_show_qr(lv_obj_has_flag(system_qr, LV_OBJ_FLAG_HIDDEN));
#endif
}

static void on_system_reboot_clicked(lv_event_t*) {
  if (system_check_running || system_install_running) return;
  if (!g_system_reboot_callback) return;
  system_show_qr(false);
  system_install_running = true;
  system_set_buttons_enabled(false);
  system_show_status(tr().system_restarting, 0xC8C8C8);
  g_system_reboot_callback();
}

static void on_system_check_clicked(lv_event_t*) {
  if (system_check_running || system_install_running) return;
  system_show_qr(false);  // Reserve the space for status and progress.

  if (system_update_available && system_latest_tag[0]) {
    // Second stage: request installation in the main loop. The sketch pauses
    // MQTT/Web Admin, downloads the release asset and restarts on success.
    if (!g_fw_install_callback) return;
    system_install_running = true;
    system_set_buttons_enabled(false);
    system_show_status(tr().system_downloading, 0xC8C8C8);
    if (system_progress_bar) {
      lv_bar_set_value(system_progress_bar, 0, LV_ANIM_OFF);
      lv_obj_clear_flag(system_progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    g_fw_install_callback(system_latest_tag);
    return;
  }

  if (!g_fw_check_callback) return;
  system_check_running = true;
  system_set_buttons_enabled(false);
  system_show_status(tr().system_checking, 0xC8C8C8);
  g_fw_check_callback();
}

// Pairing forces an MQTT reconnect. Post-connect status/settings/snapshot
// publishes let the HA Bridge rediscover the device, with the same effect
// as saving the MQTT host again through Web Admin.
static void on_system_pair_clicked(lv_event_t*) {
  if (system_check_running || system_install_running) return;
  if (!g_ha_pair_callback) return;
  system_show_qr(false);
  g_ha_pair_callback();
  system_show_status(tr().system_pair_status, 0x64B5F6);
}

// The compiled hometiles_logo_dsc is an exact rasterization of
// docs/images/logo.svg (144x144 ARGB8888; see release-helper/gen_logo.py).
// An earlier LVGL-shape reconstruction did not match tile/gap proportions
// or the plus shape precisely. This path only scales the actual image.
static lv_obj_t* create_hometiles_logo_mark(lv_obj_t* parent, int32_t size) {
  lv_obj_t* img = lv_image_create(parent);
  // Remove lv_image's inherited theme padding/border, which creates an
  // invisible frame separating the icon from text beyond its actual pixels.
  // This is the same box-model issue handled by style_plain_container
  // elsewhere in this file.
  style_plain_container(img);
  lv_image_set_src(img, &hometiles_logo_dsc);
  lv_image_set_antialias(img, true);
  const uint32_t zoom = static_cast<uint32_t>(
      (static_cast<int64_t>(size) * 256) / hometiles_logo_dsc.header.w);
  lv_image_set_scale(img, zoom);
  // lv_image's default LV_SIZE_CONTENT uses the unscaled 144x144 source,
  // not its visible scaled result. That reserves 144 px in the layout
  // although only approximately size pixels are visible, creating an
  // invisible margin. Set dimensions explicitly to the target size.
  lv_obj_set_size(img, size, size);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  return img;
}

static void build_system_popup(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  style_plain_container(box);
  lv_obj_set_width(box, LV_PCT(100));
  lv_obj_set_flex_grow(box, 1);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  // Match the display form's fixed top offset.
  lv_obj_set_style_pad_top(box, popup_layout::scale(48), 0);
  lv_obj_set_style_pad_row(box, popup_layout::scale(18), 0);

  // Branding at the top: icon on the left, product name beside it and
  // smaller version underneath, as in an app's About screen.
  lv_obj_t* brand = lv_obj_create(box);
  style_plain_container(brand);
  lv_obj_clear_flag(brand, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(brand, popup_layout::scale(18), 0);
  create_hometiles_logo_mark(brand, popup_layout::scale(100));

  lv_obj_t* brand_text = lv_obj_create(brand);
  style_plain_container(brand_text);
  lv_obj_clear_flag(brand_text, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(brand_text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(brand_text, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(brand_text, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(brand_text, popup_layout::scale(2), 0);

  lv_obj_t* brand_title = lv_label_create(brand_text);
  lv_label_set_text(brand_title, "HomeTiles");
  lv_obj_set_style_text_font(brand_title, popup_layout::font40(), 0);
  lv_obj_set_style_text_color(brand_title, lv_color_white(), 0);

  lv_obj_t* version_caption = lv_label_create(brand_text);
  lv_label_set_text(version_caption, FW_VERSION);
  lv_obj_set_style_text_font(version_caption, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(version_caption, lv_color_hex(0xA8A8A8), 0);

  // Show the device name without a label, matching the version caption.
  // The name does not change while the popup is open.
  system_info_rows = lv_label_create(box);
  lv_label_set_text(system_info_rows, Device::displayName());
  lv_obj_set_style_text_font(system_info_rows, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(system_info_rows, lv_color_hex(0xA8A8A8), 0);

#if LV_USE_QRCODE
  system_qr = lv_qrcode_create(box);
  // Leave more space below the branding than the standard 18 px row gap.
  // Use margin, not padding, to move the element without enlarging its
  // white QR frame. Size the QR to remaining space (see system_show_qr),
  // keeping it within the card on 720 px devices too.
  lv_obj_set_style_margin_top(system_qr, popup_layout::scale(14), 0);
#if defined(DEVICE_LAYOUT_480X480)
  // Leave enough vertical room for the GitHub action on the compact square
  // layout without changing the QR size on any hardware profile.
  lv_qrcode_set_size(system_qr, popup_layout::scale480(264));
#else
  lv_qrcode_set_size(system_qr, popup_layout::scale(280));
#endif
  lv_qrcode_set_dark_color(system_qr, lv_color_black());
  lv_qrcode_set_light_color(system_qr, lv_color_white());
  lv_qrcode_set_quiet_zone(system_qr, true);
  style_qr_code(system_qr);
  lv_qrcode_update(system_qr, GithubUpdate::kRepoUrl, strlen(GithubUpdate::kRepoUrl));
  system_qr_sized = true;
  lv_obj_add_flag(system_qr, LV_OBJ_FLAG_HIDDEN);
#endif

  // Push status/progress and buttons downward; also measure available
  // space here when showing the QR code.
  system_spacer = create_flex_spacer(box);

  // Place status/progress directly above the update button, where the
  // user initiated the action, instead of beside device information.
  system_status_label = lv_label_create(box);
  lv_obj_set_width(system_status_label, LV_PCT(100));
  lv_label_set_long_mode(system_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(system_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(system_status_label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(system_status_label, lv_color_hex(0xC8C8C8), 0);
  if (system_update_available && system_latest_tag[0]) {
    // Retain the known check result and restart hint when reopening.
    // Leave the row empty until the first successful check.
    system_show_update_available_status(true);
  } else {
    lv_label_set_text(system_status_label, "");
  }

  system_progress_bar = lv_bar_create(box);
  lv_obj_set_size(system_progress_bar, LV_PCT(100),
                  popup_layout::scale(18));
  lv_obj_set_style_bg_color(system_progress_bar, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(system_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(system_progress_bar,
                          popup_layout::scale(9), LV_PART_MAIN);
  lv_obj_set_style_bg_color(system_progress_bar, lv_color_hex(0x43A047), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(system_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(system_progress_bar,
                          popup_layout::scale(9), LV_PART_INDICATOR);
  lv_bar_set_range(system_progress_bar, 0, 100);
  lv_obj_add_flag(system_progress_bar, LV_OBJ_FLAG_HIDDEN);

  // Green marks an affirmative action, as with Wi-Fi connect and save.
  system_check_btn = create_popup_button(box, "", 0x2E7D32, on_system_check_clicked);
  lv_obj_set_width(system_check_btn, LV_PCT(100));
  lv_obj_set_height(system_check_btn, popup_layout::scale(76));
  system_check_btn_label = lv_obj_get_child(system_check_btn, 0);
  if (system_check_btn_label) {
    lv_obj_set_style_text_font(system_check_btn_label,
                               popup_layout::font28(), 0);
  }
  system_update_check_btn_text();

  // Restart and pairing share a row below update. Restart is neutral gray;
  // red is reserved for deletion. Pairing is blue for synchronization.
  system_action_row = lv_obj_create(box);
  style_plain_container(system_action_row);
  lv_obj_clear_flag(system_action_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(system_action_row, LV_PCT(100));
  lv_obj_set_height(system_action_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(system_action_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(system_action_row,
                              popup_layout::scale(12), 0);

  // No icon, matching the Web Admin restart button.
  system_reboot_btn = create_popup_button(system_action_row, tr().restart_button,
                                          0x424242, on_system_reboot_clicked);
  lv_obj_set_flex_grow(system_reboot_btn, 1);
  lv_obj_set_height(system_reboot_btn, popup_layout::scale(76));
  lv_obj_t* reboot_text = lv_obj_get_child(system_reboot_btn, 0);
  if (reboot_text) {
    lv_obj_set_style_text_font(reboot_text, popup_layout::font28(), 0);
  }

  system_pair_btn = create_popup_button(system_action_row, "", 0x1E88E5,
                                        on_system_pair_clicked);
  lv_obj_set_flex_grow(system_pair_btn, 1);
  lv_obj_set_height(system_pair_btn, popup_layout::scale(76));
  lv_obj_set_flex_flow(system_pair_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(system_pair_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(system_pair_btn,
                              popup_layout::scale(14), 0);
  lv_obj_t* pair_icon = lv_obj_get_child(system_pair_btn, 0);
  if (pair_icon) {
    lv_label_set_text(pair_icon, getMdiChar("home-assistant").c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(pair_icon, FONT_MDI_ICONS, 0);
    popup_layout::applyIconScale(pair_icon);
  }
  lv_obj_t* pair_text = lv_label_create(system_pair_btn);
  lv_label_set_text(pair_text, tr().system_pair_btn);
  lv_label_set_long_mode(pair_text, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(pair_text, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(pair_text, lv_color_white(), 0);

  // GitHub button with icon and text inside, matching the rotation button
  system_github_btn = create_popup_button(box, "", 0x424242, on_system_github_clicked);
  lv_obj_set_width(system_github_btn, LV_PCT(100));
  lv_obj_set_height(system_github_btn, popup_layout::scale(76));
  lv_obj_set_flex_flow(system_github_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(system_github_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(system_github_btn,
                              popup_layout::scale(14), 0);
  lv_obj_t* gh_icon = lv_obj_get_child(system_github_btn, 0);
  if (gh_icon) {
    lv_label_set_text(gh_icon, getMdiChar("github").c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(gh_icon, FONT_MDI_ICONS, 0);
    popup_layout::applyIconScale(gh_icon);
  }
  lv_obj_t* gh_text = lv_label_create(system_github_btn);
  lv_label_set_text(gh_text, "GitHub");
  lv_obj_set_style_text_font(gh_text, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(gh_text, lv_color_white(), 0);
}

static const char* popup_title_for_kind(SettingsPopupKind kind) {
  switch (kind) {
    case SettingsPopupKind::Display:
      return tr().display_label;
    case SettingsPopupKind::Wifi:
      return tr().wifi_label;
    case SettingsPopupKind::Localization:
      return tr().admin_settings_language;
    case SettingsPopupKind::Firmware:
      return "System";
  }
  return "";
}

static const char* popup_icon_for_kind(SettingsPopupKind kind) {
  switch (kind) {
    case SettingsPopupKind::Display:
      return "monitor";
    case SettingsPopupKind::Wifi:
      return "wifi";
    case SettingsPopupKind::Localization:
      return "translate";
    case SettingsPopupKind::Firmware:
      return "chip";
  }
  return "cog";
}

static void build_popup_content(SettingsPopupKind kind, lv_obj_t* parent) {
  switch (kind) {
    case SettingsPopupKind::Display:
      build_display_popup(parent);
      break;
    case SettingsPopupKind::Wifi:
      build_wifi_popup(parent);
      break;
    case SettingsPopupKind::Localization:
      build_localization_popup(parent);
      break;
    case SettingsPopupKind::Firmware:
      build_system_popup(parent);
      break;
  }
}

static void finish_settings_popup_open() {
  if (settings_popup_content) build_popup_content(settings_popup_kind, settings_popup_content);
}

static void open_settings_popup(SettingsPopupKind kind) {
  if (settings_popup_overlay) return;
  reset_popup_refs();
  settings_popup_kind = kind;

  const auto parts = create_popup_body(on_settings_popup_close_clicked, nullptr);
  settings_popup_overlay = parts.overlay;
  settings_popup_card = parts.card;
  settings_popup_title = parts.title;
  settings_popup_close_icon = lv_obj_get_child(parts.close, 0);
  lv_obj_t* header_icon = parts.icon;
  lv_obj_t* close_btn = parts.close;
  lv_label_set_text(header_icon, getMdiChar(popup_icon_for_kind(kind)).c_str());
  hometiles_title::set(settings_popup_title, popup_title_for_kind(kind));

  // Settings keeps its full-width form and readable content cap. The shared
  // header ignores flex layout; this spacer reserves the existing header band.
  lv_obj_set_style_bg_color(settings_popup_overlay, lv_color_hex(0x0A0A0A), 0);
  lv_obj_set_style_bg_opa(settings_popup_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(settings_popup_overlay, 0, 0);
  lv_obj_set_style_pad_all(settings_popup_overlay, Device::kGridPad, 0);
  lv_obj_add_flag(settings_popup_overlay, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(settings_popup_card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_radius(settings_popup_card, popup_layout::scale(22), 0);
  lv_obj_set_style_shadow_opa(settings_popup_card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_clip_corner(settings_popup_card, false, 0);
  lv_obj_set_style_pad_all(settings_popup_card, kPopupCardPad, 0);
  lv_obj_set_style_pad_row(settings_popup_card, popup_layout::scale(8), 0);
  lv_obj_set_flex_flow(settings_popup_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(settings_popup_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_t* header = lv_obj_create(settings_popup_card);
  lv_obj_set_size(header, LV_PCT(100), popup_layout::scale(84));
  style_plain_container(header);

  settings_popup_content = lv_obj_create(settings_popup_card);
  lv_obj_set_width(settings_popup_content, LV_PCT(100));
  // Cap readable content width on 1280 px devices (8-inch/Tab5), where
  // fields and list rows would otherwise be excessively wide. The cap
  // matches B4's 648 px content width and does not restrict that layout.
  // Header and keyboard retain the full card width.
  lv_obj_set_style_max_width(settings_popup_content,
                             popup_layout::scale(660), 0);
  lv_obj_set_flex_grow(settings_popup_content, 1);
  style_plain_container(settings_popup_content);
  lv_obj_set_style_pad_all(settings_popup_content, 0, 0);
  lv_obj_set_style_pad_row(settings_popup_content,
                           popup_layout::scale(8), 0);
  // Leave room below the header: the 96 px close/back button and its
  // extended touch area reach past the header edge. Without a gap, its
  // pressed highlight overlaps the upper-right content.
  lv_obj_set_style_pad_top(settings_popup_content,
                           popup_layout::scale(14), 0);
  lv_obj_set_flex_flow(settings_popup_content, LV_FLEX_FLOW_COLUMN);
  // Cross-axis CENTER centers narrow action buttons such as save;
  // full-width children stay full-width.
  lv_obj_set_flex_align(settings_popup_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  show_popup_shell(settings_popup_overlay, settings_popup_card,
                   settings_popup_title, header_icon, close_btn, hide_settings_popup);
  defer_popup_content(settings_popup_card, finish_settings_popup_open);

  lv_obj_invalidate(settings_popup_overlay);
}

static void on_settings_tile_clicked(lv_event_t* e) {
  const uintptr_t raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  finish_press_before_popup(e);
  open_settings_popup(static_cast<SettingsPopupKind>(static_cast<uint8_t>(raw)));
}

// A 3x1 tile places a one-cell icon/title area on the left and a short,
// localized description of its content on the right.
static lv_obj_t* create_settings_menu_tile(lv_obj_t* parent, uint8_t col, uint8_t row,
                                           const char* icon_name, const char* title,
                                           lv_obj_t** title_label_out,
                                           lv_obj_t** summary_label_out,
                                           SettingsPopupKind kind) {
  lv_obj_t* tile = lv_button_create(parent);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, col, 3, LV_GRID_ALIGN_STRETCH, row, 1);
  style_settings_button(tile, 0x2A2A2A);
  lv_obj_set_style_radius(tile, popup_layout::scale480(22), 0);
  lv_obj_set_style_border_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(tile, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_pad_column(tile, 0, 0);
  ui_surface_style::apply_global_tile_border(tile);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(tile, on_settings_tile_clicked, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(
                          static_cast<uintptr_t>(static_cast<uint8_t>(kind))));

  lv_obj_t* face = lv_obj_create(tile);
  style_plain_container(face);
  lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(face, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_width(face, GRID_CELL_W);
  lv_obj_set_height(face, LV_PCT(100));
  lv_obj_set_style_pad_all(face, 0, 0);

  // Center the icon alone in the left cell; put the title above the
  // right-side description, following an icon/title/subtext list row.
  lv_obj_t* icon = lv_label_create(face);
  lv_label_set_text(icon, getMdiChar(icon_name).c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(icon);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(icon, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_center(icon);

  lv_obj_t* info = lv_obj_create(tile);
  style_plain_container(info);
  lv_obj_clear_flag(info, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(info, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_height(info, LV_PCT(100));
  lv_obj_set_flex_grow(info, 1);
  lv_obj_set_style_pad_right(info, popup_layout::scale(16), 0);
  lv_obj_set_style_pad_ver(info, popup_layout::scale(8), 0);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(info, popup_layout::scale(6), 0);

  lv_obj_t* title_label = lv_label_create(info);
  lv_label_set_text(title_label, title);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(title_label, LV_PCT(100));
  lv_obj_set_style_text_font(title_label, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
  if (title_label_out) *title_label_out = title_label;

  if (summary_label_out) {
    lv_obj_t* summary = lv_label_create(info);
    lv_label_set_text(summary, "");
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_obj_set_style_text_align(summary, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(summary, popup_layout::font20(), 0);
    lv_obj_set_style_text_color(summary, lv_color_hex(0xA8A8A8), 0);
    *summary_label_out = summary;
  }
  return tile;
}

// Tiles show short localized descriptions; dynamic status belongs
// in the corresponding popups.
static void update_settings_tile_summaries() {
  if (settings_tile_display_summary) {
    lv_label_set_text(settings_tile_display_summary, tr().settings_tile_desc_display);
  }
  if (settings_tile_wifi_summary) {
    lv_label_set_text(settings_tile_wifi_summary, tr().settings_tile_desc_wifi);
  }
  if (settings_tile_locale_summary) {
    lv_label_set_text(settings_tile_locale_summary, tr().settings_tile_desc_locale);
  }
  if (settings_tile_firmware_summary) {
    static char buf[48];
    snprintf(buf, sizeof(buf), tr().settings_tile_desc_firmware_fmt, FW_VERSION);
    lv_label_set_text(settings_tile_firmware_summary, buf);
  }
}

static void build_grid_track_descriptors(lv_coord_t* dsc, uint8_t count, lv_coord_t cell_size) {
  if (!dsc) return;
  for (uint8_t i = 0; i < count; ++i) {
    dsc[i] = cell_size;
  }
  dsc[count] = LV_GRID_TEMPLATE_LAST;
}

// ========== Public API ==========
void settings_set_wifi_reconnect_callback(wifi_reconnect_callback_t cb) {
  g_wifi_reconnect_callback = cb;
}

void settings_set_fw_check_callback(fw_check_callback_t cb) {
  g_fw_check_callback = cb;
}

void settings_set_fw_install_callback(fw_install_callback_t cb) {
  g_fw_install_callback = cb;
}

void settings_set_system_reboot_callback(system_reboot_callback_t cb) {
  g_system_reboot_callback = cb;
}

void settings_set_wifi_disconnect_callback(wifi_disconnect_callback_t cb) {
  g_wifi_disconnect_callback = cb;
}

void settings_set_ha_pair_callback(ha_pair_callback_t cb) {
  g_ha_pair_callback = cb;
}

// All four responses run on the loop task, which owns LVGL. They tolerate
// a popup closed in the meantime: its statics are cleared, and helpers
// check them before use.
void settings_fw_check_result(bool ok, const char* latest_tag, bool update_available) {
  system_check_running = false;
  system_update_available = ok && update_available;
  if (ok && latest_tag && latest_tag[0]) {
    snprintf(system_latest_tag, sizeof(system_latest_tag), "%s", latest_tag);
  }
  system_set_buttons_enabled(true);
  system_update_check_btn_text();
  if (!system_status_label) return;
  if (!ok) {
    system_show_status(tr().system_check_failed, 0xFF6B6B);
  } else if (system_update_available) {
    // Retain this hint with the known check result and display it again
    // when the system popup is reopened.
    system_show_update_available_status(true);
  } else {
    system_show_status(tr().system_up_to_date, 0x51CF66);
  }
}

void settings_fw_install_progress(size_t written, size_t total) {
  if (!system_progress_bar || total == 0) return;
  lv_bar_set_value(system_progress_bar,
                   static_cast<int32_t>((written * 100ULL) / total), LV_ANIM_OFF);
}

void settings_fw_install_done() {
  system_install_running = false;
  if (system_progress_bar) lv_bar_set_value(system_progress_bar, 100, LV_ANIM_OFF);
  system_show_status(tr().system_installed_restarting, 0x51CF66);
}

void settings_fw_install_failed(const char* error) {
  system_install_running = false;
  system_set_buttons_enabled(true);
  if (system_progress_bar) lv_obj_add_flag(system_progress_bar, LV_OBJ_FLAG_HIDDEN);
  if (!system_status_label) return;
  char buf[96];
  if (error && error[0]) {
    snprintf(buf, sizeof(buf), "%s (%s)", tr().system_install_failed, error);
  } else {
    snprintf(buf, sizeof(buf), "%s", tr().system_install_failed);
  }
  system_show_status(buf, 0xFF6B6B);
}

void build_settings_tab(lv_obj_t *tab, hotspot_callback_t hotspot_cb) {
  g_hotspot_callback = hotspot_cb;
  ap_mode_confirm_pending = false;
  clear_ap_confirm_timer();
  if (settings_popup_overlay) close_settings_popup();

  lv_obj_clean(tab);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tab, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(tab, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(tab, GRID_PAD, 0);

  // 4x4 Grid
  static lv_coord_t col_dsc[GRID_COLS + 1];
  static lv_coord_t row_dsc[GRID_ROWS + 1];
  static bool dsc_ready = false;
  if (!dsc_ready) {
    build_grid_track_descriptors(col_dsc, GRID_COLS, GRID_CELL_W);
    build_grid_track_descriptors(row_dsc, GRID_ROWS, GRID_CELL_H);
    dsc_ready = true;
  }
  lv_obj_set_layout(tab, LV_LAYOUT_GRID);
  lv_obj_set_grid_dsc_array(tab, col_dsc, row_dsc);
  lv_obj_set_style_pad_column(tab, GRID_GAP, 0);
  lv_obj_set_style_pad_row(tab, GRID_GAP, 0);

  const DeviceConfig& cfg = configManager.getConfig();
  display_rotated_180 = cfg.display_rotated_180;
  display_rotation_quarters = Device::normalizeRotationQuarterTurns(cfg.display_rotation_quarters);
  display_rotation_mode = cfg.display_rotation_mode;
  wake_mode_mains = kWakeModeTouch;
  wake_mode_battery = kWakeModeTouch;

  display_section_label = nullptr;
  brightness_title_label = nullptr;
  screensaver_brightness_title_label = nullptr;
  wifi_section_label = nullptr;
  sleep_section_label = nullptr;
  sleep_label = nullptr;
  brightness_label = nullptr;
  screensaver_brightness_value_label = nullptr;
  display_rotate_btn = nullptr;
  display_rotate_label = nullptr;
  display_rotate_sub_label = nullptr;
  sleep_slider = nullptr;
  sleep_time_label = nullptr;
  screensaver_slider = nullptr;
  screensaver_time_label = nullptr;
  screensaver_label = nullptr;
  screensaver_brightness_slider = nullptr;
  ap_mode_btn = nullptr;
  ap_mode_btn_label = nullptr;
  wifi_disconnect_btn = nullptr;
  net_mode_btn = nullptr;
  net_mode_btn_label = nullptr;
  net_mode_hint_label = nullptr;
  ip_mode_hint_label = nullptr;
  ap_confirm_row = nullptr;
  ap_confirm_yes_btn = nullptr;
  ap_confirm_no_btn = nullptr;
  ap_yes_label_obj = nullptr;
  ap_no_label_obj = nullptr;

  create_settings_back_button(tab);

  // Stack all four three-cell-wide tiles vertically from row 0. On the
  // four-column B4 grid, start directly beside the back button; center
  // them horizontally on wider seven-column grids (8-inch/Tab5).
  static constexpr uint8_t kSettingsMenuTileSpanW = 3;
  const uint8_t tile_col = (GRID_COLS <= kSettingsMenuTileSpanW + 1)
                               ? GRID_COLS - kSettingsMenuTileSpanW
                               : (GRID_COLS - kSettingsMenuTileSpanW) / 2;

  create_settings_menu_tile(tab, tile_col, 0, "monitor", tr().display_label,
                            &settings_tile_display_title,
                            &settings_tile_display_summary,
                            SettingsPopupKind::Display);

  create_settings_menu_tile(tab, tile_col, 1, "wifi", tr().wifi_label,
                            &settings_tile_wifi_title,
                            &settings_tile_wifi_summary,
                            SettingsPopupKind::Wifi);

  create_settings_menu_tile(tab, tile_col, 2, "translate", tr().admin_settings_language,
                            &settings_tile_locale_title,
                            &settings_tile_locale_summary,
                            SettingsPopupKind::Localization);

  create_settings_menu_tile(tab, tile_col, 3, "chip", "System",
                            &settings_tile_firmware_title,
                            &settings_tile_firmware_summary,
                            SettingsPopupKind::Firmware);

  mains_wake_btn = nullptr;
  mains_wake_label = nullptr;
  mains_wake_sub_label = nullptr;
  battery_wake_btn = nullptr;
  battery_wake_label = nullptr;
  battery_wake_sub_label = nullptr;
  power_status_label = nullptr;
  power_level_label = nullptr;
  battery_icon_label = nullptr;
  battery_percent_label = nullptr;
  update_settings_tile_summaries();
}

// Tiles display descriptions only. Main-loop calls still keep an open
// Wi-Fi popup's information box current, including automatic reconnect
// after stopping AP mode.
void settings_update_wifi_status(bool, const char*, const char*) {
  wifi_update_conn_status_label();
}

void settings_update_wifi_status_ap(const char*, const char*) {
  wifi_update_conn_status_label();
}

void settings_update_ap_mode(bool running) {
  ap_mode_active = running;
  if (!ap_mode_btn_label && ap_mode_btn) {
    ap_mode_btn_label = lv_obj_get_child(ap_mode_btn, 0);
  }
  if (ap_mode_btn_label) {
    lv_label_set_text(ap_mode_btn_label, running ? tr().ap_disable : tr().ap_enable);
  }
  if (ap_mode_btn) {
    style_settings_button(ap_mode_btn, running ? 0xC62828 : 0xFF9800);
    if (!ap_mode_confirm_pending) {
      lv_obj_clear_flag(ap_mode_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (running) {
    hide_ap_confirm_row();
  }
  // Update Wi-Fi popup status immediately after toggling: AP and IP,
  // or connected/offline. Previously the toggle gave no feedback here.
  wifi_update_conn_status_label();
}

void settings_refresh_language() {
  const auto& s = tr();

  if (settings_tile_display_title) lv_label_set_text(settings_tile_display_title, s.display_label);
  if (settings_tile_wifi_title) lv_label_set_text(settings_tile_wifi_title, s.wifi_label);
  if (settings_tile_locale_title) lv_label_set_text(settings_tile_locale_title, s.admin_settings_language);
  if (settings_tile_firmware_title) lv_label_set_text(settings_tile_firmware_title, "System");
  if (settings_popup_title) lv_label_set_text(settings_popup_title, popup_title_for_kind(settings_popup_kind));
  if (display_section_label) lv_label_set_text(display_section_label, s.display_label);
  if (brightness_title_label) lv_label_set_text(brightness_title_label, s.brightness_label);
  if (screensaver_brightness_title_label) {
    lv_label_set_text(screensaver_brightness_title_label,
                      s.screensaver_brightness_label);
  }
  if (display_rotate_text_label) lv_label_set_text(display_rotate_text_label, s.display_rotate_btn_text);
  if (wifi_section_label) lv_label_set_text(wifi_section_label, s.wifi_label);
  if (sleep_section_label) lv_label_set_text(sleep_section_label, s.sleep_label);
  if (sleep_label) lv_label_set_text(sleep_label, s.sleep_label);
  if (screensaver_label) lv_label_set_text(screensaver_label, s.screensaver_label);
  if (ap_yes_label_obj) lv_label_set_text(ap_yes_label_obj, s.yes);
  if (ap_no_label_obj) lv_label_set_text(ap_no_label_obj, s.no);

  update_wake_button(mains_wake_label, mains_wake_sub_label, kWakeModeTouch);
  update_wake_button(battery_wake_label, battery_wake_sub_label, kWakeModeTouch);
  // Also refresh the information box if the Wi-Fi popup is open.
  settings_update_ap_mode(ap_mode_active);
  if (ethernet_dhcp_btn) net_mode_update_ui();
  update_settings_tile_summaries();

  // A Web Admin save can change status-line visibility, text and height
  // plus both bottom buttons. Updating the existing flex layout in pieces
  // left stale regions on-screen. Recreate the open Wi-Fi popup without
  // an intermediate refresh; open_settings_popup draws only the finished
  // state.
  if (settings_popup_kind == SettingsPopupKind::Wifi &&
      settings_popup_overlay) {
    close_settings_popup();
    open_settings_popup(SettingsPopupKind::Wifi);
  }

  weather_popup_refresh_language();
}
