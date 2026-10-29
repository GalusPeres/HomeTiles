// Golden Security GS-T3E v2 (PCB v2.3, issue #93) has its own driver, derived
// from the Guition ESP32-4848S040 driver, which stays untouched (2026-10-18: a
// new device never changes another device's driver). This contract checks the
// T3E's pins, init table, PCLK edge, backlight, touch and missing SD card; that
// the Guition driver keeps its own values and knows nothing of the T3E; the
// shared S3 board list; and that both boards keep separate firmware
// identities. The T3E driver's board-derived constants are compiled on the host.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {findCompiler} from '../../lib/cpp-host.mjs';
import {getBuildProfile, releaseProfiles} from '../../device-catalog.js';
import {validateFirmwareDescriptor} from '../../../docs/assets/javascripts/installer-contract.mjs';

const require = createRequire(import.meta.url);
const {parseFirmwareMetadata, readManualDeviceSelection, resolveReleaseDevice} =
  require('../../../release-helper/import-latest-arduino-build.js');

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const T3E = 'DEVICE_GOLDEN_SECURITY_GS_T3E_V2';
const GUITION = 'DEVICE_GUITION_ESP32_4848S040';
const t3eKey = 'golden_security_gs_t3e_v2';
const guitionKey = 'guition_esp32_4848s040';

// --- Catalog, build profile and firmware identity -----------------------------
const t3e = getBuildProfile(t3eKey);
const guition = getBuildProfile(guitionKey);
assert.equal(t3e.key, t3eKey);
assert.equal(t3e.metadataDeviceKey, t3eKey);
assert.equal(t3e.define, T3E);
assert.equal(t3e.chipFamily, 'ESP32-S3');
assert.equal(t3e.flashSize, 16 * 1024 * 1024);
assert.equal(t3e.siliconVariant, 'default');
assert.equal(t3e.rxVariant, 'native-s3');
assert.equal(t3e.elfFlags, '');
assert.equal(t3e.publish, true);
assert.equal(t3e.legacySlug, 'golden-security-gs-t3e-v2');
assert.equal(t3e.installer.status, 'validation-pending');
assert.match(t3e.installer.hardwareCheck, /ESP32-S3-WROOM-1/);
assert.match(t3e.installer.hardwareCheck, /PCB revision v2\.3/);
assert.match(t3e.installer.hardwareCheck, /v1\.23/, 'The check names the unsupported first revision');
// Profiles added after the T3E (the Waveshare 4.3 v3 image, #135) build later.
const addedAfterT3e = new Set(['waveshare_touch_lcd_4_3_rev3']);
assert.ok(releaseProfiles.every(profile => profile === t3e || addedAfterT3e.has(profile.key) ||
  profile.ciOrder < t3e.ciOrder),
  'The T3E builds after every release profile that existed before it');
assert.ok([...addedAfterT3e].every(key => releaseProfiles.find(profile => profile.key === key).ciOrder > t3e.ciOrder),
  'Later profiles keep the existing CI order');
for (const field of ['key', 'metadataDeviceKey', 'define', 'buildProfile', 'legacySlug']) {
  assert.notEqual(t3e[field], guition[field], `${field} must differ from the Guition S3`);
}

const sketch = read('sketch.yaml');
const sketchProfile = name =>
  sketch.match(new RegExp(`\\n  ${name}:\\n([\\s\\S]*?)(?=\\n  [A-Za-z0-9_]+:\\n|$)`))?.[1];
assert.ok(sketchProfile(t3eKey), 'T3E sketch profile');
assert.equal(sketchProfile(t3eKey), sketchProfile(guitionKey),
  'The T3E uses the Guition S3 FQBN, platform and libraries');

const metadata = read('src/core/firmware/firmware_metadata.cpp');
assert.match(metadata, new RegExp(`#elif defined\\(${T3E}\\)\\n#define FW_META_TARGET_DEVICE_KEY "${t3eKey}"\\n` +
  '#define FW_META_TARGET_DISPLAY_NAME "Golden Security GS-T3E v2"'));
assert.match(read('src/web/setup/web_config.cpp'),
  new RegExp(`#elif defined\\(${T3E}\\)\\n  return "GS_T3E_V2_Config";`));
assert.match(read('src/devices/active_device.h'),
  new RegExp(`#elif defined\\(${T3E}\\)\\n#include "src/devices/golden_security_gs_t3e_v2/` +
    'device_golden_security_gs_t3e_v2.h"\\nnamespace DeviceImpl = DeviceGoldenSecurityGST3EV2;'));

