#pragma once

#include <stdint.h>

// Board description of the GS-T3E v2.4 ST7701 RGB driver
// (device_golden_security_gs_t3e_v24.cpp): the board's constexpr kBoard in
// its device header holds the pins, init table, PCLK edge, backlight and SD
// wiring. Display/touch/backlight wiring is identical to PCB v2.3; only the
// relay pins and the missing USB socket differ.
namespace St7701S3Board {

// ST7701 initialization tables of the pinned GFX Library for Arduino 1.6.5
// (src/display/Arduino_RGB_Display.h). They differ only in two commands.
enum class PanelInit : uint8_t {
  // st7701_type9_init_operations: BK0 CD=0x00, display inversion off.
  Type9,
  // st7701_type1_init_operations: BK0 CD=0x08 and INVON (0x21).
  Type1,
};

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

struct Description {
  // Board name in "[Device/<name>]" logs.
  const char* log_name;
  // ST7701 3-wire SPI command bus. It is used only before the RGB panel
  // starts; on some boards SCK/MOSI are RGB data pins afterwards.
  int8_t panel_cs;
  int8_t panel_sck;
  int8_t panel_mosi;
  RgbPins rgb;
  PanelInit panel_init;
  // Init table name in the display-ready log.
  const char* panel_init_label;
  // esp_lcd_rgb_timing_t flags.pclk_active_neg.
  bool pclk_active_neg;
  // LEDC PWM backlight, active high.
  int8_t backlight_pin;
  uint32_t backlight_frequency_hz;
  // GT911 I2C bus without reset or interrupt pins.
  int8_t touch_sda;
  int8_t touch_scl;
  // microSD card in SPI mode. Without one, every SD path is a clean no-op.
  bool has_sd_card;
  SdSpiPins sd;
};

}  // namespace St7701S3Board
