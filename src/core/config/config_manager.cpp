#include "src/core/config/config_manager.h"
#include "src/core/config/settings_access_record.h"
#include "src/core/text/title_text.h"
#include "src/core/config/batched_nvs_write.h"
#include "src/core/i18n/i18n.h"
#include "src/types/clock/clock_format.h"
#include <Preferences.h>
#include <stddef.h>
#include <string.h>

// Shared instance.
ConfigManager configManager;

// Preferences namespace
static const char* PREF_NAMESPACE = "tab5_config";
// NVS keys are limited to 15 characters. The former 16-character names were
// rejected by NVS and therefore never persisted successfully.
static constexpr const char* STATUS_TIME_FONT_KEY = "stat_time_font";
static constexpr const char* STATUS_DATE_FONT_KEY = "stat_date_font";
static constexpr const char* SETTINGS_ACCESS_KEY = "set_access_v1";

using settings_access_record::LegacySettingsAccessRecord;
using settings_access_record::SettingsAccessRecordV3;
using settings_access_record::SettingsAccessRecord;
using settings_access_record::clear_settings_tile_snapshot;
using settings_access_record::make_settings_access_record;
using settings_access_record::apply_settings_access_record;
using settings_access_record::apply_settings_access_record_v3;
using settings_access_record::apply_legacy_settings_access_record;
using settings_access_record::fail_open_settings_access;

#if defined(DEVICE_ESP32_S3_RGB_480)
static bool persisted_config_equal(const DeviceConfig& a,
                                   const DeviceConfig& b) {
  return strcmp(a.wifi_ssid, b.wifi_ssid) == 0 &&
         strcmp(a.wifi_pass, b.wifi_pass) == 0 &&
         strcmp(a.wifi_static_ip, b.wifi_static_ip) == 0 &&
         strcmp(a.wifi_gateway, b.wifi_gateway) == 0 &&
         strcmp(a.wifi_subnet, b.wifi_subnet) == 0 &&
         strcmp(a.wifi_dns, b.wifi_dns) == 0 &&
         a.wifi_static_enabled == b.wifi_static_enabled &&
         strcmp(a.mqtt_host, b.mqtt_host) == 0 &&
         a.mqtt_port == b.mqtt_port &&
         strcmp(a.mqtt_user, b.mqtt_user) == 0 &&
         strcmp(a.mqtt_pass, b.mqtt_pass) == 0 &&
         strcmp(a.mqtt_client_id, b.mqtt_client_id) == 0 &&
         strcmp(a.mqtt_base_topic, b.mqtt_base_topic) == 0 &&
         strcmp(a.ha_prefix, b.ha_prefix) == 0 &&
         strcmp(a.language, b.language) == 0 &&
         strcmp(a.timezone, b.timezone) == 0 &&
         a.global_time_format == b.global_time_format &&
         a.global_date_format == b.global_date_format &&
         a.keyboard_layout == b.keyboard_layout &&
         a.configured == b.configured &&
         a.display_brightness == b.display_brightness &&
         a.screensaver_brightness_pct == b.screensaver_brightness_pct &&
         a.tile_borders == b.tile_borders &&
         a.display_rotated_180 == b.display_rotated_180 &&
         a.display_rotation_quarters == b.display_rotation_quarters &&
         a.display_rotation_mode == b.display_rotation_mode &&
         a.wake_mode_mains == b.wake_mode_mains &&
         a.wake_mode_battery == b.wake_mode_battery &&
         a.auto_sleep_enabled == b.auto_sleep_enabled &&
         a.auto_sleep_seconds == b.auto_sleep_seconds &&
         a.auto_screensaver_enabled == b.auto_screensaver_enabled &&
         a.auto_screensaver_seconds == b.auto_screensaver_seconds &&
         a.auto_sleep_battery_enabled == b.auto_sleep_battery_enabled &&
         a.auto_sleep_battery_seconds == b.auto_sleep_battery_seconds &&
         a.status_time_font_size == b.status_time_font_size &&
         a.status_date_font_size == b.status_date_font_size &&
         a.ethernet_enabled == b.ethernet_enabled &&
         a.settings_pin_enabled == b.settings_pin_enabled &&
         a.settings_tile_hidden == b.settings_tile_hidden &&
         a.settings_swipe_enabled == b.settings_swipe_enabled &&
         a.settings_reveal_edge == b.settings_reveal_edge &&
         memcmp(a.settings_pin_salt, b.settings_pin_salt,
                sizeof(a.settings_pin_salt)) == 0 &&
         memcmp(a.settings_pin_hash, b.settings_pin_hash,
                sizeof(a.settings_pin_hash)) == 0 &&
         strcmp(a.settings_pin_value, b.settings_pin_value) == 0 &&
         a.settings_tile_snapshot.valid == b.settings_tile_snapshot.valid &&
         strcmp(a.settings_tile_snapshot.title,
                b.settings_tile_snapshot.title) == 0 &&
         strcmp(a.settings_tile_snapshot.icon_name,
                b.settings_tile_snapshot.icon_name) == 0 &&
         a.settings_tile_snapshot.bg_color ==
             b.settings_tile_snapshot.bg_color &&
         a.settings_tile_snapshot.col == b.settings_tile_snapshot.col &&
         a.settings_tile_snapshot.row == b.settings_tile_snapshot.row &&
         a.settings_tile_snapshot.span_w == b.settings_tile_snapshot.span_w &&
         a.settings_tile_snapshot.span_h == b.settings_tile_snapshot.span_h;
}
#endif

static uint16_t normalize_sleep_seconds(uint16_t seconds) {
  uint16_t closest = kSleepOptionsSec[0];
  uint16_t best_diff = (seconds > closest) ? (seconds - closest) : (closest - seconds);
  for (size_t i = 1; i < kSleepOptionsSecCount; ++i) {
    uint16_t option = kSleepOptionsSec[i];
    uint16_t diff = (seconds > option) ? (seconds - option) : (option - seconds);
    if (diff < best_diff) {
      best_diff = diff;
      closest = option;
    }
  }
  return closest;
}

static uint8_t rotation_quarters_from_legacy(bool rotate_180) {
  return rotate_180 ? Device::kRotationFlipped : Device::kRotationDefault;
}

