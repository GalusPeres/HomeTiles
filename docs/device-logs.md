# Device logs

Read and copy your display's live serial log directly in desktop **Chrome or Edge**, using a USB data cable.

Works with all HomeTiles **ESP32-P4 and ESP32-S3** profiles. No firmware installation is needed.

**Your logs stay locally in your browser. Nothing is uploaded.** Use **Copy log** to copy them to your clipboard and share them yourself.

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

- Capture continues while you browse this documentation. The USB status in the header takes you back here.
- Reloading, closing the tab, leaving the documentation or starting a flash ends capture. Select **Connect** to resume.
- Logs survive reloads in the same tab. **Clear log** removes the saved copy; copy anything you need before closing the tab.

## Capture a restart

Connect, then briefly press **RESET** if available, not BOOT. If USB disconnects, select **Connect** again when the port returns. Output missed while disconnected cannot be recovered.

## No output or no port

- Check the USB data cable and data/debug port. Close other tools using the same port.
- Keep the display's required power supply connected. Some USB-UART adapters need a driver.

This viewer captures new output. For saved crash logs, use **Screenshot & Diagnostics** in the Web Admin; see [Troubleshooting](faq.md#the-display-crashed-or-restarted-by-itself).
