#pragma once
#ifndef ELECROW_CROWPANEL_10_1_DISPLAYS_CONFIG_H
#define ELECROW_CROWPANEL_10_1_DISPLAYS_CONFIG_H

#include <stdint.h>

#include "src/devices/device_select.h"

#if defined(DEVICE_ELECROW_CROWPANEL_10_1)

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

// Elecrow CrowPanel Advanced 10.1 (V1.1/V1.2) EK79007 timing and pins.
// Values follow Elecrow's board_config.h for the 1024x600 panel.
static constexpr DisplayConfig SCREEN_DEFAULT = {
    "ELECROW-CROWPANEL-10.1-EK79007",
    70,
    160,
    160,
    10,
    23,
    21,
    51000000,
    1000,
    1024,
    600,
    45,
    46,
    400000,
    41,
};

inline constexpr const DisplayConfig& display_cfg = SCREEN_DEFAULT;

#endif  // defined(DEVICE_ELECROW_CROWPANEL_10_1)
#endif  // ELECROW_CROWPANEL_10_1_DISPLAYS_CONFIG_H
