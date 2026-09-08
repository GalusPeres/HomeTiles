// Browser-only hardware doubles. Production page controls, navigation,
// firmware validation and flash planning execute without substitutions.
export const setupSerial = `
window.fixture = { requests: 0, closes: 0, opened: false, events: [], writes: 0, cancelChooser: false };
Object.defineProperty(navigator, 'serial', { configurable: true, value: {
  async requestPort() {
    fixture.requests++;
    if (fixture.cancelChooser) throw new DOMException('No selection', 'NotFoundError');
    let readable;
    return {
      get readable() { return readable; },
      async open() {
        if (fixture.opened) throw new Error('A serial owner still holds the port');
        fixture.opened = true;
        fixture.events.push('log-open');
        readable = new ReadableStream({
          start(controller) { fixture.controller = controller; },
          async cancel() {
            fixture.events.push('log-cancel');
            await new Promise(resolve => setTimeout(resolve, 40));
            fixture.events.push('log-cancelled');
          }
        });
      },
      async close() {
        if (readable.locked) throw new Error('Reader lock leaked');
        fixture.opened = false;
        fixture.closes++;
        fixture.events.push('log-close');
      }
    };
  }
} });
fixture.emit = text => fixture.controller.enqueue(new TextEncoder().encode(text));
`;

export const esptool = `
import { REQUIRED_PARTITIONS, PARTITION_TABLE, OTA_DATA_LAYOUT } from '/preview/assets/javascripts/installer-contract.mjs';
const ota = new Uint8Array(OTA_DATA_LAYOUT.size).fill(255);
export class Transport {
  constructor() {
    if (fixture.opened) throw new Error('Flasher connected before the reader closed');
    fixture.events.push('flash-open');
  }
  async disconnect() { fixture.events.push('flash-close'); }
}
export class ESPLoader {
  constructor({ terminal }) { this.terminal = terminal; }
  async main() { this.chip = { CHIP_NAME: fixture.wrongChip ? 'ESP32-P4' : 'ESP32-S3' }; return this.chip.CHIP_NAME; }
  async flashId() {}
  async detectFlashSize() { return '16MB'; }
  async readFlash(offset) {
    if (offset === PARTITION_TABLE.offset) {
      const bytes = new Uint8Array(PARTITION_TABLE.size).fill(255);
      const view = new DataView(bytes.buffer);
      REQUIRED_PARTITIONS.forEach((entry, index) => {
        const start = index * 32;
        view.setUint16(start, 0x50aa, true);
        bytes[start + 2] = entry.type; bytes[start + 3] = entry.subtype;
        view.setUint32(start + 4, entry.offset, true); view.setUint32(start + 8, entry.size, true);
        bytes.fill(0, start + 12, start + 28);
        bytes.set(new TextEncoder().encode(entry.label), start + 12);
      });
      return bytes;
    }
    return ota.slice();
  }
  async writeFlash({ fileArray, reportProgress }) {
    fixture.writes++;
    const file = fileArray[0];
    this.terminal.writeLine('Simulated flash write');
    if (file.address >= 0x10000) {
      reportProgress(0, 1, 2);
      await new Promise(resolve => { fixture.finishWrite = resolve; });
    } else {
      ota.set(file.data, file.address - OTA_DATA_LAYOUT.offset);
    }
    reportProgress(0, 1, 1);
  }
  async after() { fixture.events.push('flash-reset'); }
  async eraseFlash() { throw new Error('An Update must not erase the device'); }
}
`;
