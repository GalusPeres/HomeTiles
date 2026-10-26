// Values with a unit (user 2026-10-26: "so wie bei weather", everywhere,
// "in den Popups musst du es ja auch"): the unit a font step smaller after
// its number, as the weather forecast shows it (20 -> 16), after a thin gap,
// its top level with the digits. The rule (value_unit_rules.h) runs natively
// here; tiles, tile header state lines and popups use it (value_unit.h), the
// Web Admin preview draws every unit with the same proportions (.tile-unit)
// and splits finished texts the same way (unitTextHtml).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const has = (file, ...markers) => {
  const text = read(file);
  for (const marker of markers) assert.ok(text.includes(marker), `${file}: ${marker}`);
};

// The shared helper: the unit's own label as the value's child, the value
// keeping room for it, centred, right and left aligned values placing the
// pair as one, a pair too wide for its label one text as before, the
// digits' tops meeting.
has('src/ui/shared/value_unit.cpp',
  'unit_label = lv_label_create(value_label);', 'set_room(value_label, room_for(value_label, unit));',
  'if (align == LV_TEXT_ALIGN_CENTER) x = (inner - value_w) / 2 + value_w + gap;', 'else if (align == LV_TEXT_ALIGN_RIGHT) x = inner + gap;',
  'const std::string text = std::string(value) + " " + unit;',
  // Floating: a content-sized label counts its text and the room only.
  'lv_obj_add_flag(unit_label, LV_OBJ_FLAG_FLOATING);',
  'lv_obj_set_pos(unit_label, x, unit_y(digit_top(font), digit_top(small)));',
  // Found again by its tag (the LVGL user flags are taken: USER_1 marks the
  // camera tile's picture).
  'lv_obj_set_user_data(unit_label, const_cast<char*>(&kUnitTag));');
// Units take text fonts only (the larger ones serve numbers and clocks).
assert.match(read('src/ui/shared/value_unit.cpp'), /const FontSize kTextFonts\[\] = \{[\s\S]*?\{&ui_font_32, 32\},\s*\};/);

// Tiles: one place sets the sensor and energy tiles' values (also compact,
// graph and gauge); the power flow values; every tile header state line
// (switch, cover, climate, fan: "Open · 58 %"); the climate, number and
// weather tiles.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('value_unit::show(value_label, target[grid_index].unit_label, displayValue, unit_text);'));
assert.doesNotMatch(renderer, /combined \+= unit;/, 'no value and unit in one text any more');
assert.ok(renderer.includes('if (own_unit) value_unit::show_text(widgets.temp_label, temp_text);'));
has('src/types/energy_flow/renderer.cpp', 'void show_power(lv_obj_t* label, lv_obj_t*& unit, bool known, float watts) {');
has('src/tiles/runtime/tile_header.h', 'value_unit::show_text(label, text);');
has('src/types/climate/renderer.cpp', 'value_unit::show_text(value, climate_slot_text(kind, state));');
has('src/types/value/value_control.cpp',
  'value_unit::show(label, widgets[index].unit_label, editable_display_number(value), editable_display_unit(value));');

// Popups: the header value (its ellipsis leaves the unit's room), every
// write to the sensor popup's holder through one place (a reused popup
// keeps no old unit), the graph readouts (static text, the unit apart), the
// climate target, the light's percent and Kelvin, the cover's position.
has('src/ui/popups/popup_shell.cpp', 'value_unit::reserve(shell.value, shell.value_unit, unit);',
  'value_unit::place(shell.value, shell.value_unit);');
has('src/ui/popups/sensor/sensor_popup.cpp', 'value_unit::show(ctx->value_label, ctx->value_unit_label, value, shown_unit);',
  'show_header_value(ctx, editable_display_number(value), editable_display_unit(value));',
  'value_unit::reserve(ctx->readout_value_label, ctx->readout_unit_label, isfinite(value) ? ctx->unit.c_str() : "");',
  'value_unit::reserve(ctx->readout_value_label, ctx->readout_unit_label, "");');
// One count of decimals in the sensor popup (user 2026-10-26: header 0,
// readout 2, axis 1): readout and axis take popup_digits, live values keep
// the decimals the popup opened with; the axis shows its unit the same way
// and counts its room.
has('src/ui/popups/sensor/sensor_popup.cpp', 'static uint8_t popup_digits(const SensorPopupContext* ctx, float value) {',
  'const uint8_t digits = popup_digits(ctx, value);',
  'value_unit::show(ctx->y_max_label, i18n::format_number(language, max_v, popup_digits(ctx, max_v)), ctx->unit);',
  // The unit's room (the label's right padding) counts once.
  '  return size.x + lv_obj_get_style_pad_left(label, LV_PART_MAIN) +\n         lv_obj_get_style_pad_right(label, LV_PART_MAIN);',
  'value_unit::place(ctx->y_max_label);');
assert.ok(!read('src/ui/popups/sensor/sensor_popup.cpp').includes('g_sensor_popup_ctx->decimals = g_pending_value.decimals;'),
  'a live value of another tile does not change the decimals');
