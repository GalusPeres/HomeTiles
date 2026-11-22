// A tap beside a popup's card closes the popup like its X (#142). Every tile
// popup sits in the shared shell (popup_shell.cpp) whose full-screen overlay
// already caught presses beside the card; it now closes through the same
// path as the X (close_clicked: PIN refusal and cleanup stay with the popup's
// own handler). Real LVGL with a pointer: a tap beside the card closes, a tap
// on the card or on its controls does not, and neither does a press on the
// card that slides off it.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8').replace(/\r\n/g, '\n');
const shellSource = read('src/ui/popups/popup_shell.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(shellSource).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

// ---- The shell --------------------------------------------------------------
const ensure = fn('ensure_shell');
assert.ok(ensure.includes('lv_obj_add_event_cb(shell.overlay, overlay_pressed, LV_EVENT_PRESSED, nullptr);') &&
  ensure.includes('lv_obj_add_event_cb(shell.overlay, overlay_pressed, LV_EVENT_CLICKED, nullptr);'),
'the shared overlay listens for presses beside the card');
assert.ok(fn('overlay_pressed').includes('if (outside) close_clicked(event);'), 'it closes like the X');
// Every tile popup is shown in this shell.
for (const popup of ['calendar', 'camera', 'climate', 'cover', 'device', 'energy', 'light', 'media', 'pin',
  'sensor', 'vacuum', 'weather']) {
  assert.ok(read(`src/ui/popups/${popup}/${popup}_popup.cpp`).includes('show_popup_shell('), `${popup} popup in the shell`);
}

// ---- Real LVGL --------------------------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the tap needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/popup-tap-outside');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <cassert>
#include <cstdio>
#include <lvgl.h>
#include <lvgl_private.h>
struct Shell { lv_obj_t* overlay = nullptr; lv_obj_t* frame = nullptr; } shell;
int closes = 0;
void close_clicked(lv_event_t* event) { if (lv_event_get_code(event) == LV_EVENT_CLICKED) ++closes; }
${shellSource.match(/bool g_pressed_outside = false;/)[0]}
${fn('outside_card')}
${fn('overlay_pressed')}
struct Touch { lv_point_t point{}; lv_indev_state_t state = LV_INDEV_STATE_RELEASED; } touch;
lv_indev_t* pointer = nullptr;
void step() { lv_tick_inc(20); lv_indev_read(pointer); }
void tap(int x1, int y1, int x2, int y2) {
  touch.point = {x1, y1}; touch.state = LV_INDEV_STATE_PRESSED; step();
  for (int i = 1; i <= 4; ++i) { touch.point = {x1 + (x2 - x1) * i / 4, y1 + (y2 - y1) * i / 4}; step(); }
  touch.state = LV_INDEV_STATE_RELEASED; step(); step();
}
int main() {
  lv_init();
  static uint32_t pixels[320 * 160];
  auto* display = lv_display_create(320, 160);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  pointer = lv_indev_create();
  lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer, [](lv_indev_t*, lv_indev_data_t* data) { data->point = touch.point; data->state = touch.state; });
  // The shell as ensure_shell/show_popup_shell build it: a full-screen
  // clickable overlay, the frame and the popup body (a square card in the
  // middle of a landscape screen) with a button on it.
  shell.overlay = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(shell.overlay);
  lv_obj_set_size(shell.overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(shell.overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(shell.overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(shell.overlay, overlay_pressed, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(shell.overlay, overlay_pressed, LV_EVENT_CLICKED, nullptr);
  shell.frame = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(shell.frame);
  lv_obj_set_size(shell.frame, 160, 160);
  lv_obj_center(shell.frame);
  lv_obj_t* body = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, 160, 160);
  lv_obj_center(body);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE);  // an empty part of the card falls through to the overlay
  lv_obj_t* button = lv_button_create(body);
  lv_obj_set_size(button, 60, 40);
  lv_obj_center(button);
  lv_obj_update_layout(lv_screen_active());

  tap(20, 80, 20, 80);
  assert(closes == 1 && "a tap beside the card closes");
  tap(300, 20, 300, 20);
  assert(closes == 2 && "on both sides");
  tap(160, 80, 160, 80);
  assert(closes == 2 && "a tap on a control on the card does not");
  tap(100, 20, 100, 20);
  assert(closes == 2 && "nor a tap on an empty part of the card");
  tap(100, 20, 20, 20);
  assert(closes == 2 && "nor a press on the card that slides off it");
  tap(20, 80, 120, 80);
  assert(closes == 2 && "nor a press beside it that ends on the card");
  std::printf("Popup tap outside: closes beside the card only\n");
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, 'test' + (process.platform === 'win32' ? '.exe' : ''));
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8', timeout: 60000});
assert.equal(result.status, 0, result.stdout + result.stderr);
process.stdout.write(result.stdout);
console.log('Popup tap outside: shell contract and real taps passed');
