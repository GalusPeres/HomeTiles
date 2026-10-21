# Third-party touch and I2C code

`touch.*` and `gt911.*` are derived from Espressif's `esp_lcd_touch` and
`esp_lcd_touch_gt911` components; `i2c.*` is Waveshare's I2C helper. They are
the copies used by `src/devices/waveshare_touch_lcd_7/vendor` and match the
GT911 code of Waveshare's `ESP32-P4-WIFI6-Touch-LCD-5` Arduino `displays`
library (commit `d144b34d8f384e84d2442e8733523560216a2f63`): I2C0 on the
board's SDA/SCL pins, both GT911 addresses (0x5D, 0x14) probed, reset and
interrupt not driven, no swap or mirror.

HomeTiles changes: the build guard is `DEVICE_WAVESHARE_P4_DSI_BOARD`,
`DEV_I2C_Init()` takes the SDA and SCL pins and `touch_gt911_init()` the
native panel size from the board description, and the touch ID log has a
board-neutral prefix.

These files are distributed under Apache-2.0. See `LICENSE-APACHE-2.0.txt`.
Modified files retain their SPDX notices and state the HomeTiles-specific
changes in their headers.
