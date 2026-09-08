#include "src/ui/navigation/view_navigation.h"

#include <WiFi.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>  // xTaskCreatePinnedToCoreWithCaps (PSRAM task stack)
#include <nvs_flash.h>
#include <Preferences.h>  // Persist GitHub OTA retries across restarts
#include <esp_err.h>
#include <esp_wifi.h>  // Stop an active scan before changing AP mode
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_system.h>  // Reset reason for the Tab5 brownout brightness cap

#include "src/core/hardware/board_hal.h"
#include "src/core/display/display_manager.h"
#include "src/core/power/power_manager.h"
#include "src/core/config/config_manager.h"
#include "src/core/diagnostics/crash_log.h"
#include "src/core/firmware/firmware_version.h"
#include "src/core/firmware/github_update.h"
#include "src/core/display/lvgl_tick_service.h"
#include "src/devices/guition_esp32_4848s040/s3_diagnostics.h"
#include "src/ui/ui_manager.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/types/energy/energy_data.h"
#include "src/network/network_manager.h"
#include "src/network/transport/network_transport.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/web/setup/web_config.h"
#include "src/web/server/web_admin.h"
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/ui/startup/boot_splash.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/io/hardware_io.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/runtime/tile_update_service.h"
#include "src/types/media/update_service.h"
#include "src/tiles/icons/mdi_icons.h"      // MDI Icon Mapping

// MDI icon font (48px, 4bpp), defined in src/fonts/mdi_icons_48.c.
LV_FONT_DECLARE(mdi_icons_48);

// Extra loopTask stack prevents overflow inside lv_timer_handler().
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static uint32_t last_status_update = 0;
static uint32_t ap_mode_started_at = 0;
static const uint32_t AP_MODE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static uint32_t ap_mode_disable_block_until = 0;
static bool hotspot_mode_change_pending = false;
static bool hotspot_mode_requested = false;
static bool ota_display_suspended = false;
static TaskHandle_t ui_build_waiter = nullptr;
static scene_publish_cb_t ui_scene_cb = nullptr;
static hotspot_start_cb_t ui_hotspot_cb = nullptr;
static TaskHandle_t g_mqtt_worker_handle = nullptr;

#if defined(DEVICE_M5STACKS_TAB5)
// Field logs from 2026-07-06 showed brownouts when full backlight coincided
// with AP startup or station connection. Applying saved brightness before
// radio startup could create a boot loop. Cap hardware brightness during
// these phases without changing the saved value; restore it after connection.
static constexpr uint8_t kTab5SafeBrightness = 140;
static constexpr uint32_t kTab5BrightnessRestoreTimeoutMs = 30000;
static bool tab5_brightness_capped = false;
static uint32_t tab5_brightness_cap_wait_since = 0;
#endif

// Keep the device/version splash readable even when the remaining boot is fast.
static constexpr uint32_t kBootSplashMinVisibleMs = 2500;
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_M5STACKS_TAB5) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
static constexpr uint32_t kBootBlackWarmupMs = 90;
static constexpr uint32_t kBootBlackGapMs = 60;
#endif

static void log_memory_status(const char* tag) {
  const uint32_t heap_free = ESP.getFreeHeap();
  const uint32_t heap_min = ESP.getMinFreeHeap();
  const uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const uint32_t psram_free = ESP.getFreePsram();
  const uint32_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  Serial.printf("[Mem] %s | Heap free=%u KB | Heap min=%u KB | Int free=%u KB | Int largest=%u KB | DMA free=%u KB | DMA largest=%u KB | PSRAM free=%u KB | PSRAM largest=%u KB | PSRAM total=%u KB\n",
                tag ? tag : "?",
                heap_free / 1024,
                heap_min / 1024,
                int_free / 1024,
                int_largest / 1024,
                dma_free / 1024,
                dma_largest / 1024,
                psram_free / 1024,
                psram_largest / 1024,
                ESP.getPsramSize() / 1024);
  if (lv_is_initialized()) {
    lv_mem_monitor_t lv_mem{};
    lv_mem_monitor(&lv_mem);
    Serial.printf(
        "[LVGL/Mem] %s | Total=%u KB | Used=%u KB (%u%%) | "
        "Max used=%u KB | Free=%u KB | Largest=%u KB | Frag=%u%%\n",
        tag ? tag : "?",
        static_cast<unsigned>(lv_mem.total_size / 1024),
        static_cast<unsigned>((lv_mem.total_size - lv_mem.free_size) / 1024),
        static_cast<unsigned>(lv_mem.used_pct),
        static_cast<unsigned>(lv_mem.max_used / 1024),
        static_cast<unsigned>(lv_mem.free_size / 1024),
        static_cast<unsigned>(lv_mem.free_biggest_size / 1024),
        static_cast<unsigned>(lv_mem.frag_pct));
  }
  GuitionS3Diagnostics::logMemory(
      tag, xTaskGetCurrentTaskHandle(), g_mqtt_worker_handle);
  Serial.flush();
}

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_M5STACKS_TAB5) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
static void boot_black_warmup(const char* label) {
  Serial.printf("[Boot] Black display warmup: %s\n", label ? label : "?");
  Serial.flush();

  BoardHAL::displayFillScreen(0x0000);
  BoardHAL::displayWaitDisplay();
  BoardHAL::displayWakeDark();
  delay(kBootBlackWarmupMs);
  BoardHAL::displaySleep();
  delay(kBootBlackGapMs);
  BoardHAL::displayFillScreen(0x0000);
  BoardHAL::displayWaitDisplay();
}
#endif

#if defined(DEVICE_M5STACKS_TAB5)
static void tab5_timed_refresh_now(const char* label) {
  const uint32_t start_ms = millis();
  lv_refr_now(displayManager.getDisplay());
  Serial.printf("[Tab5/Setup] %s lv_refr_now=%lu ms\n",
                label ? label : "?",
                static_cast<unsigned long>(millis() - start_ms));
  Serial.flush();
}

static void tab5_timed_display_wait(const char* label) {
  const uint32_t start_ms = millis();
  BoardHAL::displayWaitDisplay();
  Serial.printf("[Tab5/Setup] %s displayWait=%lu ms\n",
                label ? label : "?",
                static_cast<unsigned long>(millis() - start_ms));
  Serial.flush();
}
#endif


static void confirm_running_ota_if_needed() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    Serial.println("[OTA] Running partition lookup failed");
    return;
  }

  esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t state_err = esp_ota_get_state_partition(running, &ota_state);
  if (state_err != ESP_OK) {
    Serial.printf("[OTA] Could not read running OTA state: %s (%d)\n", esp_err_to_name(state_err), state_err);
    return;
  }

  Serial.printf("[OTA] Running partition state: %d\n", static_cast<int>(ota_state));

  if (ota_state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }

  const esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
  if (mark_err == ESP_OK) {
    Serial.println("[OTA] Running app marked valid; rollback cancelled");
  } else {
    Serial.printf("[OTA] Failed to mark running app valid: %s (%d)\n", esp_err_to_name(mark_err), mark_err);
  }
}

