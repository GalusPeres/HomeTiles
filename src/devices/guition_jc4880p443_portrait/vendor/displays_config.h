#pragma once

#include <stdint.h>

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC4880P443_PORTRAIT)

struct DisplayConfig {
  const char* name;
  uint32_t hsync_pulse_width;
  uint32_t hsync_back_porch;
  uint32_t hsync_front_porch;
  uint32_t vsync_pulse_width;
  uint32_t vsync_back_porch;
  uint32_t vsync_front_porch;
  uint32_t prefer_speed;
  uint32_t lane_bit_rate;
  uint16_t width;
  uint16_t height;
  int8_t i2c_sda_pin;
  int8_t i2c_scl_pin;
  uint32_t i2c_clock_speed;
  int8_t lcd_rst;
};

// Guition JC4880P443C_I_W timing for the native portrait ST7701S panel
// (field-verified MIPI-DSI bring-up: 2 lanes at 500 Mbps, 34 MHz DPI clock).
inline constexpr DisplayConfig SCREEN_DEFAULT{
    "Guition-JC4880P443",
    12,
    42,
    42,
    2,
    8,
    166,
    34000000,
    500,
    480,
    800,
    7,
    8,
    400000,
    5,
};

inline constexpr const DisplayConfig& display_cfg = SCREEN_DEFAULT;

#endif  // defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
