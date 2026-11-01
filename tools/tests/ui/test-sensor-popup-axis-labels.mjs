// The Sensor popup graph's value labels (maximum, minimum, zero line) share
// one width and stand right-aligned in it: their units form one column at the
// graph and the widest label starts at the left inset. Before, all three were
// left-aligned and "0 W" stood far left under "13231 W" (user 2026-10-29).
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/ui/popups/sensor/sensor_popup.cpp').replace(/\r\n?/g, '\n');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

for (const label of ['y_max', 'y_min', 'y_zero']) {
  assert.ok(source.includes(`lv_obj_set_style_text_align(${label}, LV_TEXT_ALIGN_RIGHT, 0);`), `${label} right-aligned`);
  assert.ok(!source.includes(`lv_obj_set_style_text_align(${label}, LV_TEXT_ALIGN_LEFT, 0);`), `${label} not left-aligned`);
}

const layout = fn('update_y_axis_layout');
// The zero label's text counts for the common width, so it never gets
// narrower than its own text.
assert.ok(layout.includes('value_unit::show(ctx->y_zero_label, zero_label_text(ctx), ctx->unit);') &&
  layout.includes('zero_w = measure_label_text_width(ctx->y_zero_label);') &&
  layout.includes('lv_coord_t text_w = LV_MAX(LV_MAX(max_w, min_w), zero_w);'), 'zero label in the common width');
// All three get that width at the inset; the unit follows the right edge.
assert.ok(layout.includes('for (lv_obj_t* label : {ctx->y_max_label, ctx->y_min_label, ctx->y_zero_label}) {') &&
  layout.includes('lv_obj_set_width(label, label_w);') && layout.includes('lv_obj_set_x(label, kGraphInset);') &&
  layout.includes('value_unit::place(label);'), 'one width, one inset, unit placed');
// The zero label is placed after the width, so its row keeps that width.
assert.ok(layout.indexOf('place_zero_label(ctx);') > layout.indexOf('lv_obj_set_width(label, label_w);'));
const zero = fn('place_zero_label');
assert.ok(!zero.includes('lv_obj_set_width'), 'place_zero_label keeps the common width');

// value_unit places a right-aligned label's unit at the right edge of its
// text area, the same column for every label of one width.
const unit = readRepoFile('src/ui/shared/value_unit.cpp');
assert.ok(unit.includes('else if (align == LV_TEXT_ALIGN_RIGHT) x = inner + gap;'));

console.log('Sensor popup: axis labels right-aligned, units in one column');
