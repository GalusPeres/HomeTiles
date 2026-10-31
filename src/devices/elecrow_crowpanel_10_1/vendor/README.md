# Third-party display code

The EK79007 panel driver and the GT911 touch stack are derived from Espressif
display components and the Waveshare ESP32-P4-WIFI6-Touch-LCD BSP. They are
distributed under Apache-2.0; see `LICENSE-APACHE-2.0.txt`. Modified files
retain their SPDX notices and state the HomeTiles-specific changes in their
headers.

The Elecrow pin and timing values in `displays_config.h` come from Elecrow's
CrowPanel Advanced 10.1 V1.1/V1.2 `board_config.h` and product documentation:
1024x600, EK79007, 51 MHz DPI clock, HPW 70 / HBP 160 / HFP 160, VPW 10 /
VBP 23 / VFP 21, LCD reset 41, backlight 31 (30 kHz, active high), GT911 on
I2C SDA 45 / SCL 46 with reset 40 and interrupt 42.
