# OV5647 sensor data for the built-in camera

Compiled only for boards that select `HOMETILES_CAMERA_SENSOR_OV5647` in
`src/video/local_camera/camera_select.h` (first: Waveshare
ESP32-P4-WIFI6-Touch-LCD-8, camera beta builds only).

## Source

- Espressif `esp-video-components`, component `esp_cam_sensor` 2.6.0, commit
  `dbbdcbd5e25bdb6561d3f7fd0c5149641ba5536f`, files
  `sensors/ov5647/private_include/ov5647_settings.h` and
  `sensors/ov5647/private_include/ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps.h`.
- License: Apache-2.0, see `LICENSE-APACHE-2.0.txt`. The original SPDX notice
  is retained in `ov5647_settings.h`.

## What was taken

`ov5647_settings.h` keeps two arrays verbatim: `ov5647_mipi_reset_regs`
(SHA256 of the array text
`5fd765a2af1053014f1eac8701617d08a90e29612ef0c38e906b18c2d00fd7e4`) and
`ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps` (84 entries, SHA256
`8e9e15528a1d3ea35ea6d8226b54daec7fbe45457e54f2fc5570ce9dac5dcac2`), plus the
constants they use. The regression test
`tools/tests/devices/test-waveshare-8-local-camera.mjs` recomputes both hashes.

`ov5647_sensor.cpp` re-implements only the register access of the
esp_cam_sensor driver: chip ID `0x300A/0x300B == 0x5647`, reset and format
tables, stream on/off (`0x4800 = 0x14`, `0x0100`), mirror `0x3821` bit 1 and
flip `0x3820` bit 1. The driver's on-sensor AEC target, banding filter and AF
pad writes are deliberately not used: the HomeTiles ISP pipeline owns
exposure and colour, like on the OV02C10.

## HomeTiles overrides (not vendor data)

- Portrait output 544x960 for the quarter-turn mounting: all 960 binned rows
  of the table window, 544 centred columns (`0x3808/0x3809 = 544`, ISP x offset
  `0x3810/0x3811 = 372`). The JPEG leaves the panel portrait; the Bridge turns it
  into 960x544.
- VTS 1640 (`0x380E/0x380F`) for 30 fps instead of 45 (less CSI/PSRAM traffic).
- Manual exposure and analog gain after the Linux ov5647 driver: `0x3503 =
  0x03`, exposure `0x3500..0x3502` in 1/16 lines, gain `0x350A/0x350B` in 1/16
  steps; longer exposures stretch VTS down to 7.5 fps.
- Manual white balance (`0x3406 = 1`, R/G/B gains `0x0400` = 1x); the ISP
  balances the colours.
- A mirror or flip moves the ISP window offset (`0x3811` 372/373, `0x3813` 2/3) by
  one pixel so the Bayer order stays GBRG, as done for the OV02C10.

## Board facts (Waveshare ESP32-P4-WIFI6-Touch-LCD-X)

From `waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X`, example
`09_video_lcd_display` (`sdkconfig.defaults`, `main/app_video.c`):

- OV5647 on the board I2C bus (I2C0, SCL GPIO 8, SDA GPIO 7), the bus the
  GT911 touch driver already owns.
- `reset_pin = -1`, `pwdn_pin = -1`, no XCLK: the module carries its own
  24 MHz clock.
- 2 MIPI data lanes; the example uses the esp_cam_sensor OV5647 driver.

## Assumptions that still need hardware confirmation

- The 30 fps VTS and the manual AEC/AGC/AWB worked on the maintainer's 8-inch
  board (2026-09-24, 1280x720 window); the 544x960 window with the large ISP x
  offset is not exercised by the Waveshare example.
- Mounting: the 8-inch board showed the scene's top at the left image edge in
  landscape (a quarter turn, turned clockwise by the Bridge); the horizontal
  mirror state still needs a check.
- Bayer phase under mirror/flip, black level (4 in 8-bit units, the OV5647
  default BLC target 0x10 of 1023) and the useful analog gain range.
