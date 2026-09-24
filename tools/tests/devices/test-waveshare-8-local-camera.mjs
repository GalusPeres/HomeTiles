// Waveshare ESP32-P4-WIFI6-Touch-LCD-8 built-in camera (OV5647), camera beta
// builds only: vendored esp_cam_sensor register tables with their hashes, the
// HomeTiles 1280x720/30 fps window and manual exposure overrides, the board
// file sharing the touch I2C bus and the display's PHY supply, and the
// profile isolation (normal 8-inch builds keep the capture path out).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const ws = 'src/devices/waveshare_touch_lcd_8';
const ov = 'src/video/local_camera/sensors/ov5647';
const deviceSelect = read('src/devices/device_select.h');
const cameraSelect = read('src/video/local_camera/camera_select.h');
const version = read('src/core/firmware/firmware_version.h');
const deviceHeader = read(`${ws}/device_waveshare_touch_lcd_8.h`);
const deviceCpp = read(`${ws}/device_waveshare_touch_lcd_8.cpp`);
const boardHeader = read(`${ws}/local_camera_board.h`);
const boardCpp = read(`${ws}/local_camera_board.cpp`);
const settings = read(`${ov}/ov5647_settings.h`);
const sensorHeader = read(`${ov}/ov5647_sensor.h`);
const sensorCpp = read(`${ov}/ov5647_sensor.cpp`);
const provenance = read(`${ov}/PROVENANCE.md`);

