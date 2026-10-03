// The Weather popup's summary row capped the condition at 250 px (scaled):
// French "Partiellement nuageux" wrapped into two lines with half the row
// free (V2, 2026-10-03). The condition now takes the room the row leaves
// beside the separator and the temperature, on one line, and steps down to
// the unit font only when even that room is too narrow.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const popup = read('src/ui/popups/weather/weather_popup.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(popup).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const fit = fn('fit_condition_label');
assert.match(fit, /const int room = popup_layout::kContentWidth - 2 \* gap - sep\.x - value\.x;/);
assert.match(fit, /size\.x <= room \? FONT_VALUE : FONT_UNIT/);
assert.match(fit, /lv_obj_set_style_max_width\(ctx->condition_label, room > 0 \? room : 0, 0\);/);
assert.match(popup, /lv_obj_set_style_pad_gap\(value_row, popup_layout::scale\(14\), 0\);/, 'the gap the room subtracts');
assert.doesNotMatch(popup, /scale\(250\)/, 'no fixed cap left');
assert.match(fn('apply_weather_header'),
  /fit_condition_label\(ctx, condition_text\.c_str\(\), temp_text\.c_str\(\)\);\s*lv_label_set_text\(ctx->condition_label, condition_text\.c_str\(\)\);/);

// Every translated condition, per language: the list holding "partlycloudy".
const i18n = read('src/core/i18n/i18n.cpp');
const partly = {De: 'Teilw. bewölkt', En: 'Partly cloudy', Fr: 'Partiellement nuageux', Pl: 'Częściowo pochmurno'};
const conditions = [];
for (const [code, key] of Object.entries(partly)) {
  const head = `static const LocaleProfile kLocale${code} = {`;
  const start = i18n.indexOf(head);
  assert.ok(start >= 0, head);
  const block = i18n.slice(start, i18n.indexOf('\nstatic const ', start + head.length));
  const list = [...block.matchAll(/\{([^{}]*)\}/g)]
    .map(m => [...m[1].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(s => s[1]))
    .find(items => items.length === 15 && items[7] === key);
  assert.ok(list, `${code} weather conditions`);
  conditions.push(...list);
}

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the Weather condition width check needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/weather-condition-one-line');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
// Content width, row gap and value font per popup layout (popup_layout.h,
// tile_renderer_fonts.h): 1280x800 752/14/28, 720x720 and 1280x720 672/14/28,
// 1024x600 558/12/24, 480 layouts 448/9/20. A long temperature.
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstdio>
extern "C" { LV_FONT_DECLARE(ui_font_20) LV_FONT_DECLARE(ui_font_24) LV_FONT_DECLARE(ui_font_28) }
static int width(const char* text, const lv_font_t* font) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
static const char* kConditions[] = {${conditions.map(c => JSON.stringify(c)).join(', ')}};
int main() {
  lv_init();
  struct Layout { int content, gap; const lv_font_t* font; } layouts[] = {
      {752, 14, &ui_font_28}, {672, 14, &ui_font_28}, {558, 12, &ui_font_24}, {448, 9, &ui_font_20}};
  for (const Layout& l : layouts) {
    const int room = l.content - 2 * l.gap - width("|", l.font) - width("-10,8 °C", l.font);
    for (const char* text : kConditions) {
      if (width(text, l.font) > room) { printf("too wide: %s in %d\n", text, room); return 1; }
    }
  }
  assert(width("Partiellement nuageux", &ui_font_28) > 250 && "the V2 report: wider than the old cap");
  return 0;
}
`);
const binary = path.join(out, 'condition' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-w', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
console.log(`Weather condition: all ${conditions.length} translations fit one line at the value font on every popup layout`);
