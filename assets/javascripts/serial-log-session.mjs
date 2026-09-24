export const SERIAL_LOG_OPTIONS = Object.freeze({
  baudRate: 115200,
  dataBits: 8,
  stopBits: 1,
  parity: "none",
  flowControl: "none",
  bufferSize: 8192,
});

// Own the reader until cancellation completes, then close the port before
// allowing another connection. No writes, bootloader commands or reset signals.
export class SerialLogSession {
  constructor({ serial, onText, onState, access = null }) {
    this.serial = serial;
    this.onText = onText;
    this.onState = onState;
    this.access = access;
    this.active = false;
    this.reader = null;
    this.done = Promise.resolve();
  }

  connect() {
    if (this.active) return this.done;
    if (this.access && !this.access.acquire("logs")) {
      this.onState("busy");
      return this.done;
    }
    this.active = true;
    this.stopping = false;
    this.cancellation = null;
    this.failure = null;
    this.onState("connecting");
    this.done = this.readSession();
    return this.done;
  }

  async disconnect() {
    if (!this.active) return this.done;
    if (this.stopping) return this.done;
    this.stopping = true;
    this.onState("disconnecting");
    // Unplugging can reject cancellation; the read loop still owns cleanup.
    this.cancellation = this.reader?.cancel().catch(() => {});
    await this.cancellation;
    return this.done;
  }

  async readSession() {
    let port;
    let opened = false;
    let result = "disconnected";
    let failure;
    const decoder = new TextDecoder();
    try {
      // Keep this within the Connect gesture. Native CDC and USB-UART devices
      // must both be selectable, including vendor IDs not known to the flasher.
      port = await this.serial.requestPort();
      if (this.stopping) return;
      await port.open(SERIAL_LOG_OPTIONS);
      opened = true;
      if (this.stopping) return;
      if (!port.readable) throw new Error("The selected port has no readable stream.");
      this.reader = port.readable.getReader();
      this.onState("connected");
      while (!this.stopping) {
        const { value, done } = await this.reader.read();
        if (done) {
          if (!this.stopping) result = "ended";
          break;
        }
        if (value?.length) this.onText(decoder.decode(value, { stream: true }));
      }
    } catch (error) {
      if (!this.stopping) {
        result = error?.name === "NotFoundError" && !opened ? "cancelled" : "error";
        failure = error;
      }
    } finally {
      const tail = decoder.decode();
      if (tail) this.onText(tail);
      await this.cancellation;
      this.reader?.releaseLock();
      this.reader = null;
      if (opened) {
        try {
          await port.close();
        } catch (error) {
          result = "error";
          failure = error;
        }
      }
      this.active = false;
      this.failure = failure;
      this.access?.release("logs");
      this.onState(result, failure);
    }
  }
}
