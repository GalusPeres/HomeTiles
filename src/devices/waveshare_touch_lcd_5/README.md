# Waveshare Touch LCD 5 profile

This profile is exclusively for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5
(HW V1.0, the only revision Waveshare's BSP names): an ESP32-P4NRW32 with
32 MB PSRAM and 32 MB GD25Q256 flash, an ESP32-C6-MINI-1 for Wi-Fi 6 and
Bluetooth over ESP-Hosted, and a 5-inch 720 x 1280 portrait IPS panel with an
HX8394 controller on 2-lane MIPI-DSI and GT911 touch
([issue #76](https://github.com/GalusPeres/HomeTiles/issues/76)).

The maintainer has no such board: every fact below is taken from Waveshare's
sources, nothing is hardware-tested yet.

## Build profiles

Waveshare ships units with both ESP32-P4 silicon generations (rev1.3 and
rev3.x sdkconfig profiles in its repository). Check the revision with
`esptool chip-id` and pick the matching image:

- `waveshare_5` (release key `waveshare_touch_lcd_5`): ESP32-P4 before v3.0
  (revisions 1-199), Chip Variant `Before v3.00`.
- `waveshare_5_rev3` (release key `waveshare_touch_lcd_5_rev3`): ESP32-P4
  v3.1 or newer (301-399), Chip Variant `v3.00 or newer`. Revision v3.0 is
  unsupported by the pinned toolchain.

Both use the firmware key `waveshare_touch_lcd_5` and the define
`DEVICE_WAVESHARE_TOUCH_LCD_5`. The v3 build selects the MIPI-DSI PHY clock
v3 silicon accepts (the shared driver, like the 10.1-inch and 7B).

## Driver

The board has no driver of its own. `board.h` describes it to the shared
description-driven Waveshare ESP32-P4 MIPI-DSI driver
(`src/devices/waveshare_touch_lcd_5/dsi_driver`), which also owns the GT911, I2C and
SDMMC code; this folder only adds the HX8394 controller (`vendor/hx8394`,
Waveshare's `esp_lcd_hx8394` 2.1.0, see `vendor/README.md`), the profile and
the audio pins.

Presentation is the M5Stack Tab5's and the Waveshare 7-inch's (the same
720 x 1280 portrait panel): a 1280 x 720 landscape dashboard with the 7 x 4
grid, rotation 0 or 180 degrees, and the upright layout drawn straight into
the panel.

## Hardware facts and sources

Sources: the BSP `waveshare/esp32_p4_wifi6_touch_lcd_5` 1.0.4 and
`waveshare/esp_lcd_hx8394` 2.1.0 from `waveshareteam/Waveshare-ESP32-components`
(commit `23a6012d5e87d27399cf90fb851cab3689e29159`) and the board repository
`waveshareteam/ESP32-P4-WIFI6-Touch-LCD-5` (commit
`d144b34d8f384e84d2442e8733523560216a2f63`): `docs/IO.md` and
`docs/HARDWARE.md` (Waveshare's cross-check against the board schematic),
`examples/arduino/libraries/displays/displays_config.h`, the Arduino audio
examples and `examples/esp-idf/06_I2SCodec`.

Confirmed by these sources:

- Panel: HX8394, 720 x 1280, 2 DSI lanes at 700 Mbit/s, 58 MHz DPI clock,
  hsync pulse/back/front 20/20/40, vsync 4/10/24
  (`HX8394_720_1280_PANEL_30HZ_DPI_CONFIG`). Init: sleep-out with 120 ms,
  MADCTL 0x00, COLMOD 0x55, 0xBA 0x61 (2 lanes), then the driver's default
  vendor table. MIPI D-PHY power from LDO channel 3 at 2500 mV.
- LCD reset GPIO27, active high (BSP `flags.reset_active_high = 1`; the
  Arduino DSI display drives it high to reset). A restart holds it high.
- Backlight GPIO26, LEDC 5 kHz 10-bit PWM, brighter with more duty. The
  backlight path is AC-coupled: a static level does not light it.
- Board I2C SDA GPIO7, SCL GPIO8 (GT911, ES8311, ES7210).
- GT911 at 0x5D or 0x14 (both probed), reset and interrupt not driven by the
  BSP or the examples, touch coordinates in the panel's own orientation.
- microSD: 4-bit SDMMC slot 0, D0-D3 GPIO39-42, CMD GPIO44, CLK GPIO43, card
  power from on-chip LDO channel 4. The BSP mounts at 40 MHz; HomeTiles uses
  20 MHz like the hardware-tested Waveshare 8-inch with the same wiring.
- Camera: optional OV5647 module on the 2-lane MIPI-CSI connector, SCCB on
  the board I2C bus (GPIO7/8), no reset, power-down or clock line (Waveshare's
  `09_video_lcd_display` and `06_Camera_Preview`). HomeTiles uses the shared
  OV5647 driver with the 8-inch sensor mode, off until enabled in the Web Admin.
- Audio: ES8311 at 0x18 (playback), ES7210 at 0x40 (microphones), I2S MCLK
  GPIO13, BCLK GPIO12, LRCK GPIO10, DOUT GPIO9, DIN GPIO11, speaker amplifier
  enable GPIO53 active high. Voice uses the shared audio core at the full
  volume scale like the other Waveshare P4 boards (same circuit).
- These are the same I2C, touch, backlight, SDMMC and audio pins as on
  Waveshare's LCD-X boards (7, 8 and 10.1 inch, BSP
  `esp32_p4_wifi6_touch_lcd_x`); only the panel controller, its timing and the
  reset polarity (active low there) differ.

Not confirmed (hardware check pending):

- ESP32-C6 transport: no Waveshare source names the P4-to-C6 SDIO pins; its
  `docs/P4_C6_HOSTED_WIFI.md` and `docs/HARDWARE.md` call the mapping
  unconfirmed against the schematic. Waveshare's Wi-Fi examples
  (`05_GFX_ESPWiFiAnalyzer` on the stock `ESP32P4 Dev Module`, ESP-IDF
  `04_wifistation` with `esp_hosted` 1.4.x defaults) use the default pins,
  which are the ones every HomeTiles Waveshare P4 board uses: SDIO slot 1,
  CLK GPIO18, CMD GPIO19, D0-D3 GPIO14-17, C6 reset GPIO54.
- Touch reset and interrupt: the schematic routes TP_RST through R37 (0 ohm)
  to GPIO23 and TP_INT through the optional R108 (NC/0R) to GPIO2
  (`docs/IO.md`). HomeTiles does not drive them, like Waveshare's BSP; both
  stay out of the configurable I/O.
- Landscape orientation: rotation 0 turns the UI like the Tab5 and the
  Waveshare 7-inch; whether 0 or 180 is upright on this enclosure is
  unchecked. The touch mapping follows the display either way.
- Backlight floor: the lowest visible duty is unmeasured (1 % = duty 10 of
  1023).
- USB-host Ethernet: offered like on the LCD-X boards (the BSP has USB host
  support); untested.
- Camera orientation and mirror on this board (the 8-inch's quarter-turn
  mounting is the default; Rotation and Mirror are in the Web Admin).
