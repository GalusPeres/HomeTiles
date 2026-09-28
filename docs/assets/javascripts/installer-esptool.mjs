import { assertSupportedEspRomChip } from "./installer-contract.mjs?v=installer-ui-17";

const READ_FLASH_DIGEST_LENGTH = 16;

// HomeTiles adjustments to esptool-js 0.7.0. The installer passes the bundle it
// loads from unpkg; host tests pass the identical npm release, so both run the
// same esptool-js code through these subclasses.
export function defineHomeTilesEsptool({ ESPLoader, Transport }) {
  class HomeTilesTransport extends Transport {
    // esptool-js 0.7.0 records DTR in setSignals() only when it is asserted.
    // ClassicReset ends with setSignals(false, false), so the recorded DTR stays
    // true and every later setRTS() replays DTR=true. On USB-UART bridges that
    // holds GPIO0 low during hard_reset instead of releasing it as 0.6.1 did.
    async setSignals(dtr, rts, breakSignal) {
      await super.setSignals(dtr, rts, breakSignal);
      this._DTR_state = dtr === true;
    }
  }

  class HomeTilesESPLoader extends ESPLoader {
    // After the READ_FLASH data the stub sends a 16-byte MD5 frame. esptool.py
    // reads it before the next command; esptool-js 0.7.0 readFlash() returns
    // without it. On USB-UART bridges (CH340, CH343) the next command then
    // often got no answer at all: the OTA-data read right after the
    // partition-table read timed out after 3 s on the Guition S3 and the
    // Waveshare 8-inch, while native USB passed. Read the frame first.
    async readFlash(addr, size, onPacketReceived = null) {
      const data = await super.readFlash(addr, size, onPacketReceived);
      const digest = await this.transport.read(this.DEFAULT_TIMEOUT);
      if (!(digest instanceof Uint8Array) || digest.length !== READ_FLASH_DIGEST_LENGTH) {
        throw new Error(
          `Flash read at 0x${addr.toString(16)} ended without the expected ${READ_FLASH_DIGEST_LENGTH}-byte digest.`,
        );
      }
      return data;
    }

    // esptool-js 0.7.0 identifies the chip from the GET_SECURITY_INFO chip ID
    // itself. HomeTiles still requires that ROM identity (no magic-register
    // fallback), accepts only its chip families and stops before the stub
    // upload when the ROM runs in Secure Download Mode.
    async detectChip(mode = "default_reset", attempts = 7) {
      await super.detectChip(mode, attempts);
      const securityInfo = await this.getSecurityInfo();
      assertSupportedEspRomChip({
        chipId: securityInfo.chipId,
        chipName: this.chip?.CHIP_NAME,
        secureDownloadMode: this.secureDownloadMode === true,
      });
    }

    // When the chip does not answer at the installer baud rate, esptool-js
    // 0.7.0 resets it, reconnects at the ROM baud rate and restarts the stub,
    // but skips postConnect(). ESP32-P4 v3.1/v3.2 power the flash off on that
    // reset, so the post-connect sequence has to run again.
    async changeBaud() {
      const requestedBaudrate = this.baudrate;
      await super.changeBaud();
      if (this.baudrate !== requestedBaudrate && typeof this.chip?.postConnect === "function") {
        await this.chip.postConnect(this);
      }
    }
  }

  return { HomeTilesESPLoader, HomeTilesTransport };
}
