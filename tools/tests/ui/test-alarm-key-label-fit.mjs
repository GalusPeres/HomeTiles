// The Alarm popup's mode keys drew every label at the 28 px key font. French
// "Personnalisé" and Polish "Poza domem" filled the 7-inch key to the edge and
// left a few pixels on the 480 layouts (user, 2026-10-03). A label now keeps
// one line and its full word and steps down one font size, at most two, while
// less than 12 px (scaled) stays free on either side; the V2 keeps 28 px.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const popup = read('src/ui/popups/device/device_popup.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(popup).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const fit = fn('alarm_key_font');
assert.match(fit, /\{popup_layout::font28\(\), popup_layout::font24\(\), popup_layout::font20\(\)\}/,
  'one or two steps down from the key font');
assert.match(fit, /const int room = key_w - 2 \* popup_layout::scale\(12\);/);
assert.match(fit, /return sizes\[2\];/, 'never cut: the smallest step stays');
assert.doesNotMatch(fit, /LONG_DOT|LONG_CLIP|LONG_WRAP/);
assert.match(fn('build_alarm'), /make_label\(key, fg, alarm_key_font\(label, wide \? block_w : kw\), label\);/);

// The reported labels are the ones the panel shows.
const i18n = read('src/core/i18n/i18n.cpp');
assert.ok(i18n.slice(i18n.indexOf('static const LocaleProfile kLocaleFr = {')).includes('"Personnalisé"'));
assert.ok(i18n.slice(i18n.indexOf('static const LocaleProfile kLocalePl = {')).includes('"Poza domem"'));

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the Alarm key font check needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/alarm-key-label-fit');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
// Mode key widths from pin_keypad::keypad_geometry: 480 layouts 139 px,
// 1024x600 149 px, V2 1280x800 199 px. Fonts per layout as popup_layout.h.
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
extern "C" {
LV_FONT_DECLARE(ui_font_14) LV_FONT_DECLARE(ui_font_16) LV_FONT_DECLARE(ui_font_20)
LV_FONT_DECLARE(ui_font_24) LV_FONT_DECLARE(ui_font_28)
}
static int width(const char* text, const lv_font_t* font) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
static const lv_font_t* pick(const char* text, int key_w, int side, const lv_font_t* const* sizes) {
  for (int i = 0; i < 3; ++i) if (width(text, sizes[i]) <= key_w - 2 * side) return sizes[i];
  return sizes[2];
}
int main() {
  lv_init();
  const lv_font_t* const s480[] = {&ui_font_20, &ui_font_16, &ui_font_14};
  const lv_font_t* const s1024[] = {&ui_font_24, &ui_font_20, &ui_font_16};
  const lv_font_t* const base[] = {&ui_font_28, &ui_font_24, &ui_font_20};
  for (const char* text : {"Personnalisé", "Poza domem"}) {
    assert(pick(text, 139, 8, s480) == &ui_font_16 && "480: one step down");
    assert(pick(text, 149, 10, s1024) == &ui_font_20 && "7-inch: one step down");
    assert(pick(text, 199, 12, base) == &ui_font_28 && "V2 keeps 28 px");
    assert(width(text, &ui_font_24) > 149 - 2 * 10 && "the 7-inch key really was too tight");
  }
  // Every other label keeps the key font everywhere.
  for (const char* text : {"Zuhause", "Abwesend", "Eigene", "Vacation", "Custom", "Domicile", "Vacances", "W domu", "Własny"}) {
    assert(pick(text, 139, 8, s480) == &ui_font_20);
    assert(pick(text, 149, 10, s1024) == &ui_font_24);
  }
  printf("%d %d\n", width("Personnalisé", &ui_font_24), width("Poza domem", &ui_font_24));
  return 0;
}
`);
const binary = path.join(out, 'keys' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-w', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
console.log('Alarm keys: long mode labels step down a font size on the 7-inch and 480 layouts, never cut; V2 keeps 28 px');
