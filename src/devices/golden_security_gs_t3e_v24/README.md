# Golden Security GS-T3E v2.4

Community HomeTiles target for the Golden Security / Tuya GS-T3E 86-box wall
panel, PCB revision v2.4 (ZIGBEE_4CP_WNB_V2.4, 2024.09.09). The maintainer has
no such board: everything below comes from public configurations and is
**not yet validated on hardware**.

- ESP32-S3-WROOM-1 N16R8: 16 MB quad flash, 8 MB octal PSRAM
- 4-inch 480 x 480 ST7701S RGB panel, GT911 capacitive touch
- Mains relays (EU 3-gang): GPIO 45 / GPIO 46 / GPIO 2
- No Micro-USB socket; flash exclusively via the UART header J1

## Build profile

Use the `t3e_v24` build profile with `-DT3E_REV_V24=1` to select the v2.4
relay mapping. See `sketch.yaml` and `BOARD_SETTINGS.md` for details.

## Firmware structure

Like the GS-T3E v2.3, the board runs the Guition ESP32-4848S040 driver. It has
its own driver, `device_golden_security_gs_t3e_v24.cpp`, derived from the
Guition one, with its own board description `kBoard` in
`device_golden_security_gs_t3e_v24.h`. Display/touch/backlight pins are
identical to PCB v2.3; only the relay pins and the missing USB socket differ.

## Hardware facts and sources

Sources:

- [ESPHome device page "TuYa T3E"](https://devices.esphome.io/devices/tuya-t3e-esp32-s3)
  (written for PCB v2.4)
- [openHASP discussion #458](https://github.com/HASwitchPlate/openHASP/discussions/458)
  and the [openHASP GS-T3E page](https://openhasp.haswitchplate.com/0.7.0/hardware/golden-security/gs-t3e/)

| Item | Value | Agreement |
| :--- | :--- | :--- |
| RGB control | DE 40, VSYNC 39, HSYNC 38, PCLK 41 | openHASP and ESPHome |
| RGB data | R0..R4 8, 18, 17, 16, 0; G0..G5 13, 12, 11, 10, 9, 3; B0..B4 15, 48, 47, 21, 14 | openHASP and ESPHome |
| ST7701 SPI init bus | CS 4, SCK 15, MOSI 48 (SCK/MOSI are also B0/B1) | openHASP and ESPHome |
| Panel reset | none: software reset 0x01 | inferred |
| Init table | Arduino_GFX `st7701_type1_init_operations` (CD=0x08, INVON) | openHASP generic ST7701 branch; ESPHome's default sequence 1 |
| Timing | HSYNC fp 10 / pw 8 / bp 50, VSYNC fp 18 / pw 2 / bp 8, 12 MHz | ESPHome |
| PCLK edge | `pclk_active_neg = 0` (rising edge) | ESPHome |
| Backlight | GPIO42, LEDC PWM, active high | openHASP and ESPHome |
| Touch | GT911, SDA 7, SCL 6, no RST/INT, address 0x5D or 0x14 | openHASP and ESPHome |
| microSD | none documented | no source lists SD pins |
| UART0 / log | TX 43, RX 44 (header J1) | ESPHome |
| Zigbee co-processor | ZT3L on UART: ESP RX GPIO19, ESP TX GPIO20, 115200 Baud | ESPHome |
| Relays (EU 3-gang) | GPIO45, GPIO46, GPIO2 | ESPHome |
| Flashing | via UART header J1 only (no USB socket) | ESPHome |

### Flashing instructions

PCB v2.4 has no USB socket. Flash firmware through the 6-pin header **J1**
(upper left on the PCB):

1. Tie **IO0 (GPIO0)** to **GND** while powering on or resetting to enter the
   bootloader flash mode.
2. Connect a USB-TTL adapter to J1 (crossed: board TX to adapter RX, board RX
   to adapter TX).
3. Flash with `pio run -e t3e_v24 -t upload`.
4. After flashing, shorten or desolder the header pins so the rear plastic
   cover closes flush.

## Open points

- Hardware validation pending: no maintainer hardware.
- Backlight dimming range and audible noise unmeasured.
- Default rotation unconfirmed (Settings can flip it).
