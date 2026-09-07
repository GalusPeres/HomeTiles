import {readAdminDeliverySource} from '../../lib/admin-source.mjs';
import fs from 'node:fs';
import vm from 'node:vm';

const readText = url =>
  fs.readFileSync(url, 'utf8').replace(/\r\n?/g, '\n');

const noop = () => {};
const classList = {
  add: noop,
  remove: noop,
  toggle: noop,
  contains: () => false
};

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
    this.style = {
      removeProperty: noop,
      setProperty: noop
    };
    this.className = '';
    this.innerHTML = '';
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  removeAttribute(name) {
    delete this.attributes[name];
  }

  addEventListener(name, handler) {
    (this.listeners[name] ??= []).push(handler);
  }

  removeEventListener(name, handler) {
    this.listeners[name] = (this.listeners[name] || [])
      .filter(candidate => candidate !== handler);
  }

  appendChild(option) {
    this.options.push(option);
  }

  querySelector() {
    return null;
  }

  dispatch(name) {
    const handlers = [...(this.listeners[name] || [])];
    this.inline[name]?.({ target: this });
    for (const handler of handlers) {
      if ((this.listeners[name] || []).includes(handler)) {
        handler({ target: this });
      }
    }
  }
}

const elements = {};
const storage = {
  getItem: () => null,
  setItem: noop,
  removeItem: noop,
  key: () => null,
  length: 0
};
const document = {
  documentElement: { lang: 'de', dataset: {}, addEventListener: noop },
  addEventListener: noop,
  getElementById: id => elements[id] || null,
  querySelector: () => null,
  querySelectorAll: () => [],
  body: { classList, dataset: {} },
  createElement: () => new TestElement()
};

const statePairs = {
  '': { on: 'An', off: 'Aus' },
  battery: { on: 'Niedrig', off: 'Normal' },
  connectivity: { on: 'Verbunden', off: 'Getrennt' },
  door: { on: 'Offen', off: 'Geschlossen' },
  motion: { on: 'Erkannt', off: 'Frei' }
};
const sandbox = {
  console,
  document,
  localStorage: storage,
  sessionStorage: storage,
  setTimeout: () => 0,
  clearTimeout: noop,
  setInterval: () => 0,
  clearInterval: noop,
  requestAnimationFrame: noop,
  addEventListener: noop,
  fetch: () => Promise.reject(new Error('unexpected fetch')),
  FormData,
  URLSearchParams,
  Intl,
  Date,
  Math,
  JSON,
  Number,
  String,
  Object,
  Array,
  Map,
  Set,
  Promise,
  parseInt,
  parseFloat,
  isNaN,
  isFinite,
  APP_I18N: {},
  BINARY_SENSOR_I18N: {
    states: statePairs,
    unavailable: 'Nicht verfügbar',
    unknown: 'Unbekannt'
  },
  GRID_COLS: 6,
  GRID_ROWS: 4,
  TILES_PER_GRID: 24,
  ADMIN_WEB_SESSION_TOKEN: 'test',
  TILE_TYPE_REGISTRY: {
    0: { label: 'Leer', css: 'empty', preview: 'none' },
    20: {
      label: 'Binärsensor',
      css: 'binary_sensor',
      fields: 'binary_sensor',
      preview: 'binary_sensor',
      load: 'loadBinarySensorFields',
      save: 'saveBinarySensorFields',
      reset: 'resetBinarySensorFields',
      defaultBg: '#2A2A2A'
    }
  },
  TILE_TABS: [],
  TAB_BY_FOLDER: {},
  FOLDER_BY_TAB: { folder1: 1 },
  SCREENSAVER_FOLDER_ID: 65535,
  SCREENSAVER_TILE_DEFAULT_OPACITY: 0,
  SCREENSAVER_TILE_DEFAULT_COLOR: '#000000',
  MEDIA_TILE_TYPE: 15,
  MEDIA_TILE_MIN_SPAN: 1,
  MEDIA_TILE_MAX_SPAN: 6,
  APP_LOCALE: 'de',
  navigator: {},
  location: {},
  confirm: () => true
};
sandbox.window = sandbox;

vm.createContext(sandbox);
vm.runInContext(
  readAdminDeliverySource(),
  sandbox,
  { filename: 'admin.js' }
);

