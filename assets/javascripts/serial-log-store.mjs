import { LOG_LIMITS } from "./serial-log-buffer.mjs?v=device-logs-2";

export const SERIAL_LOG_STORAGE_KEY = "hometiles.deviceLogs.session.v1";

// Keep one bounded snapshot per browser tab. Storage is only accessed at
// lifecycle checkpoints, never for each incoming serial chunk or render.
export class SerialLogStore {
  constructor(browser) {
    this.browser = browser;
    this.failed = false;
  }

  load() {
    try {
      const raw = this.browser.sessionStorage.getItem(SERIAL_LOG_STORAGE_KEY);
      this.failed = false;
      if (raw === null) return null;
      // JSON escapes can use six characters per retained UTF-16 code unit.
      if (raw.length > LOG_LIMITS.characters * 6 + 100) throw new Error("Oversized log snapshot");
      const snapshot = JSON.parse(raw);
      if (snapshot?.version !== 1 || typeof snapshot.text !== "string" ||
          snapshot.text.length > LOG_LIMITS.characters || typeof snapshot.truncated !== "boolean") {
        throw new Error("Invalid log snapshot");
      }
      return snapshot;
    } catch {
      this.failed = true;
      return undefined;
    }
  }

  save(buffer) {
    try {
      const storage = this.browser.sessionStorage;
      if (!buffer.length) storage.removeItem(SERIAL_LOG_STORAGE_KEY);
      else storage.setItem(SERIAL_LOG_STORAGE_KEY, JSON.stringify({
        version: 1, text: buffer.text, truncated: buffer.truncated,
      }));
      this.failed = false;
      return true;
    } catch {
      this.failed = true;
      return false;
    }
  }
}
