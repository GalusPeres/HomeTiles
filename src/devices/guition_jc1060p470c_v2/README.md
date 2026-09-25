# Guition JC1060P470C V2

HomeTiles candidate target for the 7-inch `JC1060P470C_I_W_Y` 2026 `V2`
"New Panel" revision. Display, touch, brightness and SD operation still need
confirmation on the exact hardware.

The V2 marking on the rear SKU/material label is mandatory. The original
panel needs the separate `guition_jc1060p470c` profile; using the wrong panel
profile is known to produce a white image with a vertical noise band.

This target currently claims the `_I_W_Y` variant only. Guition documents the
`_I_W` and `_I_W_Y` suffixes as different hardware variants; its examples use
direct 1024x600 touch coordinates for `_I_W_Y`, while `_I_W` scales an
800x480 touch range to the panel resolution.

Hardware configuration:

- ESP32-P4 with 16 MB flash and 32 MB PSRAM
- 1024x600 JD9165 MIPI-DSI display
- GT911 touch on I2C0, SDA GPIO7, SCL GPIO8
- LCD reset GPIO0, touch reset GPIO22, touch interrupt GPIO21
- Backlight PWM on GPIO23, active high, 20 kHz/10-bit
- JD9165 MIPI-DSI: 2 lanes at 750 Mbps, 52 MHz pixel clock
- Horizontal timing 24/136/160 and vertical timing 2/21/12
- MIPI PHY LDO channel 3 at 2.5 V
- TF/SD card on SDMMC slot 0 with LDO channel 4
- Active-low TF/SD power switch on GPIO45; the driver performs the official
  200 ms power reset before each mount attempt

The HomeTiles UI uses the independently retained `DEVICE_LAYOUT_1024X600`
layout profile. The hardware-tested orientation is now the default; its
flip-only setting rotates the display and touch mapping together and persists
the selected orientation.

Camera beta builds (`HOMETILES_CAMERA_BETA`) support Guition's optional
OV02C10 module on the 15-pin 0.3 mm CSI connector, with SCCB on the touch bus
and the display's MIPI PHY supply shared. Orientation and the 1280x720
one-lane mode are pending a hardware check.

The JD9165 driver and SDMMC power setup are derived from Espressif files under
Apache-2.0. See `vendor/README.md` and `vendor/LICENSE-APACHE-2.0.txt`.

Hardware references:

- https://github.com/jtenniswood/espcontrol/pull/1668
- https://github.com/Deep-start9527/guition_product_demo/tree/62e51b0449ac520031520361f71bab7699484fe4/demo_arduino/JC1060P470C_I_W_Y
- https://github.com/cheops/JC1060P470C_I_W/tree/6304eddef32a33aa9d9a85e971e39dfb24c3cca3
