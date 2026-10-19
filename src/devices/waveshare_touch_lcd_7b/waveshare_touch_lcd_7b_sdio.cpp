// SPDX-FileCopyrightText: 2026 HomeTiles contributors
// SPDX-License-Identifier: MIT

// The Waveshare 7B with an ESP32-P4 v3.x (issue #41, a v3.2 unit): its
// factory ESP32-C6 firmware never answers the coprocessor version query, the
// SDIO link drops received packets at 40 MHz, and a software restart resets
// only the CPUs, so the next start keeps the P4's SDIO host state and cannot
// reach the C6 (a restart loop). The tester runs this board with these three
// settings in his own firmware. Only this build takes them: the 7B before
// v3.0 shares the define but not the v3 silicon, and no other board links
// these wrappers (device-profiles.json elfFlags).

#include "src/devices/device_select.h"
#include <sdkconfig.h>

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && CONFIG_ESP_REV_MIN_FULL >= 300

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_hosted.h>
#include <esp_hosted_transport_config.h>
#include <esp_rom_sys.h>
#include <esp_system.h>

#include "src/devices/waveshare_touch_lcd_7b/waveshare_touch_lcd_7b_sdio.h"

extern "C" struct esp_hosted_sdio_config
__real_esp_hosted_get_default_sdio_config(void);

// SDIO at 20 MHz (the tester's measurement: no more RX recovery at 20 MHz);
// bus width and pins stay as the core sets them.
extern "C" struct esp_hosted_sdio_config
__wrap_esp_hosted_get_default_sdio_config(void) {
  struct esp_hosted_sdio_config config =
      __real_esp_hosted_get_default_sdio_config();
  config.clock_freq_khz = 20000;
  return config;
}

// The Arduino core asks the C6 for its firmware version at every Wi-Fi start
// (esp32-hal-hosted.c hostedHasUpdate). The factory C6 never answers: the
// RPC waits for its timeout and is logged as a failure. Answered here as not
// supported at once; the core then logs that the version is unknown.
extern "C" int
__wrap_esp_hosted_get_coprocessor_fwversion(esp_hosted_coprocessor_fwver_t* ver_info) {
  (void)ver_info;
  return ESP_ERR_NOT_SUPPORTED;
}

namespace {
// Survives a system reset (RTC memory), so the second start knows it was the
// one that the first turned into a system reset.
RTC_NOINIT_ATTR uint32_t g_system_reset_marker;
constexpr uint32_t kSystemResetMarker = 0x7B5D10C6u;

bool cpu_only_restart(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return true;
    default:
      return false;
  }
}
}  // namespace

void waveshare_7b_reset_system_after_cpu_restart() {
  const esp_reset_reason_t reason = esp_reset_reason();
  if (g_system_reset_marker == kSystemResetMarker) {
    // This start came from the system reset below: go on.
    g_system_reset_marker = 0;
    Serial.println("[Device/Waveshare Touch LCD 7B] Started after a full system reset (SDIO host reset)");
    return;
  }
  if (!cpu_only_restart(reason)) {
    g_system_reset_marker = 0;
    return;
  }
  Serial.printf("[Device/Waveshare Touch LCD 7B] Restart reason %d reset only the CPUs: full system reset once "
                "(SDIO host state)\n",
                static_cast<int>(reason));
  Serial.flush();
  g_system_reset_marker = kSystemResetMarker;
  esp_rom_software_reset_system();
}

#endif
