import { SerialLogBuffer } from "./serial-log-buffer.mjs?v=device-logs-2";
import { SerialLogSession } from "./serial-log-session.mjs?v=device-logs-5";
import { SerialLogStore } from "./serial-log-store.mjs?v=device-logs-2";
import { retainPageComponent } from "./retained-page-component.mjs?v=serial-navigation-2";
import { serialAccess } from "./serial-access.mjs?v=serial-navigation-2";
import { serialActivity } from "./serial-activity.mjs?v=serial-navigation-3";

export function mountDeviceLogs(root, { browser = window, platform = navigator } = {}) {
  const find = (name) => root.querySelector(`[data-log-${name}]`);
  const elements = Object.fromEntries(
    ["connect", "disconnect", "copy", "clear", "follow", "output", "status", "action", "limit", "storage", "serial"]
      .map((name) => [name, find(name)]),
  );
  const buffer = new SerialLogBuffer();
  const store = new SerialLogStore(browser);
  let dirty = false;
  let renderTimer = null;
  let actionTimer = null;
  let disposed = false;
  let captureDetached = false;
  const supported = browser.isSecureContext && !!platform.serial;

  function restoreLog() {
    const snapshot = store.load();
    // Retain an in-memory copy on a back/forward-cache restore if storage fails.
    if (snapshot !== undefined) {
      if (hasLogSelection()) root.ownerDocument.getSelection().removeAllRanges();
      buffer.clear();
      buffer.resetStream();
      if (snapshot) {
        buffer.append(snapshot.text);
        buffer.truncated ||= snapshot.truncated;
      }
      dirty = false;
      setAction(buffer.length ? "Previous log restored." : "");
    }
    elements.storage.hidden = !store.failed;
  }

  function saveLog() {
    if (dirty && store.save(buffer)) dirty = false;
    elements.storage.hidden = !store.failed;
  }

  restoreLog();

  function status(message, kind = "info") {
    elements.status.textContent = message;
    elements.status.dataset.kind = kind;
  }

  function setAction(message, persistent = false) {
    if (actionTimer !== null) browser.clearTimeout(actionTimer);
    actionTimer = null;
    elements.action.textContent = message;
    if (message && !persistent) actionTimer = browser.setTimeout(() => {
      actionTimer = null;
      elements.action.textContent = "";
    }, 2500);
  }

  function render() {
    renderTimer = null;
    elements.copy.disabled = elements.clear.disabled = !buffer.length;
    elements.limit.hidden = !buffer.truncated;
    // Keep a manual copy selection stable while capture continues in the buffer.
    if (hasLogSelection()) return;
    const text = buffer.text;
    if (elements.output.textContent !== text) {
      const top = elements.output.scrollTop;
      elements.output.textContent = text;
      elements.output.scrollTop = elements.follow.checked ? elements.output.scrollHeight : top;
    }
  }

  function hasLogSelection() {
    const selection = root.ownerDocument.getSelection();
    return selection && !selection.isCollapsed && elements.output.contains(selection.anchorNode);
  }

  function scheduleRender() {
    // At most ten DOM updates per second, with no work while the port is idle.
    if (!disposed && root.isConnected && renderTimer === null) renderTimer = browser.setTimeout(render, 100);
  }

  const session = new SerialLogSession({
    serial: platform.serial,
    access: serialAccess,
    onText(text) {
      // The pagehide snapshot is final. A cached old page must not overwrite a
      // newer capture when its cancelled reader finishes during back navigation.
      if (captureDetached) return;
      buffer.append(text);
      dirty ||= !!text;
      scheduleRender();
    },
    onState(state, error) {
      if (disposed) return;
      elements.connect.disabled = !supported || session.active || serialAccess.owner === "installer" || serialAccess.installerPending;
      elements.disconnect.disabled = state !== "connected";
      if (state === "connecting") status("Choose your display's USB serial port.");
      else if (state === "connected") {
        captureDetached = false;
        buffer.resetStream();
        buffer.append(`${buffer.length ? "\n" : ""}--- Serial capture: ${new Date().toISOString()} (115200 baud) ---\n`);
        dirty = true;
        status("Connected at 115200 baud.", "success");
      } else if (state === "disconnecting") status("Disconnecting…");
      else if (state === "busy") status("Wait for firmware flashing to finish before connecting.", "warning");
      else if (state === "cancelled") status("No port selected. Connect when ready.");
      else if (state === "ended") status("USB connection ended. Reconnect to continue.", "warning");
      else if (state === "error") {
        status(`Connection stopped: ${error?.message || "Serial port unavailable."} Close other serial tools, then reconnect.`, "error");
      } else status("Disconnected. Your captured log is still available to copy.");
      if (state === "connected") serialActivity.set("logs", "Logs active", "success", true);
      else if (state === "connecting") serialActivity.set("logs", "Choose USB port", "busy", true);
      else if (state === "disconnecting") serialActivity.set("logs", "Disconnecting…", "busy", true);
      else if (state === "error" || state === "ended") serialActivity.set("logs", "USB connection lost", "error");
      else if (state !== "busy") serialActivity.set("logs", "USB disconnected");
      if (!session.active) saveLog();
      scheduleRender();
    },
  });

  serialAccess.disconnectLogs = async () => {
    await session.disconnect();
    if (session.failure) throw new Error("The log reader could not close its USB connection. Reconnect the device before flashing.");
  };

  serialAccess.subscribe(() => {
    elements.connect.disabled = !supported || session.active || serialAccess.owner === "installer" || serialAccess.installerPending;
    elements.serial.hidden = serialAccess.owner !== "installer" && !serialAccess.installerPending;
  });

  elements.connect.addEventListener("click", () => {
    if (!supported || disposed) return;
    setAction("");
    void session.connect();
  });
  elements.disconnect.addEventListener("click", () => void session.disconnect());
  elements.clear.addEventListener("click", () => {
    if (hasLogSelection()) root.ownerDocument.getSelection().removeAllRanges();
    buffer.clear();
    dirty = true;
    saveLog();
    setAction("Log cleared.");
    scheduleRender();
  });
  elements.copy.addEventListener("click", async () => {
    const text = buffer.text;
    if (!text) return;
    try {
      if (!platform.clipboard?.writeText) throw new Error("Clipboard unavailable");
      await platform.clipboard.writeText(
        (buffer.truncated ? "[Log viewer: older output or long lines were trimmed.]\n" : "") + text,
      );
      setAction("Log copied.");
    } catch {
      if (renderTimer !== null) browser.clearTimeout(renderTimer);
      render();
      elements.follow.checked = false;
      const selection = root.ownerDocument.getSelection();
      const range = root.ownerDocument.createRange();
      range.selectNodeContents(elements.output);
      selection.removeAllRanges();
      selection.addRange(range);
      elements.output.focus();
      setAction("Press Ctrl+C or Command+C to copy the selected log.", true);
    }
  });
  elements.output.addEventListener("scroll", () => {
    const remaining = elements.output.scrollHeight - elements.output.scrollTop - elements.output.clientHeight;
    if (remaining > 8) elements.follow.checked = false;
  });
  elements.follow.addEventListener("change", () => {
    if (elements.follow.checked) elements.output.scrollTop = elements.output.scrollHeight;
  });
  root.ownerDocument.addEventListener("selectionchange", () => {
    if (!hasLogSelection()) scheduleRender();
  });

  const dispose = () => {
    if (disposed) return;
    saveLog();
    disposed = true;
    serialActivity.set("logs", "USB disconnected");
    captureDetached = true;
    setAction("");
    if (renderTimer !== null) browser.clearTimeout(renderTimer);
    renderTimer = null;
    void session.disconnect();
  };
  browser.addEventListener("pagehide", dispose);
  root.ownerDocument.addEventListener("visibilitychange", () => {
    if (root.ownerDocument.visibilityState === "hidden") saveLog();
  });
  browser.addEventListener("pageshow", () => {
    // A page restored from the back/forward cache must not retain a USB session.
    if (!disposed) return;
    restoreLog();
    disposed = false;
    elements.connect.disabled = !supported || session.active || serialAccess.owner === "installer" || serialAccess.installerPending;
    elements.disconnect.disabled = true;
    if (supported) status("Connect to read your display's live log at 115200 baud.");
    scheduleRender();
  });

  elements.connect.disabled = !supported || serialAccess.owner === "installer" || serialAccess.installerPending;
  elements.serial.hidden = serialAccess.owner !== "installer" && !serialAccess.installerPending;
  if (!browser.isSecureContext) status("Open this page over HTTPS or on localhost to use USB serial.", "error");
  else if (!platform.serial) status("Use desktop Google Chrome or Microsoft Edge to read USB serial logs.", "error");
  else status("Connect to read your display's live log at 115200 baud.");
  render();
  return { session, dispose, pageChanged(visible) {
    if (visible) scheduleRender();
    else {
      saveLog();
      if (renderTimer !== null) browser.clearTimeout(renderTimer);
      renderTimer = null;
    }
  } };
}

retainPageComponent("[data-device-logs]", mountDeviceLogs);
