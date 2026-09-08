#ifndef TAB_SETTINGS_H
#define TAB_SETTINGS_H

#include <lvgl.h>

// Hotspot-button callback type
typedef void (*hotspot_callback_t)(bool enable);

void hide_settings_popup();
void build_settings_tab(lv_obj_t *tab, hotspot_callback_t hotspot_cb = nullptr);

// Registered by the sketch to request main-loop Wi-Fi reconnection
// with the currently saved credentials, without restarting the device.
typedef void (*wifi_reconnect_callback_t)();
void settings_set_wifi_reconnect_callback(wifi_reconnect_callback_t cb);

// GitHub update actions in the system popup invoke callbacks only.
// The sketch checks/installs on the loop task using pending flags;
// never perform TLS/network work directly in an LVGL event callback.
typedef void (*fw_check_callback_t)();
typedef void (*fw_install_callback_t)(const char* tag);
void settings_set_fw_check_callback(fw_check_callback_t cb);
void settings_set_fw_install_callback(fw_install_callback_t cb);
typedef void (*system_reboot_callback_t)();
void settings_set_system_reboot_callback(system_reboot_callback_t cb);

// Wi-Fi disconnect asks the sketch to call networkManager.disconnectWifiManual()
// in the main loop, disabling automatic reconnect until connect is requested.
typedef void (*wifi_disconnect_callback_t)();
void settings_set_wifi_disconnect_callback(wifi_disconnect_callback_t cb);

// System pairing requests main-loop MQTT reconnection through
// requestMqttReconfigure. Post-connect publishes let HA Bridge rediscover
// the device, as when the MQTT host is manually changed.
typedef void (*ha_pair_callback_t)();
void settings_set_ha_pair_callback(ha_pair_callback_t cb);
// Loop-task responses tolerate a popup closed in the meantime.
void settings_fw_check_result(bool ok, const char* latest_tag, bool update_available);
void settings_fw_install_progress(size_t written, size_t total);
void settings_fw_install_done();
void settings_fw_install_failed(const char* error);

// Update Wi-Fi status from the main loop.
void settings_update_wifi_status(bool connected, const char* ssid, const char* ip);
void settings_update_wifi_status_ap(const char* ssid, const char* password);
void settings_update_ap_mode(bool running);
void settings_refresh_language();
void settings_sync_display_rotation(bool rotated);

// Update power status from the main loop.
void settings_update_power_status();

#endif // TAB_SETTINGS_H