static void restore_display_after_ota_pause() {
  BoardHAL::displayPowerSaveOff();
  displayManager.setInputEnabled(true);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(displayManager.getDisplay());
}

static void build_ui_task(void* param) {
  (void)param;
  uiManager.buildUI(ui_scene_cb, ui_hotspot_cb);
  GuitionS3Diagnostics::logTaskWatermark("buildUI",
                                         xTaskGetCurrentTaskHandle());
  if (ui_build_waiter) {
    xTaskNotifyGive(ui_build_waiter);
  }
  vTaskDelete(nullptr);
}

static void apply_hotspot_mode(bool enable) {
  if (enable) {
    if (webConfigServer.isRunning()) {
      settings_update_ap_mode(true);
      return;
    }
    // Defer worker reconnects before disconnecting. Station status can remain
    // briefly connected, allowing an unwanted reconnect during AP setup.
    networkManager.deferMqttReconnect(AP_MODE_TIMEOUT_MS + 10000UL);
    if (networkManager.isMqttConnected())
      networkManager.disconnectMqtt();
    if (webAdminServer.isRunning())
      webAdminServer.stop();
    networkManager.stopMdns();
    settings_update_ap_mode(true);
#if defined(DEVICE_M5STACKS_TAB5)
    // Cap brightness before changing radio mode: AP startup at full
    // backlight can brown out the power supply.
    if (BoardHAL::getBrightness() > kTab5SafeBrightness) {
      BoardHAL::setBrightness(kTab5SafeBrightness);
      tab5_brightness_capped = true;
    }
    tab5_brightness_cap_wait_since = millis();
#endif
    // An asynchronous Wi-Fi scan would interfere with the mode change.
    esp_wifi_scan_stop();
    if (webConfigServer.start()) {
      ap_mode_started_at = millis();
      ap_mode_disable_block_until = ap_mode_started_at + 1500;
    } else {
      ap_mode_started_at = 0;
      ap_mode_disable_block_until = 0;
      networkManager.deferMqttReconnect(1000);
      if (configManager.isConfigured())
        networkManager.connectWifi();
    }
    return;
  }

  if (ap_mode_disable_block_until != 0 &&
      (int32_t)(millis() - ap_mode_disable_block_until) < 0) {
    settings_update_ap_mode(true);
    return;
  }

  if (!webConfigServer.isRunning()) {
    ap_mode_started_at = 0;
    ap_mode_disable_block_until = 0;
    settings_update_ap_mode(false);
    return;
  }

  webConfigServer.stop();
  ap_mode_started_at = 0;
  ap_mode_disable_block_until = 0;
  settings_update_ap_mode(false);
#if defined(DEVICE_M5STACKS_TAB5)
  // Keep the cap through the reconnect burst below. The loop restores
  // brightness when Wi-Fi connects or the recovery timeout expires.
  if (tab5_brightness_capped) tab5_brightness_cap_wait_since = millis();
#endif
  if (configManager.isConfigured()) {
    networkManager.deferMqttReconnect(6000);
    if (WiFi.status() != WL_CONNECTED) {
      // WiFi.begin() cannot connect during an active scan; this previously
      // prevented reconnection after leaving AP mode.
      esp_wifi_scan_stop();
      networkManager.connectWifi();
    }
  }
}

static void set_hotspot_mode(bool enable) {
  hotspot_mode_requested = enable;
  hotspot_mode_change_pending = true;
}

// Connecting with edited Wi-Fi credentials only sets a pending flag.
// As with hotspot changes, network work belongs to the loop, not LVGL callbacks.
static bool wifi_reconnect_pending = false;

static void request_wifi_reconnect() {
  wifi_reconnect_pending = true;
}

static void apply_wifi_reconnect() {
  if (networkTransport.isWifiDriverActive()) esp_wifi_scan_stop();
  if (networkManager.isMqttConnected()) networkManager.disconnectMqtt();
  // Disconnect first; connectWifi() reads the newly saved credentials.
  // networkManager.update() then starts Web Admin/NTP/MQTT as usual.
  if (networkTransport.isWifiConnected()) WiFi.disconnect();
  networkManager.deferMqttReconnect(6000);
  networkManager.connectWifi();
}

// Wi-Fi disconnect and Home Assistant pairing follow the same pending-flag
// pattern: the LVGL callback requests work and the loop performs it.
static bool wifi_disconnect_pending = false;
static bool ha_pair_pending = false;

static void request_wifi_disconnect() {
  wifi_disconnect_pending = true;
}

static void request_ha_pair() {
  ha_pair_pending = true;
}

// GitHub checks and installation block on the loop task. Popup callbacks
// only set these flags, as with hotspot changes and Wi-Fi reconnects.
static bool fw_check_pending = false;
static bool fw_install_pending = false;
static bool system_reboot_pending = false;
static char fw_install_tag[24] = {};
// Repeated taps and concurrent UI/Web requests must not trigger consecutive
// TLS handshakes. Reuse the last result during this short cache window.
static constexpr uint32_t kFwCheckCacheMs = 15000;
static bool fw_check_running = false;
static bool fw_last_check_valid = false;
static uint32_t fw_last_check_at = 0;
static GithubUpdate::CheckResult fw_last_check_result;

static void request_fw_check() {
  fw_check_pending = true;
}

static void request_fw_install(const char* tag) {
  snprintf(fw_install_tag, sizeof(fw_install_tag), "%s", tag ? tag : "");
  fw_install_pending = true;
}

static void request_system_reboot() {
  system_reboot_pending = true;
}

// A field test on 2026-07-16 completed the 6 MB download after a fresh boot,
// while the ESP-Hosted link failed partway through on the older running system.
// Retry installation after a controlled restart, retaining the tag and bounded
// attempt count in NVS.
static constexpr uint8_t kFwInstallMaxAutoRetries = 3;
static constexpr const char* kFwRetryNvsNamespace = "otaretry";
static bool fw_install_auto_retry_checked = false;

static void arm_fw_install_auto_retry(const char* tag) {
  Preferences prefs;
  if (!prefs.begin(kFwRetryNvsNamespace, false)) return;
  const uint8_t attempts = prefs.getUChar("att", 0);
  if (attempts >= kFwInstallMaxAutoRetries) {
    prefs.clear();
    prefs.end();
    Serial.printf("[Update] Automatic retry abandoned (%u attempts used)\n",
                  static_cast<unsigned>(attempts));
    return;
  }
  prefs.putString("tag", tag);
  prefs.putUChar("att", attempts + 1);
  prefs.end();
  Serial.printf("[Update] Automatic retry %u/%u scheduled after restart\n",
                static_cast<unsigned>(attempts + 1),
                static_cast<unsigned>(kFwInstallMaxAutoRetries));
}

