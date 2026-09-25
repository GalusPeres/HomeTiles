# Guition JC8012P4A1

This target supports the 10.1-inch Guition JC8012P4A1 family as a 1280x800
landscape HomeTiles display. The physical JD9365 panel is 800x1280; the board
driver rotates HomeTiles' framebuffer into its native portrait orientation.

## Source-verified hardware configuration

- ESP32-P4 application processor, ESP32-C6 Wi-Fi coprocessor, 16MB SPI flash
  and 32MB PSRAM
- two-lane MIPI-DSI at 1500Mbps per lane; 60MHz DPI pixel clock
- native 800x1280 timing: H 20/20/40 and V 4/8/20
- MIPI D-PHY power from on-chip LDO channel 3 at 2500mV
- LCD reset on GPIO 27
- active-high backlight on GPIO 23, LEDC low-speed timer 1, 20kHz, 10-bit
- GSL3680 touch on I2C SDA 7/SCL 8, reset GPIO 22, optional interrupt GPIO 21
- 4-bit microSD on SDMMC slot 0: D0-D3 39-42, CLK 43, CMD 44; power control
  through on-chip LDO channel 4

HomeTiles first tries the 400kHz touch clock used by the reported-working
HomeTiles fork and automatically retries at the 100kHz value used by Guition's
official Arduino rotation example. The clean raw-coordinate path is based on
Espressif's Apache-2.0 GSL3680 driver and is mapped through the exact inverse of
HomeTiles' display rotation.

## HomeTiles integration

- The target and OTA descriptor key are `guition_jc8012p4a1`.
- The 16MB partition table has two 6656KB OTA application slots.
- Display, brightness control, touch, ESP-Hosted Wi-Fi, LittleFS, 4-bit SDMMC
  and local OTA are wired in.
- The ESP-Hosted Wi-Fi link uses the Issue #30 field-validated V1 compatibility
  path: 1-bit SDIO at 40MHz with C6-to-P4 RX reads split into individual
  512-byte CMD53 transfers. This does not apply to JC8012P4A1 V2 or any other
  device profile.
- The live microSD filesystem is used by HomeTiles' file manager, wallpaper
  screensaver and screenshot paths. The existing one-time migration copies only
  legacy `/_tile_grids`, `/_tile_links` and `/icons` data to LittleFS.
- SD mount first uses 40MHz high-speed mode and retries at 20MHz default speed.
- Audio, RTC and battery reporting are not enabled yet.

## Built-in camera (camera beta builds only)

The V1 board carries the same OV02C10 camera as the V2 on connector FPC5;
Guition's V1 `video_lcd_display` demo is the archive the V2 sensor data comes
from. Only camera beta builds (`HOMETILES_CAMERA_BETA`) compile the capture
path, and the user enables it in Web Admin Settings. The board file is
`local_camera_board.cpp`; sensor data and open assumptions are in
`src/video/local_camera/sensors/ov02c10/PROVENANCE.md`.

- SCCB 0x36 shares the GSL3680 touch bus (SDA 7, SCL 8); no reset, power-down
  or XCLK pin, so "off" means software standby
- V2 capture mode and mounting defaults; orientation, the fitted module and a
  combined stream plus Web OTA soak on the Issue #30 SDIO path are pending
  hardware checks

The device target has been validated on real hardware through
[HomeTiles issue #5](https://github.com/GalusPeres/HomeTiles/issues/5):
display output, touch direction, MQTT/Bridge communication and automatic
discovery are confirmed working. CI publishes both factory and OTA binaries.

## Sources and third-party provenance

- [Guition JC8012P4A1 specification](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961095913/JC8012P4A1C_I_W%20Specifications-EN-v1.1%282%29.pdf)
- [Guition board pin definitions](https://github.com/guitionofficial/P4-series/blob/9f1510954d281e30b19cd8baf115e2ed2a65a90a/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y/1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h)
- [Guition/Espressif BSP: brightness, SDMMC and MIPI setup](https://github.com/guitionofficial/P4-series/blob/9f1510954d281e30b19cd8baf115e2ed2a65a90a/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y/1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/esp32_p4_function_ev_board.c)
- [Guition MIPI D-PHY LDO values](https://github.com/guitionofficial/P4-series/blob/9f1510954d281e30b19cd8baf115e2ed2a65a90a/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y/1-Demo/idf-examples/esp_draw_bit/main/main.c)
- [Guition JD9365 MIPI and DPI timing](https://github.com/guitionofficial/P4-series/blob/9f1510954d281e30b19cd8baf115e2ed2a65a90a/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y/1-Demo/arduino-examples/esp32p4_lvgl_v8/src/lcd/esp_lcd_jd9365.h)
- [Espressif-copyrighted Apache-2.0 JD9365 command table in Guition's repository](https://github.com/guitionofficial/P4-series/blob/9f1510954d281e30b19cd8baf115e2ed2a65a90a/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y/1-Demo/arduino-examples/esp32p4_lvgl_v8/src/lcd/esp_lcd_jd9365.c)
- [Guition Arduino touch setup](https://github.com/guitionofficial/P4-series/blob/9f1510954d281e30b19cd8baf115e2ed2a65a90a/JC8012P4A1C_I_W_Y/JC8012P4A1C_I_W_Y/1-Demo/arduino-examples/Arduino_lvgl_sw_rotation/esp32p4_lvgl_v9/lvgl_sw_rotation.c)
- [Espressif Apache-2.0 GSL3680 driver](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/92b790ed6d24b0678e2f45b1fc85f0abd2d41b33/src/drivers/touch/port/esp_lcd_touch_gsl3680.c)
- [Espressif Apache-2.0 GSL3680 firmware](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/92b790ed6d24b0678e2f45b1fc85f0abd2d41b33/src/drivers/touch/port/esp_lcd_touch_gsl3680_fw.h)
- [ESP32-P4 SDMMC documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/sdmmc_host.html)
- [HomeTiles issue #5 hardware report](https://github.com/GalusPeres/HomeTiles/issues/5#issuecomment-5049026326) and [reported-working fork commit](https://github.com/brmo/HomeTiles/commit/1d98e71dc070fb01449bbb0e88ea0ef7f8bc72c9)

The GSL3680 firmware and register protocol retain Espressif's Apache-2.0
notices. The JD9365 command table comes from an Espressif-copyrighted,
Apache-2.0 source in Guition's official repository. Apache-2.0 is included as
`vendor/LICENSE-APACHE-2.0.txt`. The GPL `gsl_point_id` implementation from the
test fork is deliberately not included or linked.
