# BIGTREETECH Panda Touch

HomeTiles target for the BIGTREETECH Panda Touch, the 4.3 inch touch panel of
3D printers:

- ESP32-S3 with 16 MB flash and 8 MB octal PSRAM
- 800x480 RGB565 panel in DE mode, 14 MHz pixel clock, active low
- 640 px popup card (a full-width card desynchronises this panel)
- GT900 capacitive touch controller on I2C0
- LEDC backlight on GPIO 21
- No microSD slot, no camera, no battery

## Sources

- The board's own display service: `bigtreetech/PandaTouch_IDF`
  (`docs/pinout.md`) for the pin assignment and the panel in DE mode
- The ESP-IDF board support package `fmauNeko/pandatouch-bsp` for the RGB
  panel configuration, the touch controller and the backlight
- The measured behaviour of the panel and the touch controller on the
  hardware (see below)

## Panel

The panel runs in DE mode: the horizontal and vertical synchronisation lines
are not wired, only the data enable (GPIO 38) and the pixel clock (GPIO 5).
No initialization sequence is needed, only the reset on GPIO 46.

The RGB bus carries B3..B7, G2..G7 and R3..R7 on sixteen lines:

| Signal | GPIO |
| --- | --- |
| PCLK | 5 |
| DE | 38 |
| Reset | 46 |
| B3, B4, B5, B6, B7 | 17, 18, 48, 47, 39 |
| G2, G3, G4, G5, G6, G7 | 11, 12, 13, 14, 15, 16 |
| R3, R4, R5, R6, R7 | 6, 7, 8, 9, 10 |

The horizontal timing (4/8/8) and the vertical timing (4/16/16) were confirmed
on the hardware with a ruler test pattern: the first line of the framebuffer
stays at the top edge and the last one at the bottom edge.

### Pixel clock

The vendor rate is 23 MHz, but the panel's interrupt has to refill the bounce
buffer while the display reads it, and a busy CPU (the web admin, the network)
starves that interrupt: the picture then jitters vertically. A lower pixel
clock gives the interrupt more time per line.

| Pixel clock | Frame time | Frame rate | Result |
| --- | --- | --- | --- |
| 23 MHz | 22.5 ms | 44.4 Hz | stable picture, but it jitters under load |
| 16 MHz | 26.4 ms | 37.8 Hz | stable with the square popup card |
| 14 MHz | 30.1 ms | 33.2 Hz | stable picture, no jitter under load, with the 640 px card |
| 12 MHz | 34.4 ms | 29.1 Hz | the picture falls apart, the screen shows one colour |

The frame time is `(800 + 16) x (480 + 36) / pclk`, so the panel's own
synchronisation needs a frame rate above roughly 30 Hz: 12 MHz falls below
that. 14 MHz is the lowest rate that keeps the picture intact and stable
with the wide card.

## Framebuffer and cache

The stock Arduino SDK for this core has no PSRAM XIP and a 32-byte data cache
line. A DMA that reads the PSRAM framebuffer directly loses its sync under
load, so the driver keeps one framebuffer in PSRAM and lets the panel driver
serve the DMA from a bounce buffer in DRAM (30 lines) with the cache
invalidation enabled.

The CPU writes into the framebuffer through the cache, while the DMA reads the
memory. The driver therefore writes the framebuffer back row by row after
every update, with a 32-byte aligned start and size; the rows of the
framebuffer are not contiguous, so a single range over the whole area would
sync the wrong bytes.

## Touch

The controller reports "GOODIX_GT900_105" and speaks the GT911 register
protocol, but its register map differs from the GT911 in the details:

| Register | Content |
| --- | --- |
| 0x8140 | Product id ("911") |
| 0x814E | Status: bit 7 marks a new frame, the low nibble the point count |
| 0x8150 | Point 1: X low, X high, Y low, Y high, size low, size high |

A frame is acknowledged with a zero write to 0x814E. The coordinates are
already in screen coordinates (0..799 / 0..479), so no mapping or scaling
applies. The controller reports up to five points; HomeTiles uses the first
one.

## USB

The USB-C port of the board is wired to a CH340K bridge, so the firmware logs
on UART0 (TX 43 / RX 44) and the native USB of the ESP32-S3 stays on the
USB-A host port, which the firmware does not use.
