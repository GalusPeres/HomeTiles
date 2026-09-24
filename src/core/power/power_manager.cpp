#include "src/core/power/power_manager.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/ui_manager.h"
#include "src/core/hardware/board_hal.h"
#include "src/core/display/display_manager.h"
#include "src/core/config/config_manager.h"
#include "src/core/power/battery_state.h"
#include "src/network/network_manager.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/video/local_camera/local_camera.h"
#include <cmath>
#if defined(ARDUINO_ARCH_ESP32)
#include "esp32-hal-cpu.h"
#include "esp_log.h"
#endif

PowerManager powerManager;

// The shared power policy currently reports mains power for every profile.
static bool detect_powered_by_mains_hw() {
  return true;
}

void PowerManager::applyCpuFrequency(uint16_t mhz) {
#if defined(ARDUINO_ARCH_ESP32)
  if (mhz == 0) return;
  if (last_cpu_mhz == 0) {
    last_cpu_mhz = static_cast<uint16_t>(getCpuFrequencyMhz());
  }
  if (last_cpu_mhz == mhz) return;
  const bool ok = setCpuFrequencyMhz(static_cast<uint32_t>(mhz));
  last_cpu_mhz = static_cast<uint16_t>(getCpuFrequencyMhz());
  Serial.printf("CPU Freq req=%u MHz, set=%s, actual=%u MHz\n",
                static_cast<unsigned>(mhz),
                ok ? "ok" : "fail",
                static_cast<unsigned>(last_cpu_mhz));
#else
  (void)mhz;
#endif
}

void PowerManager::init() {
  Serial.println("[Power] Init Power Manager...");
  disp = displayManager.getDisplay();
  is_high_performance = true;
#if defined(ARDUINO_ARCH_ESP32)
  last_cpu_mhz = static_cast<uint16_t>(getCpuFrequencyMhz());
#endif
}

void PowerManager::setHighPerformance(bool enable) {
  if (enable == is_high_performance) return;
  is_high_performance = enable;

  if (enable) {
    applyCpuFrequency(CPU_FREQ_HIGH);
    networkManager.setWifiPowerSaving(false);

#if LV_VERSION_MAJOR >= 9
    if (disp) {
        lv_timer_t * rt = lv_display_get_refr_timer(disp);
        if(rt) lv_timer_set_period(rt, 1000 / FPS_HIGH);
    }
#endif
  } else {
    applyCpuFrequency(CPU_FREQ_LOW);
    networkManager.setWifiPowerSaving(true);

#if LV_VERSION_MAJOR >= 9
    if (disp) {
        lv_timer_t * rt = lv_display_get_refr_timer(disp);
        if(rt) lv_timer_set_period(rt, 1000 / FPS_LOW);
    }
#endif
  }
}

uint32_t PowerManager::getSleepTimeout() const {
  const DeviceConfig& cfg = configManager.getConfig();
  // The current shared policy uses only the mains sleep settings.
  if (!cfg.auto_sleep_enabled) return 0xFFFFFFFF;
  return cfg.auto_sleep_seconds * 1000UL;
}

bool PowerManager::isPoweredByMains() const {
  return true;  // Current shared policy: mains power.
}

bool PowerManager::isTouchWakeEnabled() const {
  // The current shared wake policy always uses touch.
  return true;
}

bool PowerManager::isImuWakeEnabled() const {
  // The shared implementation does not use IMU wake.
  return false;
}

// IMU behavior remains unimplemented in the shared power manager.
bool PowerManager::ensureImuReady() { return false; }
bool PowerManager::detectAutoRotation(bool*) { return false; }
void PowerManager::serviceImuWake() {}

void PowerManager::update(uint32_t last_activity_time) {
  if (is_display_sleeping) {
    // Touch-wake is handled in the main loop
    return;
  }
  uint32_t now = millis();
  uint32_t sleep_timeout = getSleepTimeout();

  // Is sleep currently blocked?
  if (sleep_blocked) {
    if (sleep_block_until != 0 && (int32_t)(now - sleep_block_until) >= 0) {
      sleep_blocked = false;
    } else {
      setHighPerformance(true);
      return;
    }
  }

  if (is_high_performance && (now - last_activity_time > IDLE_TIMEOUT_MS)) {
    setHighPerformance(false);
  }

  if (!is_high_performance && sleep_timeout != 0xFFFFFFFF) {
    if (now - last_activity_time > sleep_timeout) {
      enterDisplaySleep();
    }
  }
}

