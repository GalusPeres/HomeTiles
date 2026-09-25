// Camera beta boards that reuse the shared OV5647 (Waveshare 8-inch) and
// OV02C10 (Guition V2) drivers: beta-gated selection, board file guards, the
// existing shared I2C bus, the display's MIPI PHY supply, the sensor mode
// copied from the reference board, the capability flag, the beta version, and
// the profiles that must never compile the capture path.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const exists = relative => fs.existsSync(path.join(root, relative));
const escape = text => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const body = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
};

const deviceSelect = read('src/devices/device_select.h');
const cameraSelect = read('src/video/local_camera/camera_select.h');
const version = read('src/core/firmware/firmware_version.h');

// The display drivers acquire LDO 3 at 2500 mV either literally or through
// named constants.
const literalLdo = /ldo_cfg\.chan_id = 3;\s*ldo_cfg\.voltage_mv = 2500;/;
const namedLdo = /constexpr int kMipiPhyLdoChannel = 3;\s*constexpr int kMipiPhyLdoVoltageMv = 2500;[\s\S]*ldo_cfg\.chan_id = kMipiPhyLdoChannel;\s*ldo_cfg\.voltage_mv = kMipiPhyLdoVoltageMv;/;
const sharedBus = ns => new RegExp(`i2c_master_bus_handle_t ${ns}::sharedI2cBus\\(\\) \\{\\s*return g_i2c_ready \\? g_i2c\\.bus : nullptr;\\s*\\}`);

const boards = [
  {define: 'DEVICE_WAVESHARE_TOUCH_LCD_7', dir: 'waveshare_touch_lcd_7', sensor: 'ov5647',
   flag: 'profile.h', driver: 'device_waveshare_touch_lcd_7.cpp', ldo: literalLdo,
   accessor: 'DeviceWaveshareTouchLCD7::sharedI2cBus()', owner: sharedBus('DeviceWaveshareTouchLCD7')},
  {define: 'DEVICE_WAVESHARE_TOUCH_LCD_10_1', dir: 'waveshare_touch_lcd_10_1', sensor: 'ov5647',
   flag: 'profile.h', driver: 'device_waveshare_touch_lcd_10_1.cpp', ldo: literalLdo,
   accessor: 'DeviceWaveshareTouchLCD10::sharedI2cBus()', owner: sharedBus('DeviceWaveshareTouchLCD10')},
  {define: 'DEVICE_WAVESHARE_TOUCH_LCD_7B', dir: 'waveshare_touch_lcd_7b', sensor: 'ov5647',
   flag: 'profile.h', driver: 'device_waveshare_touch_lcd_7b.cpp', ldo: namedLdo,
   accessor: 'DeviceWaveshareTouchLCD7B::sharedI2cBus()', owner: sharedBus('DeviceWaveshareTouchLCD7B')},
  {define: 'DEVICE_WAVESHARE_TOUCH_LCD_4_3', dir: 'waveshare_touch_lcd_4_3', sensor: 'ov5647',
   flag: 'profile.h', driver: 'device_waveshare_touch_lcd_4_3.cpp', ldo: literalLdo,
   accessor: 'DeviceWaveshareTouchLCD4_3::sharedI2cBus()', owner: sharedBus('DeviceWaveshareTouchLCD4_3')},
  {define: 'DEVICE_WAVESHARE_4B', dir: 'waveshare_4b', sensor: 'ov5647',
   flag: 'device_waveshare_4b.h', driver: 'device_waveshare_4b.cpp', ldo: namedLdo,
   accessor: 'DeviceWaveshare4B::sharedI2cBus()',
   owner: /i2c_master_bus_handle_t DeviceWaveshare4B::sharedI2cBus\(\) \{\s*return g_i2c_bus;\s*\}/},
  {define: 'DEVICE_GUITION_JC8012P4A1', dir: 'guition_jc8012p4a1', sensor: 'ov02c10',
   flag: 'device_guition_jc8012p4a1.h', driver: 'device_guition_jc8012p4a1.cpp', ldo: literalLdo,
   accessor: 'gsl3680_i2c_bus()', ownerFile: 'vendor/gsl3680_touch.cpp',
   owner: /i2c_master_bus_handle_t gsl3680_i2c_bus\(\) \{\s*return g_state\.initialized \? g_state\.bus : nullptr;\s*\}/},
  {define: 'DEVICE_GUITION_JC1060P470C_V2', dir: 'guition_jc1060p470c_v2', sensor: 'ov02c10',
   flag: 'device_guition_jc1060p470c_v2.h', driver: 'device_guition_jc1060p470c_v2.cpp', ldo: literalLdo,
   accessor: 'DeviceGuitionJC1060P470CV2::sharedI2cBus()', owner: sharedBus('DeviceGuitionJC1060P470CV2')},
  // Guition's demo turns the frame 270 degrees onto the portrait panel.
  {define: 'DEVICE_GUITION_JC4880P443_PORTRAIT', dir: 'guition_jc4880p443_portrait', sensor: 'ov02c10',
   flag: 'profile.h', driver: 'device_guition_jc4880p443_portrait.cpp', ldo: literalLdo, quarterTurn: 'true',
   accessor: 'DeviceGuitionJC4880P443Portrait::sharedI2cBus()', owner: sharedBus('DeviceGuitionJC4880P443Portrait')},
];

