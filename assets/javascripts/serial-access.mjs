// The log reader and flasher share one document after an internal page change.
// Keep ownership exclusive until the current operation has released its port.
export class SerialAccess {
  constructor() {
    this.owner = null;
    this.installerPending = false;
    this.disconnectLogs = null;
    this.listeners = new Set();
  }

  acquire(owner) {
    if (this.owner !== null || (owner === "logs" && this.installerPending)) return false;
    this.owner = owner;
    this.notify();
    return true;
  }

  release(owner) {
    if (this.owner !== owner) return;
    this.owner = null;
    this.notify();
  }

  async requestInstallerPort(serial) {
    if (this.installerPending || this.owner === "installer") throw new Error("The installer is already using USB.");
    this.installerPending = true;
    this.notify();
    try {
      // Open the chooser in the click gesture. Cancelling it leaves capture
      // untouched; only a selected port triggers the awaited reader cleanup.
      const port = await serial.requestPort();
      if (this.owner === "logs") await this.disconnectLogs();
      if (!this.acquire("installer")) throw new Error("The serial connection is still in use.");
      return port;
    } finally {
      this.installerPending = false;
      this.notify();
    }
  }

  subscribe(listener) {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  notify() {
    for (const listener of this.listeners) listener(this.owner);
  }
}

export const serialAccess = new SerialAccess();
