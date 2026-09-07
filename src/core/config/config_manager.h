#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>

#include "src/devices/device.h"
#include "src/core/config/pin_access.h"

// WiFi/MQTT configuration manager.
// Stores and loads the connection data in flash (Preferences).

#define CONFIG_WIFI_SSID_MAX     32
#define CONFIG_WIFI_PASS_MAX     64
#define CONFIG_IP_ADDR_MAX       16
#define CONFIG_MQTT_HOST_MAX     64
#define CONFIG_MQTT_USER_MAX     32
#define CONFIG_MQTT_PASS_MAX     64
#define CONFIG_MQTT_CLIENT_ID_MAX 64
#define CONFIG_MQTT_BASE_MAX     32
#define CONFIG_HA_PREFIX_MAX     48
#define CONFIG_LANG_MAX          8
#define CONFIG_TIMEZONE_MAX      24

static constexpr uint16_t kSleepOptionsSec[] = {5, 15, 30, 60, 300, 900, 1800, 3600};
static constexpr size_t kSleepOptionsSecCount = sizeof(kSleepOptionsSec) / sizeof(kSleepOptionsSec[0]);

static constexpr uint8_t kDisplayRotationNormal = 0;
static constexpr uint8_t kDisplayRotationFlipped = 1;
static constexpr uint8_t kDisplayRotationAuto = 2;
static constexpr uint8_t kWakeModeTouch = 0;
static constexpr uint8_t kWakeModeImu = 1;
static constexpr uint8_t kScreensaverBrightnessPctMin = 1;
static constexpr uint8_t kScreensaverBrightnessPctMax = 100;
static constexpr uint8_t kScreensaverBrightnessPctDefault = 25;

enum class SettingsRevealEdge : uint8_t {
  Left = 0,
  Right = 1,
  Top = 2,
  Bottom = 3,
};

struct SettingsTileSnapshot {
  bool valid;
  char title[256];
  char icon_name[32];
  uint32_t bg_color;
  uint8_t col;
  uint8_t row;
  uint8_t span_w;
  uint8_t span_h;
};

struct DeviceConfig {
  char wifi_ssid[CONFIG_WIFI_SSID_MAX];
  char wifi_pass[CONFIG_WIFI_PASS_MAX];
  char wifi_static_ip[CONFIG_IP_ADDR_MAX];
  char wifi_gateway[CONFIG_IP_ADDR_MAX];
  char wifi_subnet[CONFIG_IP_ADDR_MAX];
  char wifi_dns[CONFIG_IP_ADDR_MAX];
  // Shared IP configuration for WLAN and Ethernet. The wifi_ field names stay
  // as they are for compatibility with existing stored data.
  bool wifi_static_enabled;
  char mqtt_host[CONFIG_MQTT_HOST_MAX];
  uint16_t mqtt_port;
  char mqtt_user[CONFIG_MQTT_USER_MAX];
  char mqtt_pass[CONFIG_MQTT_PASS_MAX];
  char mqtt_client_id[CONFIG_MQTT_CLIENT_ID_MAX];
  char mqtt_base_topic[CONFIG_MQTT_BASE_MAX];
  char ha_prefix[CONFIG_HA_PREFIX_MAX];
  char language[CONFIG_LANG_MAX];
  char timezone[CONFIG_TIMEZONE_MAX];
  uint8_t global_time_format;
  uint8_t global_date_format;
  uint8_t keyboard_layout;  // 0=Auto (language), 1=German QWERTZ, 2=English QWERTY
  bool configured;  // Whether a configuration is stored.

