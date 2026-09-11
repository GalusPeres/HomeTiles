/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for HomeTiles: adapted Espressif's GSL3680 register protocol and
 * raw-coordinate path to the small device-driver interface used by HomeTiles.
 * The GPL point-tracking algorithm used by the original test fork is not used.
 * Source: https://github.com/esp-arduino-libs/ESP32_Display_Panel
 */

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC8012P4A1_V2)

#include "gsl3680_touch.h"

#include <stddef.h>

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "gsl3680_firmware.h"

namespace {

constexpr char kTag[] = "gsl3680";
constexpr gpio_num_t kSdaPin = GPIO_NUM_7;
constexpr gpio_num_t kSclPin = GPIO_NUM_8;
constexpr gpio_num_t kResetPin = GPIO_NUM_22;
constexpr gpio_num_t kInterruptPin = GPIO_NUM_21;
constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;
constexpr uint16_t kI2cAddress = 0x40;
constexpr uint32_t kPrimaryI2cFrequencyHz = 400000;
constexpr uint32_t kFallbackI2cFrequencyHz = 100000;
constexpr uint8_t kMaxContacts = 5;

// Raw ranges and axis relationship are from Espressif's Apache-2.0 GSL3680
// driver. The controller's X axis follows the panel's long axis, while its Y
// axis follows the short axis.
constexpr uint32_t kRawMaxX = 1650;
constexpr uint32_t kRawMaxY = 890;
constexpr uint16_t kNativeWidth = 800;
constexpr uint16_t kNativeHeight = 1280;

struct TouchState {
  i2c_master_bus_handle_t bus = nullptr;
  i2c_master_dev_handle_t device = nullptr;
  bool initialized = false;
  uint16_t x[kMaxContacts] = {};
  uint16_t y[kMaxContacts] = {};
  uint16_t strength[kMaxContacts] = {};
  uint8_t track_ids[kMaxContacts] = {};
  uint8_t count = 0;
};

TouchState g_state;

void wait_ms(uint32_t milliseconds) {
  vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

esp_err_t write_register(uint8_t reg, const uint8_t* data, size_t length) {
  if (!g_state.device || !data || length > 4) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t buffer[5] = {reg, 0, 0, 0, 0};
  for (size_t i = 0; i < length; ++i) {
    buffer[i + 1] = data[i];
  }
  return i2c_master_transmit(g_state.device, buffer, length + 1, 100);
}

esp_err_t read_register(uint8_t reg, uint8_t* data, size_t length) {
  if (!g_state.device || !data) {
    return ESP_ERR_INVALID_ARG;
  }
  return i2c_master_transmit_receive(g_state.device, &reg, 1, data, length, 100);
}

void deinit_i2c() {
  if (g_state.device) {
    i2c_master_bus_rm_device(g_state.device);
    g_state.device = nullptr;
  }
  if (g_state.bus) {
    i2c_del_master_bus(g_state.bus);
    g_state.bus = nullptr;
  }
}

esp_err_t init_i2c(uint32_t frequency_hz) {
  if (g_state.device) {
    return ESP_OK;
  }

  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = kI2cPort;
  bus_config.sda_io_num = kSdaPin;
  bus_config.scl_io_num = kSclPin;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  esp_err_t err = i2c_new_master_bus(&bus_config, &g_state.bus);
  if (err != ESP_OK) {
    return err;
  }

  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kI2cAddress;
  device_config.scl_speed_hz = frequency_hz;
  err = i2c_master_bus_add_device(g_state.bus, &device_config, &g_state.device);
  if (err != ESP_OK) {
    deinit_i2c();
  }
  return err;
}

esp_err_t clear_registers() {
  uint8_t value = 0x88;
  esp_err_t err = write_register(0xE0, &value, 1);
  if (err != ESP_OK) return err;
  wait_ms(20);

  value = 0x01;
  err = write_register(0x88, &value, 1);
  if (err != ESP_OK) return err;
  wait_ms(5);

  value = 0x04;
  err = write_register(0xE4, &value, 1);
  if (err != ESP_OK) return err;
  wait_ms(5);

  value = 0x00;
  err = write_register(0xE0, &value, 1);
  if (err != ESP_OK) return err;
  wait_ms(20);
  return ESP_OK;
}

esp_err_t select_i2c_address() {
  // GSL3680 samples INT while reset is released to select its I2C address.
  // GPIO21 low selects the 7-bit address 0x40 used by this board.
  esp_err_t err = gpio_set_level(kResetPin, 0);
  if (err != ESP_OK) return err;
  err = gpio_set_level(kInterruptPin, 0);
  if (err != ESP_OK) return err;
  wait_ms(10);

  err = gpio_set_level(kInterruptPin, 0);
  if (err != ESP_OK) return err;
  wait_ms(1);

  err = gpio_set_level(kResetPin, 1);
  if (err != ESP_OK) return err;
  wait_ms(10);
  wait_ms(50);
  return ESP_OK;
}

esp_err_t release_interrupt_pin() {
  gpio_config_t interrupt_input = {};
  interrupt_input.mode = GPIO_MODE_INPUT;
  interrupt_input.pull_up_en = GPIO_PULLUP_ENABLE;
  interrupt_input.intr_type = GPIO_INTR_DISABLE;
  interrupt_input.pin_bit_mask = 1ULL << kInterruptPin;
  return gpio_config(&interrupt_input);
}

esp_err_t reset_controller() {
  esp_err_t err = gpio_set_level(kResetPin, 0);
  if (err != ESP_OK) return err;
  wait_ms(20);
  err = gpio_set_level(kResetPin, 1);
  if (err != ESP_OK) return err;
  wait_ms(20);

  uint8_t value = 0x88;
  err = write_register(0xE0, &value, 1);
  if (err != ESP_OK) return err;
  wait_ms(10);

  value = 0x04;
  err = write_register(0xE4, &value, 1);
  if (err != ESP_OK) return err;
  wait_ms(10);

  const uint8_t zeros[4] = {0, 0, 0, 0};
  err = write_register(0xBC, zeros, sizeof(zeros));
  if (err != ESP_OK) return err;
  wait_ms(10);
  return ESP_OK;
}

esp_err_t probe_controller() {
  uint8_t status[4] = {};
  esp_err_t err = read_register(0xF0, status, sizeof(status));
  if (err != ESP_OK) {
    return err;
  }
  wait_ms(20);

  const uint8_t probe[4] = {0x12, 0x34, 0x56, 0x00};
  err = write_register(0xF0, probe, sizeof(probe));
  if (err != ESP_OK) {
    return err;
  }
  wait_ms(20);

  err = read_register(0xF0, status, sizeof(status));
  if (err != ESP_OK) {
    return err;
  }
  wait_ms(20);

  if (status[0] != probe[0]) {
    Serial.printf(
        "[Device/Guition JC8012P4A1 V2] Touch probe mismatch: "
        "%02x %02x %02x %02x\n",
        status[0], status[1], status[2], status[3]);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t load_firmware() {
  for (size_t i = 0; i < sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0]); ++i) {
    const uint32_t value = GSLX680_FW[i].val;
    const uint8_t bytes[4] = {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 24),
    };
    const size_t length = GSLX680_FW[i].offset == 0xF0 ? 1 : 4;
    const esp_err_t err = write_register(static_cast<uint8_t>(GSLX680_FW[i].offset), bytes, length);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "firmware write failed at %u: %d", static_cast<unsigned>(i), err);
      return err;
    }
  }
  return ESP_OK;
}

