# Device logs

Read and copy your display's live serial log directly in desktop **Chrome or Edge**, using a USB data cable.

Works with the serial console of all HomeTiles **ESP32-P4 and ESP32-S3** profiles, including native USB serial and USB-UART adapters. No firmware selection or installation is needed.

1. Connect the display's USB **data/debug** port to your computer and let HomeTiles start normally.
2. Close other serial monitors or flashing tools, then select **Connect** and choose the display's serial port.
3. Reproduce the problem, select **Disconnect**, then **Copy log** to include the output in your report.

<div class="ht-installer ht-device-logs" data-device-logs>
  <div class="ht-installer-action">
    <div class="ht-device-logs-connection">
      <button type="button" data-log-connect disabled>Connect</button>
      <button type="button" data-log-disconnect disabled>Disconnect</button>
      <span class="ht-installer-status" data-log-status role="status" aria-live="polite">Loading log viewer…</span>
    </div>
  </div>
  <div class="ht-installer-log-panel">
    <div class="ht-installer-log-header">
      <strong>Device log</strong>
      <button type="button" data-log-copy aria-label="Copy device log to clipboard" disabled>Copy log</button>
      <button type="button" data-log-clear disabled>Clear log</button>
      <label class="ht-device-logs-follow"><input type="checkbox" data-log-follow checked> Follow log</label>
      <span class="ht-device-logs-action" data-log-action role="status" aria-live="polite"></span>
    </div>
    <pre data-log-output role="log" aria-label="Device serial log" aria-live="off" tabindex="0"></pre>
    <p class="ht-device-logs-limit" data-log-limit hidden>Older output or long lines were trimmed. Copy the log soon after reproducing the problem.</p>
    <p class="ht-device-logs-limit" data-log-storage role="status" hidden>Browser storage is unavailable. Copy your log before reloading, closing this tab, or leaving the documentation.</p>
    <p class="ht-device-logs-limit" data-log-serial role="status" hidden>Firmware flashing is using the serial connection. Wait for it to finish before connecting.</p>
  </div>
</div>

<noscript>Enable JavaScript to connect to your display and read its serial log.</noscript>

Your captured log is restored when you return to this page or reload it in the same browser tab. **Clear log** also removes the saved copy. The viewer keeps the most recent 2,000 lines, up to 128 Ki characters, and trims individual lines beyond 4,096 characters.

The USB connection and capture continue while you browse other documentation pages in this tab. The status in the header shows the current USB activity; select it to return to the active tool. Select **Disconnect** to stop capture.

Opening the firmware installer keeps capture running. Selecting a USB port with **Connect and flash** stops capture and releases its port before the installer connects. Cancelling the port selection keeps capture running. After flashing, reconnect here to capture new output.

Reloading, closing the tab, or leaving the documentation ends the USB connection. After a reload, select **Connect** to capture new output. The saved log stays in this tab's browser session; copy it before closing the tab.

## Capture a restart

Connect first, then briefly press the display's **RESET** button if it has one. **Do not hold BOOT**: that starts the flashing bootloader instead of HomeTiles. If a restart removes the USB port, select **Connect** again after it reappears; output sent while disconnected cannot be recovered.

## No output or no port

- Use the data/debug port, not a power-only or USB host port. Some boards have more than one USB connector.
- Check the USB data cable and close any serial monitor, flasher, or other tab using the same port.
- Keep the display's normal power supply connected if required by its model. A USB-UART adapter may need its manufacturer's driver.
- Confirm HomeTiles is running normally, then wait for output or reproduce the problem. The viewer reads new serial output; it does not retrieve stored crash logs.

For a saved crash log or core dump, use **Screenshot & Diagnostics** in the Web Admin. See [Troubleshooting](faq.md#the-display-crashed-or-restarted-by-itself).
