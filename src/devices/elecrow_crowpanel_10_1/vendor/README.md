# Third-party display code

The JD9365 and GT911 sources in this directory are derived from Espressif
display components. The ILI9881C driver is based on
`espressif/esp_lcd_ili9881c` 1.1.0 at commit
`fd0098aaa277c5b35cc54779ee7bfbda72e8db1e`.

The 7-inch ILI9881C and 10.1-inch JD9365 panel initialization sequences in
`panel_init_cmds.c` are derived from Waveshare's
`ESP32-P4-WIFI6-Touch-LCD-X` BSP at commit
`6ec9ff5c8a96357f2fcac24b4fc311a3018cb878`.

These files are distributed under Apache-2.0. See
`LICENSE-APACHE-2.0.txt`. Modified files retain their SPDX notices and state
the HomeTiles-specific changes in their headers.
