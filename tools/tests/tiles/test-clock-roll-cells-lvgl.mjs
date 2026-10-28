// Rolling clock digits: each character was a cell holding a strip that held
// the shown and the incoming digit, and the strip moved; the strip was one
// more object in every draw band (V2 b592: the clock ~15 ms of a page turn).
// Now the cell holds the two labels and the animation moves them
// (clock/renderer.cpp clock_roll_y, clock_roll_done). Real LVGL draws the old
// cell and the new one with the panel's functions at every step of a roll in
// 28-row RGB565 bands: every pixel must match.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const renderer = fs.readFileSync(path.join(root, 'src/types/clock/renderer.cpp'), 'utf8').replace(/\r\n/g, '\n');
const fns = cppFunctionDefinitions(renderer);
const fn = (name) => fns.find((f) => f.name === name).source;
const rebuild = fn('clock_roll_rebuild');
assert.doesNotMatch(rebuild, /strip/, 'no strip object between a cell and its digits');
assert.ok(rebuild.includes('clock_roll_label(cell, roll.font, width, 0, ch);') &&
  rebuild.includes('clock_roll_label(cell, roll.font, width, roll.line_h, ch);'), 'the two digits sit in the cell');
assert.ok(fn('clock_roll_set_text').includes('lv_anim_set_var(&anim, cell);'), 'the roll animates the cell\'s digits');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the roll cell pixel run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" { extern const lv_font_t ui_font_48; }

${fn('clock_roll_y')}
${fn('clock_roll_done')}

static constexpr int W = 200, H = 120, kBand = 28;
static std::vector<uint16_t> g_screen(W * H);
static std::vector<uint16_t> g_band(W * kBand);

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = a->x2 - a->x1 + 1;
  const uint16_t* src = reinterpret_cast<const uint16_t*>(px);
  for (int y = a->y1; y <= a->y2; ++y) memcpy(&g_screen[y * W + a->x1], src + (y - a->y1) * w, w * 2);
  lv_display_flush_ready(d);
}

static lv_obj_t* label(lv_obj_t* parent, lv_coord_t width, lv_coord_t y, const char* text) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &ui_font_48, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xF2F2F2), 0);
  lv_obj_set_width(l, width);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(l, 0, y);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* plain(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// One digit rolling from 4 to 5 at value (0 .. -line height), old or new.
static void frame(bool strip, int32_t value, std::vector<uint16_t>& out) {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  const lv_coord_t line_h = lv_font_get_line_height(&ui_font_48);
  const lv_coord_t width = lv_font_get_glyph_width(&ui_font_48, '4', 0);
  lv_obj_t* cell = plain(screen, width, line_h);
  lv_obj_set_pos(cell, 37, 23);
  if (strip) {
    lv_obj_t* s = plain(cell, width, line_h * 2);
    label(s, width, 0, "4");
    label(s, width, line_h, "5");
    lv_obj_set_y(s, value);
  } else {
    label(cell, width, 0, "4");
    label(cell, width, line_h, "5");
    clock_roll_y(cell, value);
  }
  lv_obj_t* old = lv_screen_active();
  lv_screen_load(screen);
  if (old) lv_obj_delete(old);
  lv_obj_invalidate(screen);
  lv_refr_now(nullptr);
  out = g_screen;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_band.data(), nullptr, g_band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  const lv_coord_t line_h = lv_font_get_line_height(&ui_font_48);
  unsigned diffs = 0, steps = 0;
  std::vector<uint16_t> a, b;
  for (int32_t value = 0; value >= -line_h; value -= 3) {
    frame(true, value, a);
    frame(false, value, b);
    for (int i = 0; i < W * H; ++i) diffs += a[i] != b[i];
    ++steps;
  }
  // The end of a roll: the new digit shown, both labels back in place.
  frame(true, 0, a);
  lv_obj_t* cell = lv_obj_get_child(lv_screen_active(), 0);
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(cell, 0), 0), "5");
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  a = g_screen;
  frame(false, -line_h, b);
  cell = lv_obj_get_child(lv_screen_active(), 0);
  lv_anim_t anim;
  lv_anim_init(&anim);
  anim.var = cell;
  clock_roll_done(&anim);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  b = g_screen;
  unsigned done_diffs = 0;
  for (int i = 0; i < W * H; ++i) done_diffs += a[i] != b[i];
  printf("{\"diffs\":%u,\"steps\":%u,\"done\":%u}\n", diffs, steps, done_diffs);
  return 0;
}
`;

const out = path.join(root, 'build/tests/clock-roll-cells');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-DUI_FONT_48=1', '-std=c++17', '-O1', '-w', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.ok(run.steps >= 10, 'every few pixels of a roll');
assert.equal(run.diffs, 0, `the digits roll in the cell as they did in the strip ${JSON.stringify(run)}`);
assert.equal(run.done, 0, 'a finished roll shows the new digit in place');
console.log(`PASS: rolling digits without the strip draw the old pixels ${JSON.stringify(run)}`);
