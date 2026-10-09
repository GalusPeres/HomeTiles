// Shrinking a Climate tile keeps the mini tiles that still fit, whatever
// their item number. Reported: a 2x1.5 tile with Current and Cool target on
// top and target humidity below kept only the humidity at 2x1, because only
// the first cells-many item numbers were considered. Captions are white and
// the humidity control reads "Humidity", not "Target humidity".
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const CONTENT = {AUTO: 0, EMPTY: 1, CURRENT_TEMPERATURE: 2, CURRENT_HUMIDITY: 3,
  TARGET_TEMPERATURE: 4, TARGET_TEMPERATURE_LOW: 5, TARGET_TEMPERATURE_HIGH: 6,
  TARGET_HUMIDITY: 7, HVAC_MODE: 8};
const context = vm.createContext({
  GRID_COLS: 7, GRID_ROWS: 5,
  CLIMATE_TILE_CONTENT: CONTENT,
  CLIMATE_TARGET_LAYOUT: {AUTO: 0, HORIZONTAL: 1, VERTICAL: 2},
  CLIMATE_SUPPORTED_FEATURE: {TARGET_TEMPERATURE: 1, TARGET_TEMPERATURE_RANGE: 2,
    TARGET_HUMIDITY: 4},
  CLIMATE_LAYOUT_MAGIC: 0x434c0000, CLIMATE_LAYOUT_MAGIC_MASK: 0xffff0000,
  CLIMATE_LAYOUT_VALUE_MASK: 0x00000fff, CLIMATE_GEOMETRY_PREFIX: 'CLG2:',
  CLIMATE_I18N: {heat: 'Heat', cool: 'Cool', targetHumidity: 'Target humidity',
    humidityCaption: 'Humidity', emptyField: 'Empty', genericClimate: 'Climate',
    targetTemperature: 'Target'},
  escapeHtml: value => String(value)
});
vm.runInContext([
  'climateMaxGridColumns', 'climateMaxOuterRows', 'climateGridDimensions',
  'climateSlotCapacity', 'clampClimateGeometryItem', 'sanitizeClimateGeometryItem',
  'defaultClimateGeometry', 'decodeClimateGeometry', 'encodeClimateGeometry',
  'decodeClimateSlotConfig', 'getClimateLayoutPayload', 'decodeClimateTargetLayouts',
  'climateGeometryOverlaps', 'climatePlacementOrderFor', 'climateTargetCaption',
  'climateModeText', 'climateFeatureSupported', 'climateTargetInteractive',
  'climatePreviewSlots'
].map(extractDeliveredFunction).join('\n'), context);

const state = {valid: true, available: true, unit: '°C', mode: 'cool', action: '',
  current: '20.5', currentHumidity: null, target: '23.5', targetLow: null,
  targetHigh: null, targetHumidity: '50', supportedFeatures: 1 | 4};
// Item 0 is the humidity below, items 2 and 3 are Current and target on top.
const slots = [CONTENT.TARGET_HUMIDITY, CONTENT.EMPTY, CONTENT.CURRENT_TEMPERATURE,
  CONTENT.TARGET_TEMPERATURE, CONTENT.EMPTY, CONTENT.EMPTY];
const geometry = context.encodeClimateGeometry([
  {col: 0, row: 1, spanW: 1, spanH: 1}, {col: 0, row: 0, spanW: 1, spanH: 1},
  {col: 0, row: 0, spanW: 1, spanH: 1}, {col: 1, row: 0, spanW: 1, spanH: 2},
  {col: 0, row: 0, spanW: 1, spanH: 1}, {col: 0, row: 0, spanW: 1, spanH: 1}]);
const placed = (spanW, spanH) => [...context.climatePreviewSlots(
  state, spanW, spanH, slots, [0, 0, 0, 0, 0, 0], geometry)
  .matchAll(/data-climate-preview-item="(\d)" style="grid-column:(\d) \/ span (\d);grid-row:(\d) \/ span (\d)/g)]
  .map(match => match.slice(1).map(Number)).sort((a, b) => a[0] - b[0]);

assert.deepEqual(placed(2, 1.5), [[0, 1, 1, 2, 1], [2, 1, 1, 1, 1], [3, 2, 1, 1, 2]],
  '2x1.5 shows all three where they were placed');
assert.deepEqual(placed(2, 1), [[2, 1, 1, 1, 1], [3, 2, 1, 1, 1]],
  '2x1 keeps Current and target on top and drops only the humidity below');
assert.deepEqual(placed(1, 1), [[2, 1, 1, 1, 1]], '1x1 keeps the top-left item');

// Without a stored mini-grid the item number stays the position.
assert.deepEqual([...context.climatePlacementOrderFor([], false)], [0, 1, 2, 3, 4, 5]);

// Captions: white on both sides, "Humidity" for the humidity control.
assert.equal(context.climateTargetCaption(state, CONTENT.TARGET_HUMIDITY), 'Humidity');
assert.equal(context.climateTargetCaption(state, CONTENT.TARGET_TEMPERATURE), 'Cool');
const css = readRepoFile('src/web/assets/admin.css');
assert.match(css, /\.tile\.climate \.climate-slot-control small \{[^}]*color:#fff;/);
const renderer = readRepoFile('src/types/climate/renderer.cpp').replace(/\r\n?/g, '\n');
assert.match(renderer, /lv_obj_t\* caption = lv_label_create\(root\);\n\s*set_label_style\(caption, lv_color_hex\(ui_theme::text\(\)\), FONT_TITLE\);/);
const fn = name => cppFunctionDefinitions(renderer).find(item => item.name === name)?.source || '';
assert.match(fn('climate_slot_caption'),
  /TARGET_HUMIDITY\) \{\s*return i18n::climate_humidity_caption_label\(language\);/);
const i18n = readRepoFile('src/core/i18n/i18n.cpp').replace(/\r\n?/g, '\n');
for (const [target, caption] of [['Soll-Luftfeuchtigkeit', 'Luftfeuchte'],
  ['Target humidity', 'Humidity'], ['Humidité cible', 'Humidité']]) {
  assert.ok(i18n.includes(`"${target}",\n    "${caption}",\n`), `${caption} follows ${target}`);
}
assert.match(readRepoFile('src/types/climate/web_scripts.cpp'),
  /"humidityCaption",\s*i18n::climate_humidity_caption_label\(language\)/);

// Device: the same rule as the preview.
const build = fn('build_slot_kinds');
assert.doesNotMatch(build, /slot < slot_capacity/, 'Item numbers are not cut at the cell count');
assert.match(build, /for \(uint8_t slot = 0; slot < CLIMATE_TILE_MAX_CONTENT_SLOTS; \+\+slot\)/);
assert.match(build, /std::stable_sort\(order, order \+ candidates,[\s\S]*stored\[a\]\.row < stored\[b\]\.row[\s\S]*stored\[a\]\.col < stored\[b\]\.col/);
assert.match(build, /if \(geometry\.row >= rows\) geometry\.row = rows - 1;/);
assert.match(build, /std::min<int>\(geometry\.span_h, rows - geometry\.row\)/);

console.log('PASS Climate shrink keeps fitting mini tiles; captions white, Humidity caption');
