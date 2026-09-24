// Built-in camera: a shared core (src/video/local_camera) that never names a
// device, shared sensor drivers (sensors/<name>) and one board file per device.
// First board: Guition JC8012P4A1 V2 with the OV02C10 - vendored register
// data, exact-profile isolation, shared-bus and shared-DMA rules, and the
// capture pipeline APIs that exist on ESP32-P4 before v3.0.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const v2 = 'src/devices/guition_jc8012p4a1_v2';
const ov = 'src/video/local_camera/sensors/ov02c10';
const settings = read(`${ov}/ov02c10_settings.h`);
const sensor = read(`${ov}/ov02c10_sensor.cpp`);
// HomeTiles window override after the vendored table: 1280x720 inside the
// table's 1296x732 array window, offsets 11/8 (the former centred crop).
assert.match(sensor, /\{0x3808, 0x05\}, \{0x3809, 0x00\},[\s\S]*?\{0x380a, 0x02\}, \{0x380b, 0xd0\},/);
assert.match(sensor, /constexpr uint8_t kWindowX = 0x0b;\s*constexpr uint8_t kWindowXMirrored = 0x08;\s*constexpr uint8_t kWindowY = 0x08;\s*constexpr uint8_t kWindowYFlipped = 0x07;/);
assert.match(sensor, /ov02c10_input_24M_MIPI_1lane_raw10_1288x728_30fps,[\s\S]*?write_table\(this, kWindow1280x720, &Sensor::write\);[\s\S]*?setStream\(false\);[\s\S]*?setMirror\(mirror\)/,
  'The window override follows the vendored table, before standby and the demo mirror');
// Orientation: only FORMAT1 bits 4/3 change; the window moves with the flips
// and the registers are read back.
assert.match(sensor, /constexpr uint16_t kRegFormat1 = 0x3820;\s*constexpr uint8_t kFormat1Flip = 0x10;\s*constexpr uint8_t kFormat1Mirror = 0x08;/);
assert.match(sensor, /format1 & ~\(kFormat1Flip \| kFormat1Mirror\)/);
assert.match(sensor, /read_format1 != wanted\) return ESP_ERR_INVALID_RESPONSE;/);
assert.doesNotMatch(sensor, /0x3208|0x5002/, 'No group hold or manual CFA writes');
// Sensor digital gain above the analog maximum (Linux ov02c10.c 0x350a,
// gain << 6), written only when it changes; the table value is 1x.
assert.match(sensor, /constexpr uint16_t kRegDigitalGainHigh = 0x350a;/);
assert.match(sensor, /constexpr uint32_t kTableDigitalGainReg = 0x010041;/);
assert.match(sensor, /digital_reg = \(digital_x1024 << 6\) \| \(kTableDigitalGainReg & 0x3f\);/);
assert.match(sensor, /if \(err == ESP_OK && digital_reg != digital_gain_reg_\)/);
const sensorHeader = read(`${ov}/ov02c10_sensor.h`);
const provenance = read(`${ov}/PROVENANCE.md`);
const hardware = read(`${v2}/local_camera_board.cpp`);
const boardHeader = read(`${v2}/local_camera_board.h`);
const cameraSelect = read('src/video/local_camera/camera_select.h');
const touch = read(`${v2}/vendor/gsl3680_touch.cpp`);
const service = read('src/video/local_camera/local_camera.cpp');
const deviceSelect = read('src/devices/device_select.h');
const deviceTypes = read('src/devices/device_types.h');

// --- Vendored Espressif table: verbatim and hash-pinned ---------------------
function block(name) {
  const start = settings.indexOf(`static const ov02c10_reginfo_t ${name}[] = {`);
  assert.notEqual(start, -1, name);
  return settings.slice(start, settings.indexOf('};', start) + 2);
}
const sha = text => crypto.createHash('sha256').update(text).digest('hex');
const resetRegs = block('ov02c10_mipi_reset_regs');
const modeRegs = block('ov02c10_input_24M_MIPI_1lane_raw10_1288x728_30fps');
assert.equal(sha(resetRegs), 'c2b61a8c472e26ab8056bf59254fe5407a74ab0127499b1787e296686c51544a');
assert.equal(sha(modeRegs), 'cfacecf0d3803f0c8f646c9a98ead9a96860dfc5427fdcad6a3d1413062704da');
for (const digest of [sha(resetRegs), sha(modeRegs)]) {
  assert.ok(provenance.includes(digest), 'PROVENANCE.md must record the table hash');
}
const reg = address => {
  const match = modeRegs.match(new RegExp(`\\{0x${address}, 0x([0-9a-fA-F]{2})\\}`));
  assert.ok(match, `register 0x${address}`);
  return Number.parseInt(match[1], 16);
};
assert.equal((reg('3808') << 8) | reg('3809'), 1288, 'output width');
assert.equal((reg('380a') << 8) | reg('380b'), 728, 'output height');
assert.equal((reg('380e') << 8) | reg('380f'), 1164, 'VTS');
assert.equal((reg('3501') << 8) | reg('3502'), 0x046c, 'table exposure');
assert.equal(reg('4800'), 0x64, 'line-sync packets enabled');
assert.equal((modeRegs.match(/\{0x[0-9a-fA-F]{4}, 0x[0-9a-fA-F]{2}\}/g) || []).length, 224);
assert.match(settings, /SPDX-License-Identifier: Apache-2\.0/);
assert.ok(fs.existsSync(path.join(root, ov, 'LICENSE-APACHE-2.0.txt')));

