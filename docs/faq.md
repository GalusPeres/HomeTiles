# Troubleshooting

## Connection & pairing

<a id="the-display-is-missing-in-home-assistant-i-deleted-it-there"></a>
<a id="display-not-discovered"></a>

**Display not found after Pair**

1. Tap **Pair** again under **Settings → System → Security**; Home Assistant can find the display for two minutes.
2. Display and Home Assistant must be in the same network: Home Assistant finds the display by mDNS (zeroconf). VLANs, guest networks and Docker without host networking often block it. Then add the display by its IP address: tap **Pair**, then **Add integration → HomeTiles Bridge → Panel by IP address (direct connection)** (Bridge v0.9.4).
3. Check that the HomeTiles Bridge v0.9.0 is installed and Home Assistant was restarted afterwards.
4. Look under **Settings → Devices & services** for an existing HomeTiles Bridge entry of this display: a display that already has one shows **Connect the panel** instead of **Panel found**.

<a id="tiles-show-no-data"></a>

**Paired, but no tile data**

1. **Settings → System** on the display should show **Home Assistant: Connected**. If not, allow TCP ports `8140`–`8147` from the display to the Home Assistant host.
2. Without a [Web Admin password](web-admin.md#web-admin-password), only the entities selected under **HomeTiles Bridge → Configure → Entity configuration** reach the display.
3. A display on MQTT needs matching **Device topic base** and **Home Assistant prefix** on the display and in its Bridge entry.

See the [Installation](installer.md) for first-time installation.

<a id="ap-mode-basics"></a>

**AP mode**

The hotspot password is `12345678`, also shown with a QR code. It switches off after 10 minutes without a saved configuration. The connection to Home Assistant and the Web Admin are unavailable while AP mode is active.

<a id="what-happens-if-home-assistant-or-the-mqtt-broker-is-offline"></a>

**Home Assistant or MQTT is offline**

The display keeps running and reconnects automatically when Home Assistant or the broker returns. Entity states resync after reconnection.

<a id="states-arrive-but-commands-or-updates-fail"></a>

**States arrive, but commands or updates fail**

New values still appear, but lights do not react, the Web Admin does not load, or an OTA update stops partway. The Wi-Fi connection is stuck, and a restart from the menu may not clear it:

1. Unplug the display for 10 seconds, then power it on again.
2. If that does not help, restart the Wi-Fi router.

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

<a id="forgot-the-settings-or-folder-pin"></a>

**Forgot the Settings or folder PIN**

Enter the factory PIN **466384537** (HOMETILES on a phone keypad). It unlocks Settings and every protected folder; then change the PIN in the Web Admin. See [Settings access](web-admin.md#settings-tile-and-access).

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