esp_err_t startup_controller() {
  const uint8_t value = 0x00;
  const esp_err_t err = write_register(0xE0, &value, 1);
  if (err == ESP_OK) {
    wait_ms(10);
  }
  return err;
}

esp_err_t verify_firmware() {
  wait_ms(30);
  uint8_t status[4] = {};
  const esp_err_t err = read_register(0xB0, status, sizeof(status));
  if (err != ESP_OK) {
    return err;
  }
  for (uint8_t value : status) {
    if (value != 0x5A) {
      ESP_LOGE(kTag, "firmware check failed: %02x %02x %02x %02x",
               status[0], status[1], status[2], status[3]);
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

esp_err_t require_init_step(const char* step, esp_err_t err) {
  if (err != ESP_OK) {
    Serial.printf(
        "[Device/Guition JC8012P4A1 V2] Touch init failed at %s: %s (0x%x)\n",
        step, esp_err_to_name(err), static_cast<unsigned>(err));
  }
  return err;
}

esp_err_t init_controller() {
  // Keep the Apache-2.0 Espressif firmware together with the initialization
  // sequence used by working JC8012P4A1C implementations of that same driver.
  // Mixing this firmware with the different GPL driver sequence is untested.
  esp_err_t err = require_init_step("I2C address selection", select_i2c_address());
  if (err != ESP_OK) return err;
  err = require_init_step("controller probe", probe_controller());
  if (err != ESP_OK) return err;
  err = require_init_step("register clear", clear_registers());
  if (err != ESP_OK) return err;
  err = require_init_step("firmware reset", reset_controller());
  if (err != ESP_OK) return err;
  err = require_init_step("firmware upload", load_firmware());
  if (err != ESP_OK) return err;
  err = require_init_step("firmware start", startup_controller());
  if (err != ESP_OK) return err;
  err = require_init_step("post-upload reset", reset_controller());
  if (err != ESP_OK) return err;
  err = require_init_step("post-reset start", startup_controller());
  if (err != ESP_OK) return err;
  err = require_init_step("firmware verification", verify_firmware());
  if (err != ESP_OK) return err;
  return require_init_step("interrupt pin release", release_interrupt_pin());
}

uint16_t scale_and_clamp(uint16_t raw, uint32_t raw_max, uint16_t output_max) {
  uint32_t scaled = static_cast<uint32_t>(raw) * output_max / raw_max;
  if (scaled >= output_max) {
    scaled = output_max - 1;
  }
  return static_cast<uint16_t>(scaled);
}

}  // namespace

esp_lcd_touch_handle_t touch_gsl3680_init() {
  if (g_state.initialized) {
    return &g_state;
  }

  gpio_config_t reset_config = {};
  reset_config.mode = GPIO_MODE_OUTPUT;
  reset_config.pin_bit_mask = 1ULL << kResetPin;
  esp_err_t err = gpio_config(&reset_config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "reset GPIO init failed: %d", err);
    return nullptr;
  }

  gpio_config_t interrupt_config = {};
  interrupt_config.mode = GPIO_MODE_OUTPUT;
  interrupt_config.pull_up_en = GPIO_PULLUP_ENABLE;
  interrupt_config.intr_type = GPIO_INTR_DISABLE;
  interrupt_config.pin_bit_mask = 1ULL << kInterruptPin;
  err = gpio_config(&interrupt_config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "interrupt GPIO init failed: %d", err);
    return nullptr;
  }

  constexpr uint32_t frequencies[] = {
      kPrimaryI2cFrequencyHz,
      kFallbackI2cFrequencyHz,
  };
  for (uint32_t frequency_hz : frequencies) {
    err = init_i2c(frequency_hz);
    if (err == ESP_OK) {
      err = init_controller();
    }
    if (err == ESP_OK) {
      g_state.initialized = true;
      g_state.count = 0;
      ESP_LOGI(kTag, "GSL3680 initialized with Espressif firmware at %lu Hz",
               static_cast<unsigned long>(frequency_hz));
      return &g_state;
    }

    ESP_LOGW(kTag, "controller init failed at %lu Hz: %d",
             static_cast<unsigned long>(frequency_hz), err);
    deinit_i2c();
  }

  return nullptr;
}

