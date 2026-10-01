// Number, Select and Date/Time (user 2026-10-01): half height like a compact
// Sensor (icon, title, value), with the Sensor compact value sizes. Their
// value stays on one line and ends in dots when it is too long, instead of
// wrapping into the title (a long Select option on the device).
import assert from 'node:assert/strict';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Firmware geometry and rendering.
const geometry = read('src/tiles/config/tile_geometry.h');
assert.match(geometry, /inline bool editable\(int type\) \{ return type == TILE_NUMBER \|\| type == TILE_SELECT \|\| type == TILE_DATETIME; \}/);
assert.match(geometry, /inline bool compact_editable\(int type, float w, float h\) \{\s*return editable\(type\) && w >= 1 && h == 0\.5f;/);
assert.match(read('src/types/sensor/renderer.cpp'),
  /if \(\(tile_geometry::compact\(tile\.type, tile\.span_w, tile\.span_h\) \|\|\s*tile_geometry::compact_editable\(tile\.type, tile\.span_w, tile\.span_h\)\) &&\s*display_mode == 0\) \{\s*compact_sensor_layout::apply\(card, icon_lbl, title_label, v, tile\);/);
for (const kind of ['number', 'select', 'datetime']) {
  assert.ok(read(`src/types/${kind}/renderer.cpp`).includes(
    'lv_obj_t* card = render_sensor_tile(parent, col, row, editable_display_tile(tile), index, grid);'), kind);
}
const control = read('src/types/value/value_control.cpp');
assert.ok(control.includes(
  'display.sensor_value_font = tile.sensor_value_font == 1 ? 0 : tile.sensor_value_font == 2 ? 2 : 5;'),
  'half height: 20 and 24 stay, 28, 32 and 40 become the compact 28');
assert.match(control,
  /lv_label_set_long_mode\(label, LV_LABEL_LONG_DOT\);\s*lv_obj_set_height\(label, lv_font_get_line_height\(lv_obj_get_style_text_font\(label, LV_PART_MAIN\)\)\);\s*lv_label_set_text\(label, display\.c_str\(\)\);/,
  'one value line with dots');
assert.ok(read('src/web/server/render/web_admin_html.cpp').includes('tile_geometry::compact_editable(tile.type, span_w, span_h)'));
assert.ok(read('src/web/assets/admin.css').includes(
  '.tile-value.tile-editable-value { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }'));

// Editor: half-size types, compact classes and value sizes.
const names = ['isCompactSensorType', 'isEditableValueType', 'supportsHalfSize', 'supportedTileLayout', 'compactValueSize',
  'editableCompactValueFont', 'syncEditableValueFontOptions', 'applyCompactSensorPreview'];
const api = new Function(`${names.map(extractDeliveredFunction).join('\n')}; return {${names.join(', ')}};`)();
for (const type of [21, 22, 23]) {
  assert.ok(api.supportsHalfSize(type) && api.supportedTileLayout(type, {col: 0, row: 0.5, span_w: 1, span_h: 0.5}), `type ${type} 1x0.5`);
}
const tile = () => {
  const classes = new Set();
  return {classes, classList: {toggle: (name, on) => (on ? classes.add(name) : classes.delete(name)), contains: name => classes.has(name)},
    style: {setProperty() {}, removeProperty() {}}};
};
for (const [choice, size] of [['1', 20], ['2', 24], ['0', 28], ['3', 28], ['4', 28]]) {
  const el = tile();
  api.applyCompactSensorPreview(el, '22', {span_w: 2, span_h: 0.5}, 0, choice);
  assert.ok(el.classes.has('sensor-compact') && el.classes.has('sensor-half'), 'compact Select');
  assert.equal(el.classes.has('compact-value-24') ? 24 : el.classes.has('compact-value-28') ? 28 : 20, size, `choice ${choice}`);
}
const full = tile();
api.applyCompactSensorPreview(full, '21', {span_w: 1, span_h: 1}, 0, '2');
assert.ok(!full.classes.has('sensor-compact'), 'a full Number tile keeps its layout');
const options = ['1', '2', '0', '3', '4'].map(value => ({value, hidden: false, disabled: false}));
const select = {options, value: '4'};
api.syncEditableValueFontOptions(select, true);
assert.deepEqual(options.filter(o => !o.hidden).map(o => o.value), ['1', '2', '0']);
assert.equal(select.value, '0', '40 moves to 28 at half height');
api.syncEditableValueFontOptions(select, false);
assert.ok(options.every(o => !o.hidden && !o.disabled));
const live = read('src/web/admin/tiles/live-preview.js');
assert.ok(live.includes("syncEditableValueFontOptions(document.getElementById(prefix + '_' + kind + '_value_font'), halfHeight);"));
assert.match(live, /const sensorValueFont = isEditablePreview\(previewKind\)\s*\? \(document\.getElementById\(prefix \+ '_' \+ previewKind \+ '_value_font'\)\?\.value \?\? '2'\)/);
console.log('Number, Select and Date/Time: half height like a compact Sensor, one value line with dots');
