# Vendor source notes

The following source was taken from Espressif components mirrored in Guition's
JC1060P470C example package at commit
`6304eddef32a33aa9d9a85e971e39dfb24c3cca3`:

- `esp_lcd_jd9165.c`
- `esp_lcd_jd9165.h`

Upstream:

- https://github.com/cheops/JC1060P470C_I_W/tree/6304eddef32a33aa9d9a85e971e39dfb24c3cca3/1-Demo/Demo_IDF/examples/readme/espressif__esp_lcd_jd9165

Those files retain Espressif's Apache-2.0 SPDX header. The corresponding
license is included as `LICENSE-APACHE-2.0.txt`. The isolated V3 copy uses the
complete Guition 2026 "New Panel" JD9165 initialization sequence documented in
https://github.com/jtenniswood/espcontrol/pull/1668.

`gt911.cpp`, `gt911.h`, `touch.cpp`, and `touch.h` are adapted from
Espressif's Apache-2.0 `esp_lcd_touch` and `esp_lcd_touch_gt911`
components. The touch reset/address sequence was adapted for this board.
The Espressif source history used for comparison is:

- https://github.com/espressif/esp-bsp/tree/68abfe167d61307be3a3e6e3898bd7ffb15f4ebc/components/lcd_touch

`i2c.cpp`, `i2c.h`, and `displays_config.h` are HomeTiles MIT code.

No source code from the PolyForm Noncommercial-licensed `espcontrol` project is
included.