bool esp_lcd_touch_read_data(esp_lcd_touch_handle_t handle) {
  auto* state = static_cast<TouchState*>(handle);
  if (!state || !state->initialized) {
    return false;
  }

  uint8_t data[24] = {};
  const esp_err_t err = read_register(0x80, data, sizeof(data));
  if (err != ESP_OK) {
    state->count = 0;
    ESP_LOGW(kTag, "touch read failed: %d", err);
    return false;
  }

  const uint8_t count = data[0] & 0x0F;
  if (count == 0 || count > kMaxContacts) {
    state->count = 0;
    return true;
  }

  state->count = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const size_t offset = 4 + static_cast<size_t>(i) * 4;
    const uint16_t raw_x = (static_cast<uint16_t>(data[offset + 3] & 0x0F) << 8) | data[offset + 2];
    const uint16_t raw_y = (static_cast<uint16_t>(data[offset + 1]) << 8) | data[offset];
    // Reject the entire report before publishing contacts. Clamping invalid
    // raw coordinates would turn a malformed report into an edge press.
    if (raw_x > kRawMaxX || raw_y > kRawMaxY) {
      return true;
    }
    // Keep Espressif's clean driver relationship exactly: controller X maps
    // to the panel's 1280-pixel long axis, controller Y to its 800-pixel
    // short axis. The board driver then applies HomeTiles' landscape rotation.
    state->x[i] = scale_and_clamp(raw_x, kRawMaxX, kNativeHeight);
    state->y[i] = scale_and_clamp(raw_y, kRawMaxY, kNativeWidth);
    state->strength[i] = 100;
    state->track_ids[i] = static_cast<uint8_t>(data[offset + 3] >> 4);
  }
  state->count = count;
  return true;
}

bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t handle,
                                   uint16_t* x,
                                   uint16_t* y,
                                   uint16_t* strength,
                                   uint8_t* track_ids,
                                   uint8_t* count,
                                   uint8_t max_points) {
  auto* state = static_cast<TouchState*>(handle);
  if (!state || !x || !y || !count || max_points == 0) {
    return false;
  }

  const uint8_t available = state->count < max_points ? state->count : max_points;
  for (uint8_t i = 0; i < available; ++i) {
    x[i] = state->x[i];
    y[i] = state->y[i];
    if (strength) {
      strength[i] = state->strength[i];
    }
    if (track_ids) {
      track_ids[i] = state->track_ids[i];
    }
  }
  *count = available;
  state->count = 0;
  return available > 0;
}

#endif  // defined(DEVICE_GUITION_JC8012P4A1_V2)