for (const [id, value] of Object.entries({
  folder1_tile_title: '',
  folder1_tile_icon: '',
  folder1_tile_color: '#2A2A2A',
  folder1_tile_col: '1',
  folder1_tile_row: '1',
  folder1_tile_span_w: '1',
  folder1_tile_span_h: '1',
  folder1_tile_type: '20',
  folder1_binary_sensor_entity: '',
  folder1_binary_sensor_popup_open_mode: '1'
})) {
  elements[id] = new TestElement(value);
}
elements['folder1-tile-3'] = new TestElement();
elements['folder1-tile-3'].dataset.type = '0';

const placeholder = { value: '', textContent: 'Keine Auswahl' };
const entityOption = {
  value: 'binary_sensor.door',
  textContent: 'Tür - binary_sensor.door'
};
const entitySelect = elements.folder1_binary_sensor_entity;
entitySelect.options = [placeholder, entityOption];
entitySelect.selectedOptions = [placeholder];

vm.runInContext(`
  currentTileIndex = 3;
  currentTileTab = 'folder1';
  tilesData.folder1 = [];
  let __preview = 0;
  let __draft = 0;
  let __save = 0;
  const __realUpdateTilePreview = updateTilePreview;
  const __realUpdateDraft = updateDraft;
  updateTilePreview = () => { __preview += 1; };
  updateDraft = tab => {
    __draft += 1;
    __realUpdateDraft(tab);
  };
  scheduleAutoSave = () => { __save += 1; };
  maybeFillTitleFromEntity('folder1', '_binary_sensor_entity');
  setupLivePreview('folder1');
  setupLivePreview('folder1');
`, sandbox);

if (elements.folder1_tile_title.value !== '') {
  throw new Error('The empty placeholder became the Binary Sensor title');
}

elements.folder1_tile_type.inline.change = () => {
  vm.runInContext("updateTileType('folder1')", sandbox);
};
elements.folder1_tile_type.dispatch('change');
entitySelect.value = entityOption.value;
entitySelect.selectedOptions = [entityOption];
entitySelect.dispatch('change');
elements.folder1_binary_sensor_popup_open_mode.value = '0';
elements.folder1_binary_sensor_popup_open_mode.dispatch('change');

const editorResult = vm.runInContext(`(() => {
  const form = new FormData();
  saveBinarySensorFields('folder1', form);
  const snapshot = getTileSnapshotForSave('folder1', 3);
  return {
    preview: __preview,
    draft: __draft,
    save: __save,
    title: document.getElementById('folder1_tile_title').value,
    entity: form.get('binary_sensor_entity'),
    sensorEntity: form.get('sensor_entity'),
    popup: form.get('popup_open_mode'),
    draftEntity: snapshot?.binary_sensor_entity,
    draftPopup: snapshot?.popup_open_mode,
    cachedEntity: tilesData.folder1[3]?.sensor_entity
  };
})()`, sandbox);

const expectedEditorResult = {
  preview: 2,
  draft: 3,
  save: 3,
  title: 'Tür',
  entity: 'binary_sensor.door',
  sensorEntity: 'binary_sensor.door',
  popup: '0',
  draftEntity: 'binary_sensor.door',
  draftPopup: '0',
  cachedEntity: 'binary_sensor.door'
};
if (JSON.stringify(editorResult) !== JSON.stringify(expectedEditorResult)) {
  throw new Error(
    `Binary Sensor editor contract failed: ${JSON.stringify(editorResult)}`);
}

vm.runInContext(`
  loadBinarySensorFields('folder1', {
    sensor_entity: 'binary_sensor.temporarily_missing',
    popup_open_mode: 0
  });
`, sandbox);
if (entitySelect.value !== 'binary_sensor.temporarily_missing' ||
    entitySelect.dataset.configuredValue !==
      'binary_sensor.temporarily_missing' ||
    !entitySelect.options.some(
      option => option.value === 'binary_sensor.temporarily_missing')) {
  throw new Error('A configured Binary Sensor was lost when options were stale');
}

