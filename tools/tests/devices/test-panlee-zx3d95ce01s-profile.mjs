// Smart Panlee ZX3D95CE01S (V12) has its own driver, derived from the GS-T3E
// v2 driver, which stays untouched (a new device never changes another
// device's driver). This contract checks the board's pins, GC9503V init table,
// timing, backlight, FT6336 touch and missing SD card; that the GS-T3E and
// Guition drivers know nothing of it; and that the board keeps its own
// firmware identity. The driver's board-derived constants are compiled on the
// host.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {findCompiler} from '../../lib/cpp-host.mjs';
import {getBuildProfile} from '../../device-catalog.js';
import {validateFirmwareDescriptor} from '../../../docs/assets/javascripts/installer-contract.mjs';

const require = createRequire(import.meta.url);
const {parseFirmwareMetadata, readManualDeviceSelection, resolveReleaseDevice} =
  require('../../../release-helper/import-latest-arduino-build.js');

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const PANLEE = 'DEVICE_PANLEE_ZX3D95CE01S';
const T3E = 'DEVICE_GOLDEN_SECURITY_GS_T3E_V2';
const panleeKey = 'panlee_zx3d95ce01s';
const t3eKey = 'golden_security_gs_t3e_v2';

// --- Catalog, build profile and firmware identity -----------------------------
const panlee = getBuildProfile(panleeKey);
const t3e = getBuildProfile(t3eKey);
assert.equal(panlee.key, panleeKey);
assert.equal(panlee.metadataDeviceKey, panleeKey);
assert.equal(panlee.define, PANLEE);
assert.equal(panlee.chipFamily, 'ESP32-S3');
assert.equal(panlee.flashSize, 16 * 1024 * 1024);
assert.equal(panlee.siliconVariant, 'default');
assert.equal(panlee.rxVariant, 'native-s3');
assert.equal(panlee.publish, true);
assert.equal(panlee.installer.status, 'validation-pending');
assert.match(panlee.installer.hardwareCheck, /ZX3D95CE01S/);
assert.match(panlee.installer.hardwareCheck, /UR and AR/, 'The check names the unsupported variants');
for (const field of ['key', 'metadataDeviceKey', 'define', 'buildProfile', 'legacySlug']) {
  assert.notEqual(panlee[field], t3e[field], `${field} must differ from the GS-T3E v2`);
}

const sketch = read('sketch.yaml');
const sketchProfile = name =>
  sketch.match(new RegExp(`\\n  ${name}:\\n([\\s\\S]*?)(?=\\n  [A-Za-z0-9_]+:\\n|$)`))?.[1];
assert.ok(sketchProfile(panleeKey), 'Panlee sketch profile');
assert.match(sketchProfile(panleeKey), /FlashSize=16M/);
assert.match(sketchProfile(panleeKey), /PSRAM=opi/);
assert.match(sketchProfile(panleeKey), /CDCOnBoot=cdc/, 'The log is on the native USB port');

assert.match(read('src/core/firmware/firmware_metadata.cpp'),
  new RegExp(`#elif defined\\(${PANLEE}\\)\\n#define FW_META_TARGET_DEVICE_KEY "${panleeKey}"\\n` +
    '#define FW_META_TARGET_DISPLAY_NAME "Panlee ZX3D95CE01S"'));
assert.match(read('src/web/setup/web_config.cpp'),
  new RegExp(`#elif defined\\(${PANLEE}\\)\\n  return "Panlee_ZX3D95CE01S_Config";`));
