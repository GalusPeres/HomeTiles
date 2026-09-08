# Troubleshooting

## Connection & pairing

<a id="tiles-show-no-data"></a>

**No tile data**

1. Check the display's MQTT host, port, and credentials in the Web Admin.
2. Match its **Device topic base** and **Home Assistant prefix** to the Bridge entry.
3. Select the entity under **HomeTiles Bridge → Configure → Entity Configuration**, then assign it to a tile.
4. Check that the panel appears under the Bridge in Home Assistant. If missing, tap **Settings → System → Pairing** on the display.

See the [setup guide](home-assistant-setup.md) for first-time installation.

<a id="the-display-is-missing-in-home-assistant-i-deleted-it-there"></a>

**Display not discovered**

Initial network discovery runs only before MQTT credentials are saved. Otherwise, enter the [MQTT settings manually](home-assistant-setup.md#step-5-pair-the-display), then use **Pairing** to announce the panel again. Home Assistant may already know the device, so check the existing Bridge entries first.

<a id="ap-mode-basics"></a>

**AP mode**

The hotspot password is `12345678`, also shown with a QR code. It switches off after 10 minutes without a saved configuration. MQTT and the Web Admin are unavailable while AP mode is active.

<a id="what-happens-if-home-assistant-or-the-mqtt-broker-is-offline"></a>

**Home Assistant or MQTT is offline**

The display keeps running and reconnects automatically when the broker returns. Entity states resync after reconnection.

## Tiles & camera

<a id="the-camera-tile-asks-for-a-newer-bridge-or-never-shows-video"></a>

**No camera video**

Camera tiles are experimental and require ESP32-P4, firmware v0.6.3 or newer, and Bridge v0.6.28 or newer.

1. Update the Bridge through HACS and restart Home Assistant.
2. Select the camera in the Bridge's **Entity Configuration** and verify it works in Home Assistant.
3. Allow the display to reach the Home Assistant host on TCP ports `8124`–`8131`.
4. Check the Home Assistant log for FFmpeg or camera-source errors.

Snapshot cameras follow their source refresh rate. For a model-specific limitation or test build, open the status information in the [device list](index.md#device-support).

<a id="a-local-hardware-entity-is-missing-in-home-assistant"></a>

**Missing local I/O entity**

Save the assignment in the panel's **I/O** tab. It should become selectable by tiles on that panel immediately. For Home Assistant, use Bridge v0.6.32 or newer and an MQTT connection; restart Home Assistant after updating the Bridge, then save the assignment again or use **Pairing**.

**Empty Energy tile**

Configure Home Assistant's Energy Dashboard and enable the matching electricity, gas, or water category in the [Bridge options](bridge.md#energy-dashboard).

## Display behavior

<a id="the-esp32-s3-screen-briefly-goes-black-while-saving"></a>

**ESP32-S3 briefly goes black while saving**

The backlight pauses during flash writes to prevent a shifted image. It should recover immediately. If it stays black, [report the failure](#the-display-crashed-or-restarted-by-itself) with the exact model and serial log.

<a id="the-display-briefly-flashes-blue-when-saving-or-updating-waveshare"></a>

**Waveshare briefly flashes blue while saving or updating**

A brief blue flash is a known cosmetic limitation when flash writes interrupt display refresh.

<a id="the-tab5-dims-itself-when-enabling-ap-mode-or-after-a-reboot"></a>

**Tab5 dims during AP startup or reboot**

Temporary dimming reduces the power demand during WiFi startup. Your configured brightness returns when WiFi connects.

<a id="ghost-images-shadows-of-previous-content-waveshare"></a>

**Faint images of previous content**

LCD image retention can follow static, high-contrast content, especially on a cold panel. It normally fades; use a shorter display sleep timeout to reduce it.

## Updates & crashes

<a id="the-screen-goes-black-during-a-web-admin-ota-upload"></a>
<a id="mqtt-disconnects-during-updates"></a>

**Black screen or MQTT disconnect during an update**

Expected: a Web Admin upload turns off the display, and installation temporarily disconnects MQTT. The device restarts and reconnects afterwards. Keep it powered until the update finishes.

<a id="the-update-check-fails-even-though-wifi-works"></a>
<a id="the-update-check-says-up-to-date-but-i-expected-an-update"></a>

**Update check fails or shows no new version**

Restart the display and check again. Compare the version under **Settings → System** with the [latest release](https://github.com/GalusPeres/HomeTiles/releases/latest).

<a id="the-github-update-download-fails-or-the-device-restarts-during-it"></a>

**Update download repeatedly fails**

Use the [Web Admin upload](updating.md#2-web-admin-ota-upload) with the matching plain `.bin`, not `_factory.bin`, or use the [online flasher](installer.md) in **Update** mode.

<a id="wifi-or-mqtt-stops-responding-and-the-panel-restarts"></a>
<a id="the-display-crashed-or-restarted-by-itself"></a>

**Unexpected restart, crash, or network freeze**

1. Open **Screenshot & Diagnostics** in the Web Admin and select **Download crash log**.
2. If a core dump is available, download it and put the `.bin` in a `.zip` for GitHub.
3. For live output or a restart loop, use the browser [Device logs](device-logs.md) viewer and copy the serial log.
4. [Open an issue](https://github.com/GalusPeres/HomeTiles/issues) with the exact model, firmware version, action that triggered the problem, and logs.

A core dump can contain tile names, entity data, and other working memory. Share only the crash log if you prefer. Check the [device status](index.md#device-support) for known issues and available fixes.
