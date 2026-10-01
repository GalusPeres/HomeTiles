import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8')
    .replace(/\r\n?/g, '\n');
}

function requireMarker(source, marker, label) {
  if (!source.includes(marker)) {
    throw new Error(`${label} is missing: ${marker}`);
  }
}

function constantValue(source, name) {
  const match = source.match(new RegExp(
    `constexpr\\s+(?:u?int(?:8|16|32)_t|size_t|bool)\\s+${name}\\s*=\\s*([^;]+);`));
  if (!match) throw new Error(`Constant was not found: ${name}`);
  return match[1].trim();
}

function block(source, pattern, label) {
  const match = source.match(pattern);
  if (!match) throw new Error(`${label} was not found`);
  return match[0];
}

const dir = 'src/devices/adafruit_qualia_s3_720';
const header = read(`${dir}/device_adafruit_qualia_s3_720.h`);
const driver = read(`${dir}/device_adafruit_qualia_s3_720.cpp`);
const hardwareIo = read(`${dir}/hardware_io_profile.h`);
const deviceSelect = read('src/devices/device_select.h');
const sketch = read('HomeTiles.ino');
const metadata = read('src/core/firmware/firmware_metadata.cpp');

// The descriptor stores both strings in NUL-terminated char[32] fields.
const descriptor = metadata.match(
  /#elif defined\(DEVICE_ADAFRUIT_QUALIA_S3_720\)\n#define FW_META_TARGET_DEVICE_KEY "([^"]+)"\n#define FW_META_TARGET_DISPLAY_NAME "([^"]+)"/);
if (!descriptor) throw new Error('Qualia firmware descriptor was not found');
for (const value of descriptor.slice(1)) {
  if (Buffer.byteLength(value) >= 32) {
    throw new Error(`Firmware descriptor string does not fit char[32]: ${value}`);
  }
}

// Native 720x720 grid, identical to the 720x720 Waveshare B4 layout.
for (const marker of [
  'namespace DeviceAdafruitQualiaS3720',
  '"adafruit_qualia_s3_720"',
  '"Adafruit Qualia ESP32-S3 720x720"',
  '    720,\n    720,\n    4,\n    4,\n    16,\n    4,\n    166,\n    166,\n    4,',
  'Device::RotationStepMode::QuarterTurns,\n    0,\n    2,',
  'Device::Capabilities{false, false, false, false, false, false}',
]) {
  requireMarker(header, marker, 'Qualia device profile');
}

// Board wiring and panel timings from the Qualia and TL040HDS20 sources.
const expectedConstants = new Map([
  ['kPanelDe', '2'], ['kPanelVsync', '42'], ['kPanelHsync', '41'], ['kPanelPclk', '1'],
  ['kPanelB0', '40'], ['kPanelB1', '39'], ['kPanelB2', '38'], ['kPanelB3', '0'], ['kPanelB4', '45'],
  ['kPanelG0', '48'], ['kPanelG1', '47'], ['kPanelG2', '21'], ['kPanelG3', '14'],
  ['kPanelG4', '13'], ['kPanelG5', '12'],
  ['kPanelR0', '11'], ['kPanelR1', '10'], ['kPanelR2', '9'], ['kPanelR3', '46'], ['kPanelR4', '3'],
  ['kI2cSda', '8'], ['kI2cScl', '18'],
  ['kExpanderAddress', '0x3F'],
  ['kExpanderConfig', '0x68'],
  ['kExpanderInitialOutput', '0xED'],
  ['kExpanderBitTftCs', '1'], ['kExpanderBitTftReset', '2'], ['kExpanderBitBacklight', '4'],
  ['kTouchAddress', '0x48'],
  ['kRgbPclkHz', '12000000'],
  ['kRgbHsyncPulseWidth', '2'], ['kRgbHsyncBackPorch', '44'], ['kRgbHsyncFrontPorch', '46'],
  ['kRgbVsyncPulseWidth', '2'], ['kRgbVsyncBackPorch', '18'], ['kRgbVsyncFrontPorch', '16'],
  ['kRgbPclkActiveNeg', 'true'],
]);
for (const [name, expected] of expectedConstants) {
  const actual = constantValue(driver, name);
  if (actual !== expected) {
    throw new Error(`${name} mismatch: expected ${expected}, got ${actual}`);
  }
}

// The initial output keeps the backlight off; config drives only bits
// 0/1/2/4/7 and leaves the touch IRQ and both buttons as inputs.
if ((0xED & (1 << 4)) !== 0 || 0x68 !== (0xFF & ~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 4) | (1 << 7)))) {
  throw new Error('Expander direction/output constants no longer match the documented bits');
}

for (const marker of [
  'constexpr uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;',
  'constexpr uint32_t kExpectedPsramBytes = 8U * 1024U * 1024U;',
  'flash_bytes != kExpectedFlashBytes',
  'psram_bytes != kExpectedPsramBytes',
  'kPanelB0, kPanelB1, kPanelB2, kPanelB3, kPanelB4,\n'
    + '        kPanelG0, kPanelG1, kPanelG2, kPanelG3, kPanelG4, kPanelG5,\n'
    + '        kPanelR0, kPanelR1, kPanelR2, kPanelR3, kPanelR4};',
  'periph_module_reset(PERIPH_LCD_CAM_MODULE);',
  'config.num_fbs = 2;',
  'config.flags.double_fb = true;',
  'config.bounce_buffer_size_px = 0;',
  'constexpr gpio_drive_cap_t kRgbDataDrive = GPIO_DRIVE_CAP_0;',
  // The panel has no command channel: no SPI bus and no init table.
  'Arduino_RGB_Display(kPanelSize, kPanelSize, nullptr, rotation, true,\n'
    + '                            nullptr, GFX_NOT_DEFINED, nullptr, 0)',
]) {
  requireMarker(driver, marker, 'Qualia RGB/N16R8 contract');
}