// An image of one board is never accepted as the other's.
function firmwareImage(deviceKey) {
  const image = Buffer.alloc(24 + 8 + 256 + 4 + 32 + 32 + 32 + 4 + 2 + 2 + 16);
  image[0] = 0xe9;
  image.writeUInt32LE(0xabcd5432, 24 + 8);
  const descriptor = 24 + 8 + 256;
  image.writeUInt32LE(0x44565034, descriptor);
  image.write('esp32_p4_homeassistant_display', descriptor + 4);
  image.write(deviceKey, descriptor + 4 + 32);
  image.write('Regression fixture', descriptor + 4 + 64);
  const silicon = descriptor + 4 + 96;
  image.writeUInt32LE(0x53525634, silicon);
  image.writeUInt16LE(0, silicon + 4);
  image.writeUInt16LE(0xffff, silicon + 6);
  image.write('default', silicon + 8);
  return image;
}
assert.equal(validateFirmwareDescriptor(firmwareImage(t3eKey), t3eKey), t3eKey);
assert.throws(() => validateFirmwareDescriptor(firmwareImage(guitionKey), t3eKey), /not golden_security_gs_t3e_v2/);
assert.throws(() => validateFirmwareDescriptor(firmwareImage(t3eKey), guitionKey), /not guition_esp32_4848s040/);
const selector = define => `#if !defined(HOMETILES_CI_TARGET)\n#define ${define}\n#endif\n`;
const t3eSelection = readManualDeviceSelection(selector(T3E));
assert.equal(t3eSelection.key, t3eKey);
assert.equal(resolveReleaseDevice(t3eSelection, parseFirmwareMetadata(firmwareImage(t3eKey))).key, t3eKey);
assert.throws(() => resolveReleaseDevice(t3eSelection, parseFirmwareMetadata(firmwareImage(guitionKey))),
  /is for guition_esp32_4848s040/);
assert.throws(() => resolveReleaseDevice(readManualDeviceSelection(selector(GUITION)),
  parseFirmwareMetadata(firmwareImage(t3eKey))), /is for golden_security_gs_t3e_v2/);