assert.match(read('src/devices/active_device.h'),
  new RegExp(`#elif defined\\(${PANLEE}\\)\\n#include "src/devices/panlee_zx3d95ce01s/` +
    'device_panlee_zx3d95ce01s.h"\\nnamespace DeviceImpl = DevicePanleeZX3D95CE01S;'));

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
assert.equal(validateFirmwareDescriptor(firmwareImage(panleeKey), panleeKey), panleeKey);
assert.throws(() => validateFirmwareDescriptor(firmwareImage(t3eKey), panleeKey), /not panlee_zx3d95ce01s/);
assert.throws(() => validateFirmwareDescriptor(firmwareImage(panleeKey), t3eKey), /not golden_security_gs_t3e_v2/);
const selector = define => `#if !defined(HOMETILES_CI_TARGET)\n#define ${define}\n#endif\n`;
const selection = readManualDeviceSelection(selector(PANLEE));
assert.equal(selection.key, panleeKey);
assert.equal(resolveReleaseDevice(selection, parseFirmwareMetadata(firmwareImage(panleeKey))).key, panleeKey);
assert.throws(() => resolveReleaseDevice(selection, parseFirmwareMetadata(firmwareImage(t3eKey))),
  /is for golden_security_gs_t3e_v2/);

// --- Source structure ----------------------------------------------------------
const dir = 'src/devices/panlee_zx3d95ce01s';
const driver = read(`${dir}/device_panlee_zx3d95ce01s.cpp`);
assert.match(driver, /^#if defined\(DEVICE_PANLEE_ZX3D95CE01S\)$/m, 'The driver builds only for this board');
assert.match(driver, /constexpr const Gc9503vS3Board::Description& kBoard = DeviceImpl::kBoard;/);
assert.doesNotMatch(driver, /constexpr int8_t k\w+ = \d+;/, 'No GPIO number is hard-coded in the driver');
assert.match(driver, /gc9503v_type1_init_operations/);
assert.doesNotMatch(driver, /st7701|ST7701|GT911/, 'No ST7701 or GT911 code is left in the driver');
for (const other of ['golden_security_gs_t3e_v2', 'guition_esp32_4848s040']) {
  for (const file of fs.readdirSync(path.join(root, 'src/devices', other))) {
    assert.doesNotMatch(read(`src/devices/${other}/${file}`), /panlee|PANLEE/i, `${other}/${file} knows nothing of Panlee`);
  }
}
// The FT6336 is read in one transfer from TD_STATUS; zero points is a release.
assert.match(driver, /kTouchPointRegister = 0x02;/);
assert.match(driver, /kTouchChipIdRegister = 0xA3;/);
// The RS485 pair, the SHT20 bus and the sub board header stay unexposed.
assert.match(read(`${dir}/hardware_io_profile.h`), /kHardwareIoProfile\{\};/);
const readme = read(`${dir}/README.md`);
for (const source of ['zw3d95ce01s_ar.ini', 'doc.panel-tag.com', 'not yet validated']) {
  assert.ok(readme.includes(source), `README names ${source}`);
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: Panlee family preprocessing and driver constants need a C++ compiler');
  process.exit(0);
}
const buildRoot = path.join(root, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'panlee-tr-'));
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
  assert.deepEqual(families(PANLEE),
    ['DEVICE_GUITION_ESP32_4848S040_FAMILY', 'DEVICE_ESP32_S3_RGB_480', 'DEVICE_LAYOUT_480X480']);
  const both = spawnSync(compiler, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', `-D${PANLEE}`,
    `-D${T3E}`, '-I', root, path.join(tempRoot, 'probe.cpp')], {encoding: 'utf8'});
  assert.notEqual(both.status, 0, 'Two S3 targets at once are refused');

  // --- The driver's board-derived constants --------------------------------------
  write('Arduino.h', '#pragma once\n#include <stddef.h>\n#include <stdint.h>\n');
  write('FS.h', '#pragma once\nnamespace fs { class FS {}; }\n');
  const regionStart = driver.indexOf('\nnamespace {\n');
  const regionEnd = driver.indexOf('class GuitionAtomicRgbDisplay final');
  assert.ok(regionStart > 0 && regionEnd > regionStart, 'Driver constant section');
  write('harness.cpp', `#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "src/devices/active_device.h"
// Stand-in for the GFX Library for Arduino 1.6.5 table (size differs).
enum { BEGIN_WRITE = 1, WRITE_COMMAND_8, WRITE_C8_D8, WRITE_BYTES, END_WRITE };
static const uint8_t gc9503v_type1_init_operations[] = {1, 1, 1};
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
  std::printf("\\"spi\\":[%d,%d,%d],\\"reset\\":%d,\\"control\\":[%d,%d,%d,%d],\\"data\\":[", kPanelCs, kPanelSck,
              kPanelMosi, kPanelReset, kRgb.de, kRgb.vsync, kRgb.hsync, kRgb.pclk);
  for (size_t i = 0; i < 16; ++i) std::printf("%s%d", i ? "," : "", kRgbDataPins[i]);
  std::printf("],\\"init\\":%d,\\"label\\":\\"%s\\",\\"pclkNeg\\":%d,",
              kPanelInitOperations == gc9503v_type1_init_operations ? 1 : 0, kPanelInitLabel,
              kPclkActiveNeg ? 1 : 0);
  std::printf("\\"hsync\\":[%u,%u,%u],\\"vsync\\":[%u,%u,%u],",
              static_cast<unsigned>(kBoard.hsync.front_porch), static_cast<unsigned>(kBoard.hsync.pulse_width),
              static_cast<unsigned>(kBoard.hsync.back_porch), static_cast<unsigned>(kBoard.vsync.front_porch),
              static_cast<unsigned>(kBoard.vsync.pulse_width), static_cast<unsigned>(kBoard.vsync.back_porch));
  std::printf("\\"backlight\\":[%d,%u,%u],\\"touch\\":[%d,%d,%u],\\"sd\\":[%d,%d,%d,%d,%d],",
              kBacklightPin, static_cast<unsigned>(kBacklightFrequency),
              static_cast<unsigned>(kBacklightDutyLatchMs), kTouchSda, kTouchScl,
              static_cast<unsigned>(kTouchAddress),
              kHasSdCard ? 1 : 0, kSdSck, kSdMiso, kSdMosi, kSdCs);
  std::printf("\\"pclk\\":%u,\\"framePeriodMs\\":%u}\\n", static_cast<unsigned>(kRgbPclkHz),
              static_cast<unsigned>(kRgbFramePeriodMs));
  return 0;
}
`);
  const output = path.join(tempRoot, `harness${process.platform === 'win32' ? '.exe' : ''}`);
  const compile = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-const-variable', '-Wno-unused-variable', '-DHOMETILES_CI_TARGET', `-D${PANLEE}`,
    '-I', tempRoot, '-I', root, path.join(tempRoot, 'harness.cpp'), '-o', output], {encoding: 'utf8'});
  assert.equal(compile.status, 0, `Driver constants did not compile:\n${compile.stdout}${compile.stderr}`);
  const run = spawnSync(output, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  const values = JSON.parse(run.stdout);
  assert.deepEqual(values, {
    log: 'Panlee ZX3D95CE01S', key: panleeKey, name: 'Panlee ZX3D95CE01S',
    size: [480, 480, 4, 4], rotation: [0, 2], floor: 39,
    // openHASP [env:panlee-zw3d95ce01s-tr-4848_16MB]: SPI CS38/SCK45/MOSI48, reset on the VSYNC pin.
    spi: [38, 45, 48], reset: 41, control: [40, 41, 42, 39],
    // D0..D15 = B0..B4, G0..G5, R0..R4.
    data: [45, 48, 47, 0, 21, 14, 13, 12, 11, 16, 17, 18, 8, 3, 46, 10],
    init: 1, label: 'gc9503v-type1', pclkNeg: 0,
    hsync: [8, 10, 40], vsync: [10, 10, 40],
    backlight: [5, 150, 8], touch: [15, 6, 56], sd: [0, -1, -1, -1, -1],
    pclk: 12000000, framePeriodMs: 25,
  }, 'Panlee ZX3D95CE01S board description');
  // The panel's SPI clock and data lines become RGB B0/B1 after the init.
  assert.deepEqual(values.spi.slice(1), values.data.slice(0, 2));
  // The reset pin is the RGB VSYNC line.
  assert.equal(values.reset, values.control[1]);
} finally {
  assert.ok(path.resolve(tempRoot).startsWith(path.resolve(buildRoot) + path.sep));
  fs.rmSync(tempRoot, {recursive: true, force: true});
}

console.log('Panlee ZX3D95CE01S profile, board description, driver family and firmware identity: PASS');
