// M5Stack Tab5 built-in camera (SC2356 = esp_cam_sensor SC202CS), camera beta
// builds only: the vendored RAW8 1280x720 table with its hash, the gain and
// exposure encoding, the M5Unified SCCB transport (M5Unified owns the I2C
// port), the reset line on the IO expander, RAW8 in the shared core, and the
// profile isolation.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const tab5 = 'src/devices/m5stacks_tab5';
const sc = 'src/video/local_camera/sensors/sc202cs';
const settings = read(`${sc}/sc202cs_settings.h`);
const sensorHeader = read(`${sc}/sc202cs_sensor.h`);
const sensorCpp = read(`${sc}/sc202cs_sensor.cpp`);
const provenance = read(`${sc}/PROVENANCE.md`);
const boardHeader = read(`${tab5}/local_camera_board.h`);
const boardCpp = read(`${tab5}/local_camera_board.cpp`);
const deviceHeader = read(`${tab5}/device_m5stacks_tab5.h`);
const cameraSelect = read('src/video/local_camera/camera_select.h');
const deviceSelect = read('src/devices/device_select.h');
const version = read('src/core/firmware/firmware_version.h');
const driver = read('src/video/local_camera/camera_driver.h');
const service = read('src/video/local_camera/local_camera.cpp');

