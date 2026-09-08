# Device logs

Read live USB logs from any HomeTiles **ESP32-P4 or ESP32-S3** device in desktop **Chrome or Edge**.

!!! note ht-device-logs-privacy "Your logs stay locally in your browser. Nothing is uploaded."

Connect the display's USB **data/debug** port with a data cable. Close other serial tools, select **Connect**, then choose the port. Reproduce the issue, select **Disconnect**, then **Copy log**.

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

Capture continues while you browse this documentation. After reloading or flashing, select **Connect** to resume. Logs stay in this tab until you close it. Copy them first. **Clear log** removes the saved copy.

## Capture a restart

Select **Connect**, then choose **Settings → System → Restart** on the display. Alternatively, briefly press the **RESET** button if your device has one. If USB disconnects, select **Connect** again when the port returns.

## No output or no port

Check the data cable and data/debug port; close other serial tools. Use the display's power supply and install any required USB driver.

For saved crash logs, see [Screenshot & Diagnostics](faq.md#the-display-crashed-or-restarted-by-itself).
