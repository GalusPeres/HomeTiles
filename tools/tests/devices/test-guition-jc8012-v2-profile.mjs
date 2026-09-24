import { getBuildProfile, getReleaseProfile } from '../../device-catalog.js';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');
}

function requireMarker(source, marker, label) {
  if (!source.includes(marker)) {
    throw new Error(`${label} is missing: ${marker}`);
  }
}

function commandCount(source) {
  return (source.match(/^\s*\{0x[0-9A-Fa-f]{2},/gm) || []).length;
}

function displayConfigValues(source) {
  const match = source.match(/SCREEN_DEFAULT\s*=\s*\{([\s\S]*?)\n\};/);
  if (!match) throw new Error('Display config initializer was not found');
  return match[1]
    .replace(/\/\/.*$/gm, '')
    .split(',')
    .map((value) => value.trim())
    .filter(Boolean);
}

const v1Table = read('src/devices/guition_jc8012p4a1/vendor/jd9365_init_cmds.h');
const v2Table = read('src/devices/guition_jc8012p4a1_v2/vendor/jd9365_init_cmds.h');
if (commandCount(v1Table) !== 197) {
  throw new Error(`JC8012 V1 init table changed: ${commandCount(v1Table)} commands`);
}
if (commandCount(v2Table) !== 204) {
  throw new Error(`JC8012 V2 init table is incomplete: ${commandCount(v2Table)} commands`);
}
for (const marker of [
  '{0x01, (uint8_t[]){0x44}, 1, 0}',
  '{0x04, (uint8_t[]){0x38}, 1, 0}',
  '{0x18, (uint8_t[]){0xAF}, 1, 0}',
  'guition_jc8012p4a1_v2_init_cmds_count',
]) {
  requireMarker(v2Table, marker, 'JC8012 V2 init table');
}
if (v1Table.includes('DEVICE_GUITION_JC8012P4A1_V2') ||
    v1Table.includes('guition_jc8012p4a1_v2')) {
  throw new Error('JC8012 V1 table must remain independent from V2');
}

const v1Display = displayConfigValues(
  read('src/devices/guition_jc8012p4a1/vendor/displays_config.h'));
const v2Display = displayConfigValues(
  read('src/devices/guition_jc8012p4a1_v2/vendor/displays_config.h'));
const expectedV1 = ['4', '8', '20', '60000000'];
const expectedV2 = ['4', '10', '30', '80000000'];
if (JSON.stringify(v1Display.slice(4, 8)) !== JSON.stringify(expectedV1)) {
  throw new Error(`JC8012 V1 vertical timing/clock changed: ${v1Display.slice(4, 8)}`);
}
if (JSON.stringify(v2Display.slice(4, 8)) !== JSON.stringify(expectedV2)) {
  throw new Error(`JC8012 V2 vertical timing/clock mismatch: ${v2Display.slice(4, 8)}`);
}

const v2Header = read(
  'src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.h');
const v2Source = read(
  'src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
for (const marker of [
  'namespace DeviceGuitionJC8012P4A1V2',
  '"guition_jc8012p4a1_v2"',
  '"Guition JC8012P4A1 V2"',
]) {
  requireMarker(v2Header, marker, 'JC8012 V2 device header');
}
for (const marker of [
  '#if defined(DEVICE_GUITION_JC8012P4A1_V2)',
  'src/devices/guition_jc8012p4a1_v2/vendor/displays_config.h',
  'guition_jc8012p4a1_v2_init_cmds',
  'guition_jc8012p4a1_v2_init_cmds_count',
]) {
  requireMarker(v2Source, marker, 'JC8012 V2 device source');
}

const selector = read('src/devices/device_select.h');
const activeDevice = read('src/devices/active_device.h');
const metadata = read('src/core/firmware/firmware_metadata.cpp');
const webConfig = read('src/web/setup/web_config.cpp');
for (const marker of [
  'defined(DEVICE_GUITION_JC8012P4A1_V2)',
  '#define DEVICE_GUITION_JC8012P4A1_FAMILY',
]) {
  requireMarker(selector, marker, 'Device selection');
}
requireMarker(activeDevice,
  'namespace DeviceImpl = DeviceGuitionJC8012P4A1V2;',
  'Active V2 device selection');
requireMarker(metadata,
  '#define FW_META_TARGET_DEVICE_KEY "guition_jc8012p4a1_v2"',
  'V2 OTA metadata');
requireMarker(webConfig, 'Guition_JC8012P4A1_V2_Config', 'V2 access point name');

const sketchProfiles = read('sketch.yaml');
const workflow = read('.github/workflows/firmware.yml');
requireMarker(sketchProfiles, '  guition_jc8012p4a1_v2:', 'Arduino profile');
assert.equal(getBuildProfile('guition_jc8012p4a1_v2').define, 'DEVICE_GUITION_JC8012P4A1_V2');
for (const marker of [
  'profile: guition_jc8012p4a1_v2',
  'key: guition_jc8012p4a1_v2',
  'define: DEVICE_GUITION_JC8012P4A1_V2',
  'node tools/run-tests.mjs',
  '--verify-release-assets',
]) {
  requireMarker(workflow, marker, 'Firmware workflow');
}
assert.equal(getReleaseProfile('guition_jc8012p4a1_v2').siliconVariant, 'pre_v3');

for (const [file, marker] of [
  ['README.md', 'https://galusperes.github.io/#device-support'],
  ['docs/index.md', '| Guition JC8012P4A1 V2 |'],
  ['docs/index.md', 'Support details for Guition JC8012P4A1 V2'],
  ['RELEASING.md', '34 binaries'],
]) {
  requireMarker(read(file), marker, `${file} release documentation`);
}

console.log('Guition JC8012P4A1 V2 profile contract: PASS');
