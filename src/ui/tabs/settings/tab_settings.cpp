#include <lvgl.h>
#include <WiFi.h>
#include <cstring>
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/core/config/config_manager.h"
#include "src/core/hardware/board_hal.h"
#include "src/core/power/power_manager.h"
#include "src/network/network_manager.h"
#include "src/network/transport/network_transport.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/ui_manager.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/core/firmware/firmware_version.h"
#include "src/core/firmware/github_update.h"
#include "src/devices/device.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/clock/clock_format.h"
#include "src/web/setup/web_config.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/network/bridge/entity_search.h"
#include "src/network/secure/command_channel.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/tabs/settings/settings_model.h"
#include "src/ui/tabs/settings/settings_screen.h"

// The Settings tab: what its pages show and do (settings_model.h, drawn by
// settings_screen.cpp) on the real configuration, and the sketch's calls into
// it. The paths are the former Settings popups' (saving, scans, hotspot,
// network mode, updates, pairing, Web Admin password).

static bool display_rotated_180 = false;
static uint8_t display_rotation_quarters = Device::kRotationDefault;
static uint8_t display_rotation_mode = kDisplayRotationNormal;
static uint8_t wake_mode_mains = kWakeModeTouch;
static uint8_t wake_mode_battery = kWakeModeTouch;
static hotspot_callback_t g_hotspot_callback = nullptr;
static wifi_reconnect_callback_t g_wifi_reconnect_callback = nullptr;
static fw_check_callback_t g_fw_check_callback = nullptr;
static fw_install_callback_t g_fw_install_callback = nullptr;
static system_reboot_callback_t g_system_reboot_callback = nullptr;
static wifi_disconnect_callback_t g_wifi_disconnect_callback = nullptr;
static ha_pair_callback_t g_ha_pair_callback = nullptr;

// WiFi scan results, strongest first.
struct WifiScanEntry { char ssid[33]; int16_t rssi; bool open; };
static WifiScanEntry wifi_scan_results[24];
static size_t wifi_scan_result_count = 0;
// Pause scans briefly after requesting a connection/reconnection;
// a running scan would interfere with WiFi.begin() in the main loop.
static uint32_t wifi_scan_block_until = 0;

static bool ap_mode_active = false;
// While the hotspot is being switched, the toggle waits.
static uint32_t ap_mode_click_block_until = 0;

// Without an answer to a pairing attempt, reconnect MQTT once so an older
// Bridge finds the panel again, as the former Pairing button did.
static bool security_rediscover_pending = false;
static bool system_check_running = false;
static bool system_install_running = false;
// A known update-check result stays until the next check.
static char system_latest_tag[24] = {};
static bool system_update_available = false;

static lv_timer_t *screensaver_brightness_preview_timer = nullptr;

static const int kSettingsBrightnessPctMin =
    Device::kConfiguredBrightnessPercentMin;
static const int kSettingsBrightnessPctMax = 100;

static const i18n::Strings& tr() {
  return i18n::strings(configManager.getConfig().language);
}

