# Third-party display code

`hx8394/esp_lcd_hx8394.{c,h}` is Waveshare's `waveshare/esp_lcd_hx8394`
component 2.1.0, the HX8394 driver the board's BSP
(`waveshare/esp32_p4_wifi6_touch_lcd_5` 1.0.4) depends on, taken from
`waveshareteam/Waveshare-ESP32-components` (`display/lcd/esp_lcd_hx8394`) at
commit `23a6012d5e87d27399cf90fb851cab3689e29159`. Its default init table
(`vendor_specific_init_code_default`) is the panel's sequence: the BSP passes
no table of its own.

HomeTiles changes: the build guard `DEVICE_WAVESHARE_TOUCH_LCD_5`, a version
fallback for the Arduino build, and the standalone I2C sequence (a write to
address 0x45 on GPIO7/GPIO8) removed, as the LCD-5 BSP selects
`ESP_LCD_HX8394_SKIP_I2C_INIT`: HomeTiles owns the board I2C bus.

The component is distributed under the MIT License; `LICENSE-MIT.txt` is the
component's `license.txt` as published (with its unfilled copyright line).
