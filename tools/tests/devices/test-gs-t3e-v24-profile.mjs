// Golden Security GS-T3E PCB v2.4 (PR #137): its own folder, a copy of the
// GS-T3E v2 (PCB v2.3) driver. The contributed copy had lost the end of
// migrateStorageFromSD() (the /_migrated flag and storageWriteEnd()): every
// start migrated again and the storage guard stayed open, so later flash
// writes skipped the display protection. The driver must stay the v2 driver
// apart from its names; the board differs only in its third relay (GPIO2
// instead of GPIO44) and the missing USB socket. Every shared list that names
// the v2 names the v2.4 too, and nothing else of the v2 changes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {getBuildProfile} from '../../device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const V2 = 'DEVICE_GOLDEN_SECURITY_GS_T3E_V2';
const V24 = 'DEVICE_GOLDEN_SECURITY_GS_T3E_V24';
const v2Dir = 'src/devices/golden_security_gs_t3e_v2';
const v24Dir = 'src/devices/golden_security_gs_t3e_v24';

// ---- The driver is the v2 driver -------------------------------------------------
// Names, the define and comments aside, every line is the v2's.
const code = text => text
  .replace(/\/\/[^\n]*/g, '')
  .replace(/golden_security_gs_t3e_v24?/g, 'T3E')
  .replace(/GoldenSecurityGST3EV24?/g, 'T3E')
  .replace(/GOLDEN_SECURITY_GS_T3E_V24?/g, 'T3E')
  .replace(/"GS-T3E v2(\.4)?"/g, '"T3E"')
  .replace(/"Golden Security GS-T3E v2(\.4)?"/g, '"T3E"')
  .split('\n').map(line => line.trim()).filter(Boolean).join('\n');
for (const file of ['device_golden_security_gs_t3e_v2.cpp', 'device_golden_security_gs_t3e_v2.h',
  'st7701_s3_board.h', 'st7701_s3_driver.h']) {
  const v24File = file.replace('_v2.', '_v24.');
  assert.equal(code(read(`${v24Dir}/${v24File}`)), code(read(`${v2Dir}/${file}`)),
    `${v24File} is the v2 driver apart from its names`);
}
const driver = read(`${v24Dir}/device_golden_security_gs_t3e_v24.cpp`);
assert.match(driver, /void DeviceGoldenSecurityGST3EV24::migrateStorageFromSD\(\) \{[\s\S]*?File flag = LittleFS\.open\("\/_migrated", FILE_WRITE\);[\s\S]*?storageWriteEnd\(\);\n\}/,
  'the migration runs once and closes the storage guard it opened');

// ---- Relays (ESPHome's PCB v2.4 page: GPIO45, GPIO46, GPIO2) ---------------------
const io = read(`${v24Dir}/hardware_io_profile.h`);
const relays = [...io.matchAll(/\{(\d+),\s*Device::kHardwareIoPinSwitch \| Device::kHardwareIoPinOnboard,\s*"Relay \(GPIO (\d+)\)",\s*nullptr,\s*Device::HardwareIoOutputLogic::ActiveHigh\}/g)]
  .map(match => [Number(match[1]), Number(match[2])]);
assert.deepEqual(relays, [[45, 45], [46, 46], [2, 2]], 'three active-high onboard relays, all usable together');
assert.doesNotMatch(io, /\{44,/, 'GPIO44 is the v2.3 relay and UART0 RX of the v2.4');

// ---- Identity, catalog and build profile -----------------------------------------
const header = read(`${v24Dir}/device_golden_security_gs_t3e_v24.h`);
assert.match(header, /"golden_security_gs_t3e_v24",\s*"Golden Security GS-T3E v2\.4",/);
const profile = getBuildProfile('golden_security_gs_t3e_v24');
assert.equal(profile.define, V24);
assert.equal(profile.metadataDeviceKey, 'golden_security_gs_t3e_v24');
assert.equal(profile.chipFamily, 'ESP32-S3');
assert.equal(profile.rxVariant, 'native-s3');
assert.equal(profile.publish, true);
assert.equal(profile.installer.status, 'validation-pending');
assert.match(profile.installer.hardwareCheck, /PCB revision v2\.4/);
const sketch = read('sketch.yaml');
const fqbn = name => sketch.match(new RegExp(`\\n  ${name}:\\n    fqbn: (\\S+)`))?.[1];
assert.equal(fqbn('golden_security_gs_t3e_v24'), fqbn('golden_security_gs_t3e_v2'),
  'the same board settings as the v2: the log stays on UART0, the only port the v2.4 has');
assert.match(fqbn('golden_security_gs_t3e_v24'), /CDCOnBoot=default/);

// ---- Every shared list that names the v2 names the v2.4 --------------------------
for (const file of ['src/devices/device_select.h', 'src/devices/active_device.h',
  'src/devices/device.cpp', 'src/core/config/config_manager.cpp',
  'src/core/diagnostics/loop_stall.h', 'src/core/firmware/firmware_metadata.cpp',
  'src/core/firmware/firmware_version.h', 'src/types/value/value_control.cpp',
  'src/web/setup/web_config.cpp']) {
  const text = read(file);
  const v2 = (text.match(new RegExp(`\\b${V2}\\b`, 'g')) ?? []).length;
  const v24 = (text.match(new RegExp(`\\b${V24}\\b`, 'g')) ?? []).length;
  assert.ok(v2 > 0 && v24 === v2, `${file} names the v2.4 wherever it names the v2 (${v2} vs ${v24})`);
}
const select = read('src/devices/device_select.h');
for (const family of ['DEVICE_GUITION_ESP32_4848S040_FAMILY', 'DEVICE_ESP32_S3_RGB_480', 'DEVICE_LAYOUT_480X480']) {
  const block = select.match(new RegExp(`#if ([^#]*)#define ${family}\\n`))?.[1] ?? '';
  assert.ok(block.includes(`defined(${V24})`), `the v2.4 belongs to ${family}`);
}
assert.match(read('src/core/firmware/firmware_metadata.cpp'),
  /#elif defined\(DEVICE_GOLDEN_SECURITY_GS_T3E_V24\)\n#define FW_META_TARGET_DEVICE_KEY "golden_security_gs_t3e_v24"/);
assert.match(read('src/web/setup/web_config.cpp'), /return "GS_T3E_V24_Config";/);
console.log('GS-T3E v2.4: the v2 driver apart from its names, migration closes its guard, relays 45/46/2, own identity, listed wherever the v2 is');
