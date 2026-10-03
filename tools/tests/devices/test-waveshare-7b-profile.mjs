import { getBuildProfile, getReleaseProfile } from '../../device-catalog.js';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8')
    .replace(/\r\n/g, '\n');
}

function requireMarker(source, marker, label) {
  if (!source.includes(marker)) {
    throw new Error(`${label} is missing: ${marker}`);
  }
}

function displayConfigValues(source) {
  const match = source.match(/SCREEN_DEFAULT\s*=\s*\{([\s\S]*?)\n\};/);
  if (!match) throw new Error('Waveshare 7B display config was not found');
  return match[1]
    .replace(/\/\/.*$/gm, '')
    .split(',')
    .map((value) => value.trim())
    .filter(Boolean);
}

function normalizedInitSequence(source) {
  const table = source.match(
    /static const ek79007_lcd_init_cmd_t vendor_specific_init_default\[\] = \{([\s\S]*?)\n\};/);
  if (!table) throw new Error('EK79007 default init sequence was not found');
  return [...table[1].matchAll(
    /\{(0x[0-9A-Fa-f]+), \(uint8_t \[\]\)\{([^}]*)\}, (\d+), (\d+)\},/g)]
    .map((match) => [
      match[1].toLowerCase(),
      match[2].replace(/\s+/g, '').toLowerCase(),
      match[3],
      match[4],
    ].join(':'));
}

const base = 'src/devices/waveshare_touch_lcd_7b';
const profile = read(`${base}/profile.h`);
const hardwareIo = read(`${base}/hardware_io_profile.h`);
const driver = read(`${base}/device_waveshare_touch_lcd_7b.cpp`);
const display = read(`${base}/vendor/displays_config.h`);
const i2c = read(`${base}/vendor/i2c.h`);
const touchHeader = read(`${base}/vendor/gt911.h`);
const touchDriver = read(`${base}/vendor/gt911.cpp`);
const sdHeader = read(`${base}/sdmmc.h`);
const sdDriver = read(`${base}/sdmmc.cpp`);
const panelHeader = read(`${base}/vendor/ek79007/esp_lcd_ek79007.h`);
const panelDriver = read(`${base}/vendor/ek79007/esp_lcd_ek79007.c`);
const vendorLicense = read(`${base}/vendor/LICENSE-APACHE-2.0.txt`);
const profileReadme = read(`${base}/README.md`);
const sketchProfiles = read('sketch.yaml');
const firmwareWorkflow = read('.github/workflows/firmware.yml');
const firmwareMetadataHeader = read('src/core/firmware/firmware_metadata.h');
const firmwareMetadataSource = read('src/core/firmware/firmware_metadata.cpp');
const githubUpdate = read('src/core/firmware/github_update.cpp');
const webOta = read('src/web/server/handlers/web_admin_ota.cpp');

for (const marker of [
  '"waveshare_touch_lcd_7b"',
  '"Waveshare Touch LCD 7B / 7B-C"',
  '    1024,\n    600,\n    6,\n    4,',
  'Device::RotationStepMode::FlipOnly,\n    2,\n    0,',
  'inline constexpr uint32_t kFlashSizeBytes = 32U * 1024U * 1024U;',
  'inline constexpr uint32_t kPsramSizeBytes = 32U * 1024U * 1024U;',
]) {
  requireMarker(profile, marker, 'Waveshare 7B profile');
}
requireMarker(hardwareIo, 'Device::kNoHardwareIoProfile',
              'Waveshare 7B reserved hardware I/O contract');

const expectedDisplay = [
  '"WAVESHARE-7B-EK79007"',
  '10', '160', '160',
  '1', '23', '12',
  '52000000', '1000',
  '1024', '600',
  '7', '8', '400000', '33',
];
const actualDisplay = displayConfigValues(display);
if (JSON.stringify(actualDisplay) !== JSON.stringify(expectedDisplay)) {
  throw new Error(`Waveshare 7B display contract mismatch: ${actualDisplay}`);
}