// kMode fields in positional order (camera_driver.h), comments removed.
const modeFields = source => {
  const masked = maskCpp(source);
  const start = masked.indexOf('inline constexpr local_camera::SensorMode kMode = {');
  assert.notEqual(start, -1, 'kMode');
  const open = masked.indexOf('{', start);
  return masked.slice(open + 1, masked.indexOf('};', open)).split(',')
    .map(value => value.replace(/\s+/g, ' ').trim()).filter(Boolean);
};
const referenceMode = {
  ov5647: modeFields(read('src/devices/waveshare_touch_lcd_8/local_camera_board.h')),
  ov02c10: modeFields(read('src/devices/guition_jc8012p4a1_v2/local_camera_board.h')),
};
const kQuarterTurnField = 11;
assert.equal(referenceMode.ov5647.length, 23, 'raw_bits keeps its RAW10 default');
assert.equal(referenceMode.ov5647[kQuarterTurnField], 'true', '8-inch quarter-turn mounting');
assert.equal(referenceMode.ov02c10[kQuarterTurnField], 'false', 'V2 landscape mounting');

// --- Selection: only camera beta builds, except the exact V2 -----------------
const cameraBlock = deviceSelect.slice(deviceSelect.indexOf('// Built-in camera.'),
  deviceSelect.indexOf('#define HOMETILES_LOCAL_CAMERA 1'));
const selected = [...cameraBlock.matchAll(/defined\((DEVICE_\w+)\)/g)].map(match => match[1]);
for (const define of selected.filter(name => name !== 'DEVICE_GUITION_JC8012P4A1_V2')) {
  assert.match(cameraBlock, new RegExp(`\\(defined\\(${define}\\) && defined\\(HOMETILES_CAMERA_BETA\\)\\)`),
    `${define} is a camera beta board`);
}
assert.match(version, /#if defined\(HOMETILES_CAMERA_BETA\)\n#undef FW_VERSION\n#define FW_VERSION "v[^"]+"\n#endif/,
  'Every camera beta build carries the beta number');
assert.match(version, /defined\(DEVICE_GUITION_JC8012P4A1_V2\) && \\\n    defined\(HOMETILES_ISSUE38_BETA\)/);