static void clear_fw_install_auto_retry() {
  Preferences prefs;
  if (prefs.begin(kFwRetryNvsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
}

static GithubUpdate::CheckResult perform_fw_check() {
  const uint32_t now_ms = millis();
  if (fw_last_check_valid &&
      (uint32_t)(now_ms - fw_last_check_at) < kFwCheckCacheMs) {
    Serial.println("[Update] Check: reusing previous result");
    return fw_last_check_result;
  }

  // Loop ownership prevents concurrent synchronous callers; keep an explicit
  // guard so the shared UI/Web Admin contract remains clear.
  if (fw_check_running) {
    Serial.println("[Update] Check already running");
    return fw_last_check_valid ? fw_last_check_result
                               : GithubUpdate::CheckResult{};
  }
  fw_check_running = true;

  // Keep MQTT connected. Disconnecting it does not help the synchronous HTTPS
  // handshake and creates a retained subscribe/state burst afterwards.
  Serial.println("[Update] Check: MQTT stays connected");
#if defined(DEVICE_ESP32_S3_RGB_480)
  const bool s3_rgb_network_active = networkTransport.isConnected();
#endif
#if defined(DEVICE_GUITION_ESP32_4848S040)
  if (s3_rgb_network_active) {
    // Reduce continuous RGB scanout bandwidth before TLS/WiFi starts using
    // memory bandwidth. The device guard applies the PCLK change at VSYNC.
    Device::displayUpdateCheckGuardBegin();
  }
#endif
  GithubUpdate::CheckResult res = GithubUpdate::checkLatest();
#if defined(DEVICE_GUITION_ESP32_4848S040)
  if (s3_rgb_network_active) {
    // Restore normal scanout speed and finish with one canonical FB0 restart.
    Device::displayUpdateCheckGuardEnd();
  }
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
  if (s3_rgb_network_active) {
    // This profile retains its established post-check recovery until an
    // equivalent exact-board PCLK guard has hardware validation.
    Serial.println("[Display/S3] Resynchronizing RGB scanout after update check");
    Device::storageWriteBegin();
    Device::storageWriteEnd();
  }
#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4)
  if (s3_rgb_network_active) {
    Serial.println("[Display/S3] Resynchronizing RGB scanout after update check");
    Device::storageWriteBegin();
    Device::storageWriteEnd();
  }
#endif
  if (!res.ok && res.tls_alloc_failed) {
    Serial.println(
        "[Update] Check: TLS allocation failed after allocator fallback");
  }
  fw_last_check_result = res;
  fw_last_check_at = millis();
  fw_last_check_valid = true;
  fw_check_running = false;
  return res;
}

static void apply_fw_check() {
  // Present the search status before the TLS handshake blocks the loop
  // for roughly 1-3 seconds.
  lv_refr_now(displayManager.getDisplay());

  const GithubUpdate::CheckResult res = perform_fw_check();
  settings_fw_check_result(res.ok, res.latest_tag, res.update_available);
}

static void fw_install_progress(size_t written, size_t total) {
  displayManager.resetActivityTimer();  // Prevent display sleep during the update.

  // GitHub OTA uses the same suspended-display mode as Web OTA. Do not service
  // LVGL timers: animation/flush work would compete with SDIO RX for DMA and CPU.
  static uint32_t last_ui_ms = 0;
  static size_t last_written = 0;
  const uint32_t now_ms = millis();
  if (written < last_written || written == 0 || written == total ||
      now_ms - last_ui_ms >= 500) {
    last_written = written;
    last_ui_ms = now_ms;
    settings_fw_install_progress(written, total);
  }
  delay(1);
}

static void apply_fw_install() {
  Serial.printf("[Update] Install requested: %s\n", fw_install_tag);
  // Match Web OTA's prepareDisplayForOtaInstall(): suspend the display, move
  // draw buffers to PSRAM and pause MQTT. This preserves the internal memory
  // headroom required by the ESP32-P4 SDIO Wi-Fi driver.
  displayManager.setInputEnabled(false);
  lv_refr_now(displayManager.getDisplay());
  BoardHAL::displayPowerSaveOn();
  displayManager.setBufferLines(8);  // unter SRAM-Minimum -> PSRAM-Puffer
  networkManager.prepareMqttForOta();
  if (webAdminServer.isRunning()) webAdminServer.stop();
  log_memory_status("before-github-ota");
  delay(20);

  String err;
  const bool ok = GithubUpdate::install(fw_install_tag, fw_install_progress, err);
  if (ok) {
    clear_fw_install_auto_retry();
    settings_fw_install_done();
    Serial.println("[Update] Successful - restarting");
    BoardHAL::prepareForRestart();
    delay(800);  // Erfolgsmeldung kurz stehen lassen
    BoardHAL::restart();
    return;
  }

  Serial.printf("[Update] Failed: %s\n", err.c_str());
  webAdminServer.setGithubUpdateInstallFailed(err.c_str());
  settings_fw_install_failed(err.c_str());
  // The controlled restart below produces no core dump. Save the error and
  // install() range/memory diagnostics to /crashlog.txt for Web Admin download.
  CrashLog::appendOtaFailureReport(fw_install_tag, err,
                                   GithubUpdate::lastInstallDiag());
  // Retry after reboot only for transport failures. Retrying a permanent
  // incompatibility such as device mismatch would create a reboot loop.
  if (GithubUpdate::lastInstallRetryable()) {
    arm_fw_install_auto_retry(fw_install_tag);
  } else {
    clear_fw_install_auto_retry();
  }
  // A failed large HTTPS transfer can leave ESP-Hosted unresponsive. Restarting
  // Wi-Fi/MQTT then causes repeated five-second RPC timeouts and blocks the UI.
  // The image is staged before Update.begin(), or the failed update has been
  // aborted, so a restart still boots the unchanged active firmware.
  Serial.println("[Update] Safe restart after failed install");
  BoardHAL::prepareForRestart();
  delay(500);
  BoardHAL::restart();
}

static void apply_system_reboot() {
  Serial.println("[System] Restart requested");
  displayManager.setInputEnabled(false);
  lv_refr_now(displayManager.getDisplay());
  BoardHAL::prepareForRestart();
  delay(800);
  BoardHAL::restart();
}

static bool init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
#if defined(DEVICE_ESP32_S3_RGB_480)
    // A normal NVS scan is read-only. Only the exceptional erase/recreate path
    // can stall the S3 RGB DMA and therefore needs the display write guard.
    Device::storageWriteBegin();
    const esp_err_t erase_err = nvs_flash_erase();
    Device::storageWriteEnd();
    if (erase_err != ESP_OK) {
      Serial.printf("[Setup] NVS erase failed: %s (%d)\n",
                    esp_err_to_name(erase_err), erase_err);
      return false;
    }
#else
    nvs_flash_erase();
#endif
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    Serial.printf("[Setup] NVS init failed: %s (%d)\n", esp_err_to_name(err), err);
    return false;
  }
  Serial.println("[Setup] NVS ready");
  return true;
}

