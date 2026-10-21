#pragma once
#ifndef DISPLAYS_CONFIG_H
#define DISPLAYS_CONFIG_H

#include <stdint.h>

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)

struct DisplayConfig {
  const char* name;

  uint32_t hsync_pulse_width;
  uint32_t hsync_back_porch;
  uint32_t hsync_front_porch;
  uint32_t vsync_pulse_width;
  uint32_t vsync_back_porch;
  uint32_t vsync_front_porch;
  uint32_t prefer_speed;   // DPI pixel clock in Hz
  uint32_t lane_bit_rate;  // DSI lane bit rate in Mbps

  uint16_t width;   // native panel width, portrait
  uint16_t height;  // native panel height, portrait

  int8_t i2c_sda_pin;
  int8_t i2c_scl_pin;
  uint32_t i2c_clock_speed;
  int8_t lcd_rst;
};

// Waveshare 10.1-inch native portrait timing.
static constexpr DisplayConfig SCREEN_DEFAULT = {
    "10.1-DSI-TOUCH",
    20,
    20,
    40,
    4,
    10,
    30,
    80000000,
    1500,
    800,
    1280,
    7,
    8,
    400000,
    27,
};

inline constexpr const DisplayConfig& display_cfg = SCREEN_DEFAULT;

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
#endif  // DISPLAYS_CONFIG_H
