// Analog clock faces for the clock tile (user 2026-10-12, mockup
// build/design-mockups/clock "Analog" and the wall clock of his photo):
// clock_face::create draws the dial, the marks and the hands for the local
// time on real LVGL, moves the hands only when the minute changes and frees
// its timer with the face. The panel's clock_face_layout (clock/renderer.cpp)
// and the preview's clockFaceLayout (clock/admin.js) place the face and its
// lines alike for every tile size.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// The renderer picks the face by the style and keeps the digital clock
// unchanged; the wall clock takes its preset colors, the analog one the
// theme's icon circle.
const renderer = read('src/types/clock/renderer.cpp');
const fns = cppFunctionDefinitions(renderer);
const fn = name => fns.find(f => f.name === name).source;
assert.match(fn('render_clock_tile'),
  /if \(date_options\.style == clock_tile::STYLE_ANALOG \|\| date_options\.style == clock_tile::STYLE_WALL\) \{\s*render_clock_face\(/);
const face = fn('render_clock_face');
assert.match(face, /colors\.dial = clock_face::kDialColors\[/);
assert.match(face, /tile_icon_disc::tile_fill\(card, dial_card, ui_theme::icon\(\), false, false, false,/);
// A Transparent clock keeps a visible dial (user 2026-10-12: black on black).
assert.match(face, /tileTransparent\(tile\) \? \(ui_theme::theme_light\(\) \? ui_theme::screen\(\) : ui_theme::card\(\)\) : card_color;/);
assert.match(read('src/types/clock/admin.js'), /tileElem\.classList\.contains\('tile-transparent'\)\s*\? \(panelLight\(\) \? panelRole\('screen'\) : panelRole\('card'\)\)/);
assert.match(face, /config\.text_shadow = false;/);
// The face and its lines are one block in a flex row (beside) or column
// (below), aligned by the tile's alignment (user 2026-10-12: the lines stood
// far right and the alignment did nothing); LVGL keeps the block aligned
// when a line's width changes (b397: the time arrives after the start).
assert.match(face, /lv_obj_set_flex_flow\(block, l\.beside \? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN\);/);
assert.match(face, /clock_face::create\(block, 0, 0, l\.size, colors, marks, compact\);/);
assert.match(face, /create_clock_widget\(block, config\);/);
assert.match(face, /lv_obj_set_style_pad_column\(block, l\.text_x - l\.x - l\.size, 0\);/);
assert.match(face, /align == 1   \? LV_ALIGN_LEFT_MID\s*: align == 2 \? LV_ALIGN_RIGHT_MID\s*: LV_ALIGN_CENTER,/);
assert.match(face, /align == 1   \? LV_ALIGN_TOP_LEFT\s*: align == 2 \? LV_ALIGN_TOP_RIGHT\s*: LV_ALIGN_TOP_MID,/);
assert.doesNotMatch(face, /lv_obj_set_pos\(block/);
// Stacked lines of one alignment line up by it (the rolling time stood
// centered over the date beside the face).
assert.match(fn('create_clock_widget'), /const uint8_t line_align = !config\.horizontal && config\.time_alignment == config\.date_alignment/);
const header = read('src/types/clock/clock_face.h');
const css = read('src/web/assets/admin.css');
const admin = read('src/types/clock/admin.js');
const hex = list => list.map(v => '#' + v.toString(16).toUpperCase().padStart(6, '0'));
const dials = /kDialColors\[kDialColorCount\] = \{([^}]*)\}/.exec(header)[1].split(',').map(v => parseInt(v, 16));
const hands = /kHandColors\[kHandColorCount\] = \{([^}]*)\}/.exec(header)[1].split(',').map(v => parseInt(v, 16));
assert.equal(admin.match(/const CLOCK_DIAL_COLORS = \[([^\]]*)\]/)[1], hex(dials).map(v => `'${v}'`).join(', '), 'preview dial colors');
assert.equal(admin.match(/const CLOCK_HAND_COLORS = \[([^\]]*)\]/)[1], hex(hands).map(v => `'${v}'`).join(', '), 'preview hand colors');
assert.ok(css.includes('.tile.clock .tile-clock-face { position:absolute; overflow:visible; }'));
const html = read('src/types/clock/web_html.cpp');
for (const id of ['_clock_style"', '_clock_dial"', '_clock_hands"', '_clock_wall_colors"']) assert.ok(html.includes(id), id);
// The selects' labels come from the central translations, in the order of
// the color tables.
assert.match(html, /\{tr\.clock_color_slate, tr\.clock_color_black, tr\.clock_color_white,\s+tr\.clock_color_sage, tr\.clock_color_navy, tr\.clock_color_terracotta\}/);
assert.ok(html.includes('{tr.clock_color_gold, tr.clock_color_silver, tr.clock_color_black, tr.clock_color_white}'));
for (const key of ['clock_style_label', 'clock_style_digital', 'clock_style_analog', 'clock_style_wall', 'clock_dial_label', 'clock_hands_label']) {
  assert.ok(html.includes('tr.' + key), key);
}
// The time and date sizes only with the digital clock: the faces and the big
// date size their lines by the tile (user 2026-10-12: confusing).
assert.match(admin, /for \(const id of \['_clock_time_font', '_clock_date_font'\]\) \{[\s\S]*?select\.classList\.toggle\('hidden', style !== '0'\);\s*select\.previousElementSibling\?\.classList\.toggle\('hidden', style !== '0'\);/);
const editor = read('src/web/admin/tiles/editor.js');
for (const id of ['_clock_style', '_clock_dial', '_clock_hands']) assert.ok(editor.includes(`target.id === (prefix + '${id}')`), id);

// Layout parity: the panel's function on the host against the preview's,
// with the measures of a 1280x800 and of a 480x480 panel.
const layoutCode = renderer.slice(renderer.indexOf('struct ClockFaceLayout {'),
  renderer.indexOf("// The panel's measures for clock_face_layout"));
assert.ok(layoutCode.includes('static ClockFaceLayout clock_face_layout('));
const lvglShim = `#pragma once
#include <stdint.h>
typedef int32_t lv_coord_t;
struct lv_obj_t;
struct lv_font_t { int32_t line_height; };
#define LV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LV_MAX(a, b) ((a) > (b) ? (a) : (b))
`;
const FONTS = [
  {timeW: [254, 181, 146, 109, 91], timeH: [68, 49, 39, 29, 24], date20H: 24, date24H: 29},
  {timeW: [181, 127, 91, 73, 64], timeH: [49, 34, 24, 20, 17], date20H: 17, date24H: 20},
];
const sizes = [];
for (const [font, top] of [[0, 0], [0, 67], [1, 0], [1, 44]]) {
  for (let w = 120; w <= 900; w += 47) {
    for (let h = 60; h <= 640; h += 37) {
      if (h <= top + 40) continue;
      for (const compact of [0, 1]) {
        for (const lines of [0, 1, 2, 3]) sizes.push({w, h, top, compact, time: lines & 1, date: (lines >> 1) & 1, font});
      }
    }
  }
}
const harness = String.raw`
#include <cstdio>
#include "src/types/clock/clock_face.h"
namespace tile_layout { inline lv_coord_t scale(int v) { return v; } }
${layoutCode}
static const ClockFaceFonts kFonts[] = {${FONTS.map(f => `{{${f.timeW.join(', ')}}, {${f.timeH.join(', ')}}, ${f.date20H}, ${f.date24H}}`).join(', ')}};
struct Case { int w, h, top, compact, time, date, font; };
static const Case kCases[] = {${sizes.map(c => `{${c.w}, ${c.h}, ${c.top}, ${c.compact}, ${c.time}, ${c.date}, ${c.font}}`).join(', ')}};
int main() {
  for (const Case& c : kCases) {
    const ClockFaceLayout l =
        clock_face_layout(c.w, c.h, c.top, c.compact != 0, c.time != 0, c.date != 0, kFonts[c.font]);
    std::printf("%d %d %d %d %d %d %d %d %d %d %d %d\n", (int)l.x, (int)l.y, (int)l.size, l.show_time ? 1 : 0,
                l.show_date ? 1 : 0, l.beside ? 1 : 0, (int)l.text_x, (int)l.text_y, (int)l.text_w, (int)l.text_h,
                l.time_size, l.date_size);
  }
  return 0;
}
`;
const output = compileAndRun({label: 'Clock face layout', harness, files: {'lvgl.h': lvglShim}});
if (output !== null) {
  const panel = output.trim().split('\n').map(line => line.trim().split(' ').map(Number));
  const ctx = {};
  vm.createContext(ctx);
  const sizesConst = /const CLOCK_FACE_TIME_SIZES = \[[^\]]*\];/.exec(admin)[0];
  assert.equal(sizesConst, `const CLOCK_FACE_TIME_SIZES = [${/kClockFaceTimeSizes\[\] = \{([^}]*)\}/.exec(renderer)[1]}];`,
    'the same time sizes');
  vm.runInContext(sizesConst.replace('const ', 'var ') + extractDeliveredFunction('clockFaceTimeFit') +
    extractDeliveredFunction('clockFaceLayout'), ctx);
  const margin = Number(/kTileMargin = (\d+);/.exec(header)[1]);
  const compactMargin = Number(/kCompactMargin = (\d+);/.exec(header)[1]);
  const gap = Number(/kCompactGap = (\d+);/.exec(header)[1]);
  sizes.forEach((c, i) => {
    const l = vm.runInContext(`clockFaceLayout(${c.w}, ${c.h}, ${c.top}, ${!!c.compact}, ${!!c.time}, ${!!c.date}, ${JSON.stringify(FONTS[c.font])}, ${c.compact ? compactMargin : margin}, ${gap})`, ctx);
    const preview = [l.x, l.y, l.size, l.showTime ? 1 : 0, l.showDate ? 1 : 0, l.beside ? 1 : 0,
      l.textX || 0, l.textY || 0, l.textW || 0, l.textH || 0, l.timeSize || 0, l.dateSize || 0];
    assert.deepEqual(preview, panel[i], `${c.w}x${c.h} top ${c.top} compact ${c.compact} time ${c.time} date ${c.date} font ${c.font}`);
  });
  // A few shapes as designed (1280x800 measures): a wide tile puts the
  // lines right of the face, a square one shows the date below a smaller
  // face, a narrow half tile only the time, a 1x1 tile with a header the
  // face alone (b395 first draft: the time ran out of the card).
  const at = (w, h, top, compact, time, date) => panel[sizes.findIndex(c => c.font === 0 &&
    c.w === w && c.h === h && c.top === top && c.compact === compact && c.time === time && c.date === date)];
  const wide = at(449, 171, 0, 0, 1, 1);
  assert.deepEqual(wide.slice(0, 6), [16, 16, 139, 1, 1, 1], 'wide: face left, lines beside');
  assert.equal(wide[10], 56, 'the time at 56 where its sample fits beside the face');
  assert.equal(at(402, 171, 0, 0, 1, 1)[10], 40, 'a narrower tile: 40');
  const square = at(214, 208, 0, 0, 1, 1);
  assert.deepEqual(square.slice(3, 6), [0, 1, 0], 'square: the face shows the time, the date below');
  const half = at(214, 97, 0, 1, 1, 1);
  assert.deepEqual(half.slice(3, 5), [1, 0], 'a narrow half tile: the time alone beside the face');
  const headed = at(167, 171, 67, 0, 1, 1);
  assert.deepEqual(headed.slice(3, 6), [0, 0, 0], 'a small tile with a header: the face alone');
}

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the clock face render test needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/clock-face');
fs.mkdirSync(path.join(out, 'shim'), {recursive: true});
// The face reads the local time through Arduino's getLocalTime.
fs.writeFileSync(path.join(out, 'shim/Arduino.h'), `#pragma once
#include <stdint.h>
#include <time.h>
extern int g_hour, g_minute;
inline bool getLocalTime(struct tm* info, uint32_t = 5000) {
  *info = tm{};
  info->tm_hour = g_hour;
  info->tm_min = g_minute;
  return true;
}
`);
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <Arduino.h>
#include "src/types/clock/clock_face.h"

int g_hour = 10, g_minute = 10;
static const int W = 240;

static int timers() {
  int n = 0;
  for (lv_timer_t* t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t)) ++n;
  return n;
}

