# Flashing the Firmware

[Device list](index.md#device-support)

## Browser installer

Use desktop Chrome or Edge and a USB data cable.

<span id="update-hometiles"></span>
<span id="first-install-or-factory-reset"></span>

<div class="ht-installer" data-hometiles-installer>
  <div class="ht-installer-form">
    <div class="ht-installer-section">
      <label class="ht-installer-section-heading" for="installer-firmware-source">1. Firmware</label>
      <div class="ht-installer-section-body">
        <p class="ht-installer-section-info">Choose the published release or a local HomeTiles .bin file.</p>
        <div class="ht-installer-control ht-installer-firmware-control">
          <select id="installer-firmware-source" autocomplete="off" disabled>
            <option value="published">Loading published release...</option>
            <option value="local">Local HomeTiles .bin file</option>
          </select>
          <input id="installer-firmware-file" class="ht-installer-firmware-file" type="file" accept=".bin,application/octet-stream" hidden>
        </div>
      </div>
    </div>

    <div class="ht-installer-section">
      <label id="installer-device-label" class="ht-installer-section-heading" for="installer-device">2. Device</label>
      <div class="ht-installer-section-body">
        <p class="ht-installer-section-info">Select the exact model printed on the device or rear label.</p>
        <div class="ht-installer-control">
          <select id="installer-device" autocomplete="off">
            <option value="">Loading devices...</option>
          </select>
          <div id="installer-device-details" class="ht-installer-device-details" data-kind="info" hidden></div>
        </div>
      </div>
    </div>

    <div class="ht-installer-section">
      <div id="installer-mode-label" class="ht-installer-section-heading">3. Flash mode</div>
      <div class="ht-installer-section-body">
        <p class="ht-installer-section-info">Update keeps settings. First install / factory reset erases all local data.</p>
        <div class="ht-installer-modes" role="radiogroup" aria-labelledby="installer-mode-label">
          <label class="ht-installer-mode">
            <input type="radio" name="installer-mode" value="update" checked>
            <strong>Update</strong>
          </label>
          <label class="ht-installer-mode ht-installer-mode-danger">
            <input type="radio" name="installer-mode" value="factory">
            <strong>First install / factory reset</strong>
          </label>
        </div>
      </div>
    </div>

    <div class="ht-installer-section">
      <div id="installer-confirm-label" class="ht-installer-section-heading">4. Confirm</div>
      <div class="ht-installer-section-body">
        <p class="ht-installer-section-info">Confirm the exact label before connecting the serial port.</p>
        <div class="ht-installer-confirmations">
          <label>
            <input id="installer-exact-hardware" type="checkbox">
            <span id="installer-exact-hardware-text">I checked the exact model label.</span>
          </label>
          <label id="installer-factory-confirmation-row" class="ht-installer-danger-check" hidden>
            <input id="installer-factory-confirmation" type="checkbox">
            <span>Erase all settings and local data.</span>
          </label>
        </div>
      </div>
    </div>

    <div class="ht-installer-section ht-installer-section-connect">
      <div class="ht-installer-section-heading">5. Connect and flash</div>
      <div class="ht-installer-section-body ht-installer-action">
        <p class="ht-installer-section-info">Keep power, USB, and this browser tab open until flashing finishes. You can browse other pages in this documentation; the header shows progress and returns you here.</p>
        <div class="ht-installer-action-row">
          <button id="installer-flash" type="button" disabled>Connect and flash</button>
          <div id="installer-status" class="ht-installer-status" role="status" aria-live="polite" data-kind="info">Loading...</div>
        </div>
        <p id="installer-serial-busy" class="ht-installer-status" role="status" hidden>Log capture is active. Selecting a port with Connect and flash stops capture and releases its USB port. Cancelling the port selection keeps capture running.</p>
        <div id="installer-progress-panel" class="ht-installer-progress" hidden>
          <div class="ht-installer-progress-meta">
            <strong id="installer-phase">Preparing</strong>
            <span id="installer-progress-text">0%</span>
          </div>
          <progress id="installer-progress" max="100" value="0" aria-labelledby="installer-phase installer-progress-text"></progress>
        </div>
      </div>
      <div id="installer-log-panel" class="ht-installer-log-panel" hidden>
        <div class="ht-installer-log-header">
          <strong>Flash log</strong>
          <button id="installer-copy-log" type="button" aria-label="Copy flash log to clipboard">Copy log</button>
          <span id="installer-log-action-status" role="status" aria-live="polite"></span>
        </div>
        <pre id="installer-log-output" role="log" aria-label="Installer flash log" aria-live="off" tabindex="0"></pre>
      </div>
    </div>
  </div>
</div>


After flashing, restart the display and follow [Home Assistant setup](home-assistant-setup.md).

## Manual flashing

If the browser installer is unavailable, use [Espressif's Flash Download Tool](https://www.espressif.com/en/support/download/other-tools) or [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32/installation.html). This alternative is for a **first installation or full reset and erases all settings**. [Export your dashboard](web-admin.md#import-export) first if needed.

1. Download the exact device's `_factory.bin` from the [latest release](https://github.com/GalusPeres/HomeTiles/releases/latest). Match the complete model and revision; V1/V2, LCD-7/LCD-7B and P4/S3 images are not interchangeable.
2. In Flash Download Tool, select **ESP32-P4** or **ESP32-S3**, **UART**, and the USB port. Erase the flash, then write the factory file at **`0x0`**. Alternatively, with esptool installed:

    ```text
    python -m esptool --port COM3 chip-id
    python -m esptool --port COM3 write-flash --erase-all 0x0 your_device_factory.bin
    ```

    Replace `COM3` with your serial port (for example `/dev/ttyACM0` on Linux), and the filename with your downloaded factory image.

3. Wait until verification finishes, then restart the display.

**ESP32-P4 revision check:** standard P4 images require pre-v3 silicon (revisions 1–199). The experimental Waveshare 7B `_rev3_1` image is for **exact v3.1 only**. Check the `chip-id` output before a manual write; v3.2 and newer are unsupported. esptool does not enforce HomeTiles' narrower revision checks.

Never write a plain update `.bin` at `0x0`. To preserve settings, use the browser's **Update** mode or [Firmware Updates](updating.md).

## Troubleshooting

- **No serial port:** use a data cable, try another USB port, and close other serial monitors or flashing tools.
- **Cannot connect:** hold **BOOT** while connecting, then release it when the connection starts.
- **Wrong chip or revision:** recheck the device label and selected model; do not bypass the check.
- **Interrupted installation:** reconnect and repeat the selected operation. An interrupted Update keeps the previous app; an interrupted factory reset needs a complete factory installation.

For a blank display after flashing or connection problems after setup, see [Troubleshooting](faq.md).
