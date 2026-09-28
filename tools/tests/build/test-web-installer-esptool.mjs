// Runs the esptool-js release that the web installer loads from unpkg (the
// pinned npm devDependency is the same published package) through the
// HomeTiles loader and transport subclasses. Only the serial link and chip
// registers are simulated; esptool-js main(), chip classes and resets are real.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import { defineHomeTilesEsptool } from "../../../docs/assets/javascripts/installer-esptool.mjs";

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const read = (relativePath) => fs.readFileSync(path.join(repositoryRoot, relativePath), "utf8");
const packageDirectory = path.join(repositoryRoot, "node_modules/esptool-js");
if (!fs.existsSync(path.join(packageDirectory, "bundle.js"))) {
  throw new Error("esptool-js is missing; run npm ci --ignore-scripts before the tests.");
}

const installedVersion = JSON.parse(fs.readFileSync(path.join(packageDirectory, "package.json"), "utf8")).version;
const installerVersion = read("docs/assets/javascripts/installer.mjs")
  .match(/https:\/\/unpkg\.com\/esptool-js@([0-9.]+)\/bundle\.js/)?.[1];
assert.equal(installerVersion, "0.7.0");
assert.equal(installedVersion, installerVersion, "Tests must run the esptool-js version the installer loads.");
assert.equal(JSON.parse(read("package.json")).devDependencies["esptool-js"], installerVersion);

const esptool = await import(pathToFileURL(path.join(packageDirectory, "bundle.js")).href);
const { HomeTilesESPLoader, HomeTilesTransport } = defineHomeTilesEsptool(esptool);

const CH340 = { usbVendorId: 0x1a86, usbProductId: 0x7523 };
const P4_EFUSE_REVISION_REG = 0x5012d044 + 4 * 2;
const P4_EFUSE_RD_REPEAT_DATA1_REG = 0x5012d000 + 0x034;
const P4_EFUSE_DOWNLOAD_MODE_XPD_ON = 1 << 16;
const P4_LP_SYSTEM_REG_ANA_XPD_PAD_GROUP_REG = 0x50110000 + 0x10c;
const P4_PMU_DATE_REG = 0x50115000 + 0x3fc;
const P4_PMU_DATE_FLASH_FORCE_ON = 0x3;

function p4RevisionWord(revision) {
  const major = Math.floor(revision / 100);
  const minor = revision % 100;
  return (((major >> 2) & 1) << 23) | ((major & 0x3) << 4) | (minor & 0xf);
}

class FakeSerialPort {
  constructor(info = CH340) {
    this.info = info;
    this.calls = [];
    this.dtr = false;
    this.rts = false;
  }
  getInfo() {
    return this.info;
  }
  async setSignals(signals) {
    this.calls.push({ ...signals });
    if (signals.dataTerminalReady !== undefined) this.dtr = signals.dataTerminalReady;
    if (signals.requestToSend !== undefined) this.rts = signals.requestToSend;
  }
}

const silentTerminal = { clean() {}, write() {}, writeLine() {} };

function simulatedLoader({ chipId, registers = {}, baudrate = 115200, secureDownloadMode = false }) {
  const events = [];
  const writes = [];
  const values = new Map(Object.entries(registers).map(([address, value]) => [Number(address), value >>> 0]));
  const transport = new HomeTilesTransport(new FakeSerialPort());
  const loader = new HomeTilesESPLoader({ transport, baudrate, terminal: silentTerminal, debugLogging: false });
  loader.openAndSync = async () => {
    loader.securityInfoCache = null;
    events.push("sync");
  };
  loader.getSecurityInfo = async () => ({
    flags: 0,
    flashCryptCnt: 0,
    keyPurposes: [],
    chipId,
    apiVersion: 1,
    parsedFlags: { SECURE_DOWNLOAD_ENABLE: secureDownloadMode },
  });
  loader.readReg = async (address) => values.get(address >>> 0) ?? 0;
  loader.writeReg = async (address, value) => {
    values.set(address >>> 0, value >>> 0);
    writes.push({ address: address >>> 0, value: value >>> 0 });
    events.push(`write:${(address >>> 0).toString(16)}`);
  };
  loader.runStub = async () => {
    loader.IS_STUB = true;
    events.push("stub");
    return loader.chip;
  };
  loader.readFlashId = async () => {
    events.push("flash-id");
    return 0x184068;
  };
  return { loader, transport, events, writes, values };
}