elements.folder1_tile_title.value = 'Tür';
elements.folder1_tile_icon.value = '';
entitySelect.value = 'binary_sensor.door';
entitySelect.dataset.configuredValue = 'binary_sensor.door';
vm.runInContext(`
  updateTilePreview = __realUpdateTilePreview;
  sensorMetaCache = normalizeSensorMetaPayload({
    binary_sensor_values: {
      'binary_sensor.door': JSON.stringify({
        state: 'off', available: true, device_class: 'door',
        icon: 'mdi:door-closed'
      })
    },
    icons: { 'binary_sensor.door': 'mdi:door-open' },
    names: { 'binary_sensor.door': 'Tür' }
  });
  updateTilePreview('folder1');
`, sandbox);
let previewHtml = elements['folder1-tile-3'].innerHTML;
if (!previewHtml.includes('mdi-door-closed') ||
    !previewHtml.includes('style="color:#9e9e9e"') ||
    !previewHtml.includes('tile-binary-sensor-value') ||
    !previewHtml.includes('Geschlossen')) {
  throw new Error(`Live Binary Sensor preview is wrong: ${previewHtml}`);
}

vm.runInContext(`
  sensorMetaCache.values['binary_sensor.door'] = JSON.stringify({
    state: 'on', available: true, device_class: 'door',
    icon: 'mdi:door-open'
  });
  updateTilePreview('folder1');
`, sandbox);
previewHtml = elements['folder1-tile-3'].innerHTML;
if (!previewHtml.includes('mdi-door-open') ||
    !previewHtml.includes('style="color:#ffc107"') ||
    !previewHtml.includes('Offen')) {
  throw new Error(`Active Binary Sensor preview is wrong: ${previewHtml}`);
}

elements.folder1_tile_icon.value = 'shield-home';
vm.runInContext(`
  sensorMetaCache.values['binary_sensor.door'] = JSON.stringify({
    state: 'off', available: true, device_class: 'door',
    icon: 'mdi:door-closed'
  });
  updateTilePreview('folder1');
`, sandbox);
previewHtml = elements['folder1-tile-3'].innerHTML;
if (!previewHtml.includes('mdi-shield-home') ||
    previewHtml.includes('mdi-door-closed')) {
  throw new Error('An explicit Binary Sensor icon was replaced automatically');
}

elements.folder1_tile_icon.value = '';
vm.runInContext(`
  renderTileFromData('folder1', 3, {
    type: 20,
    title: 'Tür',
    icon_name: '',
    bg_color: 0,
    sensor_entity: 'binary_sensor.door',
    span_w: 1,
    span_h: 1
  }, sensorMetaCache);
`, sandbox);
previewHtml = elements['folder1-tile-3'].innerHTML;
if (!previewHtml.includes('mdi-door-closed') ||
    !previewHtml.includes('Geschlossen') ||
    elements['folder1-tile-3'].className !== 'tile binary_sensor') {
  throw new Error(`Cached-grid Binary Sensor preview is wrong: ${previewHtml}`);
}

const localizedStates = vm.runInContext(`[
  binarySensorPreviewStateText(parseBinarySensorPreviewPayload(
    JSON.stringify({state:'on', available:true, device_class:'motion'}))),
  binarySensorPreviewStateText(parseBinarySensorPreviewPayload(
    JSON.stringify({state:'off', available:true, device_class:'connectivity'}))),
  binarySensorPreviewStateText(parseBinarySensorPreviewPayload(
    JSON.stringify({state:'on', available:true, device_class:'battery'}))),
  binarySensorPreviewStateText(parseBinarySensorPreviewPayload(
    JSON.stringify({state:'unknown', available:true, device_class:'door'}))),
  binarySensorPreviewStateText(parseBinarySensorPreviewPayload(
    JSON.stringify({state:'on', available:false, device_class:'door'})))
]`, sandbox);
const expectedStates = [
  'Erkannt', 'Getrennt', 'Niedrig', 'Unbekannt', 'Nicht verfügbar'
];
if (JSON.stringify(localizedStates) !== JSON.stringify(expectedStates)) {
  throw new Error(
    `Binary Sensor state localization failed: ${JSON.stringify(localizedStates)}`);
}

