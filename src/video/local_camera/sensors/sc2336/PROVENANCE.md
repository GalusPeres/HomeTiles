# SC2336 sensor driver provenance

## Vendored data (Apache-2.0, Espressif Systems (Shanghai) CO LTD)

Source: `espressif/esp-video-components`, commit
`dbbdcbd5e25bdb6561d3f7fd0c5149641ba5536f` (esp_cam_sensor 2.6.0),
`esp_cam_sensor/sensors/sc2336/`:

- `include/sc2336_types.h`: `sc2336_reginfo_t`.
- `private_include/sc2336_regs.h`: register names (`SC2336_REG_*`).
- `private_include/sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps.h`:
  `sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps`, copied byte for byte.
  SHA256 of the upstream file: `d8d3782e92fc30c54292a4485714c7cc36639c4f1903501d66295047430bbf28`.

Mode facts from the upstream format list (`MIPI_2lane_24Minput_RAW8_1280x720_30fps`):
24 MHz input, 2 lanes, RAW8, MIPI clock 336 MHz, 84 MHz pixel clock, HTS 2240,
VTS 1250 (30 fps), BGGR, default exposure `0x37e`, no line-sync packets.
Register semantics from the upstream driver: SCCB address 0x30, chip ID
`0x3107/0x3108 = 0xcb3a`, stream `0x0100`, mirror `0x3221` bits [2:1], flip
bits [6:5], exposure `0x3e00..0x3e02` as lines << 4, maximum exposure VTS - 6,
analog coarse gain `0x3e09` (0x00/0x08/0x09/0x0b/0x0f = 1/2/4/8/16x from the
upstream gain map), digital coarse `0x3e06` and fine `0x3e07`.

## HomeTiles overrides (not vendor data)

As for the SC202CS of the same family: VTS written explicitly and stretched
for long exposures up to 4 x VTS, default exposure VTS - 6 at 1x gain, gain
split analog coarse first, the output window start (16/4) kept in every
orientation, the 544x720 turned window on the image centre (start x 384).

## Board facts (Elecrow CrowPanel Advanced 10.1)

From Elecrow's IDF lesson 13 (`bsp_camera`, sdkconfig): SCCB on I2C port 1,
SCL 13, SDA 12, no reset or power-down line, 2 lanes; Elecrow runs the 2-lane
RAW8 1024x600 mode, HomeTiles the 2-lane RAW8 1280x720 mode.

## Assumptions that still need hardware confirmation

- Mirror and mounting turn of the module, colours under mirror/flip.
- Black level 16 (taken from the SC202CS tuning), exposure and gain range.
