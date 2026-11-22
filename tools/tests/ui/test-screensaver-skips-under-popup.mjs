// A popup over the screensaver lies on LVGL's top layer, which LVGL draws
// from its root in every band; on a screen it starts at the topmost object
// covering the band. So every band under the opaque card drew the
// screensaver's wallpaper and tiles first (user log b638: under=127-136 ms
// per opening against 36 ms on a page). Real LVGL: a band the card covers
// draws no screensaver object and shows the card; a band beside the card or
// at its rounded corner still draws the screensaver; without a popup over the
// screensaver nothing is skipped.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (file) => readRepoFile(file).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const screensaver = read('src/ui/screensaver/image_screensaver.cpp');
const shell = read('src/ui/popups/popup_shell.cpp');

// ---- Wiring -------------------------------------------------------------------
assert.match(fn(screensaver, 'show_image_screensaver'),
  /lv_obj_add_event_cb\(st->overlay, skip_under_popup, LV_EVENT_DRAW_MAIN_BEGIN, nullptr\);/,
  'the screensaver overlay checks every band it draws');
assert.match(fn(shell, 'popup_shell_covers_over_screensaver'), /popup_shell_over_screensaver\(\) &&/,
  'only a popup over the screensaver hides it, never a page popup under it');

// ---- Real LVGL ------------------------------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the band drawing needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/screensaver-skips-under-popup');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <lvgl.h>
#include <lvgl_private.h>
struct { lv_obj_t* overlay = nullptr; } shell;
bool g_over = true;
bool popup_shell_over_screensaver() { return g_over; }
${fn(shell, 'popup_shell_covers_over_screensaver')}
${fn(screensaver, 'skip_under_popup')}
constexpr int W = 320, H = 200;
uint32_t frame[W * H];
int under_draws = 0, side_draws = 0, card_draws = 0;
uint32_t px(int x, int y) { return frame[y * W + x] & 0xFFFFFF; }
void refresh(lv_display_t* d, lv_area_t area) {
  under_draws = side_draws = card_draws = 0;
  lv_inv_area(d, &area);
  lv_refr_now(d);
}
int main() {
  lv_init();
  static uint32_t pixels[W * 40];
  auto* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* a, uint8_t* map) {
    const int32_t w = lv_area_get_width(a);
    for (int32_t y = a->y1; y <= a->y2; ++y)
      std::memcpy(&frame[y * W + a->x1], map + (y - a->y1) * w * 4, w * 4);
    lv_display_flush_ready(d);
  });
  // The screensaver: opaque overlay, a tile under the card, one beside it.
  lv_obj_t* overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, W, H);
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(overlay, skip_under_popup, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
  auto tile = [&](int x, int y, int w, int h, int* draws) {
    lv_obj_t* t = lv_obj_create(overlay);
    lv_obj_remove_style_all(t);
    lv_obj_set_pos(t, x, y);
    lv_obj_set_size(t, w, h);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(t, [](lv_event_t* e) { ++*static_cast<int*>(lv_event_get_user_data(e)); },
                        LV_EVENT_DRAW_MAIN, draws);
  };
  tile(55, 15, 210, 170, &under_draws);
  tile(0, 0, 50, H, &side_draws);
  // The popup shell above it: a transparent overlay and an opaque card.
  shell.overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(shell.overlay);
  lv_obj_set_size(shell.overlay, W, H);
  lv_obj_t* card = lv_obj_create(shell.overlay);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, 60, 20);
  lv_obj_set_size(card, 200, 160);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x2040A0), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(card, [](lv_event_t*) { ++card_draws; }, LV_EVENT_DRAW_MAIN, nullptr);
  lv_refr_now(display);
  // A band the card covers: no screensaver object, the card's pixels.
  refresh(display, {80, 50, 239, 149});
  std::printf("covered %d %d %06x\n", under_draws, card_draws, static_cast<unsigned>(px(150, 100)));
  // Beside the card and across its edge: the screensaver draws.
  refresh(display, {0, 50, 49, 149});
  std::printf("beside %d\n", side_draws);
  refresh(display, {40, 50, 120, 149});
  std::printf("edge %d %06x %06x\n", under_draws, static_cast<unsigned>(px(57, 100)), static_cast<unsigned>(px(100, 100)));
  // The card's rounded corner: not covered, the screensaver shows there.
  refresh(display, {60, 20, 69, 29});
  std::printf("corner %d %06x\n", under_draws, static_cast<unsigned>(px(60, 20)));
  // No popup over the screensaver: nothing is skipped.
  g_over = false;
  refresh(display, {80, 50, 239, 149});
  std::printf("page %d %06x\n", under_draws, static_cast<unsigned>(px(150, 100)));
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
const card = (0x2040A0).toString(16).padStart(6, '0');
assert.ok(rows.covered[0] === '0' && Number(rows.covered[1]) > 0 && rows.covered[2] === card,
  `a covered band draws only the card\n${run.stdout}`);
assert.ok(Number(rows.beside[0]) > 0, `beside the card the screensaver draws\n${run.stdout}`);
assert.ok(Number(rows.edge[0]) > 0 && rows.edge[1] === '00ff00' && rows.edge[2] === card,
  `across the card's edge: tile outside, card inside\n${run.stdout}`);
assert.ok(Number(rows.corner[0]) > 0 && rows.corner[1] === '00ff00', `the rounded corner shows the screensaver\n${run.stdout}`);
assert.ok(Number(rows.page[0]) > 0 && rows.page[1] === card, `without a popup over it nothing is skipped\n${run.stdout}`);
console.log('Screensaver under a popup: covered bands draw only the popup, its edges and corners the screensaver');