for (const marker of [
  '#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)',
  'vendor/ek79007/esp_lcd_ek79007.h',
  'constexpr gpio_num_t kBacklightPin = GPIO_NUM_32;',
  'constexpr uint32_t kBacklightFreq = 5000;',
  'constexpr ledc_timer_bit_t kBacklightBits = LEDC_TIMER_10_BIT;',
  'constexpr bool kBacklightActiveLow = true;',
  'constexpr uint8_t kBacklightInputMin = 1;',
  'constexpr uint8_t kBacklightInputMax = 255;',
  'if (value == 0) {',
  'brightness_percent = 100;',
  'duty = kMaxDuty - duty;',
  'constexpr uint32_t kPanelLaneCount = 2;',
  'constexpr int kMipiPhyLdoChannel = 3;',
  'constexpr int kMipiPhyLdoVoltageMv = 2500;',
  'esp_lcd_dsi_bus_config_t bus_cfg = {};',
  'dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;',
  'dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;',
  'dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;',
  'panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;',
  'vendor_cfg.mipi_config.lane_num = kPanelLaneCount;',
  'esp_lcd_new_panel_ek79007(g_panel_io, &panel_cfg, &g_panel)',
  'esp_lcd_panel_reset(g_panel)',
  'esp_lcd_panel_init(g_panel)',
]) {
  requireMarker(driver, marker, 'Waveshare 7B device source');
}
if (/bus_cfg\.phy_clk_src\s*=/.test(driver)) {
  throw new Error('Waveshare 7B must use the revision-aware ESP-IDF default DSI PHY clock source');
}
if (driver.includes('esp_lcd_panel_disp_on_off')) {
  throw new Error('Waveshare 7B must not call the unsupported EK79007 display-on/off operation');
}
for (const marker of [
  'void DeviceWaveshareTouchLCD7B::displaySleep() {\n  // The EK79007 panel driver has no display-on/off operation.',
  'void DeviceWaveshareTouchLCD7B::displayWake() {\n  apply_backlight(g_brightness);',
  'void DeviceWaveshareTouchLCD7B::displayWakeDark() {\n  apply_backlight(0);',
  'hold_panel_reset_low();\n  apply_backlight(0);',
]) {
  requireMarker(driver, marker, 'Waveshare 7B backlight lifecycle');
}
if (/jd9165/i.test(driver)) {
  throw new Error('Waveshare 7B must not use the unrelated JD9165 driver');
}

for (const marker of [
  '#define EXAMPLE_I2C_MASTER_SDA GPIO_NUM_7',
  '#define EXAMPLE_I2C_MASTER_SCL GPIO_NUM_8',
  '#define EXAMPLE_I2C_MASTER_FREQUENCY (400 * 1000)',
]) {
  requireMarker(i2c, marker, 'Waveshare 7B I2C wiring');
}
for (const marker of [
  '#define EXAMPLE_PIN_NUM_TOUCH_RST       (GPIO_NUM_NC)',
  '#define EXAMPLE_PIN_NUM_TOUCH_INT       (GPIO_NUM_NC)',
]) {
  requireMarker(touchHeader, marker, 'Waveshare 7B GT911 wiring');
}
for (const marker of [
  '.swap_xy = 0,',
  '.mirror_x = 1,',
  '.mirror_y = 1,',
]) {
  requireMarker(touchDriver, marker, 'Waveshare 7B GT911 transform');
}

for (const marker of [
  'int sdmmc_frequency = SDMMC_FREQ_HIGHSPEED,',
]) {
  requireMarker(sdHeader, marker, 'Waveshare 7B SDMMC interface');
}
for (const marker of [
  'static constexpr int kSdLdoChannel = 4;',
  'static constexpr int kSdD0Pin = 39;',
  'static constexpr int kSdD1Pin = 40;',
  'static constexpr int kSdD2Pin = 41;',
  'static constexpr int kSdD3Pin = 42;',
  'static constexpr int kSdCmdPin = 44;',
  'static constexpr int kSdClkPin = 43;',
  'host.slot = SDMMC_HOST_SLOT_0;',
  'host.flags = SDMMC_HOST_FLAG_4BIT;',
]) {
  requireMarker(sdDriver, marker, 'Waveshare 7B SDMMC contract');
}
requireMarker(driver,
              'Waveshare7BSDMMC.begin("/sdcard", false, SDMMC_FREQ_HIGHSPEED, 5)',
              'Waveshare 7B 40 MHz SD mount');

for (const marker of [
  'SPDX-License-Identifier: Apache-2.0',
  'ek79007_vendor_config_t',
  'EK79007_1024_600_PANEL_60HZ_CONFIG',
]) {
  requireMarker(panelHeader, marker, 'Vendored EK79007 header');
}
requireMarker(panelDriver, 'SPDX-License-Identifier: Apache-2.0',
              'Vendored EK79007 source');
requireMarker(panelDriver, '#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)',
              'Vendored EK79007 build guard');
const initSequence = normalizedInitSequence(panelDriver);
const initHash = crypto.createHash('sha256')
  .update(initSequence.join('|')).digest('hex');
if (initSequence.length !== 8 ||
    initHash !== '4cd0037ee55d79e01ad05cff3983f4408fa5210dc72ed5c42e7cb6d4731ff633') {
  throw new Error(`Waveshare EK79007 init sequence changed: ${initHash}`);
}
requireMarker(vendorLicense, 'Apache License', 'Vendored EK79007 license');