static constexpr uint32_t BACKGROUND_STATE_REFRESH_MS = 60UL * 1000UL;
static uint32_t g_last_bridge_state_refresh_ms = 0;
static bool g_bridge_state_refresh_pending = false;

static void mark_background_state_refresh_sent() {
  g_last_bridge_state_refresh_ms = millis();
  g_bridge_state_refresh_pending = false;
}

static void service_background_state_refresh(bool allow_now) {
  if (!configManager.isConfigured()) return;
  if (!networkManager.isMqttConnected()) return;

  const uint32_t now = millis();
  if (g_last_bridge_state_refresh_ms == 0) {
    g_last_bridge_state_refresh_ms = now;
    return;
  }
  if ((uint32_t)(now - g_last_bridge_state_refresh_ms) >= BACKGROUND_STATE_REFRESH_MS) {
    g_bridge_state_refresh_pending = true;
  }
  if (!g_bridge_state_refresh_pending || !allow_now) return;

  networkManager.publishBridgeRequest();
  mark_background_state_refresh_sent();
}

// Single-owner MQTT worker
//
// Only this task accesses PubSubClient (connect/loop/publish/subscribe and
// setBufferSize). Other tasks use outbound queues or volatile flags; see
// network_manager.h. Idle priority lets IDLE0 service the watchdog, and a
// PSRAM stack preserves scarce internal memory.
static void mqtt_worker_task(void* param) {
  (void)param;
  for (;;) {
    networkManager.serviceMqttWorker();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void setup() {
  g_lvgl_tick_last_ms = millis();
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=== HOMETILES STARTUP ===");
  Serial.printf("[Setup] Firmware: hometiles-%s-%s\n", FW_VERSION, Device::profile().key);
  GuitionS3Diagnostics::logBoot(FW_VERSION, Device::profile().key);
  GuitionS3Diagnostics::logOtaPartitions("boot");
  confirm_running_ota_if_needed();
  log_memory_status("boot-start");
  Serial.flush();

  // Board-HAL initialisiert I2C, Display (MIPI-DSI HX8394), GT911 Touch, Backlight
  Serial.println("[Setup] BoardHAL::init()...");
  Serial.flush();
  if (!BoardHAL::init()) {
    Serial.println("[Setup] BoardHAL init FAILED!");
    while(1) delay(1000);
  }
  Serial.println("[Setup] BoardHAL OK");
  log_memory_status("after-boardhal");
  Serial.flush();

  // LittleFS (primary storage on flash)
  Serial.println("[Setup] LittleFS init...");
  Serial.flush();
  if (!Device::initLittleFS()) {
    Serial.println("[Setup] LittleFS FAILED!");
    while(1) delay(1000);
  }
  Serial.println("[Setup] LittleFS OK");
  log_memory_status("after-littlefs");
  Serial.flush();

  // Append the reset reason and core-dump summary to /crashlog.txt for
  // Web Admin download. Record this before later initialization can crash again.
  CrashLog::logBootDiagnostics();

  // SD Card (optional, for screenshots)
  Serial.println("[Setup] SD Card init...");
  Serial.flush();
  log_memory_status("before-sd");
  BoardHAL::initSDCard();
  log_memory_status("after-sd");
  Serial.flush();

  // Migrate tile data from SD to LittleFS on first boot
  Serial.println("[Setup] Storage migration check...");
  Serial.flush();
  Device::migrateStorageFromSD();
  log_memory_status("after-storage-migration");
  Serial.flush();

  Serial.println("[Setup] displayManager.init()...");
  Serial.flush();
  if (!displayManager.init()) {
    Serial.println("[Setup] Display FAILED!");
    while(1) delay(1000);
  }
  Serial.println("[Setup] Display OK");
  log_memory_status("after-display");
  Serial.flush();
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_M5STACKS_TAB5) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
  boot_black_warmup("after-display");
#endif

  // Load NVS and configuration before the first visible splash frame, especially
  // the saved display rotation. displayManager.init() only sets the device default;
  // configManager.load() supplies the user setting. Loading it after splash wake
  // previously exposed incorrectly rotated frames that were never redrawn before
  // the splash disappeared.
  Serial.println("[Setup] NVS init...");
  Serial.flush();
  init_nvs();
  log_memory_status("after-nvs");
  Serial.flush();

  Serial.println("[Setup] Loading configs...");
  Serial.flush();
  bool has_config = configManager.load();
  haBridgeConfig.load();
  tileConfig.load();
  screensaverConfig.load();
  hardwareIo.load();
  hardwareIo.begin();
  if (has_config) {
    displayManager.setRotation(configManager.getConfig().display_rotation_quarters);
  }
  Serial.println("[Setup] Configs OK");
  log_memory_status("after-configs");
  Serial.flush();

  // An LVGL screen now exists; show the splash while the remaining setup runs.
  // Large DSI/DPI panels expose the live framebuffer, so initial strip flushes
  // can appear as a stepped flash. Render the splash with the panel dark first.
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_M5STACKS_TAB5) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
  BoardHAL::displaySleep();
  delay(kBootBlackGapMs);
  BoardHAL::displayFillScreen(0x0000);
  BoardHAL::displayWaitDisplay();
#endif
  BootSplash::show();
  // Resolve flex positions, image scaling and pivots before the first refresh.
  // Otherwise that frame can show distorted intermediate geometry until redraw.
  lv_obj_update_layout(lv_screen_active());
#if !defined(DEVICE_WAVESHARE_TOUCH_LCD_X) && \
    !defined(DEVICE_M5STACKS_TAB5) && \
    !defined(DEVICE_GUITION_JC8012P4A1_FAMILY) && \
    !defined(DEVICE_ESP32_S3_RGB_480)
  BoardHAL::displayWake();
#endif
  lv_obj_invalidate(lv_screen_active());
#if defined(DEVICE_M5STACKS_TAB5)
  tab5_timed_refresh_now("splash-1");
  tab5_timed_display_wait("splash-1");
#else
  lv_refr_now(displayManager.getDisplay());
  BoardHAL::displayWaitDisplay();
#endif
  delay(20);
  lv_obj_invalidate(lv_screen_active());
#if defined(DEVICE_M5STACKS_TAB5)
  tab5_timed_refresh_now("splash-2");
  tab5_timed_display_wait("splash-2");
#else
  lv_refr_now(displayManager.getDisplay());
  BoardHAL::displayWaitDisplay();
#endif
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_M5STACKS_TAB5) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
  BoardHAL::displayWake();
  BoardHAL::displayWaitDisplay();
