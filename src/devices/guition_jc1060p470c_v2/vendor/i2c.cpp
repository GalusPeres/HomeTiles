/*
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC1060P470C_V2)

#include "i2c.h"

#include <esp_log.h>

namespace {

constexpr gpio_num_t kSdaPin = GPIO_NUM_7;
constexpr gpio_num_t kSclPin = GPIO_NUM_8;
i2c_master_bus_handle_t g_bus = nullptr;

}  // namespace

DEV_I2C_Port DEV_I2C_Init() {
  if (g_bus) {
    return DEV_I2C_Port{g_bus};
  }

  i2c_master_bus_config_t config = {};
  config.i2c_port = I2C_NUM_0;
  config.sda_io_num = kSdaPin;
  config.scl_io_num = kSclPin;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = false;

  const esp_err_t err = i2c_new_master_bus(&config, &g_bus);
  if (err != ESP_OK) {
    ESP_LOGE("JC1060-I2C", "I2C master bus init failed: %s", esp_err_to_name(err));
    g_bus = nullptr;
  }
  return DEV_I2C_Port{g_bus};
}

#endif  // defined(DEVICE_GUITION_JC1060P470C_V2)