assert.doesNotMatch(read('src/ui/popups/sensor/sensor_popup.cpp'), /lv_label_set_text\(\s*ctx->value_label,\s*i18n/,
  'no direct writes to the header holder');
has('src/ui/popups/energy/energy_popup.cpp',
  'value_unit::show(ctx->value_label, ctx->value_unit_label, number, number != "--" ? unit : String());',
  'value_unit::place(ctx->readout_value_label, ctx->readout_unit_label);');
has('src/ui/popups/climate/climate_popup.cpp', 'value_unit::show(ctx->target_label, ctx->target_unit_label, target, target_unit);');
has('src/ui/popups/light/light_popup.cpp', 'update_value_label(ctx->top_value_label, ctx->top_value_unit, ctx->val, "%");',
  'update_value_label(ctx->temp_value_label, ctx->temp_value_unit, static_cast<int>(ctx->color_temp_kelvin), "K");');
has('src/ui/popups/cover/cover_popup.cpp', 'value_unit::show(ctx->top_value_label, ctx->top_value_unit, text, unit);');

// The Web Admin preview: 80 % after a thin gap, raised so the digits' tops
// meet (0.727 cap height x 20 % of the number = 0.18 of the unit's em).
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('.tile-unit { color:inherit; font-size:.8em; line-height:1; margin-left:.17em; vertical-align:.18em; }'));
assert.ok(!css.includes('.tile.sensor-compact .tile-unit { font-size:inherit; }'), 'compact tiles follow the rule too');
has('src/types/cover/admin.js', 'unitTextHtml(coverPreviewStateLine(state))');
has('src/types/device/admin.js', 'unitTextHtml(devicePreviewVisual(kind, d).label)');
has('src/types/switch/admin.js', 'const html = unitTextHtml(text);', 'else label.innerHTML = html;');
has('src/web/admin/tiles/live-preview.js', 'unitTextHtml(climatePreviewHeaderText(climatePreviewState))', 'editablePreviewHtml(iconEntity, previewKind)');
has('src/web/admin/tiles/grid-preview.js', 'unitTextHtml(climatePreviewHeaderText(climatePreviewState))', 'editablePreviewHtml(iconEntity, previewKind, sensorMeta)');
// The weather row takes the panel's exact step (test-weather-preview-device
// compares its pixels): the panel's unit sizes reach the page.
has('src/types/weather/admin.js', `'<span class="tile-unit" style="' + valueUnitStyle(L.value.px) +`);
has('src/types/sensor/web_scripts.cpp', 'const uint8_t* sizes = value_unit::unit_font_sizes(count);',
  'const VALUE_UNIT_FONTS = Object.freeze([');
const valueUnitStyle = new Function(extractDeliveredFunction('valueUnitStyle') + '\nreturn valueUnitStyle;')();
assert.equal(valueUnitStyle(28), 'font-size:0.7143em;margin-left:0.1500em;vertical-align:0.2908em', '28 -> 20 like the panel');
assert.equal(valueUnitStyle(20), 'font-size:0.8000em;margin-left:0.1875em;vertical-align:0.1817em', '20 -> 16 like the forecast');
has('src/types/energy_flow/admin.js', `(parts[1] ? '<span class="tile-unit">' + escapeHtml(parts[1]) + '</span>' : '')`);
// The preview splits finished texts like the panel (the same cases below).
const escapeHtml = text => String(text).replace(/[&<>"]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'})[c]);
const unitTextHtml = new Function('escapeHtml', extractDeliveredFunction('unitTextHtml') + '\nreturn unitTextHtml;')(escapeHtml);
const cases = [['21,5 °C', '21,5', '°C'], ['Open · 58 %', 'Open · 58', '%'], ['75%', '75', '%'], ['-- °C'], ['Off'],
  ['Speed 2'], ['a\n5 W'], ['20.5  °C']];
for (const [text, number, unit] of cases) {
  assert.equal(unitTextHtml(text), number ? escapeHtml(number) + '<span class="tile-unit">' + escapeHtml(unit) + '</span>'
    : escapeHtml(text), JSON.stringify(text));
}

const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: value unit rule needs a native C++ compiler'); process.exit(0); }
const texts = cases.map(([text]) => '"' + [...Buffer.from(text, 'utf8')].map(b => '\\x' + b.toString(16).padStart(2, '0')).join('') + '"');
const cpp = `
#include <cstdio>
#include <cstring>
#include "src/ui/shared/value_unit_rules.h"
using namespace value_unit;
int main() {
  const uint8_t panel[] = {16, 20, 24, 28, 32};
  const uint8_t small[] = {12, 14, 16, 20, 24, 28, 32};
  const uint8_t values[] = {16, 20, 24, 28, 32, 40, 48, 64};
  const char* texts[] = {${texts.join(', ')}};
  printf("{\\"panel\\":[");
  for (int i = 0; i < 8; ++i) printf("%s%d", i ? "," : "", unit_px(values[i], panel, 5));
  printf("],\\"small\\":[");
  for (int i = 0; i < 8; ++i) printf("%s%d", i ? "," : "", unit_px(values[i], small, 7));
  printf("],\\"gaps\\":[%d,%d,%d,%d],\\"y\\":%d,\\"none\\":%d,\\"split\\":[", gap_px(12), gap_px(16), gap_px(20),
         gap_px(24), unit_y(11, 7), unit_px(20, nullptr, 0));
  for (int i = 0; i < ${cases.length}; ++i) {
    int end = 0;
    const int start = split_unit(texts[i], static_cast<int>(strlen(texts[i])), end);
    printf("%s[%d,%d]", i ? "," : "", start, start < 0 ? -1 : end);
  }
  printf("]}\\n");
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
// The panel splits the same texts as the preview: byte positions of the
// unit and of the number's end ("Open · 58 %": the middle dot takes 2 bytes).
const bytes = text => Buffer.byteLength(text, 'utf8');
cases.forEach(([text, number, unit], i) => {
  assert.deepEqual(got.split[i], number ? [bytes(text) - bytes(unit), bytes(number)] : [-1, -1], JSON.stringify(text));
});
console.log('Value units: a step smaller after the number like the weather forecast, in tiles, popups and the preview');