// Demo facts: SCCB 0x36 at 100 kHz, chip ID 0x5602, 1 lane at 400 Mbit/s.
assert.match(sensorHeader, /kSccbAddress = 0x36;/);
assert.match(sensorHeader, /kSccbFrequencyHz = 100000;/);
assert.match(sensorHeader, /kChipId = 0x5602;/);
assert.match(sensorHeader, /kDataLanes = 1;/);
assert.match(sensorHeader, /kLaneBitRateMbps = 400;/);
assert.match(sensor, /kRegSensorIdHigh = 0x300a;/);
assert.match(sensor, /i2c_master_probe\(bus_, kSccbAddress/);
assert.match(sensor, /kRegMirror = 0x3821;/);
assert.doesNotMatch(maskCpp(sensor), /0x3a0f|0x3a10|0x3c01|0x3037/,
  'The demo driver\'s OV5640-style AE/banding registers must not be written');

// --- Shared I2C bus and PHY supply -------------------------------------------
const noComments = text => maskCpp(text);
for (const [name, text] of [['sensor', sensor], ['hardware', hardware], ['service', service]]) {
  assert.doesNotMatch(noComments(text), /i2c_new_master_bus|i2c_del_master_bus/,
    `${name} must never create or delete an I2C bus`);
}
assert.match(hardware, /gsl3680_i2c_bus\(\)/, 'The camera must reuse the touch-owned bus');
assert.match(touch, /i2c_master_bus_handle_t gsl3680_i2c_bus\(\) \{\s*return g_state\.initialized \? g_state\.bus : nullptr;/);
assert.match(hardware, /kMipiPhyLdoChannel = 3;/);
assert.match(hardware, /kMipiPhyLdoVoltageMv = 2500;/);
assert.doesNotMatch(noComments(hardware), /adjustable\s*=\s*1|esp_ldo_channel_adjust_voltage/,
  'The display\'s PHY supply is shared, never adjusted');

// --- Exact-profile isolation --------------------------------------------------
// The V2 always; the Waveshare 8-inch only in camera beta builds
// (tools/tests/devices/test-waveshare-8-local-camera.mjs).
assert.match(deviceSelect,
  /#if defined\(DEVICE_GUITION_JC8012P4A1_V2\) \|\| \\\n    \(defined\(DEVICE_WAVESHARE_TOUCH_LCD_8\) && defined\(HOMETILES_CAMERA_BETA\)\) \|\| \\\n    \(defined\(DEVICE_M5STACKS_TAB5\) && defined\(HOMETILES_CAMERA_BETA\)\)\n#define HOMETILES_LOCAL_CAMERA 1\n#endif/);
assert.match(deviceTypes, /bool has_builtin_camera = false;/);
const profileHeaders = fs.readdirSync(path.join(root, 'src/devices'), {withFileTypes: true})
  .filter(entry => entry.isDirectory())
  .flatMap(entry => fs.readdirSync(path.join(root, 'src/devices', entry.name))
    .filter(file => file.endsWith('.h'))
    .map(file => path.join('src/devices', entry.name, file)));
let cameraProfiles = 0;
for (const header of profileHeaders) {
  const text = read(header);
  for (const match of text.matchAll(/Device::Capabilities\{([^}]*)\}/g)) {
    const values = match[1].split(',').map(value => value.trim());
    if (values.length >= 7 && values[6] === 'true') {
      cameraProfiles++;
      assert.equal(header.replaceAll('\\', '/'), `${v2}/device_guition_jc8012p4a1_v2.h`,
        'Only the exact V2 profile may declare the built-in camera');
    }
  }
}
assert.equal(cameraProfiles, 1);

// Board file: exact profile only. Sensor driver: only when a board selects it.
for (const file of [`${v2}/local_camera_board.cpp`, `${v2}/local_camera_board.h`]) {
  const text = read(file);
  assert.match(text, /#if defined\(DEVICE_GUITION_JC8012P4A1_V2\) && defined\(HOMETILES_LOCAL_CAMERA\)/,
    `${file} must be profile guarded`);
}
for (const file of [`${ov}/ov02c10_sensor.cpp`, `${ov}/ov02c10_sensor.h`, `${ov}/ov02c10_settings.h`]) {
  const text = read(file);
  assert.match(text, /#if defined\(HOMETILES_CAMERA_SENSOR_OV02C10\)/, `${file} must be sensor guarded`);
  assert.match(text.trimEnd(), /#endif\s*\/\/ defined\(HOMETILES_CAMERA_SENSOR_OV02C10\)$/, file);
}
assert.match(cameraSelect,
  /#if defined\(DEVICE_GUITION_JC8012P4A1_V2\)\n#define HOMETILES_CAMERA_SENSOR_OV02C10 1\n#define HOMETILES_LOCAL_CAMERA_BOARD "src\/devices\/guition_jc8012p4a1_v2\/local_camera_board\.h"/);
assert.match(boardHeader, /using Sensor = ov02c10::Sensor;/);
assert.match(boardHeader, /COLOR_RAW_ELEMENT_ORDER_GBRG/);
// The shared core never names a device or a sensor: every file directly in
// src/video/local_camera (sensor drivers live in sensors/<name>/) and the
// shared camera transport files.
const coreDir = 'src/video/local_camera';
const coreFiles = fs.readdirSync(path.join(root, coreDir), {withFileTypes: true})
  .filter(entry => entry.isFile() && /\.(cpp|h)$/.test(entry.name) && entry.name !== 'camera_select.h')
  .map(entry => `${coreDir}/${entry.name}`)
  .concat(['src/video/tcp_ack_wire.h', 'src/video/tcp_ack_socket.h', 'src/video/tcp_ack_socket.cpp']);
for (const expected of ['local_camera.cpp', 'local_camera_upload.cpp', 'local_camera_upload.h',
  'local_camera_stream_contract.h', 'local_camera_contract.h']) {
  assert.ok(coreFiles.includes(`${coreDir}/${expected}`), `${expected} must be checked`);
}
for (const file of coreFiles) {
  assert.doesNotMatch(maskCpp(read(file)), /ov02c10|OV02C10|DeviceGuition|guition_jc8012|DEVICE_GUITION|JC8012/i,
    `${file} must only use the board interface`);
}
assert.match(service, /#include HOMETILES_LOCAL_CAMERA_BOARD/);
assert.match(service, /#if defined\(HOMETILES_LOCAL_CAMERA\)\nconstexpr bool kSupported = true;\n#else\nconstexpr bool kSupported = false;\n#endif/);
const hardwareSection = service.slice(service.indexOf('// Hardware path: devices with a camera board file (camera_select.h).'));
assert.match(hardwareSection, /^[^\n]*\n\/\/ =+\n#if defined\(HOMETILES_LOCAL_CAMERA\)/);

const cc = ['clang', 'gcc'].find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (cc) {
  const probe = path.join(root, 'build/tests/local-camera-profile/probe.cpp');
  fs.mkdirSync(path.dirname(probe), {recursive: true});
  fs.writeFileSync(probe, '#include "src/devices/device_select.h"\n#if defined(HOMETILES_LOCAL_CAMERA)\nLOCAL_CAMERA_ON\n#endif\n');
  const profiles = ['DEVICE_GUITION_JC8012P4A1_V2', 'DEVICE_GUITION_JC8012P4A1',
    'DEVICE_WAVESHARE_TOUCH_LCD_8', 'DEVICE_M5STACKS_TAB5', 'DEVICE_GUITION_ESP32_4848S040',
    'DEVICE_GUITION_JC1060P470C_V2', 'DEVICE_WAVESHARE_4B'];
  for (const profile of profiles) {
    const result = spawnSync(cc, ['-E', '-P', '-x', 'c++', '-DHOMETILES_CI_TARGET', `-D${profile}`,
      '-I', root, probe], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stdout.includes('LOCAL_CAMERA_ON'), profile === 'DEVICE_GUITION_JC8012P4A1_V2',
      `${profile} camera feature define`);
  }
} else {
  console.log('Profile preprocessing skipped: clang or gcc not found');
}

// --- Capture pipeline: pre-v3 silicon, shared 2D-DMA, bounded worker ---------
const functions = cppFunctionDefinitions(service);
const body = name => {
  const found = functions.find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
};
const capture = body('captureJpeg');
assert.match(capture, /Dma2dArbiterGuard guard\(\d+\);\s*if \(!guard\.locked\(\)\) \{[\s\S]*?ErrorCode::EncoderBusy;[\s\S]*?jpeg_encoder_process\(/,
  'Hardware JPEG must run inside the DMA2D arbiter lease');
// One encode for snapshots, one for the live stream; both under the arbiter.
assert.equal((service.match(/jpeg_encoder_process\(/g) || []).length, 2);
const streamEncode = body('encodeStreamFrame');
// The stream encodes the frozen CSI buffer directly (the V2 sensor delivers
// the JPEG size and orientation): one short arbiter lease for the encode only,
// the buffer is released after the encoder read it, and no PPA pass remains.
const streamCapture = body('streamCaptureFrame');
assert.match(streamCapture, /const uint8_t\* input = g_isr\.buffers\[frozen\];/);
assert.match(streamCapture, /Dma2dArbiterGuard guard\(kStreamArbiterTimeoutMs\);\s*if \(!guard\.locked\(\)\) \{[\s\S]*?\+\+window\.arb;[\s\S]*?encodeStreamFrame\(input, kJpegInputBytes, kImageWidth, kImageHeight,[\s\S]*?\}\s*\/\/[^\n]*\n\s*g_isr\.frozen = -1;/,
  'The stream encode reads the frozen CSI buffer inside one short arbiter lease and releases it afterwards');
assert.match(boardHeader, /false,\s*\/\/ Landscape sensor: the frame is the image, no PPA pass\./,
  'The V2 sensor is not a quarter-turn mounting');
assert.doesNotMatch(noComments(service), /ppa_do_scale_rotate_mirror|ppaPrepareStreamFrame/,
  'No PPA pass in the camera pipeline');
assert.match(streamEncode, /jpeg_encoder_process\(/);
assert.match(streamEncode, /jpeg_del_encoder_engine\(g_pipe\.jpeg\);[\s\S]*?\}\s*\}\s*if \(err != ESP_OK\)/,
  'A failed stream encode drops the engine while the arbiter is still held');
assert.match(capture, /JPEG_ENCODE_IN_FORMAT_RGB565/);
// 4:2:2 on landscape boards like the V2; quarter-turn boards use 4:2:0.
assert.match(capture, /config\.sub_sample = kJpegSubsampling;/);
assert.match(service, /kQuarterTurn \? JPEG_DOWN_SAMPLING_YUV420 : JPEG_DOWN_SAMPLING_YUV422;/);
// Pre-v3 silicon has no ISP crop block: the sensor window is the JPEG size,
// so the snapshot encodes the frozen buffer without any CPU pixel pass.
assert.doesNotMatch(capture, /compactCenterCrop|std::reverse|esp_cache_msync/,
  'The snapshot must not crop, turn or mirror on the CPU');
assert.match(capture, /applyOrientation\(false\)[\s\S]*?esp_cam_ctlr_start\([\s\S]*?setStream\(true\)/,
  'The sensor orientation is written in standby before the snapshot starts');
assert.doesNotMatch(noComments(service), /esp_isp_wbg_|esp_isp_crop_|esp_isp_blc_|JPEG_ENCODE_IN_FORMAT_YUV4[24]0|JPEG_ENCODE_IN_FORMAT_YUV444/,
  'WBG, crop, BLC and YUV420/444 JPEG input are unavailable before ESP32-P4 v3.0');
const pipeline = body('ensurePipeline');
assert.match(pipeline, /CAM_CTLR_COLOR_RAW10/);
assert.match(pipeline, /bk_buffer_dis = 1/);
assert.match(pipeline, /isp_config\.bayer_order = kMode\.bayer_order;/);
assert.match(pipeline, /csi_config\.data_lane_num = kMode\.data_lanes;/);
assert.match(pipeline, /ISP_INPUT_DATA_SOURCE_CSI/);
// esp_cam_ctlr_start() rejects a frame buffer that is not cache-line aligned
// (hardware log: 0x494eb050 from esp_cam_ctlr_alloc_buffer).
assert.match(pipeline, /heap_caps_aligned_calloc\(kFrameBufferAlign, 1, kFrameBytes, MALLOC_CAP_SPIRAM\)/,
  'Frame buffers belong in PSRAM, aligned to the cache line');
assert.ok(!pipeline.includes('esp_cam_ctlr_alloc_buffer(g_pipe'), 'No unaligned driver allocation');
assert.ok(pipeline.indexOf('esp_cam_new_csi_ctlr') < pipeline.indexOf('esp_isp_new_processor'),
  'The CSI controller must exist before the ISP claims the shared bridge');

// Request handling on the loop only validates and notifies the worker.
const request = body('handleMqttMessage');
assert.doesNotMatch(request, /captureJpeg|ensureSensor|ensurePipeline|vTaskDelay|mqttStreamPublish/);
assert.match(request, /notifyWorker\(kNotifyCapture\)/);
assert.match(request, /g_rate_limiter\.wouldAccept/);
assert.match(request, /g_capture_busy\.load\(\)/);
assert.match(body('ensureWorker'), /xTaskCreatePinnedToCoreWithCaps\([\s\S]*MALLOC_CAP_SPIRAM\)/);
assert.match(body('runCapture'), /mqttStreamPublishSubmit\(/);
assert.match(body('runCapture'), /g_capture_busy\.store\(false\)/);
assert.match(body('bridgeCapability'), /Device::kCapabilities\.has_builtin_camera/);
assert.match(body('releaseSensor'), /if \(g_sensor_identified\) sensorStandby\(\);[\s\S]*detach\(\)[\s\S]*board::release\(\)/,
  'Disable puts an identified sensor into standby and releases only this driver\'s resources');
assert.match(body('sensorStandby'), /setStream\(false\)[\s\S]*setStream\(false\)/,
  'A failed standby is retried once');
assert.match(body('ensureSensor'), /g_sensor\.probe\(&chip_id\)[\s\S]*g_sensor_identified = true;[\s\S]*loadDefaultMode/,
  'Register writes follow only a matching chip ID');
assert.match(body('ensureSensor'), /err == ESP_ERR_NOT_FOUND/,
  'Only an address NACK or ID mismatch is reported as a missing sensor');
// Stop order as in esp_video: sensor standby, then the CSI receiver.
const stop = body('stopStreaming');
assert.ok(stop.indexOf('sensorStandby()') < stop.indexOf('esp_cam_ctlr_stop('),
  'The sensor enters standby before the CSI controller stops');
const setStream = cppFunctionDefinitions(sensor).find(item => item.name === 'Sensor::setStream');
assert.ok(setStream);
const stopBranch = setStream.body.slice(setStream.body.indexOf('return err;'));
assert.match(stopBranch, /write\(kRegStreamMode, 0x00\)[\s\S]*write\(0x300d, 0x00\)/,
  'Stream-off writes 0x0100 first and always attempts the pad writes');
assert.match(cppFunctionDefinitions(sensor).find(item => item.name === 'Sensor::detach').body,
  /if \(err != ESP_OK\) return err;[\s\S]*device_ = nullptr;/,
  'A failed device removal keeps the handle for a retry');

// Disable, shutdown and the Bridge deadline stop a snapshot before it leaves.
assert.ok((capture.match(/abortRequested\(\)/g) || []).length >= 4,
  'Abort is checked during AE, before the freeze, after it and per encode attempt');
const run = body('runCapture');
assert.match(run, /abortRequested\(\)\) code = abortCode\(\);[\s\S]*mqttStreamPublishSubmit\(/);
assert.match(run, /request\.received_ms \+ kRequestDeadlineMs/);
assert.match(run, /mqttStreamPublishWait\(kStreamPollMs, false\)[\s\S]*mqttStreamPublishCancel\(\)/);
assert.match(request, /request\.received_ms = now_ms;/);
assert.match(body('setEnabled'), /if \(g_worker\) g_abort\.store\(true\);/,
  'Without a worker no abort flag may stay behind');

// Opt-in: separate NVS key, default off; the Settings record stays unchanged.
assert.match(service, /kPrefsEnabledKey\[\] = "local_cam_en";/);
assert.match(body('begin'), /prefs\.getBool\(kPrefsEnabledKey, false\)/);
assert.doesNotMatch(read('src/core/config/settings_access_record.h'), /local_cam/);

// Lifecycle hooks.
assert.match(read('src/core/power/power_manager.cpp'), /local_camera::releaseForSleep\(\);/);
assert.match(read('src/core/hardware/board_hal.cpp'), /local_camera::shutdown\("restart"\);\s*Device::prepareForRestart\(\);/);
assert.match(read('src/network/network_manager.cpp'), /void HomeTilesNetworkManager::prepareMqttForOta\(\) \{[\s\S]{0,200}local_camera::shutdown\("ota"\);/);
console.log('Guition V2 local camera: vendored table, profile isolation and pipeline contract passed.');
