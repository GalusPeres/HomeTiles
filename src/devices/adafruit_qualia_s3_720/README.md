# Adafruit Qualia ESP32-S3 720x720

This profile is for the Adafruit Qualia ESP32-S3 for RGB-666 displays
(product 5800) carrying the 4" square 720x720 TL040HDS20 capacitive panel
(product 5794). Other Qualia panels use different timings and are not
supported by this image.

## Profile and build

- Profile and firmware key: `adafruit_qualia_s3_720` (local-only, `publish`
  is `false` in `tools/device-profiles.json`)
- Device define: `DEVICE_ADAFRUIT_QUALIA_S3_720`
- ESP32-S3 N16R8: 16MB flash, 8MB octal PSRAM
- Layout: the native 720x720 layout (4x4 grid, 166 px cells, 16 px gap),
  the same geometry as the Waveshare B4. It joins the S3 RGB lifecycle
  family (`DEVICE_ESP32_S3_RGB_480`) but not `DEVICE_LAYOUT_480X480`.
- Internal LittleFS for runtime files; shared repository `partitions.csv`

```powershell
./tools/build-firmware-local.ps1 -Profile adafruit_qualia_s3_720
```

## Hardware contract

- RGB-565 bus on the 40-pin FPC: DE 2, VSYNC 42, HSYNC 41, PCLK 1,
  B 40/39/38/0/45, G 48/47/21/14/13/12, R 11/10/9/46/3. The TL040HDS20 has no
  command channel; the expander reset pulse is its complete power-on sequence.
- Timings from Adafruit's CircuitPython `square40` definition: HSYNC 2/44/46,
  VSYNC 2/18/16 (pulse/back/front), falling-edge PCLK. If the picture sits
  off-centre horizontally, the TL040HDS20 datasheet swaps the horizontal
  porches (back 46, front 44).
- 12MHz pixel clock (about 19.5Hz). GDMA scans the framebuffer out of PSRAM
  continuously; at 16MHz, full 720x720 redraws shifted the image sideways.
- RGB data, PCLK and DE are driven at `GPIO_DRIVE_CAP_0` after panel init.
  The default drive desensed the 2.4GHz front end on this board (HTTP
  requests taking tens of seconds). If the panel shows speckle or dropped
  columns, raise the drive first.
- LCD_CAM is reset before panel creation so a warm restart cannot latch the
  scanout onto a shifted VSYNC phase.
- TCA9554 expander at `0x3F` on SDA 8 / SCL 18: direction `0x68`, bit 2 panel
  reset, bit 1 panel CS (parked high), bit 4 backlight enable. The backlight
  has no PWM, so brightness is on/off only.
- FT6336 touch at `0x48` on the same bus, polled (its IRQ is an expander
  input). Raw coordinates map 1:1 to the panel in rotation 0.
- The two expander buttons (bits 5/6) are not used by HomeTiles.

## Hardware validation

Hardware facts come from the author's working Qualia bring-up in
`ha-media-remote` and Adafruit's CircuitPython Qualia sources.

First maintainer boot (USB factory image): display, FT6336 touch (chip
`0x64`, firmware `0x22`) and Wi-Fi work. With the scanout active, LAN ping
averaged 8.6 ms and the 142 KB Web Admin page loaded in about 2.1 s. PSRAM
free after setup was about 3.3 MB.

Notes: opening the native USB serial port resets the board
(`rst:0x15 USB_UART_CHIP_RESET`). After the first flash the board needed a
manual RESET to leave the ROM bootloader. The on-device Wi-Fi list was once
empty; joining through the access-point portal worked.

Pending checks: touch orientation in all rotations, backlight sleep/wake,
MQTT and Bridge pairing, flash-write and OTA resynchronization, warm-restart
image position, and PSRAM minima with an image screensaver.

## Primary hardware references

- [Adafruit Qualia ESP32-S3 guide](https://learn.adafruit.com/adafruit-qualia-esp32-s3-for-rgb666-displays)
- [Adafruit CircuitPython Qualia library (square40 panel)](https://github.com/adafruit/Adafruit_CircuitPython_Qualia)
