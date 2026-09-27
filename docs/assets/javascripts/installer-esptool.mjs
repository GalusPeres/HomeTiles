import { assertSupportedEspRomChip } from "./installer-contract.mjs?v=installer-ui-16";

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
