#pragma once

#include <stdint.h>

// Board description of the Panlee ZX3D95CE01S GC9503V RGB driver
// (device_panlee_zx3d95ce01s.cpp, derived from the GS-T3E v2 driver): the
// board's constexpr kBoard in its device header holds the pins, panel reset
// and timing, backlight and touch wiring.
namespace Gc9503vS3Board {

// RGB565 parallel bus. Index 0 is the least significant bit of each colour.
struct RgbPins {
  int8_t de;
  int8_t vsync;
  int8_t hsync;
  int8_t pclk;
  int8_t red[5];
  int8_t green[6];
  int8_t blue[5];
};

struct SdSpiPins {
  int8_t sck;
  int8_t miso;
  int8_t mosi;
  int8_t cs;
};

inline constexpr SdSpiPins kNoSdCard{-1, -1, -1, -1};

// One porch/pulse set of the RGB timing.
struct SyncTiming {
  uint16_t front_porch;
  uint16_t pulse_width;
  uint16_t back_porch;
};

struct Description {
  // Board name in "[Device/<name>]" logs.
  const char* log_name;
  // GC9503V 3-wire SPI command bus. It is used only before the RGB panel
  // starts; SCK/MOSI are RGB data pins afterwards.
  int8_t panel_cs;
  int8_t panel_sck;
  int8_t panel_mosi;
  // GPIO pulsed low before the init table, or -1 for the software reset.
  int8_t panel_reset;
  RgbPins rgb;
  SyncTiming hsync;
  SyncTiming vsync;
  // esp_lcd_rgb_timing_t flags.pclk_active_neg.
  bool pclk_active_neg;
  // LEDC PWM backlight, active high.
  int8_t backlight_pin;
  uint32_t backlight_frequency_hz;
  // FT6336 I2C bus without reset or interrupt use.
  int8_t touch_sda;
  int8_t touch_scl;
  uint8_t touch_address;
  // microSD card in SPI mode. Without one, every SD path is a clean no-op.
  bool has_sd_card;
  SdSpiPins sd;
};

}  // namespace Gc9503vS3Board