// --- Vendored tables: verbatim, hashes recorded in PROVENANCE.md ---------------
const arrayText = name => {
  const match = settings.match(new RegExp(`static const ov5647_reginfo_t ${name}\\[\\] = \\{[\\s\\S]*?\\n\\};`));
  assert.ok(match, `${name} must be vendored`);
  return match[0];
};
const sha = text => crypto.createHash('sha256').update(text).digest('hex');
const reset = arrayText('ov5647_mipi_reset_regs');
const table = arrayText('ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps');
assert.equal(sha(reset), '5fd765a2af1053014f1eac8701617d08a90e29612ef0c38e906b18c2d00fd7e4');
assert.equal(sha(table), '8e9e15528a1d3ea35ea6d8226b54daec7fbe45457e54f2fc5570ce9dac5dcac2');
assert.ok(provenance.includes(sha(reset)) && provenance.includes(sha(table)));
assert.equal((table.match(/\{0x/g) || []).length, 84);
assert.match(settings, /SPDX-License-Identifier: Apache-2\.0/);
assert.ok(fs.existsSync(path.join(root, ov, 'LICENSE-APACHE-2.0.txt')));
// Table facts the HomeTiles overrides rely on.
for (const reg of ['{0x3018, 0x44}', '{0x3808, 0x05}', '{0x3809, 0x00}', '{0x380c, 0x07}',
                   '{0x380d, 0x04}', '{0x3811, 0x04}', '{0x3813, 0x02}', '{0x3820, 0x01}',
                   '{0x3821, 0x03}', '{0x3802, 0x00}', '{0x3803, 0x0c}', '{0x3806, 0x07}',
                   '{0x3807, 0x97}']) {
  assert.ok(table.includes(reg), `table ${reg}`);
}

// --- HomeTiles overrides ------------------------------------------------------------
// The front camera is mounted a quarter turn from the landscape UI (hardware
// test 2026-09-24): a 544x960 portrait window that the PPA turns into 960x544.
assert.match(sensorHeader, /constexpr uint32_t kFrameWidth = 544;\s*constexpr uint32_t kFrameHeight = 960;/);
assert.match(sensorHeader, /constexpr uint32_t kDataLanes = 2;/);
assert.match(sensorHeader, /constexpr uint32_t kLaneBitRateMbps = 441;/);
assert.match(sensorHeader, /constexpr uint16_t kFrameLengthLines = 1640;/);
{
  // 30 fps at the table's pixel clock and line length.
  const fps = 88333333 / (1796 * 1640);
  assert.ok(fps > 29.9 && fps < 30.1, `fps ${fps}`);
  // The table's binned input: x 24..2599 -> 1288 columns, y 12..1943 -> 966 rows.
  const binnedWidth = (2599 - 24 + 1) / 2;
  const binnedRows = (1943 - 12 + 1) / 2;
  assert.equal(binnedWidth, 1288);
  assert.equal(binnedRows, 966);
  // All 960 rows with the flip offset, 544 centred columns with the mirror offset.
  const offsetX = (binnedWidth - 544) / 2;
  assert.equal(offsetX, 372);
  assert.ok(3 + 960 <= binnedRows && offsetX + 1 + 544 <= binnedWidth);
  assert.equal(offsetX % 2, 0, 'Even like the table offset 4: GBRG in the table readout');
  assert.equal(offsetX >> 8, (offsetX + 1) >> 8, 'The mirror only changes the low byte');
  // JPEG: whole 16x8 MCUs, cache-line sized RGB565 buffers.
  assert.ok(960 % 16 === 0 && 544 % 8 === 0 && (960 * 544 * 2) % 128 === 0);
}
assert.match(sensorCpp, /\{0x3808, 0x02\}, \{0x3809, 0x20\},\s*\/\/ x output size 544/);
assert.match(sensorCpp, /\{0x380a, 0x03\}, \{0x380b, 0xc0\},\s*\/\/ y output size 960/);
assert.match(sensorCpp, /constexpr uint16_t kOffsetXCentred = \(kBinnedWidth - kFrameWidth\) \/ 2;/);
assert.match(sensorCpp, /\{kRegIspXOffsetHigh, kOffsetXHigh\},\s*\{kRegIspXOffset, kOffsetX\},/);
assert.doesNotMatch(sensorCpp, /\{0x3802,|\{0x3806,/, 'The table y window stays');
assert.match(sensorCpp, /\{kRegAecAgc, kAecAgcManual\}/, 'Manual AEC/AGC: the ISP pipeline owns exposure');
assert.match(sensorCpp, /constexpr uint8_t kAecAgcManual = 0x03;/);
assert.match(sensorCpp, /\{kRegAwbManual, 0x01\}/, 'Manual white balance at 1x gains');
assert.match(sensorCpp, /write_table\(this, ov5647_mipi_reset_regs[\s\S]*?write_table\(this, ov5647_mipi_2lane_24Minput_1280x960_raw10_45fps[\s\S]*?write_table\(this, kWindow544x960/,
  'Reset, vendored table, then the overrides');
assert.doesNotMatch(sensorCpp, /0x3a0f|0x3a10|0x3a1b|0x3a1e|0x3c0/, 'No on-sensor AE target or banding writes');
// Exposure in 1/16 lines over 0x3500..0x3502, gain in 1/16 over 0x350a/0x350b,
// frame length first.
assert.match(sensorCpp, /const uint32_t exposure = static_cast<uint32_t>\(lines\) << 4;/);
assert.ok(sensorCpp.indexOf('write(kRegFrameLengthHigh') < sensorCpp.indexOf('write(kRegExposureHigh'),
  'The frame length is written before the exposure');
{
  const encode = lines => { const v = lines << 4; return [(v >> 16) & 0x0f, (v >> 8) & 0xff, v & 0xf0]; };
  assert.deepEqual(encode(1636), [0x00, 0x66, 0x40]);
  const tableEncode = lines => [(lines >> 12) & 0x0f, (lines >> 4) & 0xff, (lines << 4) & 0xf0];
  assert.deepEqual(tableEncode(1636), encode(1636), 'Default exposure bytes match setExposure()');
  assert.ok((4 * 1640 - 4) << 4 < (1 << 20), 'Maximum exposure fits 20 bits');
}
// Orientation: flip bits only, window offsets keep the Bayer phase, readback.
assert.match(sensorCpp, /constexpr uint16_t kRegTimingV = 0x3820;\s*constexpr uint16_t kRegTimingH = 0x3821;/);
assert.match(sensorCpp, /const uint8_t x = mirrored \? kOffsetX : kOffsetXMirrored;/);
assert.match(sensorCpp, /return ESP_ERR_INVALID_RESPONSE;/);
assert.match(sensorCpp, /if \(err == ESP_OK\) err = write\(kRegStreamMode, 0x01\);/);
assert.match(sensorCpp, /esp_err_t first = write\(kRegStreamMode, 0x00\);/, 'Standby first on stop');
for (const file of [`${ov}/ov5647_sensor.cpp`, `${ov}/ov5647_sensor.h`]) {
  const text = read(file);
  assert.match(text, /#if defined\(HOMETILES_CAMERA_SENSOR_OV5647\)/, `${file} must be sensor guarded`);
  assert.match(text.trimEnd(), /#endif\s*\/\/ defined\(HOMETILES_CAMERA_SENSOR_OV5647\)$/, file);
}
assert.match(sensorCpp, /i2c_master_bus_add_device\(bus, &config, &device_\)/);
assert.doesNotMatch(sensorCpp, /i2c_new_master_bus|i2c_del_master_bus/, 'The sensor never owns the bus');

// --- Board file and profile -----------------------------------------------------------
assert.match(cameraSelect,
  /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_8\)\n#define HOMETILES_CAMERA_SENSOR_OV5647 1\n#define HOMETILES_LOCAL_CAMERA_BOARD "src\/devices\/waveshare_touch_lcd_8\/local_camera_board\.h"/);
for (const text of [boardHeader, boardCpp]) {
  assert.match(text, /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_8\) && defined\(HOMETILES_LOCAL_CAMERA\)/);
}
assert.match(boardHeader, /using Sensor = ov5647::Sensor;/);
assert.match(boardHeader, /COLOR_RAW_ELEMENT_ORDER_GBRG/);
// Quarter-turn mounting: the JPEG is the portrait frame (544x960, 4:2:0);
// the Bridge turns it (the PPA turn on the panel made the display sluggish).
assert.match(boardHeader, /static_cast<uint16_t>\(ov5647::kFrameWidth\),\s*static_cast<uint16_t>\(ov5647::kFrameHeight\),\s*\/\/[^\n]*\n\s*\/\/[^\n]*\n\s*static_cast<uint16_t>\(ov5647::kFrameWidth\),\s*static_cast<uint16_t>\(ov5647::kFrameHeight\),/);
assert.ok(544 % 16 === 0 && 960 % 16 === 0, 'Whole 16x16 MCUs for the lossless turn');
assert.match(boardHeader, /true,   \/\/ The table reads out mirrored, like the Waveshare example\.\s*false,\s*(\/\/[^\n]*\n\s*)+true,\s*COLOR_RAW_ELEMENT_ORDER_GBRG,/,
  'mirror, rotate_180 false, quarter_turn true');
assert.match(boardCpp, /DeviceWaveshareTouchLCD8::sharedI2cBus\(\)/);
assert.match(boardCpp, /kMipiPhyLdoChannel = 3;/);
assert.match(boardCpp, /kMipiPhyLdoVoltageMv = 2500;/);
assert.match(deviceCpp, /ldo_cfg\.chan_id = 3;\s*ldo_cfg\.voltage_mv = 2500;/, 'Same supply as the display');
assert.doesNotMatch(boardCpp, /i2c_new_master_bus|esp_ldo_channel_adjust_voltage/);
assert.match(deviceCpp, /i2c_master_bus_handle_t DeviceWaveshareTouchLCD8::sharedI2cBus\(\) \{\s*return g_i2c_ready \? g_i2c\.bus : nullptr;/);
assert.match(deviceHeader, /#if defined\(HOMETILES_LOCAL_CAMERA\)\ninline constexpr bool kBuiltinCamera = true;\n#else\ninline constexpr bool kBuiltinCamera = false;\n#endif/);
assert.match(deviceHeader, /Device::Capabilities\{false, false, false, false, true, false, kBuiltinCamera\}/);
assert.match(version, /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_8\) && \\\n    defined\(HOMETILES_CAMERA_BETA\)\n#undef FW_VERSION/);

// Normal 8-inch builds keep the camera out; camera beta builds enable it.
const cc = ['clang', 'gcc'].find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (cc) {
  const probe = path.join(root, 'build/tests/local-camera-profile/probe-ws8.cpp');
  fs.mkdirSync(path.dirname(probe), {recursive: true});
  fs.writeFileSync(probe, '#include "src/devices/device_select.h"\n#if defined(HOMETILES_LOCAL_CAMERA)\nLOCAL_CAMERA_ON\n#endif\n');
  for (const [defines, expected] of [
    [['-DDEVICE_WAVESHARE_TOUCH_LCD_8'], false],
    [['-DDEVICE_WAVESHARE_TOUCH_LCD_8', '-DHOMETILES_CAMERA_BETA'], true],
    [['-DDEVICE_WAVESHARE_TOUCH_LCD_7', '-DHOMETILES_CAMERA_BETA'], false],
  ]) {
    const result = spawnSync(cc, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', ...defines,
      '-I', root, probe], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stdout.includes('LOCAL_CAMERA_ON'), expected, defines.join(' '));
  }
} else {
  console.log('Profile preprocessing skipped: clang or gcc not found');
}

console.log('Waveshare 8-inch OV5647 camera: vendored tables, overrides, board file and beta isolation passed.');