static uint8_t normalize_rotation_quarters(uint8_t rotation_quarters) {
  return Device::normalizeRotationQuarterTurns(rotation_quarters);
}

static uint8_t rotation_mode_from_quarters(uint8_t rotation_quarters, uint8_t fallback_mode) {
  if (fallback_mode == kDisplayRotationAuto) {
    return kDisplayRotationAuto;
  }
  return (rotation_quarters == Device::kRotationFlipped)
             ? kDisplayRotationFlipped
             : kDisplayRotationNormal;
}

static void apply_device_capability_limits(DeviceConfig& cfg) {
  if (!Device::kCapabilities.supports_auto_rotation &&
      cfg.display_rotation_mode == kDisplayRotationAuto) {
    cfg.display_rotation_mode =
        (cfg.display_rotation_quarters == Device::kRotationFlipped)
            ? kDisplayRotationFlipped
            : kDisplayRotationNormal;
  }

  if (!Device::kCapabilities.has_imu) {
    cfg.wake_mode_mains = kWakeModeTouch;
    cfg.wake_mode_battery = kWakeModeTouch;
  }

  if (!Device::kCapabilities.supports_battery_sleep_profile) {
    cfg.auto_sleep_battery_enabled = cfg.auto_sleep_enabled;
    cfg.auto_sleep_battery_seconds = cfg.auto_sleep_seconds;
  }
}