#endif
  const uint32_t boot_splash_shown_at = millis();

  Serial.println("[Setup] powerManager.init()...");
  Serial.flush();
  powerManager.init();
  Serial.println("[Setup] Power OK");
  log_memory_status("after-power");
  Serial.flush();

  // Block sleep for up to 60 seconds until the first fresh Bridge configuration
  // arrives: processMqttMessage() calls allowSleep() after successful applyJson().
  // A short idle timeout could otherwise expire before Wi-Fi/MQTT/HA deliver
  // current state. Resetting the activity timer alone cannot cover variable sync
  // latency; normal idle handling resumes after 60 seconds if no sync arrives.
  powerManager.blockSleep(60000);

  // Waveshare 720×720: Square display, no rotation needed.
  // Skip auto-rotation detection (no IMU).
  Serial.println("[Setup] Display rotation: fixed (square display)");
  Serial.flush();

  Serial.println("[Setup] Setting brightness...");
  Serial.flush();
  {
    const DeviceConfig& dcfg = configManager.getConfig();
    uint8_t boot_brightness = dcfg.display_brightness;
#if defined(DEVICE_M5STACKS_TAB5)
    // Break brownout boot loops: restoring full backlight after a brownout reset
    // can trigger the next brownout during the first Wi-Fi connection.
    if (esp_reset_reason() == ESP_RST_BROWNOUT &&
        boot_brightness > kTab5SafeBrightness) {
      boot_brightness = kTab5SafeBrightness;
      tab5_brightness_capped = true;
      tab5_brightness_cap_wait_since = millis();
      Serial.println("[Setup] Brownout reset detected: brightness limited until Wi-Fi is connected");
    }
#endif
    BoardHAL::setBrightness(boot_brightness);
  }
  Serial.println("[Setup] Brightness OK");
  Serial.flush();

  // Keep the splash visible long enough, then remove it before building the UI.
  // Splash and tiles must not share a screen: the former overlay/bringToFront
  // approach allowed intermediate UI refreshes to expose tiles. Build the two
  // views sequentially to prevent that flash.
  {
    const uint32_t elapsed = millis() - boot_splash_shown_at;
    if (elapsed < kBootSplashMinVisibleMs) {
      delay(kBootSplashMinVisibleMs - elapsed);
    }
  }
#if defined(DEVICE_ESP32_S3_RGB_480)
  // Render the complete UI into the inactive S3 framebuffer while the splash
  // remains visible. The final full-screen flush changes buffers only after a
  // completed RGB frame, so no LVGL bands become visible during the handover.
  const bool s3_atomic_boot =
      DeviceImpl::displayBeginAtomicFrame("boot-ui");
  if (!s3_atomic_boot) BoardHAL::displaySleep();
#endif
  BootSplash::hide();
  // Disable invalidation throughout UI construction and status-bar population.
  // Otherwise buildUI()/switchToTab(0) exposes tiles with an empty clock before
  // updateStatusbar() supplies current values. Re-enable it only when the whole
  // screen is ready, as in tiles_reload_layout(), for one complete transition
  // from splash to UI instead of black/partial/complete intermediate frames.
  if (lv_display_t* disp = displayManager.getDisplay()) {
    lv_display_enable_invalidation(disp, false);
  }

  Serial.println("[Setup] Building UI...");
  Serial.flush();
  ui_scene_cb = mqttPublishScene;
  image_screensaver_set_scene_callback(mqttPublishScene);
  ui_hotspot_cb = set_hotspot_mode;
  settings_set_wifi_reconnect_callback(request_wifi_reconnect);
  settings_set_wifi_disconnect_callback(request_wifi_disconnect);
  settings_set_ha_pair_callback(request_ha_pair);
  settings_set_fw_check_callback(request_fw_check);
  settings_set_fw_install_callback(request_fw_install);
  settings_set_system_reboot_callback(request_system_reboot);
  webAdminServer.setGithubUpdateCallbacks(perform_fw_check, request_fw_install);
  ui_build_waiter = xTaskGetCurrentTaskHandle();
  xTaskCreatePinnedToCore(build_ui_task, "buildUI", 24576, nullptr, 2, nullptr, ARDUINO_RUNNING_CORE);
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000)) == 0) {
    Serial.println("[Setup] WARNING: UI build timeout!");
  }
  ui_build_waiter = nullptr;
  Serial.println("[Setup] UI built");
  Serial.flush();
  preload_image_screensaver();

  uiManager.updateStatusbar();
  Serial.println("[Setup] Statusbar updated");
  Serial.flush();

  // Tiles and status bar are ready. Re-enable invalidation so the wake refresh
  // presents the completed UI in one step.
  if (lv_display_t* disp = displayManager.getDisplay()) {
    lv_display_enable_invalidation(disp, true);
  }

#if !defined(DEVICE_ESP32_S3_RGB_480)
  BoardHAL::displayWake();
#endif
  lv_obj_invalidate(lv_screen_active());
#if defined(DEVICE_M5STACKS_TAB5)
  tab5_timed_refresh_now("wake-1");
  tab5_timed_display_wait("wake-1");
#else
  lv_refr_now(displayManager.getDisplay());
  BoardHAL::displayWaitDisplay();
#endif
#if defined(DEVICE_ESP32_S3_RGB_480)
  // The first full refresh already committed the completed inactive buffer.
  // Repeat only in the blackout fallback; redrawing the visible front buffer
  // again would expose LVGL's horizontal partial-render bands.
  if (!s3_atomic_boot) {
    delay(20);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(displayManager.getDisplay());
    BoardHAL::displayWaitDisplay();
  }
#else
  delay(20);
  lv_obj_invalidate(lv_screen_active());
#if defined(DEVICE_M5STACKS_TAB5)
  tab5_timed_refresh_now("wake-2");
  tab5_timed_display_wait("wake-2");
#else
  lv_refr_now(displayManager.getDisplay());
  BoardHAL::displayWaitDisplay();
#endif
#endif
#if defined(DEVICE_ESP32_S3_RGB_480)
  BoardHAL::displayWake();
