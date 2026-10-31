// Elecrow CrowPanel Advanced 10.1 (PR #143): a 1024x600 ESP32-P4 board whose
// ESP32-C6 sits on other SDIO pins than the Waveshare boards the Arduino
// ESP-Hosted defaults are made for (board V1.1+: CLK 18, CMD 19, D0-D3
// 17/16/15/14, reset GPIO32). The board sets them in its own init, before
// the network starts; the shared network manager stays free of board pins.
// The profile is a published pre-v3 release target with its own identity.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {getBuildProfile} from '../../device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const driver = read('src/devices/elecrow_crowpanel_10_1/device_elecrow_crowpanel_10_1.cpp');
const init = driver.match(/bool DeviceElecrowCrowPanel10::init\(\) \{[\s\S]*?\n\}\n/)?.[0] ?? '';
assert.match(init, /WiFi\.setPins\(18, 19, 17, 16, 15, 14, 32\);/, 'the board sets its C6 SDIO and reset pins');
assert.match(driver, /#include <WiFi\.h>/);
const sketch = read('HomeTiles.ino');
assert.ok(sketch.indexOf('BoardHAL::init()') < sketch.indexOf('networkManager.init()'),
  'the board init runs before the network starts ESP-Hosted');
assert.doesNotMatch(read('src/network/network_manager.cpp'), /setPins|ELECROW/,
  'the shared network manager names no board pins');
assert.doesNotMatch(read('tools/build-firmware-local.ps1'), /SkipChecks/,
  'the local build keeps its profile, asset and test checks');

const profile = getBuildProfile('elecrow_crowpanel_10_1');
assert.equal(profile.define, 'DEVICE_ELECROW_CROWPANEL_10_1');
assert.equal(profile.chipFamily, 'ESP32-P4');
assert.equal(profile.siliconVariant, 'pre_v3');
assert.equal(profile.publish, true);
assert.equal(profile.installer.status, 'validation-pending');
assert.match(read('src/core/firmware/firmware_metadata.cpp'),
  /#elif defined\(DEVICE_ELECROW_CROWPANEL_10_1\)\n#define FW_META_TARGET_DEVICE_KEY "elecrow_crowpanel_10_1"/);
assert.match(read('src/web/setup/web_config.cpp'), /return "CrowPanel_10_1_Config";/);
const select = read('src/devices/device_select.h');
for (const group of ['DEVICE_P4_IDF_DSI', 'DEVICE_LAYOUT_1024X600', 'DEVICE_WAVESHARE_TOUCH_LCD_X']) {
  const block = select.match(new RegExp(`#if ([^#]*)#define ${group}\\n`))?.[1] ?? '';
  assert.ok(block.includes('defined(DEVICE_ELECROW_CROWPANEL_10_1)'), `the CrowPanel belongs to ${group}`);
}
// Local I/O: only the UART1 header (Elecrow wiki: RX 48, TX 47); the I2C
// header is the touch bus and UART3-IN the power input.
const io = read('src/devices/elecrow_crowpanel_10_1/hardware_io_profile.h');
const pins = [...io.matchAll(/\{(\d+), Device::kHardwareIoPinSwitch \| Device::kHardwareIoPinTemperature,/g)].map(m => Number(m[1]));
assert.deepEqual(pins, [47, 48], 'the UART1 header pins, nothing on the touch bus or the power input');
console.log('Elecrow CrowPanel 10.1: C6 pins in its own init, shared network code untouched, published pre-v3 profile with its own identity, UART1 header I/O');
