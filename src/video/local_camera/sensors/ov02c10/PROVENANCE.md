# OV02C10 sensor data for the built-in camera

Compiled only for boards that select `HOMETILES_CAMERA_SENSOR_OV02C10` in
`src/video/local_camera/camera_select.h` (first: Guition JC8012P4A1 V2).

## Source

- Guition JC8012P4A1 `video_lcd_display` demo archive, SHA256
  `a742d1c7b0a4252b6161d9292481e0b7c2a4f6c853d9788e8c5f68ff7fc42d16`.
- Bundled Espressif component `esp_cam_sensor` 0.9.0 (Apache-2.0), file
  `sensors/ov02c10/private_include/ov02c10_settings.h`, SHA256
  `b324062ed7a86ad603b7e74abebf5360456f5e9ea954e6f5c2989b903d4af6fe`.
- License: Apache-2.0, see `LICENSE-APACHE-2.0.txt`. The original SPDX
  notice is retained in `ov02c10_settings.h`.

## What was taken

`ov02c10_settings.h` keeps two arrays verbatim: `ov02c10_mipi_reset_regs`
(SHA256 of the array text
`c2b61a8c472e26ab8056bf59254fe5407a74ab0127499b1787e296686c51544a`) and
`ov02c10_input_24M_MIPI_1lane_raw10_1288x728_30fps` (225 entries, SHA256
`cfacecf0d3803f0c8f646c9a98ead9a96860dfc5427fdcad6a3d1413062704da`).
The regression test `tools/tests/devices/test-guition-v2-local-camera.mjs`
recomputes both hashes.

`ov02c10_sensor.cpp` re-implements only the register
access the demo driver performs: chip ID `0x300A/0x300B == 0x5602`, reset
and format tables, the stream on/off sequence (`0x4800`, `0x3002`, `0x3010`,
`0x300D`, `0x0100`) and horizontal mirror (`0x3821` bit 1). The demo's
OV5640-style AE-target and banding registers (`0x3A0x`, `0x3C0x`, PLL reads at
`0x3034..0x3037`) are deliberately not written.

## Board facts from the demo configuration

- SCCB on I2C port 0, SCL GPIO 8, SDA GPIO 7, 100 kHz, 7-bit address 0x36.
  This is the bus the GSL3680 touch driver already owns.
- Reset and power-down pins are `-1`; no XCLK pin is configured. The format
  entry declares a 24 MHz sensor clock, so the module is assumed to carry its
  own oscillator.
- The format entry declares 1 MIPI data lane at 400 Mbit/s, RAW10, Bayer GBRG,
  VTS 1164, HTS 2280. The demo enables horizontal mirror.

## Assumptions that still need hardware confirmation

- The demo's default build used the 1920x1080 table; the 1288x728 table was
  not exercised by that build. Lane count, bit rate and line-sync packets must
  be confirmed on real V2 hardware.
- Exposure (`0x3501/0x3502`, lines) and analog gain (`0x3508/0x3509`, gain in
  1/16 steps shifted left by four) are not used by the demo driver. The values
  written by HomeTiles are clamped to the table default exposure and to
  1x..15.5x gain until they are confirmed on hardware.
- Mirroring may change the effective Bayer phase. HomeTiles keeps GBRG as
  declared by the demo; swapped colours on hardware point here first.
