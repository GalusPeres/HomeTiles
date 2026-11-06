// The BTT Panda Touch drops its pixel clock while the blocking firmware update
// check runs (6 MHz, the Guition profile's value) and restores the stable
// 14 MHz afterwards: a busy CPU starves the panel's refill interrupt and the
// picture jitters. The facade and HomeTiles.ino must include this profile in
// the guard, or the panel stays at the reduced rate only by accident.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n?/g, '\n');

const sketch = read('HomeTiles.ino');
const device = read('src/devices/btt_panda_touch/device_btt_panda_touch.cpp');
const header = read('src/devices/btt_panda_touch/device_btt_panda_touch.h');
const facade = read('src/devices/device.cpp');
const select = read('src/devices/device_select.h');

// The guard API of the profile.
for (const marker of [
  'void displayUpdateCheckGuardBegin();',
  'void displayUpdateCheckGuardEnd();',
]) {
  assert.ok(header.includes(marker), `Panda header: ${marker}`);
}

// The reduced rate, the reduced rate again in Begin, the stable rate in End.
assert.match(device, /constexpr uint32_t kUpdateCheckPclkHz = 6 \* 1000 \* 1000;/);
assert.match(device, /constexpr uint32_t kPanelPclkHz = 14 \* 1000 \* 1000;/);
const beginAt = device.indexOf('void displayUpdateCheckGuardBegin() {');
const endAt = device.indexOf('void displayUpdateCheckGuardEnd() {');
assert.ok(beginAt >= 0 && endAt > beginAt, 'Panda guard functions exist, Begin first');
const guard = device.slice(beginAt, endAt);
assert.ok(guard.includes('esp_lcd_rgb_panel_set_pclk(g_panel, kUpdateCheckPclkHz)'),
  'Begin sets the reduced pixel clock');
assert.ok(guard.includes('g_update_check_pclk_active = true'),
  'Begin records that the rate is reduced');
assert.ok(guard.includes('if (g_update_check_pclk_active || !g_display_ready || !g_panel) return;'),
  'Begin is not re-entered');
const afterEnd = device.slice(endAt, endAt + 900);
assert.ok(afterEnd.includes('esp_lcd_rgb_panel_set_pclk(g_panel, kPanelPclkHz)'),
  'End restores the stable pixel clock');
assert.ok(afterEnd.includes('g_update_check_pclk_active = false'),
  'End clears the reduced-rate flag');
assert.ok(afterEnd.indexOf('g_update_check_pclk_active = false') <
          afterEnd.indexOf('esp_lcd_rgb_panel_set_pclk(g_panel, kPanelPclkHz)'),
  'End clears the flag before it restores the rate');

// The facade forwards only for the profiles that implement it.
const facadeBegin = facade.slice(facade.indexOf('void displayUpdateCheckGuardBegin() {'),
  facade.indexOf('void displayUpdateCheckGuardEnd() {'));
const facadeEnd = facade.slice(facade.indexOf('void displayUpdateCheckGuardEnd() {'),
  facade.indexOf('bool initSDCard() {'));
for (const block of [facadeBegin, facadeEnd]) {
  assert.match(block, /defined\(DEVICE_BTT_PANDA_TOUCH\)/, 'the facade includes this profile');
}

// HomeTiles.ino: the guard brackets the blocking check for this profile too.
const checkStart = sketch.indexOf('static GithubUpdate::CheckResult perform_fw_check()');
const applyStart = sketch.indexOf('static void apply_fw_check()', checkStart);
assert.ok(checkStart >= 0 && applyStart > checkStart, 'update-check boundaries');
const body = sketch.slice(checkStart, applyStart);
const checkCall = body.indexOf('GithubUpdate::CheckResult res = GithubUpdate::checkLatest();');
assert.ok(checkCall > 0, 'the check runs inside perform_fw_check');
assert.ok(body.indexOf('Device::displayUpdateCheckGuardBegin();') < checkCall &&
          body.slice(0, checkCall).includes('DEVICE_BTT_PANDA_TOUCH'),
  'Panda lowers the rate before HTTPS');
assert.ok(body.indexOf('Device::displayUpdateCheckGuardEnd();') > checkCall &&
          body.slice(checkCall, body.indexOf('Device::displayUpdateCheckGuardEnd();')).includes('DEVICE_BTT_PANDA_TOUCH'),
  'Panda restores the rate after HTTPS');
// The guard runs on an active link only, through the S3 RGB family flag.
assert.match(select, /defined\(DEVICE_BTT_PANDA_TOUCH\)[\s\S]{0,40}#define DEVICE_ESP32_S3_RGB_480/,
  'Panda is part of the S3 RGB 480 family (the guard condition)');

console.log('BTT Panda Touch update-check PCLK guard: PASS');
