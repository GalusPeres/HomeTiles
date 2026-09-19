# Third-party display and touch code

The ST7701 panel driver and GT911 touch implementation in this directory are
derived from Espressif's Apache-2.0 display and touch components. The matching
license is included as `LICENSE-APACHE-2.0.txt`.

The ST7701S initialization sequence, MIPI-DSI timings, GPIO assignments and
SDMMC configuration target the exact Guition JC4880P443C_I_W board
(JC-ESP32P4-M3 module: ESP32-P4 host + ESP32-C6 Wi-Fi co-processor):

- ST7701S init table: MIT-licensed, hardware-verified sequence from
  ultramcu/guition-jc4880p4-bsp (`board_p4_st7701_init.h`, full text in
  `LICENSE-MIT.txt`), cross-referenced against elik745i/ESP32-2432S024C-Remote;
- timings/pinout: 2 DSI lanes at 500 Mbps, 34 MHz DPI clock, LCD reset GPIO5,
  active-high backlight GPIO23, GT911 on I2C0 (SDA 7 / SCL 8), SDMMC slot 0
  on GPIO39-44 with TF_VCC from on-chip LDO channel 4;
- field notes and schematics: ultramcu/guition-jc4880p443c-i-w and Guition's
  `JC4880P443C_I_W.zip` vendor package.

Modified files are compiled only for `DEVICE_GUITION_JC4880P443_PORTRAIT`. The
HomeTiles integration uses the MIPI-DSI ST7701 path, portrait panel coordinates,
the board's active-high backlight, and the ESP32-P4 pre-v3 clock source.
