# Waveshare Touch LCD 7B / 7B-C profile

This profile is exclusively for the 1024x600 Waveshare
ESP32-P4-WIFI6-Touch-LCD-7B and 7B-C. It must not be used for the older
1280x720 `waveshare_touch_lcd_7` profile.

The EK79007 driver in `vendor/ek79007` comes from Waveshare's official 7B
ESP-IDF example, which vendors Espressif's `esp_lcd_ek79007` component version
1.0.2 (`esp-iot-solution` commit
`6a112f4ddfeaf30ec360567ea9260a39e195c385`). The copied source retains its
SPDX headers and is distributed under the Apache License 2.0 in
`vendor/LICENSE-APACHE-2.0.txt`.

The 7B-C uses the same display, touch, backlight and SDMMC contract. A normal
build does not enable or expose its optional camera; camera beta builds
(`HOMETILES_CAMERA_BETA`) support the 7B-C OV5647 module on the 15-pin 1.0 mm
CSI connector, with its orientation pending a hardware check.