for (const board of boards) {
  const base = `src/devices/${board.dir}`;
  const header = read(`${base}/local_camera_board.h`);
  const cpp = read(`${base}/local_camera_board.cpp`);
  const guard = `defined(${board.define}) && defined(HOMETILES_LOCAL_CAMERA)`;
  const label = board.dir;

  assert.ok(selected.includes(board.define), `${label}: device_select.h camera entry`);
  assert.match(cameraSelect, new RegExp(`#if defined\\(${board.define}\\)\\n` +
    `#define HOMETILES_CAMERA_SENSOR_${board.sensor.toUpperCase()} 1\\n` +
    `#define HOMETILES_LOCAL_CAMERA_BOARD "${escape(base)}/local_camera_board\\.h"\\n#endif`),
    `${label}: camera_select.h entry`);

  // Board file guards, as on the existing boards.
  assert.ok(header.indexOf('#include "src/video/local_camera/camera_select.h"') < header.indexOf(`#if ${guard}`),
    `${label}: the guard needs camera_select.h first`);
  for (const text of [header, cpp]) {
    assert.match(text, new RegExp(`#if ${escape(guard)}\\n`), `${label}: board guard`);
    assert.match(text.trimEnd(), new RegExp(`#endif  // ${escape(guard)}$`), `${label}: board guard end`);
  }
  assert.match(header, new RegExp(`#include "src/video/local_camera/sensors/${board.sensor}/${board.sensor}_sensor\\.h"`));
  assert.match(header, new RegExp(`using Sensor = ${board.sensor}::Sensor;`), `${label}: sensor driver`);
  assert.match(header, /using SccbBus = i2c_master_bus_handle_t;/);

  // Sensor mode: the reference board's mode; only a proven mounting differs.
  const expected = [...referenceMode[board.sensor]];
  if (board.quarterTurn) expected[kQuarterTurnField] = board.quarterTurn;
  assert.deepEqual(modeFields(header), expected, `${label}: kMode`);
  assert.match(header, /orientation pending hardware check/, `${label}: unproven orientation is marked`);

  // The existing board bus: never create, reset or delete a bus.
  assert.doesNotMatch(maskCpp(cpp), /i2c_new_master_bus|i2c_del_master_bus|i2c_master_bus_reset|i2c_master_bus_add_device/,
    `${label}: the board file never owns the bus`);
  const acquire = body(cpp, 'acquire');
  const busAt = acquire.indexOf(`i2c_master_bus_handle_t bus = ${board.accessor};`);
  assert.ok(busAt !== -1, `${label}: acquire() uses ${board.accessor}`);
  assert.ok(busAt < acquire.indexOf('esp_ldo_acquire_channel('),
    `${label}: no LDO reference before the bus exists`);
  assert.match(read(`${base}/${board.ownerFile || board.driver}`), board.owner, `${label}: bus accessor`);

  // The display's non-adjustable MIPI PHY supply, shared by reference count.
  assert.match(cpp, /constexpr int kMipiPhyLdoChannel = 3;\s*constexpr int kMipiPhyLdoVoltageMv = 2500;/);
  assert.match(acquire, /config\.chan_id = kMipiPhyLdoChannel;\s*config\.voltage_mv = kMipiPhyLdoVoltageMv;/);
  assert.doesNotMatch(maskCpp(cpp), /adjustable|esp_ldo_channel_adjust_voltage/, `${label}: supply never adjusted`);
  assert.match(maskCpp(body(cpp, 'release')), /^\{\s*if \(g_mipi_phy_ldo\) \{\s*esp_ldo_release_channel\(g_mipi_phy_ldo\);\s*g_mipi_phy_ldo = nullptr;\s*\}\s*\}$/,
    `${label}: release() drops only the camera's LDO reference`);
  assert.match(read(`${base}/${board.driver}`), board.ldo, `${label}: display uses LDO 3 at 2500 mV`);

  // Capability flag follows the build, never a literal true.
  const flag = read(`${base}/${board.flag}`);
  assert.ok(flag.indexOf('#include "src/devices/device_select.h"') !== -1 &&
    flag.indexOf('#include "src/devices/device_select.h"') < flag.indexOf('#if defined(HOMETILES_LOCAL_CAMERA)'),
    `${label}: the capability sees HOMETILES_LOCAL_CAMERA`);
  assert.match(flag, /#if defined\(HOMETILES_LOCAL_CAMERA\)\ninline constexpr bool kBuiltinCamera = true;\n#else\ninline constexpr bool kBuiltinCamera = false;\n#endif/);
  assert.match(flag, /Device::Capabilities\{(?:(?:true|false), ){6}kBuiltinCamera\}/, `${label}: capability`);
}

// Waveshare 4B: init_display_power() power-cycles LDO 3 by taking and
// dropping the only reference. The camera may take its reference only after
// that, which the bus accessor guarantees: the bus exists after init_touch(),
// and init() runs the display (with its power cycle) first.
{
  const driver = read('src/devices/waveshare_4b/device_waveshare_4b.cpp');
  assert.equal((maskCpp(driver).match(/^\s*g_i2c_bus = /gm) || []).length, 1, 'One bus assignment');
  assert.match(body(driver, 'init_touch'), /DEV_I2C_Port port = DEV_I2C_Init\(\);\s*g_i2c_bus = port\.bus;/);
  const init = body(driver, 'DeviceWaveshare4B::init');
  assert.ok(init.indexOf('init_display()') !== -1 && init.indexOf('init_display()') < init.indexOf('init_touch()'),
    'The display (with its power cycle) starts before the touch bus');
  const display = body(driver, 'init_display');
  assert.ok(display.indexOf('init_display_power()') < display.indexOf('new Arduino_ESP32DSIPanel('),
    'The power cycle precedes the panel that keeps LDO 3');
  assert.match(body(driver, 'init_display_power'), /^\{\s*power_cycle_mipi_phy\(\);/);
}

// --- Profiles without a proven camera never get the capture path -----------
const neverCamera = ['DEVICE_GUITION_JC1060P470C', 'DEVICE_GUITION_ESP32_4848S040',
  'DEVICE_WAVESHARE_S3_TOUCH_LCD_4', 'DEVICE_WAVESHARE_S3_TOUCH_LCD_4B',
  'DEVICE_LAYOUT_TEST_1024X600', 'DEVICE_LAYOUT_TEST_480X480'];
for (const define of neverCamera) {
  assert.ok(!cameraBlock.includes(`defined(${define})`), `${define} must not select the camera`);
  assert.ok(!cameraSelect.includes(`defined(${define})`), `${define} has no camera board`);
}
for (const dir of ['guition_jc1060p470c', 'guition_esp32_4848s040', 'waveshare_s3_touch_lcd_4',
                   'waveshare_s3_touch_lcd_4b']) {
  assert.ok(!exists(`src/devices/${dir}/local_camera_board.h`) && !exists(`src/devices/${dir}/local_camera_board.cpp`),
    `${dir} has no camera board file`);
  for (const file of fs.readdirSync(path.join(root, 'src/devices', dir)).filter(name => name.endsWith('.h'))) {
    assert.doesNotMatch(read(`src/devices/${dir}/${file}`), /kBuiltinCamera|HOMETILES_LOCAL_CAMERA/, `${dir}/${file}`);
  }
}

// Every device target is classified, so a new profile cannot slip in untested.
const betaCamera = ['DEVICE_WAVESHARE_TOUCH_LCD_8', 'DEVICE_M5STACKS_TAB5', ...boards.map(board => board.define)];
const targets = [...deviceSelect.slice(0, deviceSelect.indexOf('#error "Select only one device target."'))
  .matchAll(/defined\((DEVICE_\w+)\)/g)].map(match => match[1]).filter(name => name.startsWith('DEVICE_'));
const uniqueTargets = [...new Set(targets)].filter(name => !['DEVICE_TAB5', 'DEVICE_WAVESHARE_WIFI6_TOUCH_LCD_8'].includes(name));
assert.deepEqual([...uniqueTargets].sort(),
  ['DEVICE_GUITION_JC8012P4A1_V2', ...betaCamera, ...neverCamera].sort(), 'Every device target is classified');

const cc = ['clang', 'gcc'].find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (cc) {
  const probe = [
    '#include "src/video/local_camera/camera_select.h"',
    '#if defined(HOMETILES_LOCAL_CAMERA)', 'LOCAL_CAMERA_ON HOMETILES_LOCAL_CAMERA_BOARD', '#endif',
    '#if defined(HOMETILES_CAMERA_SENSOR_OV5647)', 'SENSOR_ov5647', '#endif',
    '#if defined(HOMETILES_CAMERA_SENSOR_OV02C10)', 'SENSOR_ov02c10', '#endif',
    '#if defined(HOMETILES_CAMERA_SENSOR_SC202CS)', 'SENSOR_sc202cs', '#endif', ''].join('\n');
  for (const define of uniqueTargets) {
    for (const beta of [false, true]) {
      const result = spawnSync(cc, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', `-D${define}`,
        ...(beta ? ['-DHOMETILES_CAMERA_BETA'] : []), '-I', root, '-'], {encoding: 'utf8', input: probe});
      assert.equal(result.status, 0, result.stderr);
      const expected = define === 'DEVICE_GUITION_JC8012P4A1_V2' || (beta && betaCamera.includes(define));
      const label = `${define}${beta ? ' + HOMETILES_CAMERA_BETA' : ''}`;
      assert.equal(result.stdout.includes('LOCAL_CAMERA_ON'), expected, `${label} camera feature define`);
      const sensors = result.stdout.match(/SENSOR_\w+/g) || [];
      assert.equal(sensors.length, expected ? 1 : 0, `${label}: one sensor driver`);
      const board = boards.find(item => item.define === define);
      if (expected && board) {
        assert.ok(result.stdout.includes(`"src/devices/${board.dir}/local_camera_board.h"`), `${label}: board file`);
        assert.equal(sensors[0], `SENSOR_${board.sensor}`, `${label}: sensor`);
      }
    }
  }
} else {
  console.log('Profile preprocessing skipped: clang or gcc not found');
}

console.log(`Local camera beta boards: ${boards.length} boards, selection, board files, shared bus, PHY supply, modes and profile isolation passed.`);