for (const marker of [
  '7B-C uses the same display, touch, backlight and SDMMC contract',
  'supports the 7B-C OV5647 module on the 15-pin 1.0 mm CSI connector',
]) {
  requireMarker(profileReadme, marker, 'Waveshare 7B-C scope');
}

const oldProfile = read('src/devices/waveshare_touch_lcd_7/profile.h');
for (const marker of [
  '"waveshare_touch_lcd_7"',
  '"Waveshare Touch LCD 7"',
  '    1280,\n    720,',
]) {
  requireMarker(oldProfile, marker, 'Existing Waveshare 7 profile');
}
if (oldProfile.includes('waveshare_touch_lcd_7b') ||
    oldProfile.includes('Waveshare Touch LCD 7B')) {
  throw new Error('The existing 1280x720 Waveshare 7 profile must stay distinct');
}

for (const marker of [
  'waveshare_7b:',
  'ChipVariant=prev3',
  'waveshare_7b_rev3:',
  'ChipVariant=postv3',
]) {
  requireMarker(sketchProfiles, marker, 'Waveshare 7B silicon build profiles');
}
for (const marker of [
  'profile: waveshare_7b\n            key: waveshare_touch_lcd_7b',
  'silicon_variant: pre_v3',
  'profile: waveshare_7b_rev3\n            key: waveshare_touch_lcd_7b_rev3',
  'metadata_key: waveshare_touch_lcd_7b',
  'silicon_variant: post_v3',
  '--silicon-variant "${{ matrix.silicon_variant || \'default\' }}"',
]) {
  requireMarker(firmwareWorkflow, marker, 'Waveshare 7B release matrix');
}
for (const marker of [
  'kSiliconRevisionDescriptorMagic',
  'kSiliconRevisionDescriptorImageOffset',
  'imageMatchesCurrentSiliconVariant',
]) {
  requireMarker(firmwareMetadataHeader, marker, 'Firmware silicon metadata contract');
}
for (const marker of [
  '#if defined(CONFIG_IDF_TARGET_ESP32P4)',
  '#if CONFIG_ESP_REV_MAX_FULL < 300',
  '#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) || defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) ||',
  '#elif CONFIG_ESP_REV_MIN_FULL >= 300 && FW_META_V3_PROFILE',
  '#define FW_META_SILICON_VARIANT "pre_v3"',
  '#define FW_META_SILICON_VARIANT "post_v3"',
  '#define FW_META_SILICON_MIN_REV 301',
  '#define FW_META_SILICON_MAX_REV 399',
  '#error "Every ESP32-P4 build must target one unambiguous silicon generation"',
  'strcmp(current_variant, "pre_v3") == 0',
  'const uint16_t chip_revision = ESP.getChipRevision();',
  'chip_revision >= incoming.minimum_revision',
  'chip_revision <= incoming.maximum_revision',
]) {
  requireMarker(firmwareMetadataSource, marker, 'Waveshare 7B silicon metadata');
}
for (const marker of [
  '#if defined(CONFIG_IDF_TARGET_ESP32P4)',
  'const uint16_t chip_revision = ESP.getChipRevision();',
  'key_out = "waveshare_touch_lcd_7b_rev3";',
  '[Update] ESP32-P4 silicon revision=%u variant=%s asset=%s',
  'asset_device_key == Device::profile().key',
  'imageMatchesCurrentSiliconVariant(',
  'matchesCurrentSiliconRevisionRange(',
  'exceeds supported range ',
]) {
  requireMarker(githubUpdate, marker, 'Waveshare 7B on-device OTA selection');
}
requireMarker(webOta, 'imageMatchesCurrentSiliconVariant(',
              'Waveshare 7B manual Web OTA validation');
const preV3Release = getReleaseProfile('waveshare_touch_lcd_7b');
const rev3Release = getReleaseProfile('waveshare_touch_lcd_7b_rev3');
assert.equal(preV3Release.siliconVariant, 'pre_v3');
assert.equal(rev3Release.metadataDeviceKey, 'waveshare_touch_lcd_7b');
// v3.1 and v3.2 boards share one v3 image, as on the 10.1 (#41).
assert.equal(rev3Release.siliconVariant, 'post_v3');
assert.equal(rev3Release.minimumRevision, 301);
assert.equal(rev3Release.maximumRevision, 399);
assert.equal(getBuildProfile('waveshare_7b_rev3').define, 'DEVICE_WAVESHARE_TOUCH_LCD_7B');

console.log('Waveshare Touch LCD 7B / 7B-C profile contract: PASS');
