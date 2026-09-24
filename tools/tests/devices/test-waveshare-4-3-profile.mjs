import { getBuildProfile, getReleaseProfile } from '../../device-catalog.js';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { DEVICE_PROFILES } from '../../../docs/assets/javascripts/installer-contract.mjs';

function read(path) {
  return fs.readFileSync(path, 'utf8').replace(/\r\n/g, '\n');
}

function requireMarker(source, marker, label) {
  assert.ok(source.includes(marker), `${label} is missing: ${marker}`);
}

const base = 'src/devices/waveshare_touch_lcd_4_3';
const select = read('src/devices/device_select.h');
const active = read('src/devices/active_device.h');
const metadata = read('src/core/firmware/firmware_metadata.cpp');
const profile = read(`${base}/profile.h`);
const config = read(`${base}/vendor/displays_config.h`);
const driver = read(`${base}/device_waveshare_touch_lcd_4_3.cpp`);
const touch = read(`${base}/vendor/gt911.h`);
const panel = read(`${base}/vendor/panel_init_cmds.c`);
const sketch = read('sketch.yaml');
const workflow = read('.github/workflows/firmware.yml');

requireMarker(select, 'defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3)', 'device selector');
requireMarker(active, `${base}/device_waveshare_touch_lcd_4_3.h`, 'active device');
requireMarker(metadata, '"waveshare_touch_lcd_4_3"', 'firmware metadata');
requireMarker(profile, '"waveshare_touch_lcd_4_3"', 'profile key');
requireMarker(profile, '    800,\n    480,', 'landscape resolution');
requireMarker(profile, '    5,\n    4,', 'compact grid');
requireMarker(profile, '    10,\n    3,\n    150,\n    111,', 'compact grid geometry');
requireMarker(profile, '    1,\n    Device::RotationStepMode::FlipOnly', 'brightness mapping');
requireMarker(config, '    30000000,\n    500,\n    480,\n    800,', 'panel timing');
requireMarker(config, '    7,\n    8,\n    400000,\n    27,', 'touch and reset pins');
requireMarker(driver, 'constexpr gpio_num_t kBacklightPin = GPIO_NUM_26;', 'backlight pin');
requireMarker(driver, 'constexpr bool kBacklightActiveLow = true;', 'backlight polarity');
requireMarker(driver, 'MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M', 'pre-v3 DSI clock');
requireMarker(driver, 'esp_lcd_new_panel_st7701', 'ST7701 driver');
requireMarker(touch, '#define EXAMPLE_PIN_NUM_TOUCH_RST       (GPIO_NUM_NC)', 'touch reset');
requireMarker(touch, '#define EXAMPLE_PIN_NUM_TOUCH_INT       (GPIO_NUM_NC)', 'touch interrupt');
requireMarker(panel, 'kWaveshareSt7701Init4_3', 'Waveshare init table');
requireMarker(sketch, '  waveshare_4_3:', 'build profile');
requireMarker(sketch, 'ChipVariant=prev3', 'pre-v3 build selection');
assert.equal(getBuildProfile('waveshare_4_3').define, 'DEVICE_WAVESHARE_TOUCH_LCD_4_3');
for (const marker of [
  'label: Waveshare Touch LCD 4.3',
  'profile: waveshare_4_3',
  'key: waveshare_touch_lcd_4_3',
  'define: DEVICE_WAVESHARE_TOUCH_LCD_4_3',
  'silicon_variant: pre_v3',
  'rx_variant: repo-a8204',
  'node tools/run-tests.mjs',
  '--verify-release-assets',
]) {
  requireMarker(workflow, marker, 'firmware workflow');
}
const releaseProfile = getReleaseProfile('waveshare_touch_lcd_4_3');
assert.equal(releaseProfile.siliconVariant, 'pre_v3');
assert.equal(releaseProfile.legacySlug, 'waveshare-touch-lcd-4-3');
assert.equal(releaseProfile.metadataDeviceKey, 'waveshare_touch_lcd_4_3');

const installer = DEVICE_PROFILES.find(
  (candidate) => candidate.key === 'waveshare_touch_lcd_4_3',
);
assert.ok(installer, 'browser installer profile is missing');
assert.equal(installer.buildProfile, 'waveshare_4_3');
assert.equal(installer.chipFamily, 'ESP32-P4');
assert.equal(installer.flashSize, 32 * 1024 * 1024);
assert.equal(installer.siliconVariant, 'pre_v3');
assert.equal(installer.status, 'validation-pending');

for (const [file, marker] of [
  ['README.md', 'https://galusperes.github.io/#device-support'],
  ['docs/index.md', '| Waveshare LCD-4.3 (P4) | 4.3" / 800×480 |'],
  ['docs/index.md', 'Support details for Waveshare LCD-4.3 (P4)'],
  ['docs/screensaver.md', '(index.md#device-support)'],
  ['RELEASING.md', '34 binaries'],
]) {
  requireMarker(read(file), marker, `${file} release documentation`);
}

console.log('Waveshare Touch LCD 4.3 profile contract: PASS');
