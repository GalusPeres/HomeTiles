// Every tile presses the same way: only its own lighter pressed color. The
// default theme darkened pressed button tiles with a black recolor over the
// whole content (icons included), except inline-colored text (the colored
// weather icons) and tiles that are no buttons (Switch with its toggle), so
// icons changed on some tiles and not on others. Runs the production helper
// on RGB565 with the device theme and its transitions.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
// render_tile() applies it to every tile type.
assert.match(read('src/tiles/runtime/tile_renderer.cpp'),
  /tile_icon_disc::apply_tile_options\(tile_obj, tile\.icon_disc_mode, tile\.icon_glow\);\s*disable_pressed_recolor\(tile_obj\);/);
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Tile press recolor needs LVGL and a host compiler');
  process.exit(0);
}
const helper = cppFunctionDefinitions(read('src/tiles/runtime/tile_renderer_shared.h'))
  .find(fn => fn.name === 'disable_pressed_recolor').source;
const out = path.join(root, 'build/tests/tile-press-recolor');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
${helper}
static uint16_t fb[200 * 100];
static uint32_t px(int x, int y) {
  const uint16_t c = fb[y * 200 + x];
  const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return ((r * 527 + 23) >> 6) << 16 | ((g * 259 + 33) >> 6) << 8 | ((b * 527 + 23) >> 6);
}
int main() {
  lv_init();
  lv_display_t* d = lv_display_create(200, 100);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, fb, nullptr, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(d, [](lv_display_t* disp, const lv_area_t*, uint8_t*) { lv_display_flush_ready(disp); });
  lv_display_set_theme(d, lv_theme_default_init(d, lv_color_hex(0x26A69A), lv_color_hex(0xC14444), false, LV_FONT_DEFAULT));
  // Two button tiles, one with the helper; each holds a white icon stand-in.
  lv_obj_t* cards[2];
  for (int i = 0; i < 2; ++i) {
    lv_obj_t* card = lv_button_create(lv_screen_active());
    lv_obj_set_size(card, 90, 90);
    lv_obj_set_pos(card, 5 + i * 100, 5);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A2A), LV_STATE_PRESSED);
    lv_obj_t* icon = lv_obj_create(card);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 30, 30);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(icon, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    if (i == 1) disable_pressed_recolor(card);
    cards[i] = card;
  }
  lv_refr_now(d);
  for (lv_obj_t* card : cards) lv_obj_add_state(card, LV_STATE_PRESSED);
  for (int t = 0; t < 300; t += 10) { lv_tick_inc(10); lv_timer_handler(); lv_refr_now(d); }
  const uint32_t theme_icon = px(50, 30), icon = px(150, 30), card = px(150, 85);
  std::printf("theme icon #%06X, icon #%06X, card #%06X\n", (unsigned)theme_icon, (unsigned)icon, (unsigned)card);
  if (theme_icon == 0xFFFFFF) { std::printf("FAIL the theme no longer darkens: the test lost its point\n"); return 1; }
  if (icon != 0xFFFFFF) { std::printf("FAIL a pressed tile changed its icon\n"); return 1; }
  if (card != 0x292829) { std::printf("FAIL the pressed tile shows #%06X instead of its pressed color\n", (unsigned)card); return 1; }
  std::printf("OK\n");
  return 0;
}
`;
const src = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(src, cpp);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', src, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Tile press: every tile shows only its pressed color, icons unchanged');
