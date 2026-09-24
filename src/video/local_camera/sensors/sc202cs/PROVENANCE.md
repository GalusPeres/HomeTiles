# SC202CS (SC2356) sensor driver provenance

## Vendored data (Apache-2.0, Espressif Systems (Shanghai) CO LTD)

Source: `espressif/esp-video-components`, commit
`dbbdcbd5e25bdb6561d3f7fd0c5149641ba5536f` (esp_cam_sensor 2.6.0),
`esp_cam_sensor/sensors/sc202cs/`:

- `include/sc202cs_types.h`: `sc202cs_reginfo_t`.
- `private_include/sc202cs_regs.h`: register names (`SC202CS_REG_*`).
- `private_include/sc202cs_mipi_1lane_24Minput_1280x720_raw8_30fps.h`:
  `sc202cs_mipi_1lane_24Minput_1280x720_raw8_30fps`, 131 entries, copied
  byte for byte. SHA256 of the array text as stored in `sc202cs_settings.h`:
  `3b33c0336463e7a4ed6879e823dbf00527e47758211f396a9941959cc19f5ffb`.

The table opens with the software reset (`0x0103 = 1`); the driver waits the
5 ms of the upstream `sc202cs_soft_reset()` after it. Mode facts from the
upstream format list: 24 MHz input, 1 lane, RAW8, MIPI clock 576 MHz
(`mipi_clk`), 72 MHz pixel clock, HTS 1920, VTS 1250 (30 fps), BGGR, default
exposure `0x3dc`. M5Stack's M5Tab5-UserDemo (commit
`b4e356bc491ca070d54004718dad789c07d5fc93`) ships the same table except
`0x3e01 = 0x4d` (changed upstream in `ce966257`).

Register semantics from the upstream driver: chip ID `0x3107/0x3108 = 0xeb52`,
stream `0x0100`, mirror `0x3221` bits [2:1], flip bits [6:5], exposure
`0x3e00..0x3e02` as lines << 4 (4 fractional bits), exposure maximum VTS - 6,
analog coarse gain `0x3e09` (0x00/0x01/0x03/0x07/0x0f = 1/2/4/8/16x), digital
coarse `0x3e06` and fine `0x3e07` (0x80..0xfc in 1/128 steps). The upstream
tuning file uses a black level of 16.

## HomeTiles overrides (not vendor data)

- VTS written explicitly (1250) and stretched for exposures above VTS - 6, up
  to 4 x VTS (7.5 fps), frame length first.
- Default exposure VTS - 6 at 1x gain; gain split analog coarse first, the
  rest as digital fine gain (upstream's default map raises digital gain
  first).
- The output window start (`0x3211`, `0x3213`) stays at the table value 4 in
  every orientation, as upstream: the sensor keeps BGGR under mirror/flip by
  itself. A one-pixel move per flip (b29) turned the Tab5 image
  green/magenta on hardware.
- SCCB through a board transport: on the Tab5, M5Unified owns the I2C port.

## Board facts (M5Stack Tab5)

From M5Stack's Tab5 schematic, espressif/esp-bsp `bsp/m5stack_tab5` (commit
`73ee07b1ad56f865f13a2739be8bb6808c1da58a`) and the M5Tab5-UserDemo:
SCCB 0x36 at 400 kHz on I2C0 (SDA 31, SCL 32); CAM_RST on PI4IOE5V6408 0x43
pin P6 (high = run, 100 ms before detection); 24 MHz oscillator on the board;
1 data lane; LDO channel 3 at 2500 mV shared with the display.

## Assumptions that still need hardware confirmation

- Colours with mirror and flip after the window fix (b30).
- The SCCB clock without the demo's (unconnected) GPIO36 clock output.
- Night exposure beyond VTS - 6 and the combined gain range.
