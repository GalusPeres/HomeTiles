// Values with a unit (user 2026-10-26: "so wie bei weather", everywhere): the
// unit a font step smaller after its number, as the weather forecast shows
// it (20 -> 16), after a thin gap, its top level with the digits. The rule
// (value_unit_rules.h) runs natively here; the panel's sensor and energy
// tiles and the power flow tile use it (value_unit.h), the Web Admin preview
// draws every unit with the same proportions (.tile-unit).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// The panel uses it: one place sets the sensor and energy tiles' values
// (also compact, graph and gauge), the power flow values and charge.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('value_unit::show(value_label, target[grid_index].unit_label, displayValue, unit_text);'));
assert.doesNotMatch(renderer, /combined \+= unit;/, 'no value and unit in one text any more');
const flow = read('src/types/energy_flow/renderer.cpp');
assert.ok(flow.includes('void show_power(lv_obj_t* label, lv_obj_t*& unit, bool known, float watts) {'));
const helper = read('src/ui/shared/value_unit.cpp');
for (const marker of [
  // The unit's own label as the value's child, the value keeping room for it.
  'unit_label = lv_label_create(value_label);', 'set_room(value_label, room);',
  // Centred, right and left aligned values place the pair as one.
  'x = (inner - value_w) / 2 + value_w + gap;', 'x = inner + gap;',
  // Too wide for its label: one text as before.
  "set_text(value_label, unit.length() ? (value + \" \" + unit).c_str() : value.c_str());",
  // The digits' tops meet.
  'lv_obj_set_pos(unit_label, x, unit_y(digit_top(font), digit_top(small)));',
]) assert.ok(helper.includes(marker), marker);
// Units take text fonts only (the larger ones serve numbers and clocks).
assert.match(helper, /const FontSize kTextFonts\[\] = \{[\s\S]*?\{&ui_font_32, 32\},\s*\};/);

// The Web Admin preview: 80 % after a thin gap, raised so the digits' tops
// meet (0.727 cap height x 20 % of the number = 0.18 of the unit's em).
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('.tile-unit { color:inherit; font-size:.8em; line-height:1; margin-left:.17em; vertical-align:.18em; }'));
assert.ok(!css.includes('.tile.sensor-compact .tile-unit { font-size:inherit; }'), 'compact tiles follow the rule too');

const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: value unit rule needs a native C++ compiler'); process.exit(0); }
const cpp = `
#include <cstdio>
#include "src/ui/shared/value_unit_rules.h"
using namespace value_unit;
int main() {
  const uint8_t panel[] = {16, 20, 24, 28, 32};
  const uint8_t small[] = {12, 14, 16, 20, 24, 28, 32};
  const uint8_t values[] = {16, 20, 24, 28, 32, 40, 48, 64};
  printf("{\\"panel\\":[");
  for (int i = 0; i < 8; ++i) printf("%s%d", i ? "," : "", unit_px(values[i], panel, 5));
  printf("],\\"small\\":[");
  for (int i = 0; i < 8; ++i) printf("%s%d", i ? "," : "", unit_px(values[i], small, 7));
  printf("],\\"gaps\\":[%d,%d,%d,%d],\\"y\\":%d,\\"none\\":%d}\\n", gap_px(12), gap_px(16), gap_px(20), gap_px(24),
         unit_y(11, 7), unit_px(20, nullptr, 0));
}`;
const out = path.join(root, 'build/tests/value-unit');
fs.mkdirSync(out, {recursive: true});
const cppPath = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cppPath, cpp);
let r = spawnSync(compiler, ['-std=c++17', '-I', root, cppPath, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
const got = JSON.parse(r.stdout);
// 16 20 24 28 32 40 48 64 -> the forecast's 20 -> 16, then a step down each,
// never larger than the text fonts' 32, never the number's own size unless
// nothing smaller exists.
assert.deepEqual(got.panel, [16, 16, 16, 20, 24, 32, 32, 32]);
assert.deepEqual(got.small, [12, 16, 16, 20, 24, 32, 32, 32]);
assert.deepEqual(got.gaps, [2, 3, 3, 4], 'a thin gap, about a sixth of the unit');
assert.equal(got.y, 4, 'the unit line starts where the digits meet');
assert.equal(got.none, 0);
console.log('Value units: a step smaller after the number like the weather forecast, on the panel and in the preview');
