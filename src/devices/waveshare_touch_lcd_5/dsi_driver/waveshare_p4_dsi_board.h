#pragma once

// The board description the shared Waveshare ESP32-P4 MIPI-DSI driver
// (waveshare_p4_dsi.cpp) runs a board from. A board names its panel
// controller, timings, lanes and pins here and nothing else; the driver
// owns the shared lifecycle: DSI bus, two DPI framebuffers with the shared
// camera presenter, the landscape UI turned a quarter into the portrait
// panel (p4_dsi_ui_ppa, p4_dsi_cpu_rotate), the upright layout, GT911 touch,
// LEDC backlight, microSD on SDMMC slot 0 and LittleFS.
//
// Scope: Waveshare ESP32-P4-WIFI6-Touch-LCD boards whose panel is upright
// by itself (portrait) under a landscape profile and whose board I2C,
// GT911, LEDC backlight, SDMMC slot-0 and ESP-Hosted wiring follow
// Waveshare's BSPs. Every value comes from the exact board's primary source.

#include <stdint.h>

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>

#include "src/devices/device_types.h"

namespace waveshare_p4_dsi {

// Creates the board's panel controller on the shared DSI bus. The driver
// fills panel_config (reset pin and level, RGB565, colour order) and the DPI
// configuration; the board adds its controller's vendor config, calls the
// controller constructor and returns its result.
using CreatePanelFn = esp_err_t (*)(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_dev_config_t panel_config,
                                    esp_lcd_dsi_bus_handle_t dsi_bus,
                                    const esp_lcd_dpi_panel_config_t* dpi_config,
                                    uint8_t lane_count,
                                    esp_lcd_panel_handle_t* panel);

struct Panel {
  // The panel controller, for the logs.
  const char* controller;
  CreatePanelFn create;
  // Native panel size: portrait, width < height.
  uint16_t width;
  uint16_t height;
  // MIPI-DSI link and DPI video timing.
  uint8_t lane_count;
  uint32_t lane_bit_rate_mbps;
  uint32_t dpi_clock_hz;
  uint32_t hsync_pulse_width;
  uint32_t hsync_back_porch;
  uint32_t hsync_front_porch;
  uint32_t vsync_pulse_width;
  uint32_t vsync_back_porch;
  uint32_t vsync_front_porch;
  // On-chip LDO channel and voltage that power the MIPI D-PHY.
  int mipi_phy_ldo_channel;
  int mipi_phy_ldo_mv;
  // Panel reset line and the level that holds the panel in reset.
  gpio_num_t reset_gpio;
  bool reset_active_high;
};

struct Backlight {
  gpio_num_t gpio;
  uint32_t pwm_hz;
  uint8_t pwm_bits;
  bool active_low;
};

struct Board {
  // Log prefix: "[Device/<log_name>]".
  const char* log_name;
  // The HomeTiles profile (rotation, backlight input floor, grid).
  const Device::Profile& profile;
  Panel panel;
  Backlight backlight;
  // The board I2C bus shared by the GT911 touch and the audio codecs.
  gpio_num_t i2c_sda;
  gpio_num_t i2c_scl;
  // microSD on the ESP32-P4 SDMMC slot 0 IO-MUX pins (CLK 43, CMD 44,
  // D0-D3 39-42), card power from the on-chip LDO channel.
  int sd_ldo_channel;
  int sd_max_freq_khz;
};

}  // namespace waveshare_p4_dsi
