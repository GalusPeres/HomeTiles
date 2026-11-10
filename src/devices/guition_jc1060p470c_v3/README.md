# Guition JC1060P470C V3

This experimental target is for the 7-inch 1024x600 Guition JC1060P470C V3
with ESP32-P4 production silicon (v3.1-v3.99). It uses the V2 panel wiring,
timing and JD9165 initialization sequence with the post-v3 firmware build.

The profile and device interface live in this directory; proven panel and
touch lifecycle code is shared with `src/devices/guition_jc1060p470c_v2/`.
Do not flash the V1 or V2 image to this board: the V3 build selects the
ESP32-P4 v3.1-v3.99 silicon range and its own firmware identity.

HomeTiles touch input and the remainder of the board still need validation on
the exact hardware. SD, expansion GPIOs, camera and audio are not enabled for
this profile until their V3 wiring is verified.

Hardware references:

- [Upstream V3 device definition](https://github.com/jtenniswood/espcontrol/tree/main/devices/guition-esp32-p4-jc1060p470-v3)