// The drive capability must be applied after esp_lcd configured the pins.
const begin = block(driver, /bool begin\(int32_t speed = GFX_NOT_DEFINED\) override \{[\s\S]*?\n  \}\n/, 'RGB begin()');
if (begin.indexOf('esp_lcd_panel_init(panel_handle_)') > begin.indexOf('quietenRgbBus();') ||
    begin.indexOf('periph_module_reset') > begin.indexOf('esp_lcd_new_rgb_panel')) {
  throw new Error('Qualia RGB bring-up order changed');
}

// Panel reset pulse through the expander precedes the RGB panel.
const expander = block(driver, /bool initExpander\(\) \{[\s\S]*?\n\}\n/, 'initExpander()');
for (const marker of [
  'setExpanderBit(kExpanderBitTftReset, true);\n  delay(10);',
  'setExpanderBit(kExpanderBitTftReset, false);\n  delay(20);',
  'setExpanderBit(kExpanderBitTftReset, true);\n  delay(120);',
]) {
  requireMarker(expander, marker, 'Qualia panel reset sequence');
}
const init = block(driver, /bool DeviceAdafruitQualiaS3720::init\(\) \{[\s\S]*?\n\}\n/, 'init()');
if (!(init.indexOf('initExpander()') < init.indexOf('initDisplay()') &&
      init.indexOf('initDisplay()') < init.indexOf('initTouch()'))) {
  throw new Error('Qualia init must reset the panel before RGB start and probe touch last');
}

// On/off backlight: zero is off, every other level is on.
requireMarker(driver, 'const bool on = value != 0;', 'Expander backlight contract');
requireMarker(driver, 'setExpanderBit(kExpanderBitBacklight, on);', 'Expander backlight contract');

// FT6336 coordinate decoding and panel-bound rejection.
for (const marker of [
  'readI2cRegisters(kTouchAddress, kTouchRegStatus, data, sizeof(data))',
  'static_cast<uint16_t>(((data[1] & 0x0F) << 8) | data[2]);',
  'static_cast<uint16_t>(((data[3] & 0x0F) << 8) | data[4]);',
  'if (raw_x >= kPanelSize || raw_y >= kPanelSize) {',
  'x = static_cast<int16_t>(raw_x);\n      y = static_cast<int16_t>(raw_y);',
  'now - g_touch_last_probe_ms < kTouchRetryIntervalMs',
]) {
  requireMarker(driver, marker, 'FT6336 touch contract');
}

// S3 lifecycle family, but the native 720x720 layout, not the 480x480 one.
const s3Family = block(deviceSelect, /#if[^#]+#define DEVICE_ESP32_S3_RGB_480/, 'S3 RGB family');
requireMarker(s3Family, 'defined(DEVICE_ADAFRUIT_QUALIA_S3_720)', 'S3 RGB family membership');
for (const layout of ['DEVICE_LAYOUT_480X480', 'DEVICE_LAYOUT_1024X600',
  'HOMETILES_LOCAL_CAMERA', 'DEVICE_P4_IDF_DSI']) {
  const guard = deviceSelect.match(new RegExp(`#if[^#]+#define ${layout}\\b`));
  if (!guard) throw new Error(`${layout} guard was not found`);
  if (guard && guard[0].includes('DEVICE_ADAFRUIT_QUALIA_S3_720')) {
    throw new Error(`Qualia must not select ${layout}`);
  }
}
requireMarker(sketch, '#elif defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) || \\\n'
  + '    defined(DEVICE_ADAFRUIT_QUALIA_S3_720)\n  if (s3_rgb_network_active) {',
  'Post update-check RGB resynchronization');

for (const forbidden of [
  '<SD.h>', 'SD.begin', 'SPIClass', 'Arduino_SWSPI', 'kPanelInitOperations',
  'ledcAttach', '#if 0', 'Guition', 'Waveshare',
]) {
  if (driver.includes(forbidden)) {
    throw new Error(`Qualia driver contains forbidden marker: ${forbidden}`);
  }
}
for (const marker of [
  'bool DeviceAdafruitQualiaS3720::initSDCard() {\n'
    + '  // The Qualia ESP32-S3 has no microSD interface.',
  'bool DeviceAdafruitQualiaS3720::sdReady() {\n  return false;\n}',
  'return LittleFS;',
]) {
  requireMarker(driver, marker, 'No-SD behavior');
}

requireMarker(hardwareIo, 'kHardwareIoProfile{}', 'Hardware I/O profile');
if (/\{\s*\d+\s*,/.test(hardwareIo)) {
  throw new Error('Reserved Qualia pins must not be exposed as I/O');
}

console.log('Adafruit Qualia ESP32-S3 720x720 profile contract: PASS');
