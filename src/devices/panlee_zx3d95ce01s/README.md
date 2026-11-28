# Panlee ZX3D95CE01S (V12)

HomeTiles target for the Smart Panlee ZX3D95CE01S 86-box wall panel,
revision V12. Pins and panel values come from public configurations. One
unit (2026-10-23, firmware v0.9.4 base) showed the right picture with the
GPIO5 backlight, touch, Wi-Fi, Web Admin and the Home Assistant link by IP
address and created and edited tiles. Other revisions and the optional parts
(SHT20, RS485) are **not yet validated**.

- WT32-S3-WROVER-N16R8: ESP32-S3, 16 MB quad flash, 8 MB octal PSRAM
- 3.95-inch 480 x 480 GC9503V RGB panel, FT6336 capacitive touch
- Optional on this board, depending on the variant: SHT20 temperature/humidity
  sensor, RS485 chip, USB socket (none of them used yet; the tested unit has
  USB and RS485)
- Native USB on GPIO19/20 (USB-Serial/JTAG): flashing and the serial log

Panlee sells the board in variants (UR, AR, TR; the suffix tells which parts
are fitted). openHASP lists different pins for them (AR: other RGB pins and an
audio codec; UR: other touch interrupt, backlight and reset). This image uses
the wiring of its TR-4848 environment; the UR and AR boards are not covered.

## Firmware structure

The board has its own driver, `device_panlee_zx3d95ce01s.cpp`, derived from
the GS-T3E v2 driver, which stays unchanged. Its pins and panel values are one
constexpr board description, `kBoard` in `device_panlee_zx3d95ce01s.h`
(`gc9503v_s3_board.h`). It has its own define `DEVICE_PANLEE_ZX3D95CE01S`,
profile key, firmware metadata key, release key and setup AP name
(`Panlee_ZX3D95CE01S_Config`), so its image and the other S3 images can
never be installed on each other. UI, grid, popups and lifecycle are the
Guition S3's.

## Hardware facts and sources

Source: [openHASP `user_setups/esp32s3/zw3d95ce01s_ar.ini`](https://github.com/HASwitchPlate/openHASP/blob/master/user_setups/esp32s3/zw3d95ce01s_ar.ini),
environment `panlee-zw3d95ce01s-tr-4848_16MB` (section `[panlee-zw3d95ce01s]`:
`gc9503v` TFT, `ft6336` touch at 0x38), and its GC9503V branch in
`src/drv/tft/tft_driver_arduinogfx.cpp`. The Panlee
[datasheet of the UR variant](https://doc.panel-tag.com/_static/espboard/ESP32S3/ZX3D95CE01S-UR/ZX3D95CE01S-UR-4848.pdf)
(PCB ZX480-S3_V13) agrees on the RGB pins; it differs on the touch interrupt
and the backlight (see below).

| Item | Value |
| --- | --- |
| RGB control | DE 40, VSYNC 41, HSYNC 42, PCLK 39 |
| RGB data | R0..R4 18, 8, 3, 46, 10; G0..G5 14, 13, 12, 11, 16, 17; B0..B4 45, 48, 47, 0, 21 |
| GC9503V SPI init bus | CS 38, SCK 45, MOSI 48 (SCK/MOSI are also B0/B1) |
| Panel reset | the VSYNC pin (41) is pulsed before the init, as openHASP does; the panel resets itself |
| Init table | Arduino_GFX 1.6.5 `gc9503v_type1_init_operations` (openHASP's choice) |
| Timing | HSYNC fp 8 / pw 10 / bp 40, VSYNC fp 10 / pw 10 / bp 40, 12 MHz, sync idle high |
| PCLK edge | rising (`pclk_active_neg = 0`; openHASP never sets it) |
| Backlight | GPIO5, LEDC PWM, active high |
| Touch | FT6336, SDA 15, SCL 6, address 0x38, reset by RC, INT GPIO4 (not used) |
| microSD | none documented |
| SHT20, RS485 | not used; no GPIO is offered on the Web Admin I/O page. The SHT20 is an I2C part (address 0x40); its bus pins are not documented for this revision |

### Open points

- **Backlight pin.** openHASP's comment reads "datasheet states 4, schematics
  states 5". The UR datasheet's GPIO4 is the TR's touch interrupt, so GPIO5 is
  used; confirmed on the V12 unit.
- **Flash mode.** One early flash of the 80 MHz QIO image looped in the ROM
  before the second-stage bootloader; later flashes of the same files booted
  and the loop did not return. The cause is unknown.
- **PCLK.** openHASP leaves the clock at Arduino_GFX's default. HomeTiles
  starts with the 12 MHz of the other 480x480 S3 boards (about 41 Hz).
- **PCLK edge, rotation.** openHASP runs panel and touch unrotated, so the
  default rotation is 0. Settings flips it if the panel stands upside down.
- **Backlight rate.** 150 Hz and the visible floor (raw 39) are taken from the
  Guition board and unmeasured here.
- **Revision.** The pins follow openHASP's TR-4848 environment; the exact
  V12 revision is unconfirmed beyond the maintainer's unit.