// Settings takes the global tile color like the home tiles: the card and
// every surface on it.
static uint32_t settings_tile_color() {
  return tileDefaultBgColor();
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

static int32_t sleep_slider_max_index() {
  return static_cast<int32_t>(kSleepOptionsSecCount);
}

static bool sleep_index_is_never(int32_t index) {
  return index >= static_cast<int32_t>(kSleepOptionsSecCount);
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

// ---------- Settings model: Display (settings_model.h) ----------
// The Settings screen (settings_screen.cpp) draws; these read and save the
// real configuration.

static void save_display_settings(uint8_t brightness_raw, bool sleep_enabled, uint16_t sleep_seconds) {
  const DeviceConfig& cfg = configManager.getConfig();
  configManager.saveDisplaySettings(
      brightness_raw,
      sleep_enabled,
      sleep_seconds,
      cfg.auto_sleep_battery_enabled,
      cfg.auto_sleep_battery_seconds,
      display_rotation_mode,
      display_rotated_180,
      display_rotation_quarters,
      wake_mode_mains,
      wake_mode_battery);
  mqttPublishDeviceSettings();
}

static int32_t sleep_index_of(bool enabled, uint16_t seconds) {
  return enabled ? sleep_index_from_seconds(seconds) : sleep_slider_max_index();
}

// Rotation steps: quarter turns where the panel supports them
// (RotationStepMode::QuarterTurns), else normal and flipped.
static uint8_t display_rotation_index() {
  if (Device::supportsQuarterTurnRotation()) {
    return static_cast<uint8_t>((display_rotation_quarters - Device::kRotationDefault) & 0x03);
  }
  return display_rotation_quarters == Device::kRotationFlipped ? 1 : 0;
}

static void apply_display_rotation(uint8_t rotation) {
  displayManager.setRotation(rotation);
  sync_display_rotation_state(rotation);
  const DeviceConfig& cfg = configManager.getConfig();
  save_display_settings(cfg.display_brightness, cfg.auto_sleep_enabled, cfg.auto_sleep_seconds);
  lv_obj_invalidate(lv_scr_act());
  lv_display_t* disp = lv_display_get_default();
  if (disp) {
    lv_refr_now(disp);
  }
}

void settings_sync_display_rotation(bool rotated) {
  sync_display_rotation_state(rotated ? Device::kRotationFlipped : Device::kRotationDefault);
  displayManager.setRotation(display_rotation_quarters);
  settings_screen::sync_rotation();
  lv_obj_invalidate(lv_scr_act());
  lv_display_t* disp = lv_display_get_default();
  if (disp) {
    lv_refr_now(disp);
  }
}

namespace settings_model {

const i18n::Strings& text() { return tr(); }

uint8_t language_count() { return static_cast<uint8_t>(i18n::language_count()); }

const i18n::Strings& text_of(uint8_t language) { return i18n::strings(i18n::language_code_at(language)); }

uint32_t card_color() { return settings_tile_color(); }

DisplayValues display_values() {
  const DeviceConfig& cfg = configManager.getConfig();
  DisplayValues values;
  values.brightness = brightness_pct_from_raw(BoardHAL::getBrightness());
  values.brightness_min = kSettingsBrightnessPctMin;
  values.brightness_max = kSettingsBrightnessPctMax;
  values.sleep_index = sleep_index_of(cfg.auto_sleep_enabled, cfg.auto_sleep_seconds);
  values.saver_index = sleep_index_of(cfg.auto_screensaver_enabled, cfg.auto_screensaver_seconds);
  values.saver_brightness = cfg.screensaver_brightness_pct;
  // The screensaver brightness starts at the device floor too.
  values.saver_brightness_min = Device::kConfiguredBrightnessPercentMin;
  values.saver_brightness_max = kScreensaverBrightnessPctMax;
  return values;
}

int saved_brightness() { return brightness_pct_from_raw(configManager.getConfig().display_brightness); }

int saved_sleep_index() {
  const DeviceConfig& cfg = configManager.getConfig();
  return sleep_index_of(cfg.auto_sleep_enabled, cfg.auto_sleep_seconds);
}

int sleep_steps() { return sleep_slider_max_index(); }

unsigned sleep_step_seconds(int index) { return sleep_seconds_from_index(index); }

bool quarter_turns() { return Device::supportsQuarterTurnRotation(); }

uint8_t rotation_index() { return display_rotation_index(); }

void brightness_changed(int percent, bool final) {
  const uint8_t raw = brightness_raw_from_pct(percent);
  BoardHAL::setBrightness(raw);
  if (!final) return;
  const DeviceConfig& cfg = configManager.getConfig();
  save_display_settings(raw, cfg.auto_sleep_enabled, cfg.auto_sleep_seconds);
}

void sleep_changed(int index, bool final) {
  if (!final) return;
  const DeviceConfig& cfg = configManager.getConfig();
  const bool enabled = !sleep_index_is_never(index);
  save_display_settings(cfg.display_brightness, enabled,
                        enabled ? sleep_seconds_from_index(index) : cfg.auto_sleep_seconds);
}

void saver_changed(int index, bool final) {
  if (!final) return;
  const DeviceConfig& cfg = configManager.getConfig();
  const bool enabled = !sleep_index_is_never(index);
  configManager.saveScreensaverTimeout(
      enabled, enabled ? sleep_seconds_from_index(index) : cfg.auto_screensaver_seconds);
}

void saver_brightness_changed(int percent, bool final) {
  // Every touch previews the level, a tap on the current value too; one
  // second after the last one the normal UI returns to its own brightness.
  powerManager.setDisplayBrightness(Device::backlightRawFromPercent(static_cast<uint8_t>(percent)));
  schedule_screensaver_brightness_preview_restore();
  if (!final) return;
  // A drag ending outside the slider (PRESS_LOST) commits like a release.
  if (configManager.saveScreensaverBrightness(static_cast<uint8_t>(percent))) {
    image_screensaver_brightness_changed();
    mqttPublishDeviceSettings();
  }
}

void rotation_selected(uint8_t index) {
  apply_display_rotation(Device::supportsQuarterTurnRotation()
                             ? static_cast<uint8_t>((Device::kRotationDefault + index) & 0x03)
                             : (index ? Device::kRotationFlipped : Device::kRotationDefault));
}

}  // namespace settings_model

// Timezone codes and display names come from the central i18n catalog
// (i18n::timezone_option and LocaleProfile::timezone_labels), keeping
// the device and Web Admin lists identical.

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

// Keeps the strongest entry per SSID, strongest first, and frees the scan.
static void wifi_collect_scan(int16_t n) {
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

// The Bridge started pairing over the direct link: Settings opens on System,
// whose dialog shows the number while the attempt runs.
void settings_show_pairing() {
  uiManager.switchToTab(3);
  settings_screen::show_category(3);
  settings_screen::system_changed();
}

// ---------- Settings model: category lines and navigation ----------
namespace settings_model {

// ---------- Localization page ----------

uint8_t locale_option_count(LocaleList list) {
  switch (list) {
    case LocaleList::Language:
      return static_cast<uint8_t>(i18n::language_count());
    case LocaleList::TimeZone:
      return static_cast<uint8_t>(i18n::kTimezoneOptionCount);
    case LocaleList::TimeFormat:
      return 3;
    case LocaleList::DateFormat:
      return 4;
    case LocaleList::Keyboard:
      return 3;
  }
  return 0;
}

// The same options as Web Admin: native language names, the time zone names
// of the current language, Auto first in the format and keyboard lists. The
// date patterns and keyboard names identify their formats and stay as they
// are in every language.
const char* locale_option(LocaleList list, uint8_t index) {
  const i18n::Strings& s = tr();
  if (index >= locale_option_count(list)) index = 0;
  switch (list) {
    case LocaleList::Language:
      return i18n::language_native_name_at(index);
    case LocaleList::TimeZone:
      return i18n::locale(configManager.getConfig().language).timezone_labels[index];
    case LocaleList::TimeFormat: {
      const char* const options[] = {s.format_auto_language, s.format_24_hour, s.format_12_hour};
      return options[index];
    }
    case LocaleList::DateFormat: {
      const char* const options[] = {s.format_auto_language, "DD.MM.YYYY", "MM/DD/YYYY", "YYYY/MM/DD"};
      return options[index];
    }
    case LocaleList::Keyboard: {
      const char* const options[] = {s.format_auto_language, "Deutsch (QWERTZ)", "English (QWERTY)"};
      return options[index];
    }
  }
  return "";
}

uint8_t locale_selected(LocaleList list) {
  const DeviceConfig& cfg = configManager.getConfig();
  switch (list) {
    case LocaleList::Language:
      return static_cast<uint8_t>(i18n::language_index(cfg.language));
    case LocaleList::TimeZone:
      return static_cast<uint8_t>(settings_timezone_index(cfg.timezone));
    case LocaleList::TimeFormat:
      return clock_tile::normalize_time_format(cfg.global_time_format);
    case LocaleList::DateFormat:
      return clock_tile::normalize_date_format(cfg.global_date_format);
    case LocaleList::Keyboard:
      return cfg.keyboard_layout > 2 ? 0 : cfg.keyboard_layout;
  }
  return 0;
}

void locale_selected_changed(LocaleList list, uint8_t index) {
  if (index >= locale_option_count(list)) return;
  DeviceConfig cfg = configManager.getConfig();
  switch (list) {
    case LocaleList::Language:
      strncpy(cfg.language, i18n::language_code_at(index), sizeof(cfg.language) - 1);
      cfg.language[sizeof(cfg.language) - 1] = '\0';
      break;
    case LocaleList::TimeZone:
      strncpy(cfg.timezone, selected_timezone_code(index), sizeof(cfg.timezone) - 1);
      cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';
      break;
    case LocaleList::TimeFormat:
      cfg.global_time_format = clock_tile::normalize_time_format(index);
      break;
    case LocaleList::DateFormat:
      cfg.global_date_format = clock_tile::normalize_date_format(index);
      break;
    case LocaleList::Keyboard:
      cfg.keyboard_layout = index;
      break;
  }
  if (!configManager.save(cfg)) {
    Serial.println("[Settings] Saving the localization failed");
    return;
  }
  if (list == LocaleList::Keyboard) return;
  // As the former Save button did: texts, clock and tiles follow at once.
  if (list == LocaleList::Language) settings_refresh_language();
  if (list == LocaleList::TimeZone) uiManager.scheduleNtpSync(0);
  tiles_request_reload_all();
}

// ---------- System page ----------
// The update check, the OTA install and Restart run through the sketch's
// callbacks as before; their results arrive in settings_fw_* below.
static UpdateState g_update_note = UpdateState::Idle;
static int g_update_progress = 0;
// When Pair and Allow were pressed: their two-minute windows count down.
static uint32_t g_pair_started_ms = 0;
static uint32_t g_password_allowed_ms = 0;
static const char* g_pairing_note = nullptr;
static constexpr uint32_t kWindowMs = 120000;

static int seconds_left(uint32_t since_ms) {
  if (!since_ms) return 0;
  const uint32_t age = millis() - since_ms;
  if (age >= kWindowMs) return 1;
  return static_cast<int>((kWindowMs - age + 999) / 1000);
}

SystemValues system_values() {
  using command_channel::PairingPhase;
  SystemValues v = {};
  const bool found = system_update_available && system_latest_tag[0];
  if (g_update_note == UpdateState::Installed || g_update_note == UpdateState::Restarting) {
    // The panel restarts next.
    v.update = g_update_note;
  } else if (system_install_running) {
    v.update = UpdateState::Downloading;
  } else if (system_check_running) {
    v.update = UpdateState::Checking;
  } else if (found && g_update_note != UpdateState::InstallFailed) {
    v.update = UpdateState::Available;
  } else {
    v.update = g_update_note;
  }
  v.progress = g_update_progress;
  v.connected = networkManager.isMqttConnected();

  const PairingPhase phase = command_channel::pairingPhase();
  // Without an answer, let an older Bridge find the panel again, as the
  // former Pairing button did (MQTT reconnect).
  if (phase == PairingPhase::NoAnswer && security_rediscover_pending) {
    security_rediscover_pending = false;
    if (g_ha_pair_callback) g_ha_pair_callback();
  }
  switch (phase) {
    case PairingPhase::Discoverable:
      v.pairing = PairState::Discoverable;
      v.pair_seconds = seconds_left(g_pair_started_ms);
      break;
    case PairingPhase::Asking:
      v.pairing = PairState::Asking;
      break;
    case PairingPhase::Compare:
      v.pairing = PairState::Compare;
      break;
    case PairingPhase::Confirmed:
      v.pairing = PairState::Confirmed;
      break;
    case PairingPhase::NoAnswer:
      v.pairing = PairState::NoAnswer;
      break;
    case PairingPhase::AlreadyPaired:
      v.pairing = PairState::AlreadyPaired;
      break;
    case PairingPhase::Busy:
      v.pairing = PairState::Busy;
      break;
    case PairingPhase::Rejected:
      v.pairing = PairState::Rejected;
      break;
    case PairingPhase::Failed:
      v.pairing = PairState::Failed;
      break;
    default:
      v.pairing = command_channel::state() == command_channel::PairingState::Active ? PairState::Paired
                                                                                    : PairState::NotPaired;
      break;
  }
  v.password_on = web_admin_auth::enabled();
  v.password_window = !v.password_on && web_admin_auth::firstPasswordAllowed();
  v.password_seconds = v.password_window ? seconds_left(g_password_allowed_ms) : 0;
  return v;
}

const char* latest_version() { return system_latest_tag; }

const char* device_name() { return Device::displayName(); }

bool panel_address(char* buf, size_t len) {
  if (ap_mode_active) {
    // softAPIP() can still return 0.0.0.0 right after the hotspot started.
    const String ip = WiFi.softAPIP().toString();
    snprintf(buf, len, "%s", ip.length() && ip != "0.0.0.0" ? ip.c_str() : "192.168.4.1");
    return true;
  }
  if (!networkTransport.isConnected()) return false;
  snprintf(buf, len, "%s", networkTransport.localIP().toString().c_str());
  return true;
}

bool pairing_number(char* buf, size_t len) {
  char number[command_channel::kPairNumberDisplaySize];
  if (!command_channel::pairingNumber(number)) return false;
  snprintf(buf, len, "%s", number);
  return true;
}

const char* pairing_note() { return g_pairing_note; }

const char* repo_url() { return GithubUpdate::kRepoUrl; }

void update_pressed() {
  if (system_check_running || system_install_running) return;
  if (system_update_available && system_latest_tag[0]) {
    // The sketch pauses MQTT and Web Admin, downloads the release asset and
    // restarts on success.
    if (!g_fw_install_callback) return;
    system_install_running = true;
    g_update_note = UpdateState::Idle;
    g_update_progress = 0;
    g_fw_install_callback(system_latest_tag);
    return;
  }
  if (!g_fw_check_callback) return;
  system_check_running = true;
  g_update_note = UpdateState::Idle;
  g_fw_check_callback();
}

void restart() {
  if (system_install_running || !g_system_reboot_callback) return;
  system_install_running = true;
  g_update_note = UpdateState::Restarting;
  settings_screen::system_changed();
  g_system_reboot_callback();
}

void pair() {
  using command_channel::PairingPhase;
  g_pairing_note = nullptr;
  const PairingPhase phase = command_channel::pairingPhase();
  // A finished attempt's result stays until it is cleared.
  if (phase == PairingPhase::NoAnswer || phase == PairingPhase::AlreadyPaired || phase == PairingPhase::Busy ||
      phase == PairingPhase::Rejected || phase == PairingPhase::Failed) {
    command_channel::endPairing();
  }
  if (command_channel::startPairing()) {
    security_rediscover_pending = true;
    g_pair_started_ms = millis() | 1;
  } else {
    g_pairing_note = tr().pairing_failed;
  }
}

void confirm_pairing() { command_channel::confirmPairing(); }

void cancel_pairing() {
  security_rediscover_pending = false;
  command_channel::endPairing();
}

void unpair() {
  bool bridge_notified = false;
  if (command_channel::disable(&bridge_notified)) {
    // The Bridge removes its side by itself when it was told; otherwise the
    // pairing has to be removed in Home Assistant as well.
    g_pairing_note = bridge_notified ? nullptr : tr().security_unpaired_offline;
  } else {
    g_pairing_note = tr().save_failed;
  }
}

void allow_password() {
  // The first Web Admin password needs this tap at the panel, like Pair.
  web_admin_auth::allowFirstPassword();
  g_password_allowed_ms = millis() | 1;
}

void remove_password() {
  if (web_admin_auth::clearCredential()) {
    // Without the password the Bridge no longer serves extra tile entities.
    entity_search::scheduleTilesReport();
  } else {
    Serial.println("[Settings] Removing the Web Admin password failed");
  }
}

// ---------- WiFi page ----------
// The former WiFi popup's paths: the same scan guards, the same saving and
// reconnect, the hotspot and network mode through the sketch's callbacks.
static bool g_wifi_scanning = false;
// A wanted scan waits while the hotspot is on, the WiFi driver is off or a
// connection was just requested, and starts once that has passed.
static bool g_wifi_scan_wanted = false;
static WifiNetwork g_wifi_list[25];
static uint8_t g_wifi_list_count = 0;
static char g_connect_ssid[33] = {};
static uint32_t g_connect_started_ms = 0;
static bool g_connect_failed = false;
static constexpr uint32_t kConnectTimeoutMs = 20000;

static uint8_t wifi_bars(int16_t rssi) { return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1; }

static bool wifi_station_connected() {
  return !ap_mode_active && networkTransport.activeKind() == NetworkTransportKind::Wifi &&
         networkTransport.isWifiConnected();
}

// The scan's networks without the connected one, plus the saved network when
// the scan missed it (hidden networks are not listed by a scan).
static void wifi_rebuild_list() {
  const char* saved = configManager.getConfig().wifi_ssid;
  const bool connected = wifi_station_connected();
  g_wifi_list_count = 0;
  bool saved_seen = false;
  for (size_t i = 0; i < wifi_scan_result_count && g_wifi_list_count < 24; ++i) {
    const WifiScanEntry& entry = wifi_scan_results[i];
    const bool is_saved = saved[0] && strcmp(saved, entry.ssid) == 0;
    if (is_saved) saved_seen = true;
    if (is_saved && connected) continue;
    WifiNetwork& network = g_wifi_list[g_wifi_list_count++];
    snprintf(network.ssid, sizeof(network.ssid), "%s", entry.ssid);
    network.bars = wifi_bars(entry.rssi);
    network.locked = !entry.open;
  }
  if (saved[0] && !saved_seen && !connected) {
    WifiNetwork& network = g_wifi_list[g_wifi_list_count++];
    snprintf(network.ssid, sizeof(network.ssid), "%s", saved);
    network.bars = 0;
    network.locked = true;
  }
}

static bool wifi_scan_blocked() {
  return ap_mode_active || !networkTransport.isWifiDriverActive() ||
         (wifi_scan_block_until != 0 && (int32_t)(millis() - wifi_scan_block_until) < 0);
}

static void wifi_try_scan() {
  if (!g_wifi_scan_wanted || g_wifi_scanning || wifi_scan_blocked()) return;
  g_wifi_scan_wanted = false;
  // scanNetworks() runs several ESP-Hosted calls; probe the channel first so
  // a stuck coprocessor triggers the central recovery instead.
  if (!networkManager.probeWifiDriverHealth("WiFi scan in Settings")) return;
  WiFi.scanDelete();
  if (WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_FAILED) return;
  g_wifi_scanning = true;
}

static void wifi_stop_scan() {
  g_wifi_scanning = false;
  g_wifi_scan_wanted = false;
  if (networkTransport.isWifiDriverActive()) WiFi.scanDelete();
}

WifiValues wifi_values() {
  if (g_wifi_scanning) {
    const int16_t n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) {
      g_wifi_scanning = false;
      wifi_collect_scan(n);
    }
  }
  wifi_try_scan();
  wifi_rebuild_list();
  const DeviceConfig& cfg = configManager.getConfig();
  WifiValues v = {};
  v.ethernet_panel = NetworkTransportManager::deviceSupportsEthernet();
  v.ethernet_selected = cfg.ethernet_enabled;
  v.ethernet_active = networkTransport.isEthernetMode();
  v.access_point = ap_mode_active;
  v.hotspot_switching = ap_mode_click_block_until != 0 && (int32_t)(millis() - ap_mode_click_block_until) < 0;
  v.connected = v.ethernet_active ? networkTransport.isConnected() : wifi_station_connected();
  v.bars = 4;
  for (size_t i = 0; i < wifi_scan_result_count; ++i) {
    if (strcmp(wifi_scan_results[i].ssid, cfg.wifi_ssid) == 0) v.bars = wifi_bars(wifi_scan_results[i].rssi);
  }
  // A wanted scan that waits for a connection attempt already counts.
  v.scanning = g_wifi_scanning || (g_wifi_scan_wanted && !ap_mode_active && networkTransport.isWifiDriverActive());
  if (g_connect_started_ms) {
    if (wifi_station_connected() && strcmp(cfg.wifi_ssid, g_connect_ssid) == 0) {
      g_connect_started_ms = 0;
    } else if (millis() - g_connect_started_ms >= kConnectTimeoutMs) {
      g_connect_started_ms = 0;
      g_connect_failed = true;
    }
  }
  v.connecting = g_connect_started_ms != 0;
  v.connect_failed = g_connect_failed;
  v.static_ip = cfg.wifi_static_enabled;
  v.static_ip_available = selected_static_addressing_available();
  const bool boot_static = configManager.bootStaticAddressingEnabled();
  // On WiFi only while a static address is in use or was just changed: the
  // way back to DHCP when a wrong address made the panel unreachable.
  v.ip_mode_offered = v.ethernet_active || (boot_static && (v.static_ip || v.static_ip_available)) ||
                      v.static_ip != boot_static;
  v.restart_needed = (v.ethernet_panel && v.ethernet_selected != v.ethernet_active) || v.static_ip != boot_static;
  return v;
}

// As the former keyboard chose it (ui_keyboard layout_for_config).
uint8_t keyboard_layout() {
  const DeviceConfig& cfg = configManager.getConfig();
  if (cfg.keyboard_layout == 1) return 1;
  if (cfg.keyboard_layout == 2) return 0;
  const char* lang = cfg.language;
  if (lang[0] == 'd' && lang[1] == 'e') return 1;
  if (lang[0] == 'f' && lang[1] == 'r') return 2;
  return 0;
}

bool ethernet_panel() { return NetworkTransportManager::deviceSupportsEthernet(); }

bool ethernet_active() { return networkTransport.isEthernetMode(); }

uint8_t wifi_network_count() { return g_wifi_list_count; }

const WifiNetwork& wifi_network(uint8_t index) {
  return g_wifi_list[index < g_wifi_list_count ? index : 0];
}

const char* wifi_saved_password(const char* ssid) {
  const DeviceConfig& cfg = configManager.getConfig();
  return ssid && ssid[0] && strcmp(ssid, cfg.wifi_ssid) == 0 ? cfg.wifi_pass : "";
}

void hotspot_details(char* ssid, size_t ssid_len, char* password, size_t password_len) {
  snprintf(ssid, ssid_len, "%s", webConfigApSsid());
  snprintf(password, password_len, "%s", webConfigApPassword());
}

bool static_address(char* buf, size_t len) {
  const char* ip = configManager.getConfig().wifi_static_ip;
  if (!valid_static_ip_value(ip)) return false;
  snprintf(buf, len, "%s", ip);
  return true;
}

void wifi_scan() {
  g_wifi_scan_wanted = true;
  wifi_try_scan();
}

// Saves the network (a new one resets a static address to DHCP, so an old
// address cannot make the panel unreachable) and connects in the main loop.
void wifi_connect(const char* ssid, const char* password) {
  if (!ssid || !ssid[0]) return;
  const char* pass = password ? password : "";
  const DeviceConfig& current = configManager.getConfig();
  const bool changed = strcmp(current.wifi_ssid, ssid) != 0 || strcmp(current.wifi_pass, pass) != 0;
  if (changed) {
    DeviceConfig cfg = configManager.getConfig();
    strncpy(cfg.wifi_ssid, ssid, CONFIG_WIFI_SSID_MAX - 1);
    cfg.wifi_ssid[CONFIG_WIFI_SSID_MAX - 1] = '\0';
    strncpy(cfg.wifi_pass, pass, CONFIG_WIFI_PASS_MAX - 1);
    cfg.wifi_pass[CONFIG_WIFI_PASS_MAX - 1] = '\0';
    cfg.wifi_static_enabled = false;
    cfg.wifi_static_ip[0] = '\0';
    cfg.wifi_gateway[0] = '\0';
    cfg.wifi_subnet[0] = '\0';
    cfg.wifi_dns[0] = '\0';
    if (!configManager.save(cfg)) {
      Serial.println("[Settings] Saving the WiFi network failed");
      g_connect_failed = true;
      return;
    }
  }
  wifi_stop_scan();
  // No scan may compete with the connection setup.
  wifi_scan_block_until = millis() + 10000UL;
  if (ap_mode_active) {
    // The main loop connects with the saved network once the hotspot is off.
    if (g_hotspot_callback) g_hotspot_callback(false);
  } else if (changed || !networkTransport.isWifiConnected()) {
    if (g_wifi_reconnect_callback) g_wifi_reconnect_callback();
  }
  snprintf(g_connect_ssid, sizeof(g_connect_ssid), "%s", ssid);
  g_connect_started_ms = millis() | 1;
  g_connect_failed = false;
}

void wifi_disconnect() {
  if (!g_wifi_disconnect_callback) return;
  wifi_stop_scan();
  g_wifi_disconnect_callback();
}

void hotspot_selected(bool on) {
  if (on == ap_mode_active) return;
  if (ap_mode_click_block_until != 0 && (int32_t)(millis() - ap_mode_click_block_until) < 0) return;
  // The mode changes in the main loop and must not overlap a scan.
  wifi_stop_scan();
  if (g_hotspot_callback) g_hotspot_callback(on);
  ap_mode_click_block_until = millis() + 2500;
}

void network_mode_selected(bool ethernet) {
  if (ethernet == configManager.getConfig().ethernet_enabled) return;
  if (!configManager.saveEthernetEnabled(ethernet)) return;
  Serial.printf("[Settings] Network mode saved: %s (applies after restart)\n", ethernet ? "Ethernet" : "Wi-Fi");
}

void ip_mode_selected(bool static_ip) {
  if (static_ip == configManager.getConfig().wifi_static_enabled) return;
  if (static_ip && !selected_static_addressing_available()) return;
  if (!configManager.saveStaticAddressingEnabled(static_ip)) return;
  Serial.printf("[Settings] Shared IP mode saved: %s (applies after restart)\n", static_ip ? "static" : "DHCP");
}

void wifi_connect_done() {
  g_connect_started_ms = 0;
  g_connect_failed = false;
}

void close_settings() { uiManager.switchToTab(0); }

bool access_point_on() { return ap_mode_active; }

bool network_connected() { return networkTransport.isConnected(); }

void network_name(char* buf, size_t len) {
  const String name = networkTransport.activeKind() == NetworkTransportKind::Wifi
                          ? String(configManager.getConfig().wifi_ssid)
                          : String(networkTransport.activeName());
  snprintf(buf, len, "%s", name.c_str());
}

const char* language_name() { return i18n::locale(configManager.getConfig().language).native_name; }

bool time_text(char* buf, size_t len) {
  struct tm now;
  if (!getLocalTime(&now, 0) || now.tm_year + 1900 < 2023) return false;
  const DeviceConfig& cfg = configManager.getConfig();
  const bool h12 = clock_tile::resolve_time_format(clock_tile::TIME_FORMAT_AUTO, cfg.global_time_format,
                                                   cfg.language) == clock_tile::TIME_FORMAT_12H;
  if (h12) {
    const int hour = now.tm_hour % 12 ? now.tm_hour % 12 : 12;
    snprintf(buf, len, "%d:%02d %s", hour, now.tm_min, now.tm_hour < 12 ? "AM" : "PM");
  } else {
    snprintf(buf, len, "%02d:%02d", now.tm_hour, now.tm_min);
  }
  return true;
}

bool update_available() { return system_update_available; }

const char* firmware_version() { return FW_VERSION; }

}  // namespace settings_model

void settings_prepare_show() {
  settings_screen::prepare_show();
}

// After Settings hid: a running brightness preview ends, the page goes.
void settings_did_hide() {
  if (screensaver_brightness_preview_timer) {
    cancel_screensaver_brightness_preview();
    restore_normal_brightness_after_preview();
  }
  settings_screen::did_hide();
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

// All four responses run on the loop task, which owns LVGL; the System page
// follows them (now while it shows, else when it opens).
void settings_fw_check_result(bool ok, const char* latest_tag, bool update_available) {
  system_check_running = false;
  system_update_available = ok && update_available;
  if (ok && latest_tag && latest_tag[0]) {
    snprintf(system_latest_tag, sizeof(system_latest_tag), "%s", latest_tag);
  }
  settings_model::g_update_note = !ok                    ? settings_model::UpdateState::CheckFailed
                                  : update_available ? settings_model::UpdateState::Idle
                                                     : settings_model::UpdateState::UpToDate;
  settings_screen::system_changed();
  settings_screen::refresh_lines();
}

void settings_fw_install_progress(size_t written, size_t total) {
  if (total == 0) return;
  const int percent = static_cast<int>((written * 100ULL) / total);
  if (percent == settings_model::g_update_progress) return;
  settings_model::g_update_progress = percent;
  settings_screen::system_changed();
}

void settings_fw_install_done() {
  // The page keeps its buttons away until the restart.
  settings_model::g_update_note = settings_model::UpdateState::Installed;
  settings_model::g_update_progress = 100;
  system_install_running = false;
  settings_screen::system_changed();
}

void settings_fw_install_failed(const char* error) {
  system_install_running = false;
  settings_model::g_update_note = settings_model::UpdateState::InstallFailed;
  Serial.printf("[Settings] Update failed: %s\n", error && error[0] ? error : "unknown");
  settings_screen::system_changed();
}

void build_settings_tab(lv_obj_t *tab, hotspot_callback_t hotspot_cb) {
  g_hotspot_callback = hotspot_cb;

  lv_obj_clean(tab);
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(tab, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(tab, LV_OPA_TRANSP, 0);
  // The panel carries the grid margins: positions inside are grid
  // coordinates, so the screen lines up with the Home tiles.
  lv_obj_set_style_pad_left(tab, GRID_PAD_LEFT, 0);
  lv_obj_set_style_pad_right(tab, GRID_PAD_RIGHT, 0);
  lv_obj_set_style_pad_top(tab, GRID_PAD_TOP, 0);
  lv_obj_set_style_pad_bottom(tab, GRID_PAD_BOTTOM, 0);

  const DeviceConfig& cfg = configManager.getConfig();
  display_rotated_180 = cfg.display_rotated_180;
  display_rotation_quarters = Device::normalizeRotationQuarterTurns(cfg.display_rotation_quarters);
  display_rotation_mode = cfg.display_rotation_mode;
  wake_mode_mains = kWakeModeTouch;
  wake_mode_battery = kWakeModeTouch;

  // The frame now; the page when Settings opens (settings_prepare_show).
  settings_screen::build(tab);
}

// Navigation closes every popup: the Settings pages' lists, dialogs and the
// network entry.
void hide_settings_popup() {
  settings_screen::close_overlays();
}

// The main loop reports the network, the hotspot loop on every pass: the
// category lines follow at most once a second (the WiFi page polls the
// rest), so the reports never keep LVGL drawing.
static void refresh_lines_now_and_then() {
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if (last_ms != 0 && now - last_ms < 1000) return;
  last_ms = now | 1;
  settings_screen::refresh_lines();
}

void settings_update_wifi_status(bool, const char*, const char*) { refresh_lines_now_and_then(); }

void settings_update_wifi_status_ap(const char*, const char*) { refresh_lines_now_and_then(); }

void settings_update_ap_mode(bool running) {
  if (running == ap_mode_active) return;
  ap_mode_active = running;
  // The switch is done: the toggle takes touches again.
  ap_mode_click_block_until = 0;
  settings_screen::refresh_lines();
}

// No battery to show on the supported panels.
void settings_update_power_status() {}

void settings_refresh_language() {
  // The Settings screen takes the new texts: now while it shows, else when
  // it opens next.
  settings_screen::texts_changed();
  weather_popup_refresh_language();
}