static void set_language_code(char* dst, size_t dst_size, const char* language_code) {
  if (!dst || dst_size == 0) return;
  const char* normalized = i18n::normalize_language_code(language_code);
  strncpy(dst, normalized, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

static const char* normalize_timezone_code(const char* timezone_code) {
  if (!timezone_code || !timezone_code[0]) return "berlin";
  struct KnownTimezone {
    const char* code;
  };
  static const KnownTimezone kKnownTimezones[] = {
      {"berlin"},
      {"london"},
      {"utc"},
      {"athens"},
      {"istanbul"},
      {"moscow"},
      {"johannesburg"},
      {"nairobi"},
      {"dubai"},
      {"karachi"},
      {"kolkata"},
      {"dhaka"},
      {"bangkok"},
      {"singapore"},
      {"perth"},
      {"new_york"},
      {"chicago"},
      {"denver"},
      {"phoenix"},
      {"los_angeles"},
      {"honolulu"},
      {"buenos_aires"},
      {"sao_paulo"},
      {"tokyo"},
      {"darwin"},
      {"sydney"},
      {"auckland"},
  };
  for (const auto& tz : kKnownTimezones) {
    if (strcasecmp(timezone_code, tz.code) == 0) {
      return tz.code;
    }
  }
  return "berlin";
}

static void set_timezone_code(char* dst, size_t dst_size, const char* timezone_code) {
  if (!dst || dst_size == 0) return;
  const char* normalized = normalize_timezone_code(timezone_code);
  strncpy(dst, normalized, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

static uint8_t normalize_global_time_format(int raw) {
  return clock_tile::normalize_time_format(raw);
}

static uint8_t normalize_global_date_format(int raw) {
  return clock_tile::normalize_date_format(raw);
}

ConfigManager::ConfigManager() {
  memset(&config, 0, sizeof(config));
  config.configured = false;
  config.mqtt_port = 1883;  // Default MQTT Port
  strncpy(config.mqtt_base_topic, "hometiles", CONFIG_MQTT_BASE_MAX - 1);
  strncpy(config.ha_prefix, "ha/statestream", CONFIG_HA_PREFIX_MAX - 1);
  set_language_code(config.language, sizeof(config.language), "en");
  set_timezone_code(config.timezone, sizeof(config.timezone), "berlin");
  config.global_time_format = clock_tile::TIME_FORMAT_AUTO;
  config.global_date_format = clock_tile::DATE_FORMAT_AUTO;
  config.keyboard_layout = 0;  // Auto (follows the language).

  // Display & Power Defaults
  config.display_brightness = 200;
  config.screensaver_brightness_pct = kScreensaverBrightnessPctDefault;
  config.tile_borders = true;
  config.display_rotated_180 = false;
  config.display_rotation_quarters = Device::kRotationDefault;
  config.display_rotation_mode = kDisplayRotationNormal;
  config.wake_mode_mains = kWakeModeTouch;
  config.wake_mode_battery = kWakeModeTouch;
  config.auto_sleep_enabled = true;
  config.auto_sleep_seconds = 60;
  config.auto_screensaver_enabled = false;
  config.auto_screensaver_seconds = 60;
  config.auto_sleep_battery_enabled = config.auto_sleep_enabled;
  config.auto_sleep_battery_seconds = config.auto_sleep_seconds;
  config.status_time_font_size = 48;
  config.status_date_font_size = 24;
  config.ethernet_enabled = false;
  config.wifi_static_enabled = false;
  config.settings_pin_enabled = false;
  config.settings_tile_hidden = false;
  config.settings_swipe_enabled = false;
  config.settings_reveal_edge =
      static_cast<uint8_t>(SettingsRevealEdge::Left);
  clear_settings_tile_snapshot(config);
}

static uint8_t normalize_display_brightness(uint8_t brightness) {
  const uint8_t configured_min = Device::backlightRawFromPercent(
      Device::kConfiguredBrightnessPercentMin);
  if (brightness >= configured_min) return brightness;
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) || \
    defined(DEVICE_WAVESHARE_4B)
  // Preserve raw 0 for the low-level sleep/restart paths only. Persisted
  // normal brightness must remain at the exact panel's measured visible floor.
  return configured_min;
#elif defined(DEVICE_GUITION_ESP32_4848S040)
  // The first S3 brightness test build could persist values below the panel's
  // measured visible threshold. Upgrade those values to the new 1 % floor
  // instead of unexpectedly jumping to the generic default.
  return Device::kBacklightInputMin;
#else
  return 200;
#endif
}

bool ConfigManager::load() {
  Preferences prefs;

  if (!prefs.begin(PREF_NAMESPACE, true)) {  // readonly
    Serial.println("⚠️ ConfigManager: Failed to open preferences");
    return false;
  }

  // Check whether a configuration is stored.
  config.configured = prefs.getBool("configured", false);

  if (!config.configured) {
    Serial.println("ℹ️ ConfigManager: No configuration available");
    prefs.end();
    return false;
  }

  // Load configuration data.
  config.ethernet_enabled = prefs.getBool("eth_mode", false);
  prefs.getString("wifi_ssid", config.wifi_ssid, CONFIG_WIFI_SSID_MAX);
  prefs.getString("wifi_pass", config.wifi_pass, CONFIG_WIFI_PASS_MAX);
  prefs.getString("wifi_ip", config.wifi_static_ip, CONFIG_IP_ADDR_MAX);
  prefs.getString("wifi_gw", config.wifi_gateway, CONFIG_IP_ADDR_MAX);
  prefs.getString("wifi_subnet", config.wifi_subnet, CONFIG_IP_ADDR_MAX);
  prefs.getString("wifi_dns", config.wifi_dns, CONFIG_IP_ADDR_MAX);
  const bool legacy_wifi_static =
      config.wifi_static_ip[0] || config.wifi_gateway[0] ||
      config.wifi_subnet[0] || config.wifi_dns[0];
  char legacy_eth_ip[CONFIG_IP_ADDR_MAX] = {};
  char legacy_eth_gateway[CONFIG_IP_ADDR_MAX] = {};
  char legacy_eth_subnet[CONFIG_IP_ADDR_MAX] = {};
  char legacy_eth_dns[CONFIG_IP_ADDR_MAX] = {};
  prefs.getString("eth_ip", legacy_eth_ip, sizeof(legacy_eth_ip));
  prefs.getString("eth_gw", legacy_eth_gateway, sizeof(legacy_eth_gateway));
  prefs.getString("eth_subnet", legacy_eth_subnet, sizeof(legacy_eth_subnet));
  prefs.getString("eth_dns", legacy_eth_dns, sizeof(legacy_eth_dns));
  const bool legacy_ethernet_static =
      legacy_eth_ip[0] || legacy_eth_gateway[0] ||
      legacy_eth_subnet[0] || legacy_eth_dns[0];
  const bool migrate_ethernet_values =
      legacy_ethernet_static &&
      (config.ethernet_enabled || !legacy_wifi_static);
  if (migrate_ethernet_values) {
    strncpy(config.wifi_static_ip, legacy_eth_ip, CONFIG_IP_ADDR_MAX - 1);
    strncpy(config.wifi_gateway, legacy_eth_gateway, CONFIG_IP_ADDR_MAX - 1);
    strncpy(config.wifi_subnet, legacy_eth_subnet, CONFIG_IP_ADDR_MAX - 1);
    strncpy(config.wifi_dns, legacy_eth_dns, CONFIG_IP_ADDR_MAX - 1);
    config.wifi_static_ip[CONFIG_IP_ADDR_MAX - 1] = '\0';
    config.wifi_gateway[CONFIG_IP_ADDR_MAX - 1] = '\0';
    config.wifi_subnet[CONFIG_IP_ADDR_MAX - 1] = '\0';
    config.wifi_dns[CONFIG_IP_ADDR_MAX - 1] = '\0';
  }
  const bool legacy_static_enabled =
      migrate_ethernet_values
          ? prefs.getBool("eth_static", legacy_ethernet_static)
          : prefs.getBool("wifi_static", legacy_wifi_static);
  config.wifi_static_enabled =
      prefs.isKey("net_static")
          ? prefs.getBool("net_static", legacy_static_enabled)
          : legacy_static_enabled;
  prefs.getString("mqtt_host", config.mqtt_host, CONFIG_MQTT_HOST_MAX);
  config.mqtt_port = prefs.getUShort("mqtt_port", 1883);
  prefs.getString("mqtt_user", config.mqtt_user, CONFIG_MQTT_USER_MAX);
  prefs.getString("mqtt_pass", config.mqtt_pass, CONFIG_MQTT_PASS_MAX);
  prefs.getString("mqtt_client_id", config.mqtt_client_id, CONFIG_MQTT_CLIENT_ID_MAX);
  prefs.getString("mqtt_base", config.mqtt_base_topic, CONFIG_MQTT_BASE_MAX);
  prefs.getString("ha_prefix", config.ha_prefix, CONFIG_HA_PREFIX_MAX);
  char stored_language[CONFIG_LANG_MAX] = {0};
  prefs.getString("lang", stored_language, CONFIG_LANG_MAX);
  set_language_code(config.language, sizeof(config.language), stored_language);
  char stored_timezone[CONFIG_TIMEZONE_MAX] = {0};
  prefs.getString("tz", stored_timezone, CONFIG_TIMEZONE_MAX);
  set_timezone_code(config.timezone, sizeof(config.timezone), stored_timezone);
  config.global_time_format =
      normalize_global_time_format(prefs.getUChar("time_fmt", clock_tile::TIME_FORMAT_AUTO));
  config.global_date_format =
      normalize_global_date_format(prefs.getUChar("date_fmt", clock_tile::DATE_FORMAT_AUTO));
  config.keyboard_layout = prefs.getUChar("kb_layout", 0);
  if (config.keyboard_layout > 2) config.keyboard_layout = 0;

  // Load display and power settings.
  config.display_brightness = prefs.getUChar("disp_bright", 200);
  // Existing installations should also visibly dim the screensaver.
  // Use its own default instead of deriving it from normal brightness,
  // which made the feature visually ineffective at 100 %.
  config.screensaver_brightness_pct =
      prefs.getUChar("ss_bright", kScreensaverBrightnessPctDefault);
  config.tile_borders = prefs.getBool("tile_border", true);
  bool rot_180 = prefs.getBool("disp_rot180", false);
  uint8_t rot_mode = rot_180 ? kDisplayRotationFlipped : kDisplayRotationNormal;
  if (prefs.isKey("disp_rot_mode")) {
    rot_mode = prefs.getUChar("disp_rot_mode", rot_mode);
  }
  if (rot_mode > kDisplayRotationAuto) {
    rot_mode = rot_180 ? kDisplayRotationFlipped : kDisplayRotationNormal;
  }
  uint8_t rotation_quarters = rotation_quarters_from_legacy(rot_180);
  if (prefs.isKey("disp_rot_q")) {
    rotation_quarters = prefs.getUChar("disp_rot_q", rotation_quarters);
  }
  rotation_quarters = normalize_rotation_quarters(rotation_quarters);
  config.display_rotation_quarters = rotation_quarters;
  config.display_rotated_180 = (rotation_quarters == Device::kRotationFlipped);
  config.display_rotation_mode = rotation_mode_from_quarters(rotation_quarters, rot_mode);
  uint8_t wake_mains = prefs.getUChar("wake_mains", config.wake_mode_mains);
  if (wake_mains > kWakeModeImu) wake_mains = config.wake_mode_mains;
  config.wake_mode_mains = wake_mains;
  uint8_t wake_battery = prefs.getUChar("wake_bat", config.wake_mode_battery);
  if (wake_battery > kWakeModeImu) wake_battery = config.wake_mode_battery;
  config.wake_mode_battery = wake_battery;
  config.auto_sleep_enabled = prefs.getBool("sleep_en", true);
  uint16_t sleep_seconds = 60;
  if (prefs.isKey("sleep_sec")) {
    sleep_seconds = prefs.getUShort("sleep_sec", 60);
  } else {
    uint16_t sleep_minutes = prefs.getUShort("sleep_min", 1);
    if (sleep_minutes == 0) {
      sleep_minutes = 1;
    }
    sleep_seconds = sleep_minutes * 60;
  }
  config.auto_sleep_seconds = normalize_sleep_seconds(sleep_seconds);
  config.auto_screensaver_enabled = prefs.getBool("ss_auto_en", false);
  config.auto_screensaver_seconds = normalize_sleep_seconds(
      prefs.getUShort("ss_auto_sec", 60));
  config.auto_sleep_battery_enabled = prefs.isKey("sleep_bat_en")
                                      ? prefs.getBool("sleep_bat_en", config.auto_sleep_enabled)
                                      : config.auto_sleep_enabled;
  uint16_t sleep_bat_seconds = config.auto_sleep_seconds;
  if (prefs.isKey("sleep_bat_sec")) {
    sleep_bat_seconds = prefs.getUShort("sleep_bat_sec", config.auto_sleep_seconds);
  } else if (prefs.isKey("sleep_bat_min")) {
    uint16_t sleep_bat_minutes = prefs.getUShort("sleep_bat_min", (sleep_seconds / 60));
    if (sleep_bat_minutes == 0) {
      sleep_bat_minutes = 1;
    }
    sleep_bat_seconds = sleep_bat_minutes * 60;
  }
  config.auto_sleep_battery_seconds = normalize_sleep_seconds(sleep_bat_seconds);
  config.status_time_font_size = prefs.getUChar(STATUS_TIME_FONT_KEY, 48);
  if (config.status_time_font_size != 24 && config.status_time_font_size != 48) config.status_time_font_size = 48;
  config.status_date_font_size = prefs.getUChar(STATUS_DATE_FONT_KEY, 24);
  if (config.status_date_font_size != 20 && config.status_date_font_size != 24) config.status_date_font_size = 24;

  fail_open_settings_access(config);
  const size_t access_length = prefs.getBytesLength(SETTINGS_ACCESS_KEY);
  if (access_length == sizeof(SettingsAccessRecord)) {
    SettingsAccessRecord access{};
    const bool valid =
        prefs.getBytes(SETTINGS_ACCESS_KEY, &access, sizeof(access)) ==
            sizeof(access) &&
        apply_settings_access_record(access, config);
    pin_access::secureClear(&access, sizeof(access));
    if (!valid) fail_open_settings_access(config);
  } else if (access_length == sizeof(SettingsAccessRecordV3)) {
    SettingsAccessRecordV3 access{};
    const bool valid =
        prefs.getBytes(SETTINGS_ACCESS_KEY, &access, sizeof(access)) ==
            sizeof(access) &&
        apply_settings_access_record_v3(access, config);
    pin_access::secureClear(&access, sizeof(access));
    if (!valid) fail_open_settings_access(config);
  } else if (access_length == sizeof(LegacySettingsAccessRecord)) {
    LegacySettingsAccessRecord access{};
    const bool valid =
        prefs.getBytes(SETTINGS_ACCESS_KEY, &access, sizeof(access)) ==
            sizeof(access) &&
        apply_legacy_settings_access_record(access, config);
    pin_access::secureClear(&access, sizeof(access));
    if (!valid) fail_open_settings_access(config);
  } else if (access_length == 0) {
    // One-time compatibility path for test builds that wrote the original
    // individual keys. The next successful save replaces them with one
    // checksummed NVS blob.
    config.settings_pin_enabled = prefs.getBool("set_pin_en", false);
    config.settings_tile_hidden = prefs.getBool("set_hidden", false);
    config.settings_swipe_enabled = config.settings_tile_hidden;
    config.settings_reveal_edge = prefs.getUChar(
        "set_edge", static_cast<uint8_t>(SettingsRevealEdge::Left));
    if (prefs.getBytesLength("set_pin_salt") ==
        sizeof(config.settings_pin_salt)) {
      prefs.getBytes("set_pin_salt", config.settings_pin_salt,
                     sizeof(config.settings_pin_salt));
    }
    if (prefs.getBytesLength("set_pin_hash") ==
        sizeof(config.settings_pin_hash)) {
      prefs.getBytes("set_pin_hash", config.settings_pin_hash,
                     sizeof(config.settings_pin_hash));
    }
  }
  if (config.settings_reveal_edge >
          static_cast<uint8_t>(SettingsRevealEdge::Bottom) ||
      (config.settings_pin_enabled &&
       !pin_access::credentialIsSet(config.settings_pin_salt,
                                    config.settings_pin_hash))) {
    fail_open_settings_access(config);
  } else {
    if (!config.settings_pin_enabled) {
      pin_access::clearCredential(config.settings_pin_salt,
                                  config.settings_pin_hash);
      pin_access::secureClear(config.settings_pin_value,
                              sizeof(config.settings_pin_value));
    }
  }

  // Keep the access record binary compatible; long titles use an optional key.
  auto& stored_snapshot = config.settings_tile_snapshot;
  const String stored_title = prefs.getString("set_title", "");
  if (stored_snapshot.valid && strlen(stored_snapshot.title) == 31 &&
      stored_title.length() >= 32 && stored_title.length() <= hometiles_title::kMaxBytes &&
      stored_title.startsWith(stored_snapshot.title)) {
    stored_title.toCharArray(stored_snapshot.title, sizeof(stored_snapshot.title));
  }

  apply_device_capability_limits(config);
  boot_static_enabled = config.wifi_static_enabled;

  config.display_brightness =
      normalize_display_brightness(config.display_brightness);
  if (config.screensaver_brightness_pct < kScreensaverBrightnessPctMin ||
      config.screensaver_brightness_pct > kScreensaverBrightnessPctMax) {
    config.screensaver_brightness_pct =
        Device::backlightPercentFromRaw(config.display_brightness);
  }
  if (config.screensaver_brightness_pct <
      Device::kConfiguredBrightnessPercentMin) {
    config.screensaver_brightness_pct =
        Device::kConfiguredBrightnessPercentMin;
  }

  // Restore missing MQTT topic defaults.
  if (config.mqtt_base_topic[0] == '\0') {
    strncpy(config.mqtt_base_topic, "hometiles", CONFIG_MQTT_BASE_MAX - 1);
  }
  if (config.ha_prefix[0] == '\0') {
    strncpy(config.ha_prefix, "ha/statestream", CONFIG_HA_PREFIX_MAX - 1);
  }

  prefs.end();

  Serial.println("✓ ConfigManager: Configuration loaded");
  Serial.printf("  WiFi SSID: %s\n", config.wifi_ssid);
  Serial.printf("  MQTT Host: %s:%u\n", config.mqtt_host, config.mqtt_port);
  Serial.printf("  MQTT User: %s\n", config.mqtt_user);

  return true;
}

bool ConfigManager::save(const DeviceConfig& cfg) {
  DeviceConfig normalized = cfg;
  normalized.display_rotation_quarters =
      normalize_rotation_quarters(normalized.display_rotation_quarters);
  normalized.display_rotated_180 =
      (normalized.display_rotation_quarters == Device::kRotationFlipped);
  normalized.display_rotation_mode = rotation_mode_from_quarters(
      normalized.display_rotation_quarters, normalized.display_rotation_mode);
  normalized.display_brightness =
      normalize_display_brightness(normalized.display_brightness);
  if (normalized.screensaver_brightness_pct < kScreensaverBrightnessPctMin ||
      normalized.screensaver_brightness_pct > kScreensaverBrightnessPctMax) {
    normalized.screensaver_brightness_pct =
        Device::backlightPercentFromRaw(normalized.display_brightness);
  }
  if (normalized.screensaver_brightness_pct <
      Device::kConfiguredBrightnessPercentMin) {
    normalized.screensaver_brightness_pct =
        Device::kConfiguredBrightnessPercentMin;
  }
  apply_device_capability_limits(normalized);
  set_language_code(normalized.language, sizeof(normalized.language),
                    normalized.language);
  set_timezone_code(normalized.timezone, sizeof(normalized.timezone),
                    normalized.timezone);
  normalized.global_time_format =
      normalize_global_time_format(normalized.global_time_format);
  normalized.global_date_format =
      normalize_global_date_format(normalized.global_date_format);
  if (normalized.keyboard_layout > 2) normalized.keyboard_layout = 0;
  if (normalized.settings_reveal_edge >
      static_cast<uint8_t>(SettingsRevealEdge::Bottom)) {
    normalized.settings_reveal_edge =
        static_cast<uint8_t>(SettingsRevealEdge::Left);
  }
  if (normalized.settings_tile_hidden) {
    normalized.settings_swipe_enabled = true;
  }
  if (!normalized.settings_pin_enabled ||
      !pin_access::credentialIsSet(normalized.settings_pin_salt,
                                   normalized.settings_pin_hash)) {
    normalized.settings_pin_enabled = false;
    pin_access::clearCredential(normalized.settings_pin_salt,
                                normalized.settings_pin_hash);
    pin_access::secureClear(normalized.settings_pin_value,
                            sizeof(normalized.settings_pin_value));
  } else {
    normalized.settings_pin_value[
        sizeof(normalized.settings_pin_value) - 1] = '\0';
    const String stored_pin(normalized.settings_pin_value);
    if (!pin_access::isValidUserPin(stored_pin) ||
        !pin_access::verifyCredential(stored_pin.c_str(),
                                      normalized.settings_pin_salt,
                                      normalized.settings_pin_hash)) {
      pin_access::secureClear(normalized.settings_pin_value,
                              sizeof(normalized.settings_pin_value));
    }
  }
  SettingsTileSnapshot& snapshot = normalized.settings_tile_snapshot;
  snapshot.title[sizeof(snapshot.title) - 1] = '\0';
  const std::string normalized_title = hometiles_title::normalize(snapshot.title);
  strncpy(snapshot.title, normalized_title.c_str(), sizeof(snapshot.title));
  snapshot.icon_name[sizeof(snapshot.icon_name) - 1] = '\0';
  if (!snapshot.valid || snapshot.col >= Device::kGridCols ||
      snapshot.row >= Device::kGridRows || snapshot.span_w < 1 ||
      snapshot.span_h < 1 || snapshot.span_w > Device::kGridCols ||
      snapshot.span_h > Device::kGridRows) {
    clear_settings_tile_snapshot(normalized);
  }
#if defined(DEVICE_ESP32_S3_RGB_480)
  normalized.auto_screensaver_seconds =
      normalize_sleep_seconds(normalized.auto_screensaver_seconds);
  normalized.status_time_font_size =
      (normalized.status_time_font_size == 24) ? 24 : 48;
  normalized.status_date_font_size =
      (normalized.status_date_font_size == 20) ? 20 : 24;
  normalized.configured = true;

  if (!runtime_rotation_dirty && persisted_config_equal(normalized, config)) {
    return true;
  }
#endif

  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;

  if (!prefs.begin(PREF_NAMESPACE, false)) {  // read/write
    Serial.println("⚠️ ConfigManager: Failed to open preferences");
    return false;
  }

  // Save every field.
  prefs.putString("wifi_ssid", normalized.wifi_ssid);
  prefs.putString("wifi_pass", normalized.wifi_pass);
  prefs.putString("wifi_ip", normalized.wifi_static_ip);
  prefs.putString("wifi_gw", normalized.wifi_gateway);
  prefs.putString("wifi_subnet", normalized.wifi_subnet);
  prefs.putString("wifi_dns", normalized.wifi_dns);
  prefs.putBool("wifi_static", normalized.wifi_static_enabled);
  prefs.putBool("net_static", normalized.wifi_static_enabled);
  // Older test builds with separate profiles still read the same values.
  prefs.putString("eth_ip", normalized.wifi_static_ip);
  prefs.putString("eth_gw", normalized.wifi_gateway);
  prefs.putString("eth_subnet", normalized.wifi_subnet);
  prefs.putString("eth_dns", normalized.wifi_dns);
  prefs.putBool("eth_static", normalized.wifi_static_enabled);
  prefs.putString("mqtt_host", normalized.mqtt_host);
  prefs.putUShort("mqtt_port", normalized.mqtt_port);
  prefs.putString("mqtt_user", normalized.mqtt_user);
  prefs.putString("mqtt_pass", normalized.mqtt_pass);
  prefs.putString("mqtt_client_id", normalized.mqtt_client_id);
  prefs.putString("mqtt_base", normalized.mqtt_base_topic);
  prefs.putString("ha_prefix", normalized.ha_prefix);
  prefs.putString("lang", normalized.language);
  prefs.putString("tz", normalized.timezone);
  prefs.putUChar("time_fmt", normalized.global_time_format);
  prefs.putUChar("date_fmt", normalized.global_date_format);
  prefs.putUChar("kb_layout", normalized.keyboard_layout);

  // Save display and power settings.
  prefs.putUChar("disp_bright", normalized.display_brightness);
  prefs.putUChar("ss_bright", normalized.screensaver_brightness_pct);
  prefs.putBool("tile_border", normalized.tile_borders);
  prefs.putBool("eth_mode", normalized.ethernet_enabled);
  prefs.putBool("disp_rot180", normalized.display_rotated_180);
  prefs.putUChar("disp_rot_q", normalized.display_rotation_quarters);
  prefs.putUChar("disp_rot_mode", normalized.display_rotation_mode);
  prefs.putUChar("wake_mains", normalized.wake_mode_mains);
  prefs.putUChar("wake_bat", normalized.wake_mode_battery);
  prefs.putBool("sleep_en", normalized.auto_sleep_enabled);
  prefs.putUShort("sleep_sec", normalized.auto_sleep_seconds);
  prefs.putBool("ss_auto_en", normalized.auto_screensaver_enabled);
  prefs.putUShort("ss_auto_sec",
                  normalize_sleep_seconds(normalized.auto_screensaver_seconds));
  prefs.putBool("sleep_bat_en", normalized.auto_sleep_battery_enabled);
  prefs.putUShort("sleep_bat_sec", normalized.auto_sleep_battery_seconds);
  prefs.putUChar(STATUS_TIME_FONT_KEY,
                 (normalized.status_time_font_size == 24) ? 24 : 48);
  prefs.putUChar(STATUS_DATE_FONT_KEY,
                 (normalized.status_date_font_size == 20) ? 20 : 24);
  SettingsAccessRecord settings_access =
      make_settings_access_record(normalized);
  const bool settings_access_written =
      prefs.putBytes(SETTINGS_ACCESS_KEY, &settings_access,
                     sizeof(settings_access)) == sizeof(settings_access);
  pin_access::secureClear(&settings_access, sizeof(settings_access));

  const char* snapshot_title = normalized.settings_tile_snapshot.valid &&
      strlen(normalized.settings_tile_snapshot.title) >= 32
      ? normalized.settings_tile_snapshot.title : "";
  const bool snapshot_title_written =
      prefs.putString("set_title", snapshot_title) == strlen(snapshot_title);

  uint16_t sleep_minutes = (normalized.auto_sleep_seconds + 59) / 60;
  if (sleep_minutes == 0) {
    sleep_minutes = 1;
  }
  prefs.putUShort("sleep_min", sleep_minutes);

  uint16_t sleep_bat_minutes = (normalized.auto_sleep_battery_seconds + 59) / 60;
  if (sleep_bat_minutes == 0) {
    sleep_bat_minutes = 1;
  }
  prefs.putUShort("sleep_bat_min", sleep_bat_minutes);

  prefs.putBool("configured", true);

  const bool transaction_finished = BatchedNvsWrite::finish(prefs);
  if (!settings_access_written || !snapshot_title_written || !transaction_finished) {
    Serial.println("[Config] NVS transaction failed");
    return false;
  }

  // Update the local copy.
  config = normalized;
  config.configured = true;
  runtime_rotation_dirty = false;

  Serial.println("✓ ConfigManager: Configuration saved");
  Serial.printf("  WiFi SSID: %s\n", config.wifi_ssid);
  Serial.printf("  MQTT Host: %s:%u\n", config.mqtt_host, config.mqtt_port);

  return true;
}

bool ConfigManager::saveDisplaySettings(uint8_t brightness,
                                        bool sleep_enabled,
                                        uint16_t sleep_seconds,
                                        bool sleep_battery_enabled,
                                        uint16_t sleep_battery_seconds,
                                        uint8_t rotation_mode,
                                        bool rotate_180,
                                        uint8_t rotation_quarters,
                                        uint8_t wake_mode_mains,
                                        uint8_t wake_mode_battery) {
  // Save display settings only.
  brightness = normalize_display_brightness(brightness);
  uint16_t normalized_sleep_seconds = normalize_sleep_seconds(sleep_seconds);
  uint16_t normalized_bat_seconds = normalize_sleep_seconds(sleep_battery_seconds);
  rotation_quarters = normalize_rotation_quarters(rotation_quarters);
  rotate_180 = (rotation_quarters == Device::kRotationFlipped);
  rotation_mode = rotation_mode_from_quarters(rotation_quarters, rotation_mode);
  DeviceConfig normalized_cfg = config;
  normalized_cfg.display_brightness = brightness;
  normalized_cfg.display_rotated_180 = rotate_180;
  normalized_cfg.display_rotation_quarters = rotation_quarters;
  normalized_cfg.display_rotation_mode = rotation_mode;
  normalized_cfg.wake_mode_mains = wake_mode_mains;
  normalized_cfg.wake_mode_battery = wake_mode_battery;
  normalized_cfg.auto_sleep_enabled = sleep_enabled;
  normalized_cfg.auto_sleep_seconds = normalized_sleep_seconds;
  normalized_cfg.auto_sleep_battery_enabled = sleep_battery_enabled;
  normalized_cfg.auto_sleep_battery_seconds = normalized_bat_seconds;
  apply_device_capability_limits(normalized_cfg);
  wake_mode_mains = normalized_cfg.wake_mode_mains;
  wake_mode_battery = normalized_cfg.wake_mode_battery;
  sleep_battery_enabled = normalized_cfg.auto_sleep_battery_enabled;
  sleep_battery_seconds = normalized_cfg.auto_sleep_battery_seconds;
  normalized_bat_seconds = sleep_battery_seconds;

#if defined(DEVICE_ESP32_S3_RGB_480)
  if (!runtime_rotation_dirty &&
      config.display_brightness == brightness &&
      config.display_rotated_180 == rotate_180 &&
      config.display_rotation_quarters == rotation_quarters &&
      config.display_rotation_mode == rotation_mode &&
      config.wake_mode_mains == wake_mode_mains &&
      config.wake_mode_battery == wake_mode_battery &&
      config.auto_sleep_enabled == sleep_enabled &&
      config.auto_sleep_seconds == normalized_sleep_seconds &&
      config.auto_sleep_battery_enabled == sleep_battery_enabled &&
      config.auto_sleep_battery_seconds == normalized_bat_seconds) {
    return true;
  }
#endif

  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;

  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("⚠️ ConfigManager: Failed to open preferences");
    return false;
  }

  prefs.putUChar("disp_bright", brightness);
  prefs.putBool("disp_rot180", rotate_180);
  prefs.putUChar("disp_rot_q", rotation_quarters);
  prefs.putUChar("disp_rot_mode", rotation_mode);
  prefs.putUChar("wake_mains", wake_mode_mains);
  prefs.putUChar("wake_bat", wake_mode_battery);
  prefs.putBool("sleep_en", sleep_enabled);
  prefs.putUShort("sleep_sec", normalized_sleep_seconds);
  prefs.putBool("sleep_bat_en", sleep_battery_enabled);
  prefs.putUShort("sleep_bat_sec", normalized_bat_seconds);

  uint16_t sleep_minutes = (normalized_sleep_seconds + 59) / 60;
  if (sleep_minutes == 0) {
    sleep_minutes = 1;
  }
  prefs.putUShort("sleep_min", sleep_minutes);

  uint16_t sleep_bat_minutes = (normalized_bat_seconds + 59) / 60;
  if (sleep_bat_minutes == 0) {
    sleep_bat_minutes = 1;
  }
  prefs.putUShort("sleep_bat_min", sleep_bat_minutes);

  if (!BatchedNvsWrite::finish(prefs)) {
    Serial.println("ConfigManager: Display NVS transaction failed");
    return false;
  }

  // Update the local copy.
  config.display_brightness = brightness;
  config.display_rotated_180 = rotate_180;
  config.display_rotation_quarters = rotation_quarters;
  config.display_rotation_mode = rotation_mode;
  config.wake_mode_mains = wake_mode_mains;
  config.wake_mode_battery = wake_mode_battery;
  config.auto_sleep_enabled = sleep_enabled;
  config.auto_sleep_seconds = normalized_sleep_seconds;
  config.auto_sleep_battery_enabled = sleep_battery_enabled;
  config.auto_sleep_battery_seconds = normalized_bat_seconds;
  apply_device_capability_limits(config);
  runtime_rotation_dirty = false;

  Serial.println("✓ ConfigManager: Display settings saved");
  return true;
}

bool ConfigManager::saveScreensaverTimeout(bool enabled, uint16_t seconds) {
  const uint16_t normalized_seconds = normalize_sleep_seconds(seconds);
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (config.auto_screensaver_enabled == enabled &&
      config.auto_screensaver_seconds == normalized_seconds) {
    return true;
  }
#endif
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("ConfigManager: Failed to open screensaver preferences");
    return false;
  }
  prefs.putBool("ss_auto_en", enabled);
  prefs.putUShort("ss_auto_sec", normalized_seconds);
  if (!BatchedNvsWrite::finish(prefs)) return false;

  config.auto_screensaver_enabled = enabled;
  config.auto_screensaver_seconds = normalized_seconds;
  return true;
}

bool ConfigManager::saveTileBorders(bool enabled) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (config.tile_borders == enabled) return true;
#endif
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("ConfigManager: Failed to open tile border preferences");
    return false;
  }
  prefs.putBool("tile_border", enabled);
  if (!BatchedNvsWrite::finish(prefs)) return false;

  config.tile_borders = enabled;
  return true;
}