const flashPowerWrites = (writes) =>
  writes.filter(({ address }) =>
    address === P4_LP_SYSTEM_REG_ANA_XPD_PAD_GROUP_REG || address === P4_PMU_DATE_REG);

// ESP32-P4 v3.1 and v3.2 boot with the flash powered off; 0.7.0 must power it on
// before the stub and the first flash command (esptool-js PR #268).
for (const revision of [301, 302]) {
  const { loader, events, writes, values } = simulatedLoader({
    chipId: 18,
    registers: { [P4_EFUSE_REVISION_REG]: p4RevisionWord(revision) },
  });
  assert.equal(await loader.main(), `ESP32-P4 (revision v3.${revision % 100})`);
  assert.equal(loader.chip.CHIP_NAME, "ESP32-P4");
  assert.equal(await loader.chip.getChipRevision(loader), revision);
  assert.equal(loader.IS_STUB, true);
  assert.ok(
    writes.some(({ address, value }) => address === P4_LP_SYSTEM_REG_ANA_XPD_PAD_GROUP_REG && value === 1),
    `ESP32-P4 v3.${revision % 100} must power up the flash pad group.`,
  );
  assert.equal(
    values.get(P4_PMU_DATE_REG) & P4_PMU_DATE_FLASH_FORCE_ON,
    P4_PMU_DATE_FLASH_FORCE_ON,
    `ESP32-P4 v3.${revision % 100} must force the flash supply on.`,
  );
  const lastPowerWrite = events.lastIndexOf(`write:${P4_PMU_DATE_REG.toString(16)}`);
  assert.ok(lastPowerWrite >= 0 && lastPowerWrite < events.indexOf("stub"), "Flash power-on precedes the stub.");
  assert.ok(events.indexOf("stub") < events.indexOf("flash-id"), "The flash is read only after power-on.");
}

// ECO7 with the download-mode XPD eFuse: the ROM already powered the flash, so
// esptool-js only releases the force-on bits instead of repeating the sequence.
{
  const { loader, writes, values } = simulatedLoader({
    chipId: 18,
    registers: {
      [P4_EFUSE_REVISION_REG]: p4RevisionWord(302),
      [P4_EFUSE_RD_REPEAT_DATA1_REG]: P4_EFUSE_DOWNLOAD_MODE_XPD_ON,
      [P4_PMU_DATE_REG]: 0x2025_0000 | P4_PMU_DATE_FLASH_FORCE_ON,
    },
  });
  await loader.main();
  assert.deepEqual(flashPowerWrites(writes), [{ address: P4_PMU_DATE_REG, value: 0x2025_0000 }]);
  assert.equal(values.get(P4_PMU_DATE_REG) & P4_PMU_DATE_FLASH_FORCE_ON, 0);
}

// ESP32-P4 before v3 (and v3.0) keeps the 0.6.1 behavior: no flash power writes
// and the pre-v3 stub.
for (const revision of [100, 300]) {
  const { loader, writes } = simulatedLoader({
    chipId: 18,
    registers: { [P4_EFUSE_REVISION_REG]: p4RevisionWord(revision) },
  });
  await loader.main();
  assert.equal(await loader.chip.getChipRevision(loader), revision);
  assert.deepEqual(flashPowerWrites(writes), [], `ESP32-P4 revision ${revision} must not touch flash power.`);
}
assert.equal((await esptool.getStubJsonByChipName("ESP32-P4", 100)).text_start, 0x4ff10000);
assert.equal((await esptool.getStubJsonByChipName("ESP32-P4", 301)).text_start, 0x4ff50000);

