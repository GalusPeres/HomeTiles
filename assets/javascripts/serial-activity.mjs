// Two current snapshots, with no history, polling or device queries.
export class SerialActivity {
  constructor() {
    this.entries = new Map();
    this.listeners = new Set();
  }

  set(source, label, kind = "idle", active = false) {
    const previous = this.entries.get(source);
    if (previous?.label === label && previous.kind === kind && previous.active === active) return;
    this.entries.delete(source);
    this.entries.set(source, { source, label, kind, active });
    for (const listener of this.listeners) listener(this.current);
  }

  get current() {
    const entries = [...this.entries.values()];
    return entries.find(entry => entry.source === "installer" && entry.active)
      || entries.find(entry => entry.active)
      || entries.at(-1)
      || { source: "logs", label: "USB disconnected", kind: "idle", active: false };
  }

  clear(source) {
    if (!this.entries.delete(source)) return;
    for (const listener of this.listeners) listener(this.current);
  }

  subscribe(listener) {
    this.listeners.add(listener);
    listener(this.current);
    return () => this.listeners.delete(listener);
  }
}

export const serialActivity = new SerialActivity();
