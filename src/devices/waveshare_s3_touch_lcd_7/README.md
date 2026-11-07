# Waveshare ESP32-S3-Touch-LCD-7

This profile is for the exact **ESP32-S3-Touch-LCD-7** board: an ESP32-S3
N16R8 (16 MB flash, 8 MB octal PSRAM), a 7-inch 800x480 RGB panel using
16 parallel RGB565 data lines (ST7262 timing controller), a GT911
capacitive touch controller and a CH422G I/O expander.

## Profile and build

- Profile and release key: `waveshare_s3_touch_lcd_7`
- Device define: `DEVICE_WAVESHARE_S3_TOUCH_LCD_7`
- ESP32-S3, 16 MB flash, 8 MB octal PSRAM, custom `partitions.csv`
- Build profile in `sketch.yaml`: `waveshare_s3_touch_lcd_7`
- The RGB panel and GT911 touch are driven directly through ESP-IDF/Arduino
  (`esp_lcd_new_rgb_panel`, `Wire`); no display-panel library is required.
- Internal LittleFS only; the microSD slot is not implemented

## Board wiring

- Shared I2C bus: SDA GPIO8, SCL GPIO9
- CH422G outputs (register-less, WR_SET `0x24`, WR_IO `0x38`):
  TP_RST EXIO1, LCD_BL EXIO2, LCD_RST EXIO3, SD_CS EXIO4, USB_SEL EXIO5
- GT911 at `0x5D` (`0x14` alternate), INT GPIO4. The reset line is
  CH422G EXIO1; the address is latched from INT during the reset pulse.
- RGB: PCLK GPIO7, DE GPIO5, HSYNC GPIO46, VSYNC GPIO3, data
  GPIO14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40

## Display configuration

The values come from Waveshare's board configuration and the working
ESP-IDF LVGL demo for this panel:

- 16 MHz PCLK, HSYNC 4/8/8, VSYNC 4/8/8, RGB565 through 16 data lines
- The panel latches data on the rising PCLK edge, so the peripheral
  launches data on the falling edge (`pclk_active_neg`). The library
  default launches on the rising edge and leaves no setup time.
- 20-line bounce buffer. It is refilled from PSRAM in the GDMA EOF
  interrupt, so the larger buffer absorbs Wi-Fi/LVGL stalls.
- The per-VSYNC GDMA restart is masked. Arduino-ESP32 3.3.7 ships
  ESP-IDF 5.5.2, whose bounce-buffer index is not re-synchronised by that
  restart (IDFGH-18263, fixed after 5.5.2); one late EOF otherwise left
  the picture flickering with horizontal bands of stale pixels.

## Hardware validation and limitations

Display output and GT911 detection (`0x5D`) were verified on 2026-11-07.
The PCLK-edge/bounce/VSYNC display fix still needs a longer hardware
observation, and touch interaction is not yet validated end to end.

The backlight is a hard on/off through CH422G EXIO2; `setBrightness()`
only stores the value and does not dim the panel yet. Rotation,
`displaySleep()`/`displayWake()` and the camera are not implemented for
this bring-up profile. The profile stays `publish: false` in
`tools/device-profiles.json`, so it has no installer entry or release
image until the board is validated.

## Primary hardware references

- [Waveshare product page](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
- [Waveshare documentation](https://docs.waveshare.net/ESP32-S3-Touch-LCD-7)
- [Official board examples and board configuration](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7)
