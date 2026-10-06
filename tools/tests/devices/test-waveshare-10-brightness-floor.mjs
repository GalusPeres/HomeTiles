import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');
}

function sliceFunction(source, startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);
  assert.ok(start >= 0 && end > start, `could not isolate ${startMarker}`);
  return source.slice(start, end);
}

function rawFromPercent(percent, inputMin, configuredPercentMin) {
  percent = Math.max(configuredPercentMin, Math.min(100, percent));
  const span = 255 - inputMin;
  return inputMin + Math.floor(((percent - 1) * span + 49) / 99);
}

function percentFromRaw(raw, inputMin, configuredPercentMin) {
  raw = Math.max(inputMin, raw);
  const span = 255 - inputMin;
  const percent = span === 0
    ? 100
    : 1 + Math.floor(((raw - inputMin) * 99 + Math.floor(span / 2)) / span);
  return Math.max(configuredPercentMin, percent);
}

const deviceHeader = read('src/devices/device.h');
const configManager = read('src/core/config/config_manager.cpp');
const settingsUi = read('src/ui/tabs/settings/tab_settings.cpp');
const mqttHandlers = read('src/network/mqtt/mqtt_handlers.cpp');
const waveshareProfile = read(
  'src/devices/waveshare_touch_lcd_10_1/profile.h',
);
const waveshareDriver = read(
  'src/devices/waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.cpp',
);

assert.match(
  waveshareProfile,
  /Device::RotationStepMode::FlipOnly,\s*2,\s*0,/,
  'Waveshare 10.1 must boot in the hardware-confirmed 180-degree orientation',
);

assert.match(
  deviceHeader,
  /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_10_1\)\s*inline constexpr uint8_t kConfiguredBrightnessPercentMin = 2;\s*#else\s*inline constexpr uint8_t kConfiguredBrightnessPercentMin = 1;\s*#endif/,
  'only the exact Waveshare 10.1 profile may use the 2 percent floor',
);
assert.match(
  deviceHeader,
  /backlightRawFromPercent[\s\S]*?percent < kConfiguredBrightnessPercentMin[\s\S]*?percent = kConfiguredBrightnessPercentMin/,
  'percent-to-raw conversion must enforce the configured visible floor',
);
assert.match(
  deviceHeader,
  /backlightPercentFromRaw[\s\S]*?percent < kConfiguredBrightnessPercentMin[\s\S]*?percent = kConfiguredBrightnessPercentMin/,
  'reported percentages must not fall below the configured visible floor',
);

const waveshareInputMin = 121;
assert.equal(rawFromPercent(0, waveshareInputMin, 2), 122);
assert.equal(rawFromPercent(1, waveshareInputMin, 2), 122);
assert.equal(rawFromPercent(2, waveshareInputMin, 2), 122);
assert.equal(rawFromPercent(100, waveshareInputMin, 2), 255);
assert.equal(percentFromRaw(0, waveshareInputMin, 2), 2);
assert.equal(percentFromRaw(121, waveshareInputMin, 2), 2);
assert.equal(percentFromRaw(122, waveshareInputMin, 2), 2);
assert.equal(rawFromPercent(1, waveshareInputMin, 1), 121);
assert.equal(percentFromRaw(121, waveshareInputMin, 1), 1);

const normalizeDisplay = sliceFunction(
  configManager,
  'static uint8_t normalize_display_brightness',
  'bool ConfigManager::load()',
);
assert.match(
  normalizeDisplay,
  /Device::backlightRawFromPercent\(\s*Device::kConfiguredBrightnessPercentMin\)/,
  'persisted normal brightness must use the configured raw floor',
);
assert.match(
  normalizeDisplay,
  /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_10_1\)[\s\S]*?return configured_min;/,
  'legacy Waveshare 10.1 values below 2 percent must migrate to 2 percent',
);

const saveScreensaver = sliceFunction(
  configManager,
  'bool ConfigManager::saveScreensaverBrightness',
  'bool ConfigManager::saveStaticAddressingEnabled',
);
assert.match(
  saveScreensaver,
  /brightness_pct < Device::kConfiguredBrightnessPercentMin[\s\S]*?brightness_pct = Device::kConfiguredBrightnessPercentMin/,
  'screensaver persistence must clamp to the exact-device floor',
);

const loadConfig = sliceFunction(
  configManager,
  'bool ConfigManager::load()',
  'bool ConfigManager::save(const DeviceConfig& cfg)',
);
assert.match(
  loadConfig,
  /config\.screensaver_brightness_pct <\s*Device::kConfiguredBrightnessPercentMin[\s\S]*?config\.screensaver_brightness_pct =\s*Device::kConfiguredBrightnessPercentMin/,
  'loading a persisted 1 percent screensaver value must migrate it to 2 percent',
);
const saveConfig = sliceFunction(
  configManager,
  'bool ConfigManager::save(const DeviceConfig& cfg)',
  'bool ConfigManager::saveDisplaySettings',
);
assert.match(
  saveConfig,
  /normalized\.screensaver_brightness_pct <\s*Device::kConfiguredBrightnessPercentMin[\s\S]*?normalized\.screensaver_brightness_pct =\s*Device::kConfiguredBrightnessPercentMin/,
  'full configuration saves must normalize screensaver brightness to the device floor',
);

assert.match(
  settingsUi,
  /kSettingsBrightnessPctMin =\s*Device::kConfiguredBrightnessPercentMin;/,
  'the on-device normal brightness slider must start at the device floor',
);
assert.match(
  settingsUi,
  /values\.saver_brightness_min = Device::kConfiguredBrightnessPercentMin;\s*values\.saver_brightness_max = kScreensaverBrightnessPctMax;/,
  'the on-device screensaver brightness slider must start at the device floor',
);

const mqttDisplay = sliceFunction(
  mqttHandlers,
  'static void handleDisplayBrightnessCommand',
  'static void handleScreensaverBrightnessCommand',
);
assert.match(
  mqttDisplay,
  /Device::backlightRawFromPercent\(\s*Device::kConfiguredBrightnessPercentMin\)/,
  'legacy raw MQTT brightness must also use the configured floor',
);
const mqttScreensaver = sliceFunction(
  mqttHandlers,
  'static void handleScreensaverBrightnessCommand',
  'static void handleDisplayRotateCommand',
);
assert.match(
  mqttScreensaver,
  /percent < Device::kConfiguredBrightnessPercentMin[\s\S]*?percent = Device::kConfiguredBrightnessPercentMin/,
  'screensaver MQTT brightness must clamp to the configured floor',
);

assert.match(
  waveshareDriver,
  /if \(value == 0\) \{\s*brightness_percent = 0;/,
  'the low-level driver must retain raw 0 for blanking',
);
assert.match(
  waveshareDriver,
  /void DeviceWaveshareTouchLCD10::displaySleep\(\)[\s\S]*?apply_backlight\(0\);/,
  'display sleep must still blank the backlight directly',
);
assert.match(
  waveshareDriver,
  /void DeviceWaveshareTouchLCD10::prepareForRestart\(\)[\s\S]*?apply_backlight\(0\);/,
  'restart preparation must still blank the backlight directly',
);
assert.doesNotMatch(
  waveshareDriver,
  /kConfiguredBrightnessPercentMin/,
  'the measured UI floor must not alter the low-level PWM curve',
);

console.log('Waveshare 10.1 brightness floor contract OK');