#endif
  Serial.println("[Setup] Display wake OK");
  log_memory_status("after-ui-build");
  Serial.flush();

  Serial.println("[Setup] MQTT Topics...");
  Serial.flush();
  TopicSettings ts;
  if (has_config) {
    const DeviceConfig& dcfg = configManager.getConfig();
    ts.device_base = dcfg.mqtt_base_topic;
    ts.ha_prefix = dcfg.ha_prefix;
  }
  mqttTopics.begin(ts);
  Serial.println("[Setup] MQTT Topics OK");
  Serial.flush();

  if (has_config) {
    Serial.println("[Setup] Network init...");
    Serial.flush();
    // Configured media tiles require the 24 KB receive buffer before init().
    // Otherwise PubSubClient drops retained artwork immediately after subscribing.
    networkManager.setMqttMediaBufferNeeded(mqttAnyMediaTileConfigured());
    networkManager.init();
    if (networkTransport.isConnected()) uiManager.scheduleNtpSync(0);
    Serial.println("[Setup] Network OK");
    log_memory_status("after-network-init");
    Serial.flush();

    Serial.println("[Setup] MQTT-Worker...");
    Serial.flush();
    networkManager.beginMqttWorker();
    const BaseType_t mqtt_worker_core = (ARDUINO_RUNNING_CORE == 0) ? 1 : 0;
    if (xTaskCreatePinnedToCoreWithCaps(mqtt_worker_task, "mqttWorker", 12288,
                                        nullptr, tskIDLE_PRIORITY,
                                        &g_mqtt_worker_handle, mqtt_worker_core,
                                        MALLOC_CAP_SPIRAM) == pdPASS) {
      Serial.printf("[Setup] MQTT worker started on core %d\n", (int)mqtt_worker_core);
    } else {
      // Do not silently fall back to the loop task. Sharing the MQTT client would
      // reintroduce the race this single-owner design prevents; fail explicitly.
      g_mqtt_worker_handle = nullptr;
      Serial.println("[Setup] ERROR: MQTT worker could not be started -- MQTT remains offline!");
    }
    log_memory_status("after-mqtt-worker");
    Serial.flush();
  } else {
    Serial.println("[Setup] Skipping network (no configuration)");
  }

#if defined(DEVICE_ESP32_S3_RGB_480)
  // Normal boot keeps automatic VSYNC restarts masked, while boot-time UI and
  // network work can still disturb the direct PSRAM scanout after any reset.
  // Repair possible RGB timing drift once all heavy setup work has finished.
  Serial.println("[Display/S3] Resynchronizing RGB scanout after boot");
  Device::storageWriteBegin();
  Device::storageWriteEnd();
#endif

  // displayManager.init() starts the activity timer before configuration,
  // splash and UI setup, which can take up to 30 seconds. Restart the timer now
  // that the UI is usable; otherwise a short sleep timeout can expire during
  // boot before the user has had a chance to interact.
  displayManager.resetActivityTimer();

  Serial.println("\n=== SETUP COMPLETE ===\n");
  log_memory_status("setup-complete");
  Serial.flush();
}