// ESP32-S3 is identified from the same ROM chip ID and has no flash power step.
{
  const { loader, writes, events } = simulatedLoader({ chipId: 9 });
  await loader.main();
  assert.equal(loader.chip.CHIP_NAME, "ESP32-S3");
  assert.deepEqual(writes, []);
  assert.deepEqual(events, ["sync", "stub", "flash-id"]);
}

// Unsupported chips and Secure Download Mode stop before the stub upload.
for (const [options, pattern] of [
  [{ chipId: 13 }, /Unsupported ESP ROM chip ID 13/],
  [{ chipId: 9, secureDownloadMode: true }, /ESP32-S3 is in Secure Download Mode/],
  [{ chipId: 18, secureDownloadMode: true }, /ESP32-P4 is in Secure Download Mode/],
]) {
  const { loader, events } = simulatedLoader(options);
  await assert.rejects(loader.main(), pattern);
  assert.ok(!events.includes("stub"), "Rejected chips must not receive the flasher stub.");
}

// A failed installer baud rate makes 0.7.0 reset and reconnect at 115200. That
// reset powers the v3.1 flash off again, so HomeTiles repeats postConnect().
{
  const { loader, transport, events, writes } = simulatedLoader({
    chipId: 18,
    baudrate: 460800,
    registers: { [P4_EFUSE_REVISION_REG]: p4RevisionWord(301) },
  });
  loader.command = async () => [0, new Uint8Array(0)];
  transport.changeBaudrate = async () => {
    throw new Error("simulated host baud-rate failure");
  };
  transport.drainInput = async () => {};
  transport.disconnect = async () => {};
  await loader.main();
  assert.equal(loader.baudrate, 115200);
  assert.deepEqual(events.filter((event) => event === "sync" || event === "stub"), ["sync", "stub", "sync", "stub"]);
  const padGroupWrites = writes.filter(({ address }) => address === P4_LP_SYSTEM_REG_ANA_XPD_PAD_GROUP_REG);
  assert.equal(padGroupWrites.length, 2, "The flash must be powered on again after the fallback reset.");
  const secondStub = events.lastIndexOf("stub");
  const lastPowerWrite = events.lastIndexOf(`write:${P4_PMU_DATE_REG.toString(16)}`);
  assert.ok(secondStub < lastPowerWrite && lastPowerWrite < events.indexOf("flash-id"));
}

// A working baud-rate change does not repeat postConnect().
{
  const { loader, transport, writes } = simulatedLoader({
    chipId: 18,
    baudrate: 460800,
    registers: { [P4_EFUSE_REVISION_REG]: p4RevisionWord(301) },
  });
  loader.command = async () => [0, new Uint8Array(0)];
  transport.changeBaudrate = async () => true;
  transport.drainInput = async () => {};
  await loader.main();
  assert.equal(loader.baudrate, 460800);
  assert.equal(writes.filter(({ address }) => address === P4_LP_SYSTEM_REG_ANA_XPD_PAD_GROUP_REG).length, 1);
}

