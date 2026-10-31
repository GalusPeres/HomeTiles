# Elecrow CrowPanel Advanced 10.1 profile

This profile is for the 1024x600 Elecrow CrowPanel Advanced 10.1-inch
ESP32-P4 HMI AI Display. It uses the EK79007 panel controller and is not
interchangeable with the 1280x800 Waveshare Touch LCD 10.1 profile.

The EK79007 driver in `vendor/ek79007` comes from Espressif's
`esp_lcd_ek79007` component, as vendored by Waveshare's ESP32-P4 7B BSP.
The copied source retains its SPDX headers and is distributed under the
Apache License 2.0 in `vendor/LICENSE-APACHE-2.0.txt`.

Display, touch, backlight, and SDMMC values follow Elecrow's V1.1/V1.2
`board_config.h`: 1024x600 EK79007, 51 MHz DPI clock, HPW 70 / HBP 160 /
HFP 160, VPW 10 / VBP 23 / VFP 21, LCD reset 41, backlight 31 (30 kHz,
active high), GT911 on I2C SDA 45 / SCL 46 with reset 40 and interrupt 42.
The SD card uses 1-bit SDMMC on CLK 43 / CMD 44 / D0 39. The ESP32-C6 Wi-Fi
co-processor uses ESP-Hosted slot 1, CLK 18, CMD 19, D0-D3 17/16/15/14, and
reset GPIO32. This matches the factory firmware log from the tested panel;
HomeTiles configures these pins before starting ESP-Hosted. This observed
reset pin differs from the GPIO 54 listed for V1.2 in `BOARD_SETTINGS.md`;
confirm the board revision before applying the setting to other revisions.
The board has no configurable local I/O GPIOs in this profile.
