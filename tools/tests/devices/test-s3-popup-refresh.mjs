import assert from 'node:assert/strict';
import fs from 'node:fs';

function read(relativePath) {
  return fs.readFileSync(new URL(`../../../${relativePath}`, import.meta.url), 'utf8');
}

const shared = read('src/tiles/runtime/tile_renderer_shared.h');
const weather = read('src/types/weather/renderer.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
const deviceSelect = read('src/devices/device_select.h');

const helperStart = shared.indexOf(
  'static inline void finish_press_before_popup(lv_event_t* event)');
const helperEnd = shared.length;
assert.ok(helperStart >= 0 && helperEnd > helperStart,
          'Shared popup press helper must exist');
const helper = shared.slice(helperStart, helperEnd);
assert.doesNotMatch(helper, /lv_refr_now\s*\(/,
                    'P4 and S3 must both present the popup with the regular refresh');
assert.match(helper, /lv_timer_ready\(lv_display_get_refr_timer/,
             'The prepared popup must request the next regular frame immediately');
assert.match(helper, /lv_event_get_current_target/,
             'Bubbled child events must release the owning card');
const callbackStart = weather.indexOf('auto show_popup = [](lv_event_t* e)');
const callbackEnd = weather.indexOf('lv_obj_add_event_cb(card, show_popup, popup_event, data);', callbackStart);
assert.ok(callbackStart >= 0 && callbackEnd > callbackStart);
const callback = weather.slice(callbackStart, callbackEnd);
assert.match(callback, /finish_press_before_popup\(e\);\s+show_weather_popup\(init\);/);
assert.doesNotMatch(callback, /defer_popup_until_source_refreshed/,
                    'A cold weather payload must not delay the common shell');
assert.doesNotMatch(callback, /lv_refr_now/);
const settingsOpenStart = settings.indexOf('static void open_settings_popup(SettingsPopupKind kind) {');
const settingsClickStart = settings.indexOf(
  'static void on_settings_tile_clicked(', settingsOpenStart);
assert.ok(settingsOpenStart >= 0 && settingsClickStart > settingsOpenStart,
          'Settings popup open function must exist');
const settingsOpen = settings.slice(settingsOpenStart, settingsClickStart);
assert.doesNotMatch(settingsOpen, /lv_refr_now\s*\(/,
                    'Settings must use the normal popup refresh on P4 and S3');
assert.match(settingsOpen, /show_popup_shell\(/);
assert.match(settingsOpen, /defer_popup_content\(/);

for (const marker of [
  'defined(DEVICE_GUITION_ESP32_4848S040) ||',
  'defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)',
  '#define DEVICE_ESP32_S3_RGB_480',
]) {
  assert.ok(deviceSelect.includes(marker),
            `S3 RGB family mapping is missing: ${marker}`);
}

for (const renderer of [
  'src/types/camera/renderer.cpp',
  'src/types/climate/renderer.cpp',
  'src/types/cover/renderer.cpp',
  'src/types/energy/renderer.cpp',
  'src/types/media/renderer.cpp',
  'src/types/sensor/renderer.cpp',
  'src/types/switch/renderer.cpp',
  'src/ui/tabs/settings/tab_settings.cpp',
]) {
  assert.match(read(renderer), /finish_press_before_popup\s*\(/,
               `${renderer} must use the shared popup refresh path`);
}

console.log('ESP32-S3 popup refresh contract OK');