bool ConfigManager::saveEthernetEnabled(bool enabled) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (config.ethernet_enabled == enabled) return true;
#endif
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("ConfigManager: Failed to open network mode preferences");
    return false;
  }
  prefs.putBool("eth_mode", enabled);
  if (!BatchedNvsWrite::finish(prefs)) return false;

  config.ethernet_enabled = enabled;
  return true;
}

bool ConfigManager::saveScreensaverBrightness(uint8_t brightness_pct) {
  if (brightness_pct < Device::kConfiguredBrightnessPercentMin) {
    brightness_pct = Device::kConfiguredBrightnessPercentMin;
  } else if (brightness_pct > kScreensaverBrightnessPctMax) {
    brightness_pct = kScreensaverBrightnessPctMax;
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  if (config.screensaver_brightness_pct == brightness_pct) return true;
#endif
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("ConfigManager: Could not save screensaver brightness");
    return false;
  }
  prefs.putUChar("ss_bright", brightness_pct);
  if (!BatchedNvsWrite::finish(prefs)) return false;

  config.screensaver_brightness_pct = brightness_pct;
  return true;
}

bool ConfigManager::saveStaticAddressingEnabled(bool enabled) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (config.wifi_static_enabled == enabled) return true;
#endif
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("ConfigManager: Failed to open IP mode preferences");
    return false;
  }
  prefs.putBool("net_static", enabled);
  prefs.putBool("wifi_static", enabled);
  prefs.putBool("eth_static", enabled);
  if (!BatchedNvsWrite::finish(prefs)) return false;

  config.wifi_static_enabled = enabled;
  return true;
}

