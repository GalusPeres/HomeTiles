# Guition JC8012P4A1 V2

This target is exclusively for the second 10.1-inch Guition panel revision
identified by `V2` in the material number on the rear label, for example
`SKU:10153002-V2`. It has its own build profile and OTA descriptor so a V1
firmware image cannot be installed on a V2 panel through HomeTiles OTA.

## V2-specific display configuration

- Panel controller: JD9365 with the 204-command new-panel initialization table
  supplied in Guition's separate V2 board bundle
- Native panel resolution: 800x1280; HomeTiles renders 1280x800 landscape and
  rotates the framebuffer into the native portrait orientation
- Two-lane MIPI-DSI at 1500Mbps per lane
- DPI pixel clock: 80MHz
- Horizontal timing: pulse 20, back porch 20, front porch 40
- Vertical timing: pulse 4, back porch 10, front porch 30

The V1 target remains unchanged at 60MHz with vertical timing 4/8/20 and its
own old-panel JD9365 command table.

## Shared board hardware, isolated driver tree

Issue #18 confirms that the V2 revision keeps the same ESP32-P4 board, GSL3680
touch controller, backlight, SDMMC and expansion-pin wiring. Those sources are
copied into this target's own driver tree so future revision-specific changes
cannot silently alter the V1 image.

- ESP32-P4, ESP32-C6 Wi-Fi coprocessor, 16MB SPI flash and 32MB PSRAM
- LCD reset GPIO 27; active-high backlight GPIO 23
- GSL3680 touch on SDA 7, SCL 8, reset GPIO 22
- 4-bit SDMMC slot 0 on D0-D3 39-42, CLK 43 and CMD 44
- HomeTiles target, release asset and OTA key: `guition_jc8012p4a1_v2`
- Build define: `DEVICE_GUITION_JC8012P4A1_V2`

## Built-in camera (opt-in, hardware validation pending)

The V2 board carries an OV02C10 MIPI-CSI camera. HomeTiles compiles the
capture path only for this profile (`HOMETILES_LOCAL_CAMERA`) and keeps it off
until the user enables it in Web Admin Settings. Board facts, the vendored
Espressif register table and open assumptions are listed in
`vendor/ov02c10/PROVENANCE.md`; the hardware glue is `local_camera_hw.cpp`.

- SCCB 0x36 shares the touch bus (SDA 7, SCL 8); no reset, power-down or XCLK
  pin, so "off" means software standby
- 1288x728 RAW10, one lane at 400 Mbit/s, cropped to 1280x720 JPEG snapshots
  requested by the Home Assistant Bridge over MQTT

## Provenance and validation

The V2 command table was extracted verbatim from the
`JC8012P4A1C_I_W_Y_NewPanel` Guition bundle and reported in
[HomeTiles issue #18](https://github.com/GalusPeres/HomeTiles/issues/18). The
reporter confirmed the 80MHz timing, display output and unchanged touch path on
real V2 hardware. The table carries the same Espressif Apache-2.0 provenance as
the existing V1 table; the license is included as
`vendor/LICENSE-APACHE-2.0.txt`.

This remains a community-testing target until an official HomeTiles CI artifact
has completed the physical display, touch, storage, network and OTA checklist.
