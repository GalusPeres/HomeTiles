import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relativePath => fs.readFileSync(path.join(root, relativePath), 'utf8');

const driver = read('src/devices/guition_jc1060p470c_v3/device_guition_jc1060p470c_v3.cpp');
const sdDriver = read('src/devices/guition_jc1060p470c_v3/vendor/guition_sdmmc.cpp');
const diagnostics = read('src/web/server/handlers/web_admin_diagnostics.cpp');
const diagnosticsHeader = diagnostics.slice(0, diagnostics.indexOf('using namespace web_admin_handlers;'));
const readme = read('src/devices/guition_jc1060p470c_v3/README.md');

const initStart = driver.indexOf('bool DeviceGuitionJC1060P470CV3::initSDCard() {');
const initEnd = driver.indexOf('\nbool DeviceGuitionJC1060P470CV3::storageReady()', initStart);
assert.ok(initStart >= 0 && initEnd > initStart, 'V3 SD initialization method exists');
const init = driver.slice(initStart, initEnd);
assert.doesNotMatch(init, /return false;\s*#else/, 'V3 must not disable SD initialization');
assert.match(init, /JC1060SDMMC\.begin\("\/sdcard", requested_frequency, 5\)/);
assert.match(init, /JC1060SDMMC\.testWritable\(\)/);

for (const marker of [
  'static_assert(BOARD_SDMMC_POWER_CHANNEL == kSdLdoChannel',
  'static_assert(BOARD_SDMMC_POWER_PIN == 45',
  'static_assert(BOARD_SDMMC_POWER_ON_LEVEL == LOW',
  'host.slot = SDMMC_HOST_SLOT_0;',
  'slot_config.width = 4;',
]) {
  assert.ok(sdDriver.includes(marker), `V3 SD driver missing ${marker}`);
}

assert.match(
  diagnostics,
  /defined\(DEVICE_GUITION_JC1060P470C_V2\) \|\| \\\n\s+defined\(DEVICE_GUITION_JC1060P470C_V3\)/,
  'V3 SD diagnostics report the JC1060 bus and driver error details',
);
assert.ok(
  diagnosticsHeader.includes('src/devices/guition_jc1060p470c_v3/vendor/guition_sdmmc.h'),
  'V3 SD diagnostics include its device-local driver',
);
assert.match(readme, /contributor confirms SD-card operation on V3 hardware/);

console.log('Guition JC1060P470C V3 SD profile: PASS');