bool ConfigManager::clearStaticAddressing() {
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (!config.wifi_static_enabled && config.wifi_static_ip[0] == '\0' &&
      config.wifi_gateway[0] == '\0' && config.wifi_subnet[0] == '\0' &&
      config.wifi_dns[0] == '\0') {
    return true;
  }
#endif
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("ConfigManager: Failed to open DHCP preferences");
    return false;
  }
  prefs.putBool("net_static", false);
  prefs.putBool("wifi_static", false);
  prefs.putString("wifi_ip", "");
  prefs.putString("wifi_gw", "");
  prefs.putString("wifi_subnet", "");
  prefs.putString("wifi_dns", "");
  prefs.putBool("eth_static", false);
  prefs.putString("eth_ip", "");
  prefs.putString("eth_gw", "");
  prefs.putString("eth_subnet", "");
  prefs.putString("eth_dns", "");
  if (!BatchedNvsWrite::finish(prefs)) return false;

  config.wifi_static_enabled = false;
  config.wifi_static_ip[0] = '\0';
  config.wifi_gateway[0] = '\0';
  config.wifi_subnet[0] = '\0';
  config.wifi_dns[0] = '\0';
  return true;
}

void ConfigManager::clear() {
  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;

  if (!prefs.begin(PREF_NAMESPACE, false)) {
    Serial.println("⚠️ ConfigManager: Failed to open preferences");
    return;
  }

  prefs.clear();
  if (!BatchedNvsWrite::finish(prefs)) {
    Serial.println("ConfigManager: Could not erase NVS");
    return;
  }

  memset(&config, 0, sizeof(config));
  config.configured = false;
  config.mqtt_port = 1883;
  strncpy(config.mqtt_base_topic, "hometiles", CONFIG_MQTT_BASE_MAX - 1);
  strncpy(config.ha_prefix, "ha/statestream", CONFIG_HA_PREFIX_MAX - 1);
  set_language_code(config.language, sizeof(config.language), "en");
  set_timezone_code(config.timezone, sizeof(config.timezone), "berlin");
  config.global_time_format = clock_tile::TIME_FORMAT_AUTO;
  config.global_date_format = clock_tile::DATE_FORMAT_AUTO;
  config.display_brightness = 200;
  config.screensaver_brightness_pct = kScreensaverBrightnessPctDefault;
  config.tile_borders = true;
  config.display_rotated_180 = false;
  config.display_rotation_quarters = Device::kRotationDefault;
  config.display_rotation_mode = kDisplayRotationNormal;
  config.status_time_font_size = 48;
  config.wifi_static_enabled = false;
  boot_static_enabled = false;
  config.status_date_font_size = 24;
  config.settings_pin_enabled = false;
  config.settings_tile_hidden = false;
  config.settings_swipe_enabled = false;
  config.settings_reveal_edge =
      static_cast<uint8_t>(SettingsRevealEdge::Left);
  pin_access::clearCredential(config.settings_pin_salt,
                              config.settings_pin_hash);
  clear_settings_tile_snapshot(config);
  runtime_rotation_dirty = false;

  Serial.println("✓ ConfigManager: Configuration deleted");
}

