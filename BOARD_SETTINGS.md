# Board Settings

This file documents the working Arduino IDE board settings for the device profiles used in this project.

Note:
- The remaining values should be set exactly as listed below.
- This repo now contains a shared sketch-root `partitions.csv`.
- Arduino ESP32 uses that file automatically for both boards during build.
- The shared layout keeps both OTA app slots below `16MB`.
- The normal ESP32-P4 profiles use the vendors' P4NRW32/pre-v3 modules and
  HomeTiles explicitly guards them to chip revisions 1–199. They are not
  generic all-revision P4 builds. The only separate post-v3 profile is the
  experimental, exact-v3.1 Waveshare 7B entry documented below.

## M5Stacks Tab5

Used for:
- `src/devices/m5stacks_tab5`

Important:
- Leave `Partition Scheme` on the normal Tab5 default.
- The actual partition layout still comes from the shared repo `partitions.csv`.

Arduino IDE:
- Board: `M5Tab5`
- USB CDC On Boot: `Enabled`
- CPU Frequency: `360MHz`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Default (2 x 6.5 MB app, 3.6 MB SPIFFS)`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `Hardware CDC and JTAG`

## Waveshare B4 / ESP32-P4-86-Panel-ETH-2RO

Used for:
- `src/devices/waveshare_4b`

Important:
- The ESP32-P4-86-Panel-ETH-2RO uses this same firmware profile. Its native
  Ethernet connection is supported; its two onboard relays are not exposed by
  HomeTiles yet.
- Leave `Partition Scheme` on the normal 32MB B4 setting.
- The actual partition layout still comes from the shared repo `partitions.csv`.
- This avoids switching partition files between boards.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- CPU Frequency: `360MHz`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `32MB (256Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `32M Flash (13MB APP/6.75MB SPIFFS)`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Waveshare Touch LCD 4.3

Used for:
- `src/devices/waveshare_touch_lcd_4_3`
- build profile `waveshare_4_3`

Important:
- This profile is only for the exact `ESP32-P4-WIFI6-Touch-LCD-4.3` product.
- The native panel is a `480x800` ST7701 MIPI-DSI display. HomeTiles renders
  it as an `800x480` landscape dashboard.
- The tested board reports ESP32-P4 revision v1.3 and uses the pre-v3
  revisions 1–199 firmware contract.
- Touch uses GT911 on SDA GPIO7 and SCL GPIO8 without interrupt or reset pins.
- The active-low backlight is GPIO26 and the active-low panel reset is GPIO27.
- The microSD interface uses SDMMC slot 0 on GPIO39-44 and P4 LDO channel 4.
- Display, touch, Wi-Fi, Web Admin, tile persistence, MQTT, Home Assistant
  discovery and a live Weather tile have been confirmed on physical hardware.
  Complete microSD and OTA validation remains pending.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- Chip Variant: `Before v3.00`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `32MB (256Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `32M Flash (13MB APP/6.75MB SPIFFS)`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Waveshare Touch LCD 7 / 8 / 10.1

Used for:
- `src/devices/waveshare_touch_lcd_7`
- `src/devices/waveshare_touch_lcd_8`
- `src/devices/waveshare_touch_lcd_10_1`
- build profiles `waveshare_7`, `waveshare_8`, `waveshare_10_1`, and
  `waveshare_10_1_rev3`

Important:
- The 8-inch profile is hardware-confirmed. The 7-inch and 10.1-inch profiles
  still need complete real-device validation in
  [issue #7](https://github.com/GalusPeres/HomeTiles/issues/7).
- The 7-inch model renders at `1280x720` and uses the Tab5 dashboard layout.
- The 8-inch and 10.1-inch models render at `1280x800` and share the same
  dashboard layout.
- Select the build profile matching the physical panel. The 7-inch panel uses
  ILI9881C; the 8-inch and 10.1-inch panels use JD9365 with model-specific
  initialization and timing.
- Leave `Partition Scheme` on the normal 32MB ESP32-P4 setting.
- The actual partition layout still comes from the shared repo `partitions.csv`.
- The `Chip Variant` must be set to `Before v3.00` for this hardware, except
  for 10.1-inch boards with ESP32-P4 v3.1 or newer: use `waveshare_10_1_rev3`
  with `v3.00 or newer`. Check the revision with `esptool chip-id` first. The
  v3 build is experimental and contributor-tested on v3.2 only.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- Chip Variant: `Before v3.00`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `32MB (256Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `32M Flash (13MB APP/6.75MB SPIFFS)`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Waveshare Touch LCD 7B / 7B-C

Used for:
- `src/devices/waveshare_touch_lcd_7b`
- build profile `waveshare_7b` for P4NRW32/pre-v3 revisions 1–199
- build profile `waveshare_7b_rev3_1` for exact ESP32-P4 revision v3.1 (301)

Important:
- This is the `1024x600` EK79007 7B/7B-C profile. It is not interchangeable
  with the older `1280x720` Waveshare Touch LCD 7-inch profile.
- The 7B-C uses the same display, touch, backlight, and SDMMC contract. Its
  optional camera is not exposed by HomeTiles.
- ESP32-P4 with `32MB` flash and `32MB` PSRAM.
- Touch uses SDA 7 and SCL 8. The panel reset is GPIO 33.
- The microSD interface uses SDMMC slot 0 on GPIO39-44 and P4 LDO channel 4.
- The browser installer shows two explicit device entries: **ESP32-P4 before
  v3.0 (revisions 1–199)** and **exact ESP32-P4 v3.1**. The selected entry
  determines the release asset. Revision detection rejects a mismatch before
  flashing; it does not change the selection automatically.
- The exact-v3.1 entry is experimental and has not been validated on that
  hardware. ESP32-P4 v3.2 or newer is unsupported with Arduino-ESP32 3.3.7 /
  ESP-IDF 5.5.2 and must not be flashed.
- HomeTiles browser, Web Admin, and OTA paths enforce exact revision metadata.
  A direct Arduino IDE or `esptool` write bypasses that HomeTiles check. The
  underlying ESP header from the Arduino `v3.00 or newer` choice can cover
  revisions 301–399, so directly writing the v3.1 image to v3.2 or newer is
  unsafe.
- For an Arduino IDE build, use `esptool chip-id` first. Continue only for
  revisions 1–199 or exact v3.1; stop for v3.2 or newer and do not guess.
- Complete validation on both paths, especially exact v3.1 hardware, is still
  pending in [issue #7](https://github.com/GalusPeres/HomeTiles/issues/7).
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- Chip Variant: `Before v3.00` for `waveshare_7b`, or `v3.00 or newer` for
  `waveshare_7b_rev3_1`. HomeTiles v0.6.8 narrows the latter firmware contract
  to exact v3.1 even though the Arduino/ESP image header remains 301–399.
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `32MB (256Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `32M Flash (13MB APP/6.75MB SPIFFS)`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Guition JC8012P4A1

Used for:
- `src/devices/guition_jc8012p4a1`

Important:
- The module has `16MB` flash and `32MB` PSRAM.
- The native panel is `800x1280`; HomeTiles renders `1280x800` and rotates it.
- Backlight is active-high on GPIO 23 using 20kHz, 10-bit LEDC PWM.
- Touch uses SDA 7, SCL 8 and reset 22. The target tries 400kHz first and the
  official Guition 100kHz setting as a compatibility fallback.
- The microSD slot uses native 4-bit SDMMC slot 0 (`CLK 43`, `CMD 44`,
  `D0-D3 39-42`) and P4 LDO channel 4.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.
- Do not use `16M Flash (3MB APP/9.9MB FATFS)`: the application is larger
  than its 3MB app slot.
- The `Chip Variant` must be set to `Before v3.00` for the tested hardware.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- Chip Variant: `Before v3.00`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Guition JC8012P4A1 V2

Used for:
- `src/devices/guition_jc8012p4a1_v2`
- build profile `guition_jc8012p4a1_v2`

Important:
- Use this profile only when the rear material-number sticker contains `V2`,
  for example `SKU:10153002-V2`.
- V2 uses its own JD9365 init table, 80MHz DPI clock and vertical timing
  `4/10/30`. The original target remains at 60MHz and `4/8/20`.
- V1 and V2 factory/OTA images are deliberately incompatible and have separate
  embedded device keys.
- Board, flash, PSRAM, partition, upload, touch and SDMMC settings are the
  same as the Guition JC8012P4A1 V1 section above.
- USB: the V2 USB-C port is the ESP32-P4 USB-Serial-JTAG, so HomeTiles logs
  (`Serial`) must go there: USB CDC On Boot `Enabled`, USB Mode
  `Hardware CDC and JTAG`, like the Tab5 and JC4880P443. With the V1 settings
  the browser log viewer shows only ESP-IDF system errors.
- Physical release and OTA validation is tracked in
  [issue #18](https://github.com/GalusPeres/HomeTiles/issues/18).

## Guition JC1060P470C_I_W_Y

Used for:
- `src/devices/guition_jc1060p470c`
- build profile `guition_jc1060p470c`

Important:
- This target is only for the exact `JC1060P470C_I_W_Y` variant. The `_I_W`
  variant uses different touch coordinates and is not covered.
- The profile is published, but complete validation on the exact hardware is
  still pending; report results in [issue #8](https://github.com/GalusPeres/HomeTiles/issues/8).
- ESP32-P4 with `16MB` flash and `32MB` PSRAM.
- Native `1024x600` JD9165 MIPI-DSI panel with GT911 touch.
- SDMMC slot 0 uses GPIO39-44 and LDO VO4. GPIO45 controls the active-low
  card-power switch and is reserved from configurable hardware I/O.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- Chip Variant: `Before v3.00`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Guition JC1060P470C V2 / New Panel

Used for:
- `src/devices/guition_jc1060p470c_v2`
- build profile `guition_jc1060p470c_v2`

Important:
- Use this profile only for the marked `JC1060P470C` V2 / New Panel revision.
  The original `_I_W_Y` panel requires the separate
  `guition_jc1060p470c` profile.
- Display, touch, brightness, storage, networking, and OTA behavior still need
  confirmation on the exact V2 hardware in
  [issue #27](https://github.com/GalusPeres/HomeTiles/issues/27).
- ESP32-P4 with `16MB` flash and `32MB` PSRAM.
- Native `1024x600` JD9165 MIPI-DSI panel with two lanes at 750Mbps and GT911
  touch on SDA 7 / SCL 8.
- LCD reset is GPIO 5; backlight is active-high on GPIO 23 using 20kHz,
  10-bit PWM.
- SDMMC slot 0 uses GPIO39-44 and LDO VO4. GPIO45 controls the active-low card
  power switch and is reserved from configurable hardware I/O.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Disabled`
- Chip Variant: `Before v3.00`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `USB-OTG (TinyUSB)`

## Guition JC4880P443

Used for:
- `src/devices/guition_jc4880p443_portrait`
- build profile `guition_jc4880p443_portrait`

Important:
- This target is only for the exact `JC4880P443C_I_W` variant
  (JC-ESP32P4-M3 module: ESP32-P4 host + ESP32-C6 Wi-Fi co-processor). It is
  not interchangeable with the Waveshare Touch LCD 4.3 profile even though
  both use a 480x800 ST7701 panel.
- This profile is hardware-tested on the exact `JC4880P443C_I_W` board
  (display, touch, rotation, camera, sleep/wake, Wi-Fi, MQTT, tiles,
  microSD and Web OTA).
- ESP32-P4 with `16MB` flash and `32MB` PSRAM.
- Native `480x800` ST7701S MIPI-DSI panel (2 lanes at 500 Mbps, 34 MHz DPI
  clock, vsync timing 2/8/166), rendered as a `480x800` portrait dashboard
  (`4x6` grid).
- LCD reset is GPIO 5 (active-low); backlight is active-high on GPIO 23
  using LEDC PWM. MIPI-DSI PHY power uses P4 LDO channel 3 at 2500 mV.
- Touch is GT911 on SDA 7 / SCL 8 without interrupt or reset pins. Both
  GT911 addresses are probed at 400 kHz first with a 100 kHz fallback.
- SDMMC slot 0 uses GPIO39-44 with TF_VCC from on-chip LDO channel 4.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32P4 Dev Module`
- USB CDC On Boot: `Enabled`
- Chip Variant: `Before v3.00`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Frequency: `80MHz`
- Flash Mode: `QIO`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `Enabled`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `Hardware CDC and JTAG`

The sketch console (`Serial`) runs on the USB-Serial/JTAG flashing port. The
second USB-C connector does not enumerate a data device, so all diagnostics
must be read from the flashing port.

## GUITION ESP32-4848S040

Used for:
- `src/devices/guition_esp32_4848s040`
- build profile `guition_esp32_4848s040`

Important:
- This profile is hardware-tested; repeatable failures can be reported in
  [issue #9](https://github.com/GalusPeres/HomeTiles/issues/9).
- Camera tiles are intentionally unavailable on this ESP32-S3 target.
- This is the ESP32-S3 `ESP32-4848S040C_I` family with `16MB` flash and
  `8MB` octal PSRAM.
- The panel is a `480x480` ST7701 RGB display. The initial test profile uses a
  conservative 10MHz pixel clock and a ten-line bounce buffer.
- Capacitive touch is GT911 on SDA 19 / SCL 45.
- Backlight PWM is active-high on GPIO 38.
- The microSD card uses SPI mode (`SCK 48`, `MOSI 47`, `MISO 41`, `CS 42`).
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled`
- CPU Frequency: `240MHz (WiFi)`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Mode: `QIO 80MHz`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `OPI PSRAM`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `Hardware CDC and JTAG`

## Waveshare ESP32-S3-Touch-LCD-4 Rev 4.0

Used for:
- `src/devices/waveshare_s3_touch_lcd_4`
- build profile `waveshare_s3_touch_lcd_4`

Important:
- This profile is only for the Waveshare ESP32-S3-Touch-LCD-4 Rev 4.0 with
  the CH32V003 helper MCU. Revisions 1.0–3.0 use different hardware and are
  not supported by this profile. The S3 LCD-4B uses a separate image.
- Display, backlight dimming, touch, Wi-Fi, Web Admin, MQTT, HomeTiles Bridge,
  and Web OTA were reported working by the contributor in
  [PR #35](https://github.com/GalusPeres/HomeTiles/pull/35).
- Factory and OTA release images are scheduled for the next release; this
  profile is not included in `v0.6.9`.
- ESP32-S3 with `16MB` flash and `8MB` octal PSRAM.
- The panel is a `480x480` ST7701 RGB display using direct 3-wire SPI on GPIO 42
  (CS), GPIO 2 (SCK), GPIO 1 (SDA), and 16-bit RGB bus on DE 40, VSYNC 39,
  HSYNC 38, PCLK 41, R(46,3,8,18,17), G(14,13,12,11,10,9), B(5,45,48,47,21).
- Capacitive touch is GT911 on SDA 15 / SCL 7.
- Display power/reset and backlight PWM use the onboard CH32V003 helper MCU
  at I2C address `0x24` on the same SDA 15 / SCL 7 bus.
- The board has a microSD slot, but this HomeTiles profile does not implement
  SD access. Runtime files use LittleFS. Camera tiles are also unavailable.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.
- Exact hardware references and support scope are documented in the
  [device README](src/devices/waveshare_s3_touch_lcd_4/README.md).

Arduino IDE:
- Board: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled`
- CPU Frequency: `240MHz (WiFi)`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Mode: `QIO 80MHz`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `OPI PSRAM`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `Hardware CDC and JTAG`

## Waveshare ESP32-S3-Touch-LCD-4B

Used for:
- `src/devices/waveshare_s3_touch_lcd_4b`
- build profile `waveshare_s3_touch_lcd_4b`

Important:
- This profile is only for the exact Waveshare ESP32-S3-Touch-LCD-4B. It is
  not the ESP32-P4 Waveshare Touch LCD 4B / 86 Panel profile.
- Complete physical-device validation is pending in
  [issue #26](https://github.com/GalusPeres/HomeTiles/issues/26).
- ESP32-S3 with `16MB` flash and `8MB` octal PSRAM.
- The panel is a `480x480` ST7701 RGB display using the official 16MHz pixel
  clock and Waveshare initialization table.
- Capacitive touch is GT911 on SDA 47 / SCL 48.
- Backlight PWM is active-low on GPIO 4 using 5kHz, 10-bit PWM.
- This exact board has no microSD interface. Runtime files use LittleFS, and
  Camera tiles are unavailable on this ESP32-S3 profile.
- Use the repository's `partitions.csv`; HomeTiles needs two 6.5MB OTA slots.

Arduino IDE:
- Board: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled`
- CPU Frequency: `240MHz (WiFi)`
- Core Debug Level: `None`
- USB DFU On Boot: `Disabled`
- Erase All Flash Before Sketch Upload: `Disabled`
- Flash Mode: `QIO 80MHz`
- Flash Size: `16MB (128Mb)`
- JTAG Adapter: `Disabled`
- USB Firmware MSC On Boot: `Disabled`
- Partition Scheme: `Custom`
- PSRAM: `OPI PSRAM`
- Upload Mode: `UART0 / Hardware CDC`
- Upload Speed: `921600`
- USB Mode: `Hardware CDC and JTAG`