// The color at a point given in degrees from 12 and a share of the radius
// of a face of 200 px at (20, 20).
static uint32_t at(lv_draw_buf_t* shot, float degrees, float share) {
  const float a = degrees * 3.14159265f / 180.0f;
  const int x = 20 + static_cast<int>(lroundf(100 + share * 100 * sinf(a)));
  const int y = 20 + static_cast<int>(lroundf(100 - share * 100 * cosf(a)));
  const uint8_t* px = shot->data + y * shot->header.stride + x * 4;
  return (px[2] << 16) | (px[1] << 8) | px[0];
}

static lv_draw_buf_t* shoot(lv_obj_t* screen) {
  lv_obj_invalidate(screen);
  lv_refr_now(nullptr);
  return lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, W);
  static uint32_t buffer[W * W];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  clock_face::Colors analog{0x333333, 0xFFFFFF, 0x888888, 0xEEEEEE, 0xDDDDDD, 0xCCCCCC};
  const int before = timers();
  lv_obj_t* face = clock_face::create(screen, 20, 20, 200, analog, clock_face::Marks::kDots, false);
  std::printf("timers %d %d\n", before, timers());
  std::printf("clickable %d\n", lv_obj_has_flag(face, LV_OBJ_FLAG_CLICKABLE) ? 1 : 0);
  lv_draw_buf_t* shot = shoot(screen);
  // 10:10: the hour hand at 305 degrees, the minute hand at 60.
  std::printf("analog %06X %06X %06X %06X %06X %06X %06X %06X\n", at(shot, 0, 0), at(shot, 305, 0.3f),
              at(shot, 60, 0.5f), at(shot, 0, 0.8f), at(shot, 30, 0.8f), at(shot, 135, 0.5f), at(shot, 45, 1.2f),
              at(shot, 60, 0.75f));
  lv_draw_buf_destroy(shot);
  // The next minute that moves the hands: 10:25, the minute hand at 150.
  g_minute = 25;
  lv_tick_inc(1000);
  lv_timer_handler();
  shot = shoot(screen);
  std::printf("moved %06X %06X\n", at(shot, 60, 0.5f), at(shot, 150, 0.5f));
  lv_draw_buf_destroy(shot);
  lv_obj_delete(face);
  std::printf("freed %d\n", timers());

  // The wall clock: batons, small keeps the dots' quarters only.
  clock_face::Colors wall{0x5E6C82, 0xC9A45C, 0xC9A45C, 0xC9A45C, 0xC9A45C, 0xC9A45C};
  g_minute = 10;
  face = clock_face::create(screen, 20, 20, 200, wall, clock_face::Marks::kBatons, false);
  shot = shoot(screen);
  std::printf("wall %06X %06X %06X\n", at(shot, 90, 0.77f), at(shot, 75, 0.77f), at(shot, 0, 0));
  lv_draw_buf_destroy(shot);
  lv_obj_delete(face);
  face = clock_face::create(screen, 20, 20, 200, analog, clock_face::Marks::kDots, true);
  shot = shoot(screen);
  std::printf("small %06X %06X\n", at(shot, 90, 0.8f), at(shot, 30, 0.8f));
  lv_draw_buf_destroy(shot);
  lv_obj_delete(face);
  return 0;
}
`);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let run = spawnSync(host.cxx, ['-I', path.join(out, 'shim'), ...host.flags, '-std=c++17', '-Wno-deprecated-declarations', source,
  path.join(root, 'src/types/clock/clock_face.cpp'), path.join(root, 'src/ui/shared/rounded_corners.cpp'), path.join(root, 'src/ui/shared/kept_shadow.cpp'), host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const result = Object.fromEntries(run.stdout.trim().split('\n').map(l => l.trim().split(' ')).map(([k, ...v]) => [k, v]));
const [before, during] = result.timers.map(Number);
assert.equal(during, before + 1, 'one timer per face');
assert.equal(Number(result.freed[0]), before, 'the timer goes with the face');
assert.equal(result.clickable[0], '0', 'a tap reaches the tile');
const [center, hourHand, minuteHand, twelve, one, empty, outside, minuteTip] = result.analog;
assert.equal(center, 'CCCCCC', 'the center cap');
assert.equal(hourHand, 'EEEEEE', 'the hour hand at 10:10');
assert.equal(minuteHand, 'DDDDDD', 'the minute hand at 10:10');
assert.equal(twelve, 'FFFFFF', 'a quarter dot in the major color');
assert.equal(one, '888888', 'the other dots in the minor color');
assert.equal(empty, '333333', 'the dial');
assert.equal(outside, '101010', 'round: the screen outside');
assert.equal(minuteTip, '333333', 'the minute hand ends at two thirds of the radius');
assert.deepEqual(result.moved, ['333333', 'DDDDDD'], 'the minute hand follows the time');
assert.deepEqual(result.wall, ['C9A45C', '5E6C82', 'C9A45C'], 'batons at the hours, the dial between');
assert.deepEqual(result.small, ['FFFFFF', '333333'], 'a small face keeps the quarters only');
console.log('Clock face: dial, marks and hands on LVGL, timer lifecycle, panel and preview layout alike');
