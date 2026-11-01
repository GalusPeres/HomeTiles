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
the board's own init sets these pins before ESP-Hosted starts. Elecrow's
V1.2 notes list GPIO 54 for the reset; confirm the board revision before
applying the setting to other revisions.

Audio (Elecrow wiki and the BSP of IDF lesson 11): a codec-less I2S power
amplifier on LRCLK 21 / BCLK 22 / DATA 23 with its enable on GPIO 30 (active
low) and a PDM microphone on CLK 24 / DATA 26. HomeTiles drives the
amplifier without a codec (volume and mute in software) and the microphone on
the P4's PDM receiver (I2S0); see `speaker_board.h`.

Camera ("With 2MP Camera" variant, BSP of IDF lesson 13): an SC2336 on the
CSI connector, 2 lanes, SCCB on I2C1 SCL 13 / SDA 12 without reset or
power-down lines. HomeTiles runs the sensor's 2-lane RAW8 1280x720 mode
(`src/video/local_camera/sensors/sc2336`); see `local_camera_board.h`.

Local I/O: the UART1 header pins GPIO 47 and 48. The I2C header shares the
touch bus and UART3-IN is the power input; both stay reserved.

Still to confirm on hardware: speaker level, microphone level, the camera's
mirror and mounting, microSD and the UART1 header I/O.