void loop() {
  static bool first_run = true;
  static bool was_asleep = false;
  static bool logged_wifi_connected = false;
  static bool logged_mqtt_connected = false;
  static uint32_t last_mem_log_ms = 0;
  if (first_run) {
    Serial.println("[Loop] FIRST ITERATION!");
    Serial.flush();
  }

  if (first_run) Serial.println("[Loop] millis()...");
  uint32_t now = millis();

  if (first_run) Serial.println("[Loop] lv_tick_inc()...");
  lv_tick_inc(now - g_lvgl_tick_last_ms);
  g_lvgl_tick_last_ms = now;

  if (hotspot_mode_change_pending) {
    hotspot_mode_change_pending = false;
    apply_hotspot_mode(hotspot_mode_requested);
  }

  if (wifi_reconnect_pending) {
    wifi_reconnect_pending = false;
    apply_wifi_reconnect();
  }

  if (wifi_disconnect_pending) {
    wifi_disconnect_pending = false;
    if (networkTransport.isWifiDriverActive()) esp_wifi_scan_stop();
    networkManager.disconnectWifiManual();
  }
  if (ha_pair_pending) {
    ha_pair_pending = false;
    // Erzwungener MQTT-Reconnect: die Post-Connect-Publishes (Status/
    // Settings/Snapshot) lassen die HA-Bridge das Geraet neu erkennen.
    networkManager.requestMqttReconfigure();
  }

  // Consume the saved OTA retry once Wi-Fi connects and the fresh boot settles.
  if (!fw_install_auto_retry_checked && millis() > 30000 &&
      networkManager.isNetworkConnected()) {
    fw_install_auto_retry_checked = true;
    Preferences prefs;
    if (prefs.begin(kFwRetryNvsNamespace, true)) {
      const String retry_tag = prefs.getString("tag", "");
      prefs.end();
      if (retry_tag.length()) {
        Serial.printf("[Update] Automatically resuming update after restart: %s\n",
                      retry_tag.c_str());
        request_fw_install(retry_tag.c_str());
      }
    }
  }

  if (fw_check_pending) {
    fw_check_pending = false;
    apply_fw_check();
  }
  if (fw_install_pending) {
    fw_install_pending = false;
    apply_fw_install();
  }
  if (system_reboot_pending) {
    system_reboot_pending = false;
    apply_system_reboot();
  }

  const bool ota_in_progress = webAdminOtaInProgress();

  if (ota_in_progress) {
    if (!ota_display_suspended) {
      displayManager.setInputEnabled(false);
      BoardHAL::displayPowerSaveOn();
      ota_display_suspended = true;
    }
    displayManager.resetActivityTimer();
    if (webAdminServer.isRunning()) webAdminServer.handle();
    delay(1);
    if (first_run) {
      Serial.println("[Loop] OTA mode active - display suspended");
      Serial.flush();
      first_run = false;
    }
    return;
  }

  if (ota_display_suspended) {
    restore_display_after_ota_pause();
    ota_display_suspended = false;
  }

  if (first_run) Serial.println("[Loop] BoardHAL::update()...");
  BoardHAL::update();

  if (webConfigServer.isRunning()) {
    if (first_run) Serial.println("[Loop] AP mode active...");
    if (webAdminServer.isRunning()) webAdminServer.stop();

    if (first_run) {
      Serial.println("[Loop] lv_timer_handler()...");
      Serial.flush();
    }
    yield();
    sync_popup_shell();
    lv_timer_handler();
    yield();
    if (first_run) {
      Serial.println("[Loop] lv_timer_handler() COMPLETE!");
      Serial.flush();
    }

    tiles_process_pending_folder_switch();

    GuitionS3Diagnostics::service();
    webConfigServer.handle();
    // Folder taps only set a pending flag. Consume it in AP mode as well as the
    // normal loop; otherwise navigation remains stuck until AP mode ends.
    tiles_process_reload_requests();
    settings_update_ap_mode(true);
    settings_update_wifi_status_ap(webConfigApSsid(), webConfigApPassword());
    settings_update_power_status();

    if (webConfigServer.hasNewConfig()) {
      webConfigServer.resetConfigFlag();
      // WebConfigServer::handleSave() already persisted the Wi-Fi credentials.
      // Leaving AP mode performs a live reconnect with them, just like the settings
      // popup. No restart is needed.
      set_hotspot_mode(false);
    }

    if (ap_mode_started_at != 0 && (uint32_t)(now - ap_mode_started_at) > AP_MODE_TIMEOUT_MS) {
      set_hotspot_mode(false);
    }

#if defined(DEVICE_M5STACKS_TAB5)
    // Enforce the cap throughout AP mode because the display brightness slider
    // remains usable. Preserve its configured value and restore it when AP ends.
    if (BoardHAL::getBrightness() > kTab5SafeBrightness) {
      BoardHAL::setBrightness(kTab5SafeBrightness);
      tab5_brightness_capped = true;
      tab5_brightness_cap_wait_since = millis();
    }
#endif

    delay(1);

    if (first_run) {
      Serial.println("[Loop] === FIRST ITERATION COMPLETE ===");
      Serial.flush();
      first_run = false;
    }
    return;
  }

#if defined(DEVICE_M5STACKS_TAB5)
  // Lift the brownout cap after connection, or after the timeout if the radio
  // cannot finish connecting (for example, when the router is unavailable).
  if (tab5_brightness_capped) {
    if (WiFi.status() == WL_CONNECTED ||
        (uint32_t)(now - tab5_brightness_cap_wait_since) > kTab5BrightnessRestoreTimeoutMs) {
      tab5_brightness_capped = false;
      if (is_image_screensaver_visible()) {
        image_screensaver_brightness_changed();
      } else {
        powerManager.setDisplayBrightness(
            configManager.getConfig().display_brightness);
      }
      Serial.println("[Power] Brownout brightness limit removed");
    } else if (BoardHAL::getBrightness() > kTab5SafeBrightness) {
      // Auch waehrend der Wartephase durchsetzen (Slider-Aenderung im
      // Reconnect-Fenster wuerde den Schutz sonst umgehen).
      BoardHAL::setBrightness(kTab5SafeBrightness);
    }
  }
#endif

  // An open or stopping camera stream counts as activity. Keep screensaver and
  // sleep away from the PPA/JPEG pipeline, then restart normal idle timing on close.
  if (camera_popup_is_busy()) {
    displayManager.resetActivityTimer();
  }

  service_image_screensaver_auto(displayManager.getLastActivityTime());
  if (first_run) Serial.println("[Loop] powerManager.update()...");
  powerManager.update(displayManager.getLastActivityTime());

  if (first_run) Serial.println("[Loop] After powerManager.update()!");

  // --- SLEEP ---
  if (powerManager.isInSleep()) {
    if (!was_asleep) {
      Serial.println("[Loop] SLEEP MODE ACTIVE!");
      was_asleep = true;
    }
    // Clear first_run here too. If boot exceeds the idle timeout, the first loop
    // can enter sleep and otherwise print its startup diagnostics on every pass.
    // The old touch callback masked this by treating each LVGL timer call as a wake.
    if (first_run) {
      Serial.println("[Loop] Sleep detected immediately after boot");
      Serial.flush();
      first_run = false;
    }
    if (configManager.isConfigured()) {
      networkManager.update();
      if (webAdminServer.isRunning()) webAdminServer.handle();
      // The MQTT worker keeps receiving during sleep. Drain inbound messages to
      // prevent queue drops, and run post-connect work so subscriptions/discovery
      // resume after a sleeping reconnect too.
      mqttServicePostConnect();
    viewNavigationService();
      mqtt_process_inbound_queue();
      // Keep live tile state current during sleep. The paused refresh timer
      // prevents drawing to the sleeping display, so wake needs no catch-up.
      // Graph history remains on the active request/response path below.
      process_tile_update_queues<TileUpdateBudget::DrainAll>();
      // Keep clock, Wi-Fi and power labels current during sleep. Servicing them
      // only in the active loop left the clock stale until wake. These inexpensive
      // status reads and label updates can also run at the sleep-loop cadence.
      settings_update_power_status();
      if (networkManager.isNetworkConnected()) {
        const DeviceConfig& sleep_cfg = configManager.getConfig();
        const char* network_name =
            networkTransport.activeKind() == NetworkTransportKind::Wifi
                ? sleep_cfg.wifi_ssid
                : networkTransport.activeName();
        settings_update_wifi_status(
            true, network_name,
            networkTransport.localIP().toString().c_str());
      } else {
        settings_update_wifi_status(false, nullptr, nullptr);
      }
      uiManager.updateStatusbar();
      // Apply pending structure/icon changes from the periodic Bridge sync during
      // sleep as well. Service local battery and OneWire sensor state too; that
      // service already limits itself to 500 ms intervals.
      tiles_process_visible_cache_refresh(true);
      mqttServiceLocalSensors();
      tiles_process_bridge_cache_refresh(true);
      service_background_state_refresh(true);
      process_energy_response_queue();
      energy_service_periodic();
    }
    // Touch wake only changes display hardware. Background state processing runs
    // in both modes, so wake needs no extra request or queue drain. The 20 ms sleep
    // loop delay keeps newly received state close to presentation without adding
    // a separate wake-time data path.
    BoardHAL::TouchPoint tp;
    if (BoardHAL::getTouch(&tp)) {
      Serial.printf("[Power] Sleep-poll touch detected: x=%d y=%d\n", tp.x, tp.y);
      powerManager.wakeFromDisplaySleep("sleep-poll");
      was_asleep = false;
      return;
    }
    delay(20);
    return;
  }
  // Resume active mode.
  was_asleep = false;

  // Diagnostic: bracket every major step between here and the lv_timer_handler()
  // call below, so a slow segment shows up by name instead of having to guess
  // and instrument one function at a time (see project memory: repeated rounds
  // of "found one cost, animation still hitches" -- this covers the whole gap
  // in one pass). Only prints if the total exceeds 80ms.
  uint32_t t_loop0 = millis();
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  const uint32_t s3_loop_started_us = micros();
#endif

  // Do not add wake-triggered Bridge or Energy requests. Both services already
  // refresh every 60 seconds in active and sleep modes. Waking should enable
  // the display, not introduce another data-processing path.
  uint32_t t_wake = millis();

  process_popup_open();
  sync_popup_shell();
  const bool camera_popup_busy = camera_popup_is_busy();
  const bool admin_busy = webAdminRecentlyActive(20000);
  const bool ui_idle_for_background_refresh =
      !camera_popup_busy &&
      !PopupFirstFrame::any_pending() &&
      !powerManager.isHighPerformance() &&
      !admin_busy;
  service_background_state_refresh(ui_idle_for_background_refresh);
  uint32_t t_bg_refresh = millis();
  tiles_process_bridge_cache_refresh(ui_idle_for_background_refresh);
  uint32_t t_bridge_cache = millis();
  tiles_process_visible_cache_refresh(ui_idle_for_background_refresh);
  uint32_t t_visible_cache = millis();

  // --- ACTIVE ---
  // Lokale Sensoren (z. B. externer OneWire-Temperatursensor)
  mqttServiceLocalSensors();
  uint32_t t_local_sensors = millis();

  if (first_run) Serial.println("[Loop] process_sensor_update_queue()...");
  // The camera covers other popups. Keep their coalesced updates queued until
  // it closes so weather parsing and graph construction do not interrupt frames.
  if (!camera_popup_busy && !PopupFirstFrame::any_pending()) {
    process_sensor_popup_queue();
    if (!PopupFirstFrame::any_pending()) {
      process_weather_popup_queue();
      process_energy_response_queue();
      process_energy_popup_queue();
    }
  }
  process_camera_popup();
  uint32_t t_popup_queues = millis();

  // In idle mode, retain the normal tile budgets and service pending media
  // separately so artwork does not wait for the two-second background batch.
  if (!camera_popup_busy && !PopupFirstFrame::any_pending()) {
    static uint32_t last_queue_ms = 0;
    bool idle = !powerManager.isHighPerformance();
    if (!idle || (millis() - last_queue_ms >= 2000)) {
      // Apply each type's bounded batch before LVGL services the next frame.
      // Remaining idle work waits for the next two-second interval.
      process_tile_update_queues<TileUpdateBudget::Active>();
      process_tile_graph_queue();
      if (idle) energy_service_periodic();
      last_queue_ms = millis();
    } else {
      process_idle_media_updates();
    }
  }
  uint32_t t_update_queues = millis();
  // Retain navigation/layout/style reload flags while the camera covers them;
  // rebuilding invisible content could otherwise cost a complete camera frame.
  if (!camera_popup_busy && !PopupFirstFrame::any_pending()) {
    tiles_process_reload_requests();
  }
  uint32_t t_reload_requests = millis();

  if ((t_reload_requests - t_loop0) >= 80) {
    Serial.printf(
        "[LoopGap] total=%ums wake=%u bg_refresh=%u bridge_cache=%u visible_cache=%u "
        "local_sensors=%u popup_queues=%u update_queues=%u reload_requests=%u\n",
        (unsigned)(t_reload_requests - t_loop0), (unsigned)(t_wake - t_loop0),
        (unsigned)(t_bg_refresh - t_wake), (unsigned)(t_bridge_cache - t_bg_refresh),
        (unsigned)(t_visible_cache - t_bridge_cache), (unsigned)(t_local_sensors - t_visible_cache),
        (unsigned)(t_popup_queues - t_local_sensors), (unsigned)(t_update_queues - t_popup_queues),
        (unsigned)(t_reload_requests - t_update_queues));
  }

  if (first_run) {
    Serial.println("[Loop] lv_timer_handler()...");
    Serial.flush();
  }
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  const uint32_t s3_pre_lvgl_us = micros() - s3_loop_started_us;
  const uint32_t s3_lvgl_started_us = micros();
#endif
  yield();  // Yield so the watchdog can be serviced.
  sync_popup_shell();
  lv_timer_handler();
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  GuitionS3Diagnostics::noteUiLoop(
      s3_pre_lvgl_us, micros() - s3_lvgl_started_us);
#endif
  tiles_process_pending_folder_switch();
  GuitionS3Diagnostics::service();
  yield();  // Yield so the watchdog can be serviced.
  if (first_run) {
    Serial.println("[Loop] lv_timer_handler() COMPLETE!");
    Serial.flush();
  }

  // Keep this pause at 1 ms for camera throughput.
  delay(1);

  if (first_run) Serial.println("[Loop] webAdminServer.handle()...");
  if (webAdminServer.isRunning()) webAdminServer.handle();

  if (first_run) Serial.println("[Loop] Network check...");
  if (configManager.isConfigured()) {
    // The worker owns the MQTT socket, reconnects and buffer maintenance.
    // Only the application-facing queue ends remain on the loop: post-connect
    // subscriptions/discovery and incoming handlers that touch flash or LVGL.
    mqttServicePostConnect();
    viewNavigationService();
    // Keep S3 input service bounded when Home Assistant echoes a live slider
    // command or sends a retained-state burst. Eight messages per UI cycle
    // still drains far more than normal traffic without starving the next
    // touch/read/render pass. P4 keeps its established unlimited drain path.
#if defined(DEVICE_ESP32_S3_RGB_480)
    mqtt_process_inbound_queue(camera_popup_is_busy() ? 4 : 8);
#else
    mqtt_process_inbound_queue(camera_popup_is_busy() ? 4 : 0);
#endif
    if (!camera_popup_is_busy()) {
      mqttServiceDynamicSlotsReload();
    }
    static uint8_t net_tick = 0;
    if (++net_tick % 5 == 0) {
      if (first_run) Serial.println("[Loop] networkManager.update()...");
      networkManager.update();
    }
    if (!logged_wifi_connected && networkManager.isNetworkConnected()) {
      logged_wifi_connected = true;
      log_memory_status("network-connected");
    }
    if (!logged_mqtt_connected && networkManager.isMqttConnected()) {
      logged_mqtt_connected = true;
      log_memory_status("mqtt-connected");
    }
  }

  if (now - last_mem_log_ms >= 60000UL) {
    last_mem_log_ms = now;
    log_memory_status("runtime-60s");
  }

  // Report new heap lows immediately instead of waiting for the 60-second log.
  // The timestamp helps identify the cause; the 8 KB threshold suppresses normal noise.
  {
    static uint32_t last_reported_min_heap = 0xFFFFFFFF;
    const uint32_t min_heap = ESP.getMinFreeHeap();
    if (min_heap + 8192 < last_reported_min_heap) {
      last_reported_min_heap = min_heap;
      Serial.printf("[Mem] NEW LOW WATERMARK: min=%u KB (free=%u KB, largest=%u KB) @ %lu ms\n",
                    static_cast<unsigned>(min_heap / 1024),
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                    static_cast<unsigned long>(now));
    }
  }


  if (now - last_status_update > 2000UL) {
    last_status_update = now;
    settings_update_power_status();
    if (configManager.isConfigured()) {
      uiManager.serviceNtpSync();
      const DeviceConfig& c = configManager.getConfig();
      if (networkManager.isNetworkConnected()) {
        const char* network_name =
            networkTransport.activeKind() == NetworkTransportKind::Wifi
                ? c.wifi_ssid
                : networkTransport.activeName();
        settings_update_wifi_status(
            true, network_name,
            networkTransport.localIP().toString().c_str());
      } else {
        settings_update_wifi_status(false, nullptr, nullptr);
      }
      uiManager.updateStatusbar();
    }
  }

  if (first_run) {
    Serial.println("[Loop] === FIRST ITERATION COMPLETE ===");
    Serial.flush();
    first_run = false;
  }
}