const iconContract = vm.runInContext(`[
  resolveBinarySensorPreviewIcon('', 'binary_sensor.door',
    parseBinarySensorPreviewPayload('{"state":"off","device_class":"door","icon":"mdi:door-closed"}'),
    {'binary_sensor.door':'mdi:door-open'}),
  resolveBinarySensorPreviewIcon('', 'binary_sensor.door',
    parseBinarySensorPreviewPayload('{"state":"on","device_class":"door","icon":"mdi:door-open"}'),
    {'binary_sensor.door':'mdi:door-closed'}),
  resolveBinarySensorPreviewIcon('account-alert', 'binary_sensor.door',
    parseBinarySensorPreviewPayload('{"state":"on","device_class":"door"}'), {}),
  resolveBinarySensorPreviewIcon('', 'binary_sensor.door',
    parseBinarySensorPreviewPayload('{"state":"off","device_class":"door"}'),
    {'binary_sensor.door':'mdi:shield-home'}),
  resolveBinarySensorPreviewIcon('', 'binary_sensor.door',
    parseBinarySensorPreviewPayload('{"state":"off","device_class":"door","icon":"mdi:door-open"}'),
    {})
]`, sandbox);
const expectedIcons = [
  'door-closed', 'door-open', 'account-alert', 'shield-home', 'door-open'
];
if (JSON.stringify(iconContract) !== JSON.stringify(expectedIcons)) {
  throw new Error(`Binary Sensor icon contract failed: ${JSON.stringify(iconContract)}`);
}

const css = readText(new URL('../../../src/web/assets/admin.css', import.meta.url));
for (const pattern of [
  /\.tile\.binary_sensor(?:\s*,[^{}]+)?\s*\{/,
  /\.tile\.binary_sensor \.tile-title(?:\s*,[^{}]+)?\s*\{/,
  /\.tile\.binary_sensor \.tile-icon(?:\s*,[^{}]+)?\s*\{/
]) {
  if (!pattern.test(css)) {
    throw new Error('Binary Sensor preview no longer shares the Sensor layout');
  }
}

const adminJs = readText(
  new URL('../../../src/web/assets/admin.js', import.meta.url));
if (!/const supportedTypes = new Set\(\[[^\]]*\b20\b[^\]]*\]\)/
  .test(adminJs)) {
  throw new Error('Binary Sensor is missing from the screensaver import allow-list');
}

const webScripts = readText(
  new URL('../../../src/types/binary_sensor/web_scripts.cpp', import.meta.url));
if (!webScripts.includes('i18n::binary_sensor_state_label') ||
    !webScripts.includes('const BINARY_SENSOR_I18N')) {
  throw new Error('Binary Sensor Web translations are not centrally exported');
}

const webHtml = readText(
  new URL('../../../src/types/binary_sensor/web_html.cpp', import.meta.url));
for (const marker of [
  'i18n::binary_sensor_label(language, 1)',
  '_binary_sensor_entity',
  '_binary_sensor_popup_open_mode'
]) {
  if (!webHtml.includes(marker)) {
    throw new Error(`Binary Sensor HTML contract is missing: ${marker}`);
  }
}

const adminHtml = readText(
  new URL('../../../src/web/server/render/web_admin_html.cpp', import.meta.url));
if (!/screensaver_mode[\s\S]*?<option value=\\"20\\">[\s\S]*?i18n::binary_sensor_label/
  .test(adminHtml)) {
  throw new Error('Binary Sensor is missing from the localized screensaver type selector');
}

const handlers = readText(
  new URL('../../../src/web/server/handlers/web_admin_tiles.cpp', import.meta.url));
for (const marker of [
  'binary_sensor_values',
  'ha.binary_sensors_text',
  'binary_sensors'
]) {
  if (!handlers.includes(marker)) {
    throw new Error(`Binary Sensor Web API contract is missing: ${marker}`);
  }
}
if (!/screensaver_grid && !tileTypeAllowedInScreensaver\(type\)/
  .test(handlers)) {
  throw new Error('Binary Sensor is missing from the screensaver save allow-list');
}

let importRequest = null;
sandbox.fetch = async (url, options = {}) => {
  importRequest = {
    url,
    method: options.method,
    fields: Object.fromEntries(options.body.entries())
  };
  return { json: async () => ({ success: true }) };
};
await vm.runInContext(`postTile(1, 3, {
  type: 20,
  title: 'Tür',
  icon_name: '',
  bg_color: 0,
  sensor_entity: 'binary_sensor.door',
  popup_open_mode: 0,
  col: 1,
  row: 1,
  span_w: 1,
  span_h: 1
})`, sandbox);
if (importRequest?.url !== '/api/tiles' ||
    importRequest?.method !== 'POST' ||
    importRequest?.fields?.type !== '20' ||
    importRequest?.fields?.binary_sensor_entity !== 'binary_sensor.door' ||
    importRequest?.fields?.popup_open_mode !== '0') {
  throw new Error(
    `Binary Sensor import payload is incomplete: ${JSON.stringify(importRequest)}`);
}

console.log('Binary Sensor Web Admin contract: PASS');