// READ_FLASH: the stub follows the data with a 16-byte MD5 frame. HomeTiles reads
// it before returning, so the next command cannot overtake it on USB-UART
// bridges (OTA-data read timeouts on CH340/CH343, 2026-09-28).
function readFlashLoader(LoaderClass, frames) {
  const transport = new HomeTilesTransport(new FakeSerialPort());
  const queue = [...frames];
  const acks = [];
  transport.read = async () => {
    if (!queue.length) throw new Error("No serial data received.");
    return queue.shift();
  };
  transport.write = async (bytes) => {
    acks.push(bytes);
  };
  const loader = new LoaderClass({ transport, baudrate: 460800, terminal: silentTerminal });
  loader.checkCommand = async () => 0;
  return { loader, queue, acks };
}
{
  const data = new Uint8Array(0x2000).map((_, index) => index & 0xff);
  const frames = () => [data.slice(0, 0x1000), data.slice(0x1000), new Uint8Array(16).fill(0xab)];

  const { loader, queue, acks } = readFlashLoader(HomeTilesESPLoader, frames());
  assert.deepEqual(await loader.readFlash(0xd000, 0x2000), data);
  assert.equal(acks.length, 2, "Every data packet is acknowledged.");
  assert.equal(queue.length, 0, "The digest frame must be read before the next command.");

  const upstream = readFlashLoader(esptool.ESPLoader, frames());
  await upstream.loader.readFlash(0xd000, 0x2000);
  assert.equal(
    upstream.queue.length,
    1,
    "Upstream esptool-js now reads the READ_FLASH digest; the HomeTiles readFlash override can be removed.",
  );

  const truncated = readFlashLoader(HomeTilesESPLoader, [data.slice(0, 0x1000), data.slice(0x1000), new Uint8Array(4)]);
  await assert.rejects(truncated.loader.readFlash(0xd000, 0x2000), /expected 16-byte digest/);
}

// UART bridges: ClassicReset (connect) followed by hard_reset must release
// GPIO0 like 0.6.1. Upstream 0.7.0 replays a stale DTR=true in setRTS().
async function hardResetAfterConnect(TransportClass) {
  const port = new FakeSerialPort();
  const transport = new TransportClass(port);
  const loader = new HomeTilesESPLoader({ transport, baudrate: 115200, terminal: silentTerminal });
  loader.readReg = async () => {
    throw new Error("hard_reset must not read registers");
  };
  await new esptool.ClassicReset(transport, 1).reset();
  assert.deepEqual(port.calls, [
    { dataTerminalReady: false, requestToSend: true, break: undefined },
    { dataTerminalReady: true, requestToSend: false, break: undefined },
    { dataTerminalReady: false, requestToSend: false, break: undefined },
  ]);
  port.calls.length = 0;
  await loader.after("hard_reset", false);
  return port;
}
{
  const port = await hardResetAfterConnect(HomeTilesTransport);
  assert.deepEqual(port.calls, [{ requestToSend: false }, { dataTerminalReady: false }]);
  assert.equal(port.dtr, false, "GPIO0 must be released after hard_reset.");
  assert.equal(port.rts, false);
  const upstream = await hardResetAfterConnect(esptool.Transport);
  assert.equal(
    upstream.dtr,
    true,
    "Upstream esptool-js now tracks DTR in setSignals(); HomeTilesTransport can be removed.",
  );
}

// Guition ESP32-4848S040 (CH340): the custom normal-boot sequence stays an EN
// pulse with GPIO0 released.
{
  const port = new FakeSerialPort();
  const transport = new HomeTilesTransport(port);
  const loader = new HomeTilesESPLoader({ transport, baudrate: 115200, terminal: silentTerminal });
  await new esptool.ClassicReset(transport, 1).reset();
  port.calls.length = 0;
  const sequence = read("docs/assets/javascripts/installer.mjs")
    .match(/GUITION_S3_NORMAL_BOOT_RESET_SEQUENCE = "([^"]+)"/)?.[1];
  assert.equal(sequence, "D0|R1|W100|R0|W100|D0");
  await loader.after("custom_reset", undefined, sequence);
  assert.deepEqual(port.calls, [
    { dataTerminalReady: false },
    { requestToSend: true },
    { dataTerminalReady: false },
    { requestToSend: false },
    { dataTerminalReady: false },
    { dataTerminalReady: false },
  ]);
  assert.equal(port.dtr, false);
  assert.equal(port.rts, false);
}

console.log("Web installer esptool-js 0.7.0 integration checks passed.");
