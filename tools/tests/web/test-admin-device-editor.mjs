// Every tile type with the device field set (Lock, Alarm panel, Fan,
// Vacuum: types_registry.cpp preview kind "device") runs through the real
// delivered Admin script: choosing its entity updates the preview, the draft
// and schedules the autosave, and the draft keeps the entity, so a rebuilt
// editor shows it again (2026-10-30: the Vacuum entity was missing from the
// editor's list, was never saved and jumped back after every redraw). The
// Vacuum's popup layout follows the same path.
import {readAdminDeliverySource, readRepoFile} from '../../lib/admin-source.mjs';
import assert from 'node:assert/strict';
import vm from 'node:vm';

// The device types from the firmware registry, with their numeric IDs.
const tileTypes = readRepoFile('src', 'types', 'tile_type.h');
const idOf = name => Number(new RegExp(`${name} = (\\d+)`).exec(tileTypes)[1]);
const registry = readRepoFile('src', 'types', 'types_registry.cpp');
const devices = [...registry.matchAll(/\{\s*(TILE_\w+),\s*"[^"]*",\s*"device",\s*"(\w+)",\s*"device",\s*nullptr,\s*"(load\w+)",\s*"(save\w+)",\s*"(reset\w+)"/g)]
  .map(m => ({type: idOf(m[1]), prefix: m[2], load: m[3], save: m[4], reset: m[5]}));
assert.deepEqual(devices.map(d => d.prefix).sort(), ['alarm', 'fan', 'lock', 'vacuum']);

const noop = () => {};
const classList = {add: noop, remove: noop, toggle: noop, contains: () => false};
class TestElement {
  constructor(value = '') {
    this.value = value;
    this.dataset = {};
    this.listeners = {};
    this.attributes = {};
    this.inline = {};
    this.options = [];
    this.selectedOptions = [];
    this.classList = classList;
    this.style = {removeProperty: noop, setProperty: noop};
    this.className = '';
    this.innerHTML = '';
  }
  setAttribute(name, value) { this.attributes[name] = String(value); }
  removeAttribute(name) { delete this.attributes[name]; }
  getAttribute(name) { return Object.prototype.hasOwnProperty.call(this.attributes, name) ? this.attributes[name] : null; }
  addEventListener(name, handler) { (this.listeners[name] ??= []).push(handler); }
  removeEventListener(name, handler) { this.listeners[name] = (this.listeners[name] || []).filter(h => h !== handler); }
  appendChild(option) { this.options.push(option); }
  querySelector() { return null; }
  querySelectorAll() { return []; }
  dispatch(name) {
    const handlers = [...(this.listeners[name] || [])];
    this.inline[name]?.({target: this});
    for (const handler of handlers) {
      if ((this.listeners[name] || []).includes(handler)) handler({target: this});
    }
  }
}

const elements = {};
const storage = {getItem: () => null, setItem: noop, removeItem: noop, key: () => null, length: 0};
const document = {
  documentElement: {lang: 'en', dataset: {}, addEventListener: noop},
  addEventListener: noop,
  getElementById: id => elements[id] || null,
  querySelector: () => null,
  querySelectorAll: () => [],
  body: {classList, dataset: {}},
  createElement: () => new TestElement()
};
const sandbox = {
  console, document, localStorage: storage, sessionStorage: storage,
  setTimeout: () => 0, clearTimeout: noop, setInterval: () => 0, clearInterval: noop,
  requestAnimationFrame: noop, addEventListener: noop,
  fetch: () => Promise.reject(new Error('unexpected fetch')),
  FormData, URLSearchParams, Intl, Date, Math, JSON, Number, String, Object, Array, Map, Set, Promise,
  parseInt, parseFloat, isNaN, isFinite,
  APP_I18N: {}, DEVICE_I18N: {vacuum: {docked: 'Docked', clean: 'Clean', pause: 'Pause'}},
  GRID_COLS: 6, GRID_ROWS: 4, TILES_PER_GRID: 24, ADMIN_WEB_SESSION_TOKEN: 'test',
  TILE_TYPE_REGISTRY: Object.fromEntries(devices.map(d => [d.type, {
    fields: d.prefix, preview: 'device', load: d.load, save: d.save, reset: d.reset}])),
  TILE_TABS: [], TAB_BY_FOLDER: {}, FOLDER_BY_TAB: {folder1: 1},
  SCREENSAVER_FOLDER_ID: 65535, SCREENSAVER_TILE_DEFAULT_OPACITY: 0, SCREENSAVER_TILE_DEFAULT_COLOR: '#000000',
  MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 1, MEDIA_TILE_MAX_SPAN: 6, APP_LOCALE: 'en',
  navigator: {}, location: {}, confirm: () => true
};
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(readAdminDeliverySource(), sandbox, {filename: 'admin.js'});

const base = {
  folder1_tile_title: '', folder1_tile_icon: '', folder1_tile_color: '#2A2A2A', folder1_tile_col: '0',
  folder1_tile_row: '0', folder1_tile_span_w: '2', folder1_tile_span_h: '2', folder1_tile_type: '0'
};
for (const [id, value] of Object.entries(base)) elements[id] = new TestElement(value);
for (const d of devices) {
  for (const field of ['entity', 'value_font', 'popup_open_mode']) {
    elements[`folder1_${d.prefix}_${field}`] = new TestElement(field === 'popup_open_mode' ? '1' : field === 'value_font' ? '0' : '');
  }
}
elements.folder1_vacuum_layout = new TestElement('0');
elements.folder1_vacuum_map = new TestElement('');
elements['folder1-tile-3'] = new TestElement();
elements['folder1-tile-3'].dataset.type = '0';

vm.runInContext(`
  currentTileIndex = 3;
  currentTileTab = 'folder1';
  tilesData.folder1 = [];
  var __preview = 0, __draft = 0, __save = 0;
  const __realUpdateDraft = updateDraft;
  updateTilePreview = () => { __preview += 1; };
  updateDraft = tab => { __draft += 1; __realUpdateDraft(tab); };
  scheduleAutoSave = () => { __save += 1; };
  setupLivePreview('folder1');
  setupLivePreview('folder1');
`, sandbox);
const counters = () => vm.runInContext('({preview: __preview, draft: __draft, save: __save})', sandbox);

for (const d of devices) {
  elements.folder1_tile_type.value = String(d.type);
  const entity = `${d.prefix === 'alarm' ? 'alarm_control_panel' : d.prefix}.test`;
  const select = elements[`folder1_${d.prefix}_entity`];
  const before = counters();
  select.value = entity;
  select.dispatch('change');
  const after = counters();
  assert.deepEqual(
    {preview: after.preview - before.preview, draft: after.draft - before.draft, save: after.save - before.save},
    {preview: 1, draft: 1, save: 1}, `${d.prefix}: choosing the entity updates preview, draft and autosave once`);
  const snapshot = vm.runInContext("getTileSnapshotForSave('folder1', 3)", sandbox);
  assert.equal(snapshot?.[`${d.prefix}_entity`], entity, `${d.prefix}: the draft keeps the entity`);
  const form = vm.runInContext(`(() => { const f = new FormData(); ${d.save}('folder1', f); return Object.fromEntries(f); })()`, sandbox);
  assert.equal(form[`${d.prefix}_entity`], entity, `${d.prefix}: the save sends the entity`);
  assert.equal(form.sensor_entity, entity, `${d.prefix}: the save sends sensor_entity`);
}

// The Vacuum's popup layout: draft and autosave, sent as vacuum_layout.
const vacuum = devices.find(d => d.prefix === 'vacuum');
elements.folder1_tile_type.value = String(vacuum.type);
const before = counters();
elements.folder1_vacuum_layout.value = '1';
elements.folder1_vacuum_layout.dispatch('change');
const after = counters();
assert.equal(after.draft - before.draft, 1, 'the layout updates the draft');
assert.equal(after.save - before.save, 1, 'the layout schedules the autosave');
const form = vm.runInContext("(() => { const f = new FormData(); saveVacuumFields('folder1', f); return Object.fromEntries(f); })()", sandbox);
assert.equal(form.vacuum_layout, '1', 'the save sends the layout');
assert.equal(vm.runInContext("getTileSnapshotForSave('folder1', 3)", sandbox)?.vacuum_layout, '1', 'the draft keeps the layout');

// The Vacuum's map: draft and autosave, sent as vacuum_map, loaded back.
const beforeMap = counters();
elements.folder1_vacuum_map.value = 'camera.robo_map';
elements.folder1_vacuum_map.dispatch('change');
const afterMap = counters();
assert.equal(afterMap.draft - beforeMap.draft, 1, 'the map updates the draft');
assert.equal(afterMap.save - beforeMap.save, 1, 'the map schedules the autosave');
const mapForm = vm.runInContext("(() => { const f = new FormData(); saveVacuumFields('folder1', f); return Object.fromEntries(f); })()", sandbox);
assert.equal(mapForm.vacuum_map, 'camera.robo_map', 'the save sends the map');
assert.equal(vm.runInContext("getTileSnapshotForSave('folder1', 3)", sandbox)?.vacuum_map, 'camera.robo_map', 'the draft keeps the map');
vm.runInContext("loadVacuumFields('folder1', {type: 29, sensor_entity: 'vacuum.test', vacuum_map: 'image.floor', sensor_display_mode: 1})", sandbox);
assert.equal(elements.folder1_vacuum_map.value, 'image.floor', 'a reload shows the stored map');
assert.equal(elements.folder1_vacuum_layout.value, '1', 'a reload shows the stored layout');
vm.runInContext("resetVacuumFields('folder1')", sandbox);
assert.equal(elements.folder1_vacuum_map.value, '', 'a reset clears the map');

// The segmented choices follow their hidden selects.
const switchAdmin = readRepoFile('src', 'types', 'switch', 'admin.js');
for (const d of devices) assert.ok(switchAdmin.includes(`'${d.prefix}_value_font'`), `${d.prefix}: state size buttons`);
assert.ok(switchAdmin.includes("'vacuum_layout'"), 'popup layout buttons');
console.log(`Device editor: ${devices.map(d => d.prefix).join(', ')} save their entity through the real Admin script`);
