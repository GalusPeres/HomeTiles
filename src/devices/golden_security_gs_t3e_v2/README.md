# Golden Security GS-T3E v2

Community HomeTiles target for the Golden Security / Tuya GS-T3E 86-box wall
panel, PCB revision v2.3 (issue #93). The maintainer has no such board:
everything below comes from public configurations and is **not yet validated
on hardware**.

- ESP32-S3-WROOM-1 N16R8: 16 MB quad flash, 8 MB octal PSRAM
- 4-inch 480 x 480 ST7701S RGB panel, GT911 capacitive touch
- Mains relays (2-4 depending on the variant) and an unused Tuya Zigbee module

## Firmware structure

The board is built like the Guition ESP32-4848S040. It has its own driver,
`device_golden_security_gs_t3e_v2.cpp`, derived from the Guition driver,
which stays unchanged. Its pins and panel values are one constexpr board
description, `kBoard` in `device_golden_security_gs_t3e_v2.h`
(`st7701_s3_board.h`). It has its own define `DEVICE_GOLDEN_SECURITY_GS_T3E_V2`,
profile key, firmware metadata key, release key and setup AP name
(`GS_T3E_V2_Config`), so its image and the Guition image can never be
installed on each other. UI, grid, popups and lifecycle are the Guition S3's.

## Hardware facts and sources

Sources:

- [openHASP `user_setups/esp32s3/gs-t3e.ini`](https://github.com/HASwitchPlate/openHASP/blob/master/user_setups/esp32s3/gs-t3e.ini),
  section `[odm-t3e-v2]` (last change 2023-04-10, "Update GS-T3E to PCB v2.3"),
  and its ST7701 branch in `src/drv/tft/tft_driver_arduinogfx.cpp`
- [ESPHome device page "TuYa T3E"](https://devices.esphome.io/devices/tuya-t3e-esp32-s3)
  (written for PCB v2.4; it states that screen and touch pins match other
  revisions)
- [openHASP discussion #458](https://github.com/HASwitchPlate/openHASP/discussions/458)
  and the [openHASP GS-T3E page](https://openhasp.haswitchplate.com/0.7.0/hardware/golden-security/gs-t3e/)

| Item | Value | Agreement |
| --- | --- | --- |
| RGB control | DE 40, VSYNC 39, HSYNC 38, PCLK 41 | openHASP and ESPHome |
| RGB data | R0..R4 8, 18, 17, 16, 0; G0..G5 13, 12, 11, 10, 9, 3; B0..B4 15, 48, 47, 21, 14 | openHASP and ESPHome |
| ST7701 SPI init bus | CS 4, SCK 15, MOSI 48 (SCK/MOSI are also B0/B1) | openHASP and ESPHome |
| Panel reset | none: openHASP and ESPHome name GPIO4 (the CS) as reset; HomeTiles sends the software reset 0x01 | inferred |
| Init table | Arduino_GFX `st7701_type1_init_operations` (CD=0x08, INVON) | openHASP generic ST7701 branch; ESPHome's default sequence 1 writes the same register values and `invert_colors: true` adds the same INVON |
| Timing | HSYNC fp 10 / pw 8 / bp 50, VSYNC fp 10 / pw 8 / bp 20, 12 MHz | openHASP and ESPHome |
| PCLK edge | `pclk_active_neg = 0` (rising edge, as on the Guition) | see below |
| Backlight | GPIO42, LEDC PWM, active high | openHASP and ESPHome; best rate unknown |
| Touch | GT911, SDA 7, SCL 6, no RST/INT, address 0x5D or 0x14 (both probed) | openHASP and ESPHome |
| microSD | none documented | no source lists SD pins |
| UART0 / log | TX 43, RX 44 (header) | ESPHome |

### Open points and disagreements

- **PCLK edge.** openHASP's `gs-t3e.ini` sets `TFT_PCLK_ACTIVE_NEG=1`, but its
  ST7701 branch never passes that value to `Arduino_ESP32RGBPanel`, so openHASP
  runs Arduino_GFX's default 0. ESPHome sets `pclk_inverted: false` explicitly
  (its default is true). HomeTiles therefore uses 0. If the picture shimmers
  or shows a one-pixel seam, try 1 (`pclk_active_neg` in `kBoard`).
- **Backlight rate.** openHASP's default 20 kHz cuts the screen off below about
  200 of 255 (users in discussion #458), the same symptom as the Guition board.
  ESPHome uses 1 kHz. HomeTiles starts with the Guition's silent 150 Hz and its
  calibrated visible floor (raw 39); both are unmeasured on this board.
- **Default rotation.** openHASP and ESPHome run panel and touch unrotated, so
  the default rotation is 0 (the Guition default is 180 degrees). Settings
  flips it if the panel stands upside down.
- **Relays.** openHASP lists GPIO45/46/44 for the EU v2.3 3-gang board and
  GPIO1/2/46/45 for the CN 4-gang board; ESPHome lists GPIO45/46/2 for EU
  v2.4 (`inverted: false`). GPIO45 and GPIO46 are relays on every variant.
  A v2.3 owner confirmed GPIO44 as the third relay (issue #93), so the Web
  Admin I/O page offers GPIO45, GPIO46 and GPIO44 (active high), all three
  at once. GPIO44 is also UART0 RX: while it drives the relay the serial
  console takes no input; the log output on GPIO43 stays. Discussion #458
  names GPIO43 (UART0 TX, the serial log) as relay L4 on v2.3; it stays
  unexposed, like the CN board's GPIO1/2.
- **Serial log.** The build profile keeps the Guition's USB settings (USB CDC
  on boot off), so the log is on UART0 (GPIO43/44 header). Discussion #458
  places the v2.3 micro-USB on the native USB pins GPIO19/20: flashing works
  there, the log does not appear on it. ESPHome's v2.4 page wires GPIO19/20 to
  the Zigbee module instead.
- **PCB v2.4** has no USB socket (flash through the header pins). ESPHome's
  page indicates the same screen and touch wiring, but this profile is
  described for v2.3. The discontinued v1.23 board has different wiring and
  is not supported.

## Hardware validation

Pending (issue #93): display init and colors, touch alignment and rotation,
backlight dimming range and audible noise, flash-write blackout, Wi-Fi,
MQTT/Bridge, Web OTA.
