/*
 * SPDX-FileCopyrightText: 2026 HomeTiles contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC1060P470C_V2)

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

  uint16_t width;   // native panel width, landscape
  uint16_t height;  // native panel height, landscape

  int8_t i2c_sda_pin;
  int8_t i2c_scl_pin;
  uint32_t i2c_clock_speed;
  int8_t lcd_rst;
};

// Guition JC1060P470C V2 "New Panel" native landscape JD9165 timing. The
// 2026 V2 panel needs this exact timing and its own vendor init sequence.
static constexpr DisplayConfig SCREEN_DEFAULT = {
    "JC1060P470C V2 New Panel",
    24,
    136,
    160,
    2,
    21,
    12,
    52000000,
    750,
    1024,
    600,
    7,
    8,
    400000,
    0,
};

inline constexpr const DisplayConfig& display_cfg = SCREEN_DEFAULT;

#endif  // defined(DEVICE_GUITION_JC1060P470C_V2)