  // Display & Power Settings
  uint8_t display_brightness;  // Device-specific raw level: backlight_input_min..255
  // One visible percentage for every device. Device::backlightRawFromPercent()
  // converts it to the range of the respective driver.
  uint8_t screensaver_brightness_pct;  // 1-100
  bool tile_borders;           // Thin borders around normal dashboard tiles.
  bool display_rotated_180;    // Display rotated by 180 degrees?
  uint8_t display_rotation_quarters; // 0=0°, 1=90°, 2=180°, 3=270°
  uint8_t display_rotation_mode; // 0=Normal, 1=180, 2=Auto
  uint8_t wake_mode_mains;       // 0=Touch, 1=IMU
  uint8_t wake_mode_battery;     // 0=Touch, 1=IMU
  bool auto_sleep_enabled;     // Auto-sleep enabled?
  uint16_t auto_sleep_seconds; // Seconds until auto-sleep (5-3600)
  bool auto_screensaver_enabled;     // Start the screensaver after inactivity?
  uint16_t auto_screensaver_seconds; // Seconds until screensaver (5-3600)
  bool auto_sleep_battery_enabled;     // Auto-sleep enabled on battery power?
  uint16_t auto_sleep_battery_seconds; // Seconds until auto-sleep (5-3600)
  uint8_t status_time_font_size;      // 24 or 48
  uint8_t status_date_font_size;      // 20 or 24

  // Fixed network mode: false = WLAN (default), true = Ethernet. Evaluated at
  // boot only. In Ethernet mode WLAN/ESP-Hosted never starts, so the two stacks
  // cannot compete for the internal DMA RAM.
  bool ethernet_enabled;

  // Local parental-control settings. The hash remains authoritative; the
  // bounded recovery copy exists only so Web Admin can implement "Show".
  bool settings_pin_enabled;
  bool settings_tile_hidden;
  bool settings_swipe_enabled;
  uint8_t settings_reveal_edge;
  uint8_t settings_pin_salt[pin_access::kSaltSize];
  uint8_t settings_pin_hash[pin_access::kHashSize];
  char settings_pin_value[pin_access::kUserPinMaxDigits + 1];
  SettingsTileSnapshot settings_tile_snapshot;
};

class ConfigManager {
public:
  ConfigManager();

  // Loads the configuration from flash.
  bool load();

  // Save the configuration to flash.
  bool save(const DeviceConfig& cfg);

  // Saves the display settings only.
  bool saveDisplaySettings(uint8_t brightness,
                           bool sleep_enabled,
                           uint16_t sleep_seconds,
                           bool sleep_battery_enabled,
                           uint16_t sleep_battery_seconds,
                           uint8_t rotation_mode,
                           bool rotate_180,
                           uint8_t rotation_quarters,
                           uint8_t wake_mode_mains,
                           uint8_t wake_mode_battery);

  bool saveScreensaverTimeout(bool enabled, uint16_t seconds);
  bool saveScreensaverBrightness(uint8_t brightness_pct);
  bool saveTileBorders(bool enabled);
  bool saveEthernetEnabled(bool enabled);
  bool saveStaticAddressingEnabled(bool enabled);

  // Removes the shared static addressing. The network mode stays as it is.
  bool clearStaticAddressing();

  void setRuntimeDisplayRotation(bool rotate_180);
  void setRuntimeDisplayRotationQuarters(uint8_t rotation_quarters);

  // Delete the stored configuration.
  void clear();

  // Reports whether a valid configuration is stored.
  bool isConfigured() const { return config.configured; }
  bool hasWifiCredentials() const { return config.wifi_ssid[0] != '\0'; }
  bool hasMqttConfig() const { return config.mqtt_host[0] != '\0'; }
  bool bootStaticAddressingEnabled() const { return boot_static_enabled; }

  // Accessors for the configuration data.
  const DeviceConfig& getConfig() const { return config; }
  bool verifySettingsPin(const char* pin) const;
  bool getSettingsPin(String& out) const;

private:
  DeviceConfig config;
  bool boot_static_enabled = false;
  // Rotation can be applied to the live display before the settings save is
  // issued. Keep that runtime mutation from being mistaken for an NVS no-op.
  bool runtime_rotation_dirty = false;
};

// Shared instance.
extern ConfigManager configManager;

#endif // CONFIG_MANAGER_H