// --- Source structure ----------------------------------------------------------
// The T3E's own driver, only in T3E builds, its values from its description.
const driver = read('src/devices/golden_security_gs_t3e_v2/device_golden_security_gs_t3e_v2.cpp');
assert.match(driver, /^#if defined\(DEVICE_GOLDEN_SECURITY_GS_T3E_V2\)$/m, 'The T3E driver builds only for the T3E');
assert.match(driver, /constexpr const St7701S3Board::Description& kBoard = DeviceImpl::kBoard;/);
assert.doesNotMatch(driver, /constexpr int8_t k\w+ = \d+;/, 'No GPIO number is hard-coded in the T3E driver');
assert.doesNotMatch(driver, /DeviceGuitionESP324848S040|DEVICE_GUITION_ESP32_4848S040\b/,
  'The T3E driver is not the Guition driver');
const t3eHeader = read('src/devices/golden_security_gs_t3e_v2/device_golden_security_gs_t3e_v2.h');
assert.doesNotMatch(t3eHeader, /DeviceGuitionESP324848S040|guition_esp32_4848s040\//,
  'The T3E header neither includes nor uses the Guition driver');
// The Guition driver keeps its own exact-board constants and never mentions the T3E.
const guitionDriver = read('src/devices/guition_esp32_4848s040/device_guition_esp32_4848s040.cpp');
assert.match(guitionDriver, /^#if defined\(DEVICE_GUITION_ESP32_4848S040\)$/m);
for (const constant of ['kPanelCs = 39', 'kPanelSck = 48', 'kPanelMosi = 47', 'kBacklightPin = 38',
  'kBacklightFrequency = 150', 'kTouchSda = 19', 'kTouchScl = 45', 'kSdCs = 42']) {
  assert.ok(guitionDriver.includes(`constexpr ${constant.includes('Frequency') ? 'uint32_t' : 'int8_t'} ${constant};`),
    `The Guition driver keeps ${constant}`);
}
assert.match(guitionDriver, /st7701_type9_init_operations,\s*sizeof\(st7701_type9_init_operations\)/);
for (const file of fs.readdirSync(path.join(root, 'src/devices/guition_esp32_4848s040'))) {
  assert.doesNotMatch(read(`src/devices/guition_esp32_4848s040/${file}`), /T3E|GOLDEN_SECURITY|golden_security/i,
    `${file} knows nothing of the T3E`);
}
// The PCB v2.3's relays: GPIO45/46 on every published variant (active high in
// ESPHome) and GPIO44, confirmed by a board owner (issue #93). One board has
// them all, so none is tied to a board variant: a save holds one variant
// (test-hardware-io-onboard-together runs the real check). GPIO43 is the log
// output and the other variants' GPIO1/2 stay unexposed.
const io = read('src/devices/golden_security_gs_t3e_v2/hardware_io_profile.h');
assert.deepEqual([...io.matchAll(/\{(\d+), Device::kHardwareIoPinSwitch \| Device::kHardwareIoPinOnboard,/g)].map(m => Number(m[1])),
  [45, 46, 44], 'the three relays of the v2.3');
assert.equal((io.match(/nullptr, Device::HardwareIoOutputLogic::ActiveHigh/g) || []).length, 3,
  'all three active high, none tied to a board variant');
assert.doesNotMatch(io, /\{(43|2|1), Device::/, 'no other variant\'s relay GPIO, not the log output');
assert.match(io, /UART0 RX/, 'GPIO44 is noted as the serial input');
const readme = read('src/devices/golden_security_gs_t3e_v2/README.md');
for (const source of ['user_setups/esp32s3/gs-t3e.ini', 'devices.esphome.io/devices/tuya-t3e-esp32-s3',
  'discussions/458', 'not yet validated']) {
  assert.ok(readme.includes(source), `README names ${source}`);
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: GS-T3E family preprocessing and driver constants need a C++ compiler');
  process.exit(0);
}
const buildRoot = path.join(root, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'gs-t3e-'));
try {
  const write = (name, text) => fs.writeFileSync(path.join(tempRoot, name), text);

  // --- Device families from the real device_select.h -----------------------------
  const macros = ['DEVICE_GUITION_ESP32_4848S040_FAMILY', 'DEVICE_ESP32_S3_RGB_480',
    'DEVICE_LAYOUT_480X480', 'HOMETILES_LOCAL_CAMERA', 'DEVICE_P4_IDF_DSI'];
  write('probe.cpp', '#include "src/devices/device_select.h"\n' +
    macros.map(name => `#if defined(${name})\nHAS_${name}\n#endif`).join('\n') + '\n');
  const families = define => {
    const run = spawnSync(compiler, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', `-D${define}`,
      '-I', root, path.join(tempRoot, 'probe.cpp')], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stderr);
    return macros.filter(name => run.stdout.includes(`HAS_${name}`));
  };
  const s3Square = ['DEVICE_ESP32_S3_RGB_480', 'DEVICE_LAYOUT_480X480'];
  assert.deepEqual(families(T3E), ['DEVICE_GUITION_ESP32_4848S040_FAMILY', ...s3Square]);
  assert.deepEqual(families(GUITION), ['DEVICE_GUITION_ESP32_4848S040_FAMILY', ...s3Square]);
  assert.deepEqual(families('DEVICE_WAVESHARE_S3_TOUCH_LCD_4B'), s3Square, 'Other S3 boards keep their own driver');
  assert.ok(!families('DEVICE_WAVESHARE_4B').includes('DEVICE_GUITION_ESP32_4848S040_FAMILY'));
  const both = spawnSync(compiler, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', `-D${T3E}`,
    `-D${GUITION}`, '-I', root, path.join(tempRoot, 'probe.cpp')], {encoding: 'utf8'});
  assert.notEqual(both.status, 0, 'Two S3 targets at once are refused');

  // --- The driver's board-derived constants, compiled for each board -------------
  write('Arduino.h', '#pragma once\n#include <stddef.h>\n#include <stdint.h>\n');
  write('FS.h', '#pragma once\nnamespace fs { class FS {}; }\n');
  const regionStart = driver.indexOf('\nnamespace {\n');
  const regionEnd = driver.indexOf('class GuitionAtomicRgbDisplay final');
  assert.ok(regionStart > 0 && regionEnd > regionStart, 'Driver constant section');
  write('harness.cpp', `#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "src/devices/active_device.h"
// Stand-ins for the GFX Library for Arduino 1.6.5 tables (sizes differ).
enum { BEGIN_WRITE = 1, WRITE_COMMAND_8, WRITE_C8_D8, WRITE_BYTES, END_WRITE };
static const uint8_t st7701_type1_init_operations[] = {1, 1, 1};
static const uint8_t st7701_type9_init_operations[] = {9, 9};
${driver.slice(regionStart, regionEnd)}
}  // namespace
int main() {
  std::printf("{\\"log\\":\\"%s\\",\\"key\\":\\"%s\\",\\"name\\":\\"%s\\",", kLogName, DeviceImpl::kProfile.key,
              DeviceImpl::kProfile.display_name);
  std::printf("\\"size\\":[%u,%u,%u,%u],\\"rotation\\":[%u,%u],\\"floor\\":%u,",
              DeviceImpl::kProfile.screen_width, DeviceImpl::kProfile.screen_height,
              DeviceImpl::kProfile.grid_cols, DeviceImpl::kProfile.grid_rows,
              DeviceImpl::kProfile.rotation_default, DeviceImpl::kProfile.rotation_flipped,
              DeviceImpl::kProfile.backlight_input_min);
  std::printf("\\"spi\\":[%d,%d,%d],\\"control\\":[%d,%d,%d,%d],\\"data\\":[", kPanelCs, kPanelSck,
              kPanelMosi, kRgb.de, kRgb.vsync, kRgb.hsync, kRgb.pclk);
  for (size_t i = 0; i < 16; ++i) std::printf("%s%d", i ? "," : "", kRgbDataPins[i]);
  std::printf("],\\"init\\":%d,\\"initSize\\":%u,\\"cd\\":%u,\\"label\\":\\"%s\\",\\"pclkNeg\\":%d,",
              kPanelInitOperations == st7701_type1_init_operations ? 1
              : kPanelInitOperations == st7701_type9_init_operations ? 9 : 0,
              static_cast<unsigned>(kPanelInitOperationsSize), static_cast<unsigned>(kPanelCd),
              kPanelInitLabel, kPclkActiveNeg ? 1 : 0);
  std::printf("\\"backlight\\":[%d,%u,%u],\\"touch\\":[%d,%d],\\"sd\\":[%d,%d,%d,%d,%d],",
              kBacklightPin, static_cast<unsigned>(kBacklightFrequency),
              static_cast<unsigned>(kBacklightDutyLatchMs), kTouchSda, kTouchScl,
              kHasSdCard ? 1 : 0, kSdSck, kSdMiso, kSdMosi, kSdCs);
  std::printf("\\"pclk\\":%u,\\"framePeriodMs\\":%u}\\n", static_cast<unsigned>(kRgbPclkHz),
              static_cast<unsigned>(kRgbFramePeriodMs));
  return 0;
}
`);
  const flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-const-variable',
    '-Wno-unused-variable', '-DHOMETILES_CI_TARGET', '-I', tempRoot, '-I', root];
  const compile = (define, extra = []) => {
    const output = path.join(tempRoot, `${define}${process.platform === 'win32' ? '.exe' : ''}`);
    const result = spawnSync(compiler, [...flags, `-D${define}`, ...extra,
      path.join(tempRoot, 'harness.cpp'), '-o', output], {encoding: 'utf8'});
    return {result, output};
  };
  const boardValues = (define, extra = []) => {
    const {result, output} = compile(define, extra);
    assert.equal(result.status, 0, `${define} driver constants did not compile:\n${result.stdout}${result.stderr}`);
    const run = spawnSync(output, [], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.stdout + run.stderr);
    return JSON.parse(run.stdout);
  };

  const t3eValues = boardValues(T3E);
  assert.deepEqual(t3eValues, {
    log: 'GS-T3E v2', key: t3eKey, name: 'Golden Security GS-T3E v2',
    // Same grid and UI; the panel runs unrotated like openHASP and ESPHome.
    size: [480, 480, 4, 4], rotation: [0, 2], floor: 39,
    spi: [4, 15, 48], control: [40, 39, 38, 41],
    // D0..D15 = B0..B4, G0..G5, R0..R4 (openHASP [odm-t3e-v2], ESPHome).
    data: [15, 48, 47, 21, 14, 13, 12, 11, 10, 9, 3, 8, 18, 17, 16, 0],
    init: 1, initSize: 3, cd: 8, label: 'type1', pclkNeg: 0,
    backlight: [42, 150, 8], touch: [7, 6], sd: [0, -1, -1, -1, -1],
    pclk: 12000000, framePeriodMs: 24,
  }, 'GS-T3E v2 board description');
  // The ST7701 SPI clock and data lines become RGB B0/B1 after the init.
  assert.deepEqual(t3eValues.spi.slice(1), t3eValues.data.slice(0, 2));

  // The type9 CD=0x08 A/B switch cannot be applied to the type1 board.
  const t3eCd08 = compile(T3E, ['-DHOMETILES_GUITION_S3_PANEL_CD=8']).result;
  assert.notEqual(t3eCd08.status, 0, 'CD=0x08 override on the type1 board must not compile');
  assert.match(t3eCd08.stdout + t3eCd08.stderr, /applies to type9 boards only/);
} finally {
  assert.ok(path.resolve(tempRoot).startsWith(path.resolve(buildRoot) + path.sep));
  fs.rmSync(tempRoot, {recursive: true, force: true});
}

console.log('GS-T3E v2 profile, board descriptions, driver family and firmware identity: PASS');