// --- Vendored table ----------------------------------------------------------------
const table = settings.match(/static const sc202cs_reginfo_t sc202cs_mipi_1lane_24Minput_1280x720_raw8_30fps\[\] = \{[\s\S]*?\n\};/);
assert.ok(table, 'RAW8 1280x720 table vendored');
const sha = crypto.createHash('sha256').update(table[0]).digest('hex');
assert.equal(sha, '3b33c0336463e7a4ed6879e823dbf00527e47758211f396a9941959cc19f5ffb');
assert.ok(provenance.includes(sha));
assert.equal((table[0].match(/\{0x|\{SC202CS_REG_SLEEP_MODE/g) || []).length, 131);
assert.match(settings, /SPDX-License-Identifier: Apache-2\.0/);
assert.ok(fs.existsSync(path.join(root, sc, 'LICENSE-APACHE-2.0.txt')));
// Table facts the driver relies on: reset first, 1280x720 output, window
// start 4/4 inside the 1288x728 array window, default exposure 0x3dc, 1x gain.
assert.match(table[0], /\{0x0103, 0x01\},\s*\{SC202CS_REG_SLEEP_MODE, 0x00\},/);
for (const reg of ['{0x3208, 0x05}', '{0x3209, 0x00}', '{0x320a, 0x02}', '{0x320b, 0xd0}',
                   '{0x3211, 0x04}', '{0x3213, 0x04}', '{0x3204, 0x05}', '{0x3205, 0xa7}',
                   '{0x3206, 0x03}', '{0x3207, 0xc7}', '{0x3e01, 0x3d}', '{0x3e02, 0xc0}',
                   '{0x3e09, 0x00}']) {
  assert.ok(table[0].includes(reg), `table ${reg}`);
}
assert.equal(((0x05a7 - 0x00a0 + 1)), 1288, 'array window width');
assert.equal(((0x03c7 - 0x00f0 + 1)), 728, 'array window height');
assert.doesNotMatch(table[0], /\{0x320e|\{0x320f/, 'The table leaves VTS at its reset value');

// --- Sensor constants and encoding ----------------------------------------------------
assert.match(sensorHeader, /constexpr uint16_t kChipId = 0xeb52;/);
assert.match(sensorHeader, /constexpr uint16_t kSccbAddress = 0x36;/);
assert.match(sensorHeader, /constexpr uint32_t kSccbFrequencyHz = 400000;/);
assert.match(sensorHeader, /constexpr uint32_t kFrameWidth = 1280;\s*constexpr uint32_t kFrameHeight = 720;/);
assert.match(sensorHeader, /constexpr uint32_t kDataLanes = 1;\s*constexpr uint8_t kRawBits = 8;/);
assert.match(sensorHeader, /constexpr uint32_t kLaneBitRateMbps = 576;/);
assert.match(sensorHeader, /constexpr uint16_t kFrameLengthLines = 1250;/);
assert.match(sensorHeader, /constexpr uint16_t kExposureMarginLines = 6;/);
{
  const fps = 72e6 / (1920 * 1250);
  assert.ok(Math.abs(fps - 30) < 0.01, `fps ${fps}`);
}
// Gain split: analog coarse first, digital fine for the rest (JS model of
// gainRegistersFor()).
{
  const split = gain => {
    let analogX16 = 16;
    let analog = 0;
    while (analogX16 < 256 && analogX16 * 2 <= gain) { analogX16 *= 2; analog = (analog << 1) | 1; }
    let fine = Math.floor((gain * 0x80 + analogX16 / 2) / analogX16);
    fine = Math.min(0xfc, Math.max(0x80, fine));
    return [analog, fine];
  };
  assert.deepEqual(split(16), [0x00, 0x80]);
  assert.deepEqual(split(40), [0x01, 0xa0]);
  assert.deepEqual(split(256), [0x0f, 0x80]);
  assert.deepEqual(split(504), [0x0f, 0xfc]);
  for (let gain = 16; gain <= 504; ++gain) {
    const [analog, fine] = split(gain);
    const total = (analog + 1) * fine / 0x80 * 16;
    assert.ok(Math.abs(total - gain) <= (analog + 1) / 2 + 0.5, `gain ${gain} -> ${total}`);
  }
}
assert.match(sensorHeader, /constexpr uint16_t kMaxTotalGainX16 = kMaxGainX16 \* 0xfc \/ 0x80;/);
assert.match(sensorCpp, /write\(SC202CS_REG_SHUTTER_TIME_H, static_cast<uint8_t>\(\(lines >> 12\) & 0x0f\)\)/);
assert.match(sensorCpp, /write\(SC202CS_REG_SHUTTER_TIME_L, static_cast<uint8_t>\(\(lines & 0x0f\) << 4\)\)/);
assert.ok(sensorCpp.indexOf('write(SC202CS_REG_TOTAL_HEIGHT_H') < sensorCpp.indexOf('write(SC202CS_REG_SHUTTER_TIME_H'),
  'Frame length before exposure');
assert.match(sensorCpp, /if \(err == ESP_OK && table\[i\]\.reg == kRegSoftReset\) \{\s*vTaskDelay\(pdMS_TO_TICKS\(kSoftResetDelayMs\)\);/);
assert.match(sensorCpp, /write_table\(this, sc202cs_mipi_1lane_24Minput_1280x720_raw8_30fps[\s\S]*?write_table\(this, kOverrides/);
// Orientation: 0x3221 bits, window start follows the flips, readback.
assert.match(sensorCpp, /constexpr uint8_t kMirrorBits = 0x06;\s*constexpr uint8_t kFlipBits = 0x60;/);
assert.match(sensorCpp, /return ESP_ERR_INVALID_RESPONSE;/);
assert.doesNotMatch(sensorCpp, /i2c_master|Wire|M5\./, 'The sensor only uses the board transport');
for (const file of [`${sc}/sc202cs_sensor.cpp`, `${sc}/sc202cs_sensor.h`]) {
  const text = read(file);
  assert.match(text, /#if defined\(HOMETILES_CAMERA_SENSOR_SC202CS\)/, file);
  assert.match(text.trimEnd(), /#endif\s*\/\/ defined\(HOMETILES_CAMERA_SENSOR_SC202CS\)$/, file);
}

// --- Board file --------------------------------------------------------------------------
for (const text of [boardHeader, boardCpp]) {
  assert.match(text, /#if defined\(DEVICE_M5STACKS_TAB5\) && defined\(HOMETILES_LOCAL_CAMERA\)/);
}
assert.match(boardHeader, /using SccbBus = sc202cs::Transport\*;/);
assert.match(boardHeader, /using Sensor = sc202cs::Sensor;/);
assert.match(boardHeader, /COLOR_RAW_ELEMENT_ORDER_BGGR/);
// Mounting (hardware 2026-09-25): the demo mirror plus a 180 degree turn.
assert.match(boardHeader, /true,\s*\/\/ Mirrored like the M5Stack demo[^\n]*\n\s*true,\s*\/\/ Upside down in the landscape UI/);
// The SC202CS keeps BGGR under mirror/flip itself (upstream writes 0x3221
// only); a one-pixel window move per flip turned the image green/magenta.
assert.doesNotMatch(sensorCpp, /kStartXMirrored|kStartYFlipped/);
assert.match(sensorCpp, /const uint8_t x = kStartX;\s*const uint8_t y = kStartY;/);
assert.match(sensorCpp, /constexpr uint8_t kStartX = 0x04;\s*constexpr uint8_t kStartY = 0x04;/);
assert.match(boardHeader, /false,\s*\/\/ The mode sends no MIPI line-sync packets\./);
assert.match(boardHeader, /sc202cs::kRawBits,\s*\};/);
// SCCB only through M5Unified (it owns I2C0 with a per-port mutex).
assert.match(boardCpp, /M5\.In_I2C\.start\(sc202cs::kSccbAddress, false, sc202cs::kSccbFrequencyHz\) &&\s*M5\.In_I2C\.write\(buffer, sizeof\(buffer\)\) && M5\.In_I2C\.stop\(\);/);
assert.match(boardCpp, /M5\.In_I2C\.restart\(sc202cs::kSccbAddress, true, sc202cs::kSccbFrequencyHz\) &&\s*M5\.In_I2C\.read\(value, 1, true\) && M5\.In_I2C\.stop\(\);/);
assert.doesNotMatch(boardCpp, /i2c_new_master_bus|i2c_master_bus_add_device|i2cBusHandle|Wire\./,
  'No second I2C driver on the M5Unified port');
// Reset: expander 0 (0x43) pin 6, level before direction, 100 ms.
assert.match(boardCpp, /constexpr uint8_t kResetExpander = 0;\s*constexpr uint8_t kResetPin = 6;\s*constexpr uint32_t kResetReleaseDelayMs = 100;/);
assert.match(boardCpp, /expander\.digitalWrite\(kResetPin, true\);\s*expander\.setDirection\(kResetPin, true\);\s*expander\.setHighImpedance\(kResetPin, false\);/);
assert.match(boardCpp, /if \(M5\.getBoard\(\) != m5::board_t::board_M5Tab5\)/);
assert.match(boardCpp, /kMipiPhyLdoChannel = 3;/);
assert.match(boardCpp, /kMipiPhyLdoVoltageMv = 2500;/);
assert.doesNotMatch(boardCpp, /esp_ldo_channel_adjust_voltage|flags\.adjustable/);

// --- Shared core: RAW8 and the board bus type -----------------------------------------------
assert.match(driver, /uint8_t raw_bits = 10;\s*\};/);
assert.match(driver, /local_camera::BoardError acquire\(SccbBus\* sccb_bus\);/);
assert.match(service, /board::SccbBus g_sccb_bus\{\};/);
assert.match(service, /csi_config\.input_data_color_type = kRaw8 \? CAM_CTLR_COLOR_RAW8 : CAM_CTLR_COLOR_RAW10;/);
assert.match(service, /isp_config\.input_data_color_type = kRaw8 \? ISP_COLOR_RAW8 : ISP_COLOR_RAW10;/);
for (const board of ['src/devices/guition_jc8012p4a1_v2/local_camera_board.h',
                     'src/devices/waveshare_touch_lcd_8/local_camera_board.h']) {
  assert.match(read(board), /using SccbBus = i2c_master_bus_handle_t;/, board);
}

// --- Profile ----------------------------------------------------------------------------------
assert.match(cameraSelect,
  /#if defined\(DEVICE_M5STACKS_TAB5\)\n#define HOMETILES_CAMERA_SENSOR_SC202CS 1\n#define HOMETILES_LOCAL_CAMERA_BOARD "src\/devices\/m5stacks_tab5\/local_camera_board\.h"/);
assert.match(deviceSelect, /\(defined\(DEVICE_M5STACKS_TAB5\) && defined\(HOMETILES_CAMERA_BETA\)\)\n#define HOMETILES_LOCAL_CAMERA 1/);
assert.match(deviceHeader, /#if defined\(HOMETILES_LOCAL_CAMERA\)\ninline constexpr bool kBuiltinCamera = true;\n#else\ninline constexpr bool kBuiltinCamera = false;\n#endif/);
assert.match(deviceHeader, /Device::Capabilities\{false, false, false, false, false, false, kBuiltinCamera\}/);
assert.match(version, /#if defined\(DEVICE_M5STACKS_TAB5\) && \\\n    defined\(HOMETILES_CAMERA_BETA\)\n#undef FW_VERSION/);

const cc = ['clang', 'gcc'].find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (cc) {
  const probe = path.join(root, 'build/tests/local-camera-profile/probe-tab5.cpp');
  fs.mkdirSync(path.dirname(probe), {recursive: true});
  fs.writeFileSync(probe, '#include "src/devices/device_select.h"\n#if defined(HOMETILES_LOCAL_CAMERA)\nLOCAL_CAMERA_ON\n#endif\n');
  for (const [defines, expected] of [
    [['-DDEVICE_M5STACKS_TAB5'], false],
    [['-DDEVICE_M5STACKS_TAB5', '-DHOMETILES_CAMERA_BETA'], true],
  ]) {
    const result = spawnSync(cc, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', ...defines,
      '-I', root, probe], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stdout.includes('LOCAL_CAMERA_ON'), expected, defines.join(' '));
  }
} else {
  console.log('Profile preprocessing skipped: clang or gcc not found');
}

console.log('Tab5 SC202CS camera: vendored table, encoding, M5Unified transport, reset line, RAW8 core and beta isolation passed.');