void ConfigManager::setRuntimeDisplayRotation(bool rotate_180) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  const uint8_t previous_quarters = config.display_rotation_quarters;
  const uint8_t previous_mode = config.display_rotation_mode;
  const bool previous_rotated = config.display_rotated_180;
#endif
  config.display_rotated_180 = rotate_180;
  config.display_rotation_quarters = rotation_quarters_from_legacy(rotate_180);
  config.display_rotation_mode = rotate_180 ? kDisplayRotationFlipped : kDisplayRotationNormal;
  apply_device_capability_limits(config);
#if defined(DEVICE_ESP32_S3_RGB_480)
  runtime_rotation_dirty = runtime_rotation_dirty ||
      previous_quarters != config.display_rotation_quarters ||
      previous_mode != config.display_rotation_mode ||
      previous_rotated != config.display_rotated_180;
#endif
}

bool ConfigManager::verifySettingsPin(const char* pin) const {
  if (!config.settings_pin_enabled) return true;
  return pin_access::isRecoveryPin(pin) ||
         pin_access::verifyCredential(pin, config.settings_pin_salt,
                                      config.settings_pin_hash);
}

bool ConfigManager::getSettingsPin(String& out) const {
  out = "";
  if (!config.settings_pin_enabled) return false;
  const String stored_pin(config.settings_pin_value);
  if (!pin_access::isValidUserPin(stored_pin) ||
      !pin_access::verifyCredential(stored_pin.c_str(),
                                    config.settings_pin_salt,
                                    config.settings_pin_hash)) {
    return false;
  }
  out = stored_pin;
  return true;
}

void ConfigManager::setRuntimeDisplayRotationQuarters(uint8_t rotation_quarters) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  const uint8_t previous_quarters = config.display_rotation_quarters;
  const uint8_t previous_mode = config.display_rotation_mode;
  const bool previous_rotated = config.display_rotated_180;
#endif
  config.display_rotation_quarters = normalize_rotation_quarters(rotation_quarters);
  config.display_rotated_180 = (config.display_rotation_quarters == Device::kRotationFlipped);
  if (config.display_rotation_mode != kDisplayRotationAuto) {
    config.display_rotation_mode =
        config.display_rotated_180 ? kDisplayRotationFlipped : kDisplayRotationNormal;
  }
  apply_device_capability_limits(config);
#if defined(DEVICE_ESP32_S3_RGB_480)
  runtime_rotation_dirty = runtime_rotation_dirty ||
      previous_quarters != config.display_rotation_quarters ||
      previous_mode != config.display_rotation_mode ||
      previous_rotated != config.display_rotated_180;
#endif
}
