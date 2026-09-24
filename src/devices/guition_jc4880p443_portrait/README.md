# Guition JC4880P443C_I_W

HomeTiles profile for the exact 4.3-inch Guition board (JC-ESP32P4-M3 module,
covering `JC4880P443C_I_W` and `JC4880P443C_I_W_Y`).

Hardware contract:

- native 480 x 800 ST7701S MIPI-DSI panel, rendered as 480 x 800 portrait
  with a 4 x 6 grid;
- two DSI lanes at 500 Mbps and a 34 MHz DPI clock;
- GT911 touch on SDA GPIO7 and SCL GPIO8, polled without INT or RST;
- active-high backlight PWM on GPIO23 and active-low panel reset on GPIO5;
- MIPI-DSI PHY power from LDO channel 3 at 2500 mV;
- SDMMC slot 0 on GPIO39-44 with LDO channel 4;
- ESP32-C6 networking through the existing ESP-Hosted transport.

The 180-degree flip is mirrored on the CPU; changing the ST7701 scan direction
over DCS loses DSI video lock. The tested hardware reports ESP32-P4 revision
v1.3 and uses the pre-v3 build profile. Display, touch, flip, camera, Wi-Fi,
MQTT, tile persistence, microSD and OTA are confirmed on physical hardware.
