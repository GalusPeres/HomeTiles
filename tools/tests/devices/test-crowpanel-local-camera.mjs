// Elecrow CrowPanel Advanced 10.1 camera (PR #143, "With 2MP Camera"
// variant): an SC2336 on the CSI connector, its own SCCB bus on I2C1
// (SCL 13, SDA 12, Elecrow BSP of IDF lesson 13) and the display's MIPI PHY
// supply. The SC2336 driver follows esp_cam_sensor 2.6.0 and the SC202CS of
// the same family; the driver runs on the host against a recording SCCB
// transport: chip ID, the vendored 2-lane RAW8 1280x720 table with its
// overrides, orientation, the turned window and the gain/exposure encoding.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {compileAndRun} from '../../lib/cpp-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const sc = 'src/video/local_camera/sensors/sc2336';
const settings = read(`${sc}/sc2336_settings.h`);
const sensorHeader = read(`${sc}/sc2336_sensor.h`);
const boardHeader = read('src/devices/elecrow_crowpanel_10_1/local_camera_board.h');
const boardCpp = read('src/devices/elecrow_crowpanel_10_1/local_camera_board.cpp');
const driver = read('src/devices/elecrow_crowpanel_10_1/device_elecrow_crowpanel_10_1.cpp');

// ---- Vendored data and provenance -------------------------------------------------
const table = settings.match(/static const sc2336_reginfo_t sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps\[\] = \{[\s\S]*?\n\};/);
assert.ok(table, 'the 2-lane RAW8 1280x720 table');
assert.equal((table[0].match(/\{0x|\{SC2336_REG_SLEEP_MODE/g) || []).length, 166, 'every upstream entry');
assert.match(table[0], /^static const sc2336_reginfo_t [^\n]+\n    \{0x0103, 0x01\},\n    \{SC2336_REG_SLEEP_MODE, 0x00\},/);
assert.match(table[0], /\{SC2336_REG_END, 0x00\},\n\};$/);
assert.match(read(`${sc}/PROVENANCE.md`), /dbbdcbd5e25bdb6561d3f7fd0c5149641ba5536f/);
assert.ok(fs.existsSync(path.join(root, `${sc}/LICENSE-APACHE-2.0.txt`)));
assert.match(sensorHeader, /constexpr uint16_t kChipId = 0xcb3a;/);
assert.match(sensorHeader, /constexpr uint16_t kSccbAddress = 0x30;/);
assert.match(sensorHeader, /constexpr uint32_t kDataLanes = 2;/);
assert.match(sensorHeader, /constexpr uint32_t kLaneBitRateMbps = 336;/);

// ---- Board file: own bus, shared PHY supply, selection -------------------------
const guard = 'defined(DEVICE_ELECROW_CROWPANEL_10_1) && defined(HOMETILES_LOCAL_CAMERA)';
for (const text of [boardHeader, boardCpp]) assert.ok(text.includes(`#if ${guard}\n`) && text.trimEnd().endsWith(`#endif  // ${guard}`));
assert.match(boardHeader, /using SccbBus = sc2336::Transport\*;\nusing Sensor = sc2336::Sensor;/);
assert.match(boardCpp, /constexpr i2c_port_t kSccbPort = I2C_NUM_1;\nconstexpr gpio_num_t kSccbScl = GPIO_NUM_13;\nconstexpr gpio_num_t kSccbSda = GPIO_NUM_12;/,
  'the camera bus of the Elecrow BSP, not the touch bus');
assert.match(read('src/devices/elecrow_crowpanel_10_1/vendor/i2c.h'), /#define EXAMPLE_I2C_MASTER_NUM I2C_NUM_0/, 'touch keeps I2C0');
assert.match(boardCpp, /constexpr int kMipiPhyLdoChannel = 3;\nconstexpr int kMipiPhyLdoVoltageMv = 2500;/);
assert.match(driver, /constexpr int kMipiPhyLdoChannel = 3;\nconstexpr int kMipiPhyLdoVoltageMv = 2500;/, 'the display takes the same supply');
assert.match(boardCpp, /void release\(\) \{\n  if \(g_mipi_phy_ldo\) \{\n[^\n]*\n    esp_ldo_release_channel\(g_mipi_phy_ldo\);\n    g_mipi_phy_ldo = nullptr;\n  \}/,
  'release drops only the camera\'s LDO reference');
assert.match(read('src/video/local_camera/camera_select.h'),
  /#if defined\(DEVICE_ELECROW_CROWPANEL_10_1\)\n#define HOMETILES_CAMERA_SENSOR_SC2336 1\n#define HOMETILES_LOCAL_CAMERA_BOARD "src\/devices\/elecrow_crowpanel_10_1\/local_camera_board\.h"\n#endif/);
const select = read('src/devices/device_select.h');
const cameraBlock = select.slice(select.indexOf('// Built-in camera on every'), select.indexOf('#define HOMETILES_LOCAL_CAMERA 1'));
assert.ok(cameraBlock.includes('defined(DEVICE_ELECROW_CROWPANEL_10_1)'), 'every CrowPanel build carries the camera (off until enabled)');
const profile = read('src/devices/elecrow_crowpanel_10_1/profile.h');
assert.match(profile, /#if defined\(HOMETILES_LOCAL_CAMERA\)\ninline constexpr bool kBuiltinCamera = true;\n#else\ninline constexpr bool kBuiltinCamera = false;\n#endif/);
assert.match(profile, /Device::Capabilities\{(?:(?:true|false), ){6}kBuiltinCamera\}/);

// ---- The driver on the host -------------------------------------------------------
const harness = String.raw`
#include <cstdio>
#include <map>
#include "src/video/local_camera/sensors/sc2336/sc2336_sensor.h"
struct Recorder final : sc2336::Transport {
  std::map<uint16_t, uint8_t> regs;
  int writes = 0;
  uint16_t first = 0;
  uint8_t first_value = 0;
  esp_err_t probe() override { return ESP_OK; }
  esp_err_t write(uint16_t reg, uint8_t value) override {
    if (writes++ == 0) { first = reg; first_value = value; }
    regs[reg] = value;
    return ESP_OK;
  }
  esp_err_t read(uint16_t reg, uint8_t* value) override { *value = regs[reg]; return ESP_OK; }
};
int main() {
  Recorder bus;
  sc2336::Sensor sensor;
  sensor.attach(&bus);
  bus.regs[0x3107] = 0xcb; bus.regs[0x3108] = 0x3a;
  uint16_t id = 0;
  const bool found = sensor.probe(&id) == ESP_OK;
  std::printf("probe %d %04x\n", found, id);
  bus.regs[0x3108] = 0x52;
  std::printf("wrong %d\n", sensor.probe(&id) == ESP_ERR_NOT_FOUND);
  bus.writes = 0;
  std::printf("load %d\n", sensor.loadDefaultMode(true, false) == ESP_OK);
  std::printf("first %04x %02x writes %d\n", bus.first, bus.first_value, bus.writes);
  std::printf("vts %02x %02x exp %02x %02x %02x stream %02x orient %02x start %02x %02x %02x\n",
              bus.regs[0x320e], bus.regs[0x320f], bus.regs[0x3e00], bus.regs[0x3e01], bus.regs[0x3e02],
              bus.regs[0x0100], bus.regs[0x3221], bus.regs[0x3210], bus.regs[0x3211], bus.regs[0x3213]);
  // Call first, then print: argument evaluation order is unspecified.
  const bool turned = sensor.loadDefaultMode(false, true) == ESP_OK;
  std::printf("turned %d width %02x %02x start %02x %02x\n", turned,
              bus.regs[0x3208], bus.regs[0x3209], bus.regs[0x3210], bus.regs[0x3211]);
  const bool flipped = sensor.setOrientation(true, true) == ESP_OK;
  std::printf("flip %d %02x\n", flipped, bus.regs[0x3221] & 0x66);
  for (unsigned gain : {16u, 32u, 48u, 64u, 128u, 256u, 512u}) {
    sensor.setExposure(100, static_cast<uint16_t>(gain));
    std::printf("gain %u %02x %02x %02x\n", gain, bus.regs[0x3e09], bus.regs[0x3e06], bus.regs[0x3e07]);
  }
  sensor.setExposure(3000, 16);
  std::printf("long %02x %02x %02x %02x %02x\n", bus.regs[0x320e], bus.regs[0x320f],
              bus.regs[0x3e00], bus.regs[0x3e01], bus.regs[0x3e02]);
  const bool streaming = sensor.setStream(true) == ESP_OK;
  std::printf("stream %d %02x\n", streaming, bus.regs[0x0100]);
  return 0;
}
`;
const out = compileAndRun({
  label: 'SC2336 driver',
  harness,
  files: {
    'src/video/local_camera/camera_select.h': '#pragma once\n#define HOMETILES_CAMERA_SENSOR_SC2336 1\n',
    'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG 0x102\n'
      + '#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NOT_FOUND 0x105\n#define ESP_ERR_INVALID_RESPONSE 0x108\n',
    'freertos/FreeRTOS.h': '#pragma once\n#include <cstdint>\n#define pdMS_TO_TICKS(ms) (ms)\n',
    'freertos/task.h': '#pragma once\ninline void vTaskDelay(uint32_t) {}\n',
  },
  sources: [`${sc}/sc2336_sensor.cpp`],
});
if (out !== null) {
  const lines = out.trim().split('\n');
  assert.deepEqual(lines, [
    'probe 1 cb3a',
    'wrong 1',
    'load 1',
    // The table opens with the software reset; 165 table writes, the overrides,
    // stream off and the orientation.
    lines[3].startsWith('first 0103 01 writes ') ? lines[3] : 'first 0103 01 writes ?',
    // VTS 1250, exposure VTS - 6 = 1244 lines as lines << 4, stream off, the
    // board mirror (bits 2:1), window start 16/4 as the table.
    'vts 04 e2 exp 00 4d c0 stream 00 orient 06 start 00 10 04',
    // Turned: 544 wide from x 384 on the image centre.
    'turned 1 width 02 20 start 01 80',
    'flip 1 66',
    // Analog coarse first with the upstream codes, the rest digital fine.
    'gain 16 00 00 80',
    'gain 32 08 00 80',
    'gain 48 08 00 c0',
    'gain 64 09 00 80',
    'gain 128 0b 00 80',
    'gain 256 0f 00 80',
    'gain 512 0f 00 fc',
    // A long exposure stretches the frame (3000 + 6 lines) first.
    'long 0b be 00 bb 80',
    'stream 1 01',
  ], out);
  assert.ok(Number(lines[3].split(' ').pop()) > 165, 'every table entry is written');
}
console.log('CrowPanel SC2336 camera: vendored table, own SCCB bus, shared PHY supply, selection, and the driver\'s register writes on the host');