void PowerManager::enterDisplaySleep() {
  if (is_display_sleeping) return;
  Serial.println("[Power] Sleep...");

  uiManager.lockProtectedAccess();

  saved_brightness = BoardHAL::getBrightness();

  // 1) Stop LVGL rendering immediately.
#if LV_VERSION_MAJOR >= 9
  if (disp) {
    lv_timer_t* rt = lv_display_get_refr_timer(disp);
    if (rt) lv_timer_pause(rt);
  }
#endif
  // 2) Wait for DMA completion.
  BoardHAL::displayWaitDisplay();

  // Keep touch wake enabled under the current shared policy.
  display_hw_sleeping = false;
  BoardHAL::setBrightness(0);
  BoardHAL::displayWaitDisplay();
  delay(30);
  BoardHAL::displayPowerSaveOn();
  BoardHAL::displayWaitDisplay();
  delay(60);
  esp_log_level_set("lcd.dsi.dpi", ESP_LOG_NONE);
  applyCpuFrequency(CPU_FREQ_SLEEP);
  esp_log_level_set("lcd.dsi.dpi", ESP_LOG_ERROR);
  BoardHAL::displayWaitDisplay();
  delay(20);

  displayManager.setInputEnabled(true);  // Keep touch active for wake-up.
  networkManager.setSleepWifiProfile(false);
  networkManager.setWifiPowerSaving(false);
  is_display_sleeping = true;
  is_high_performance = false;
  // Free the camera pipeline buffers; snapshot requests re-create them.
  local_camera::releaseForSleep();
  mqttPublishDeviceSettings();
}

void PowerManager::wakeFromDisplaySleep(const char* reason) {
  if (!is_display_sleeping) return;
  Serial.printf("[Power] Wake (%s)\n", reason ? reason : "unknown");

  BoardHAL::setBrightness(0);

  esp_log_level_set("lcd.dsi.dpi", ESP_LOG_NONE);
  applyCpuFrequency(CPU_FREQ_HIGH);
  esp_log_level_set("lcd.dsi.dpi", ESP_LOG_ERROR);
  BoardHAL::displayWaitDisplay();
  delay(20);
  displayManager.setInputEnabled(true);
  if (display_hw_sleeping) {
    BoardHAL::displayWake();
    display_hw_sleeping = false;
  }
  BoardHAL::displayPowerSaveOff();
  BoardHAL::displayWaitDisplay();
  delay(30);

#if LV_VERSION_MAJOR >= 9
    if (disp) {
        lv_timer_t * rt = lv_display_get_refr_timer(disp);
        if(rt) {
            lv_timer_resume(rt);
            lv_timer_set_period(rt, 1000 / FPS_HIGH);
        }
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(disp);
    }
#endif
  BoardHAL::displayWaitDisplay();
  delay(10);
  BoardHAL::setBrightness(saved_brightness);

  is_display_sleeping = false;
  is_high_performance = true;

  networkManager.setSleepWifiProfile(false);
  networkManager.setWifiPowerSaving(false);
  displayManager.resetActivityTimer();
  displayManager.armWakeTouchGuard();
  mqttPublishDeviceSettings();
}

void PowerManager::setDisplayBrightness(uint8_t brightness) {
  if (is_display_sleeping) {
    saved_brightness = brightness;
    return;
  }
  BoardHAL::setBrightness(brightness);
}

void PowerManager::updatePowerMode() {
}

void PowerManager::blockSleep(uint32_t duration_ms) {
  sleep_blocked = true;
  sleep_block_until = duration_ms ? millis() + duration_ms : 0;
  if (is_display_sleeping) {
    wakeFromDisplaySleep("blockSleep");
  }
  setHighPerformance(true);
}

void PowerManager::allowSleep() {
  sleep_blocked = false;
  sleep_block_until = 0;
}

bool PowerManager::isSleepBlocked() const {
  if (!sleep_blocked) return false;
  if (sleep_block_until == 0) return true;
  return (int32_t)(millis() - sleep_block_until) < 0;
}
