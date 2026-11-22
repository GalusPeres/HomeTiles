// The page under the open screensaver is hidden (user log b637: PopupDraw
// under=214 ms over the screensaver against 36 ms on a page). LVGL draws the
// active screen under every area it refreshes on the top layer, so every
// value change, press and popup on the screensaver also drew the page's
// tiles. Real LVGL: while hidden, a refresh of a top-layer area draws no
// page object; hiding redraws nothing by itself; shown again, the page draws.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const screensaver = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const fn = (name) => {
  const found = cppFunctionDefinitions(screensaver).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// ---- Wiring -------------------------------------------------------------------
const show = fn('show_image_screensaver');
assert.ok(show.indexOf('hide_page_under_screensaver(true);') > show.lastIndexOf('apply_configured_screensaver_brightness();'),
  'the page goes once the screensaver is up');
const hide = fn('hide_image_screensaver');
assert.ok(hide.indexOf('hide_page_under_screensaver(false);') >= 0 &&
  hide.indexOf('hide_page_under_screensaver(false);') < hide.indexOf('lv_obj_invalidate(lv_screen_active());'),
'the page comes back before closing draws the whole screen');
assert.ok(fn('on_global_overlay_delete').includes('hide_page_under_screensaver(false);'),
  'an overlay LVGL deleted itself brings the page back too');

// ---- Real LVGL ------------------------------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the page drawing needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/screensaver-hides-page');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <cstdio>
#include <cstdint>
#include <lvgl.h>
#include <lvgl_private.h>
${screensaver.match(/lv_obj_t\* g_hidden_page = nullptr;/)[0]}
${fn('hidden_page_deleted')}
${fn('hide_page_under_screensaver')}
int page_draws = 0;
int main() {
  lv_init();
  static uint32_t pixels[320 * 200];
  auto* display = lv_display_create(320, 200);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  // A page tile on the screen and the screensaver's opaque overlay with a
  // value on the top layer.
  lv_obj_t* tile = lv_obj_create(lv_screen_active());
  lv_obj_set_size(tile, 300, 180);
  lv_obj_add_event_cb(tile, [](lv_event_t*) { ++page_draws; }, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_t* overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, 320, 200);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_t* value = lv_label_create(overlay);
  lv_label_set_text(value, "1");
  lv_refr_now(display);
  page_draws = 0;
  lv_label_set_text(value, "2");
  lv_refr_now(display);
  std::printf("open %d\n", page_draws);
  // The screensaver hides the page: nothing redraws for it, and a value
  // change on the screensaver no longer draws the page.
  hide_page_under_screensaver(true);
  std::printf("hidden %u\n", static_cast<unsigned>(display->inv_p));
  page_draws = 0;
  lv_label_set_text(value, "3");
  lv_refr_now(display);
  std::printf("under %d\n", page_draws);
  // Closing brings it back; the next refresh draws it.
  hide_page_under_screensaver(false);
  lv_obj_invalidate(lv_screen_active());
  page_draws = 0;
  lv_refr_now(display);
  std::printf("back %d %d\n", page_draws, lv_obj_has_flag(lv_screen_active(), LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
  return 0;
}
`);
const binary = path.join(out, 'test' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-Wno-deprecated-declarations', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const rows = Object.fromEntries(run.stdout.trim().split('\n').map((line) => {
  const [key, ...rest] = line.trim().split(' ');
  return [key, rest];
}));
assert.ok(Number(rows.open[0]) > 0, `before: a top-layer change drew the page under it\n${run.stdout}`);
assert.deepEqual(rows.hidden, ['0'], 'hiding the page redraws nothing');
assert.deepEqual(rows.under, ['0'], 'hidden: a top-layer change draws no page object');
assert.ok(Number(rows.back[0]) > 0 && rows.back[1] === '0', `shown again, the page draws\n${run.stdout}`);
console.log('Screensaver hides the page under it: top-layer changes no longer draw the page');
