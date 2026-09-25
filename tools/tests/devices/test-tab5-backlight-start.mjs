// M5Stack Tab5 backlight: the driver keeps running when dimmed down to 1%, but
// does not start from off at that level, so waking at 1% left the display dark.
// Every runtime backlight change must go through apply_backlight(), which
// starts from off at a safe level before settling on the requested value.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs
  .readFileSync(path.join(root, 'src/devices/m5stacks_tab5/device_m5stacks_tab5.cpp'), 'utf8')
  .replaceAll('\r\n', '\n');

const helper = source.match(/void apply_backlight\(uint8_t value\) \{([\s\S]*?)\n\}/);
assert.ok(helper, 'apply_backlight() helper is missing');
assert.match(
  helper[1],
  /if \(g_backlight_applied == 0 && value > 0 && value < kBacklightStartLevel\) \{\s*M5\.Display\.setBrightness\(kBacklightStartLevel\);\s*delay\(kBacklightStartHoldMs\);\s*\}/,
  'switching on from off must start at the safe level first');
assert.match(helper[1], /M5\.Display\.setBrightness\(value\);\s*g_backlight_applied = value;/);

const runtimeStart = source.indexOf('void DeviceM5StacksTab5::setBrightness(uint8_t value) {');
const runtimeEnd = source.indexOf('bool DeviceM5StacksTab5::initSDCard() {');
assert.ok(runtimeStart > 0 && runtimeEnd > runtimeStart, 'runtime backlight section not found');
const runtime = source.slice(runtimeStart, runtimeEnd);
assert.doesNotMatch(runtime, /M5\.Display\.setBrightness\(/,
  'runtime backlight paths must not bypass apply_backlight()');

for (const fn of ['setBrightness', 'displayWake', 'displayPowerSaveOff']) {
  const body = runtime.match(new RegExp(`void DeviceM5StacksTab5::${fn}\\([^)]*\\) \\{([\\s\\S]*?)\\n\\}`));
  assert.ok(body, `${fn}() not found`);
  assert.match(body[1], /apply_backlight\(/, `${fn}() must use apply_backlight()`);
}

// Sleep still only blanks the backlight; the panel and touch stay awake.
const sleep = runtime.match(/void DeviceM5StacksTab5::displaySleep\(\) \{([\s\S]*?)\n\}/);
assert.ok(sleep);
assert.match(sleep[1], /apply_backlight\(0\);/);
assert.doesNotMatch(sleep[1], /M5\.Display\.sleep\(\)/);

console.log('Tab5 backlight start: OK');
