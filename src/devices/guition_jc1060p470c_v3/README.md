# Guition JC1060P470C V3

This experimental target is for the 7-inch 1024x600 Guition JC1060P470C V3
with ESP32-P4 production silicon (v3.1-v3.99). It uses the V2 panel wiring,
timing and JD9165 initialization sequence with the post-v3 firmware build.

The profile, panel driver, vendor panel/touch code and camera board support
live in this directory, independent from the V2 implementation.
Do not flash the V1 or V2 image to this board: the V3 build selects the
ESP32-P4 v3.1-v3.99 silicon range and its own firmware identity.

Display, touch and local OV02C10 camera operation are confirmed on V3 hardware
by the contributor. The V3 camera driver has its own OV02C10 board
configuration and panel PHY-supply handling. SD, expansion GPIOs and audio
remain disabled because their V3 wiring is unverified.

Hardware references:

- [Upstream V3 device definition](https://github.com/jtenniswood/espcontrol/tree/main/devices/guition-esp32-p4-jc1060p470-v3)
