import {readAdminDeliverySource} from '../../lib/admin-source.mjs';
import fs from 'node:fs';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const noop = () => {};

class TestStyle {
  setProperty(name, value) {
    this[name] = value;
  }

  removeProperty(name) {
    delete this[name];
  }
}

class TestElement {
  constructor(value = '') {
    this.value = value;
    this.dataset = {};
    this.listeners = {};
    this.attributes = {};
    this.options = [];
    this.selectedOptions = [];
    this.style = new TestStyle();
    this.childrenBySelector = {};
    this.classNames = new Set();
    this.classList = {
      add: (...names) => names.forEach(name => this.classNames.add(name)),
      remove: (...names) => names.forEach(name => this.classNames.delete(name)),
      contains: name => this.classNames.has(name),
      toggle: (name, force) => {
        const enabled = force ?? !this.classNames.has(name);
        if (enabled) this.classNames.add(name);
        else this.classNames.delete(name);
        return enabled;
      }
    };
    this._className = '';
    this._innerHTML = '';
  }

  set className(value) {
    this._className = String(value || '');
    this.classNames = new Set(this._className.split(/\s+/).filter(Boolean));
  }

  get className() {
    return this._className;
  }

  set innerHTML(value) {
    this._innerHTML = String(value || '');
    this.childrenBySelector = {};
    if (/class="[^"]*\btile-icon\b/.test(this._innerHTML)) {
      this.childrenBySelector['.tile-icon'] = new TestElement();
    }
    if (this._innerHTML.includes('class="tile-switch"')) {
      this.childrenBySelector['.tile-switch'] = new TestElement();
    }
    if (this._innerHTML.includes('tile-switch-state')) {
      this.childrenBySelector['.tile-switch-state'] = new TestElement();
    }
  }

  get innerHTML() {
    return this._innerHTML;
  }

  setAttribute(name, value) {
    this.attributes[name] = String(value);
  }

  removeAttribute(name) {
    delete this.attributes[name];
  }

  getAttribute(name) {
    return Object.prototype.hasOwnProperty.call(this.attributes, name)
      ? this.attributes[name]
      : null;
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

  querySelector(selector) {
    return this.childrenBySelector[selector] || null;
  }
}

const elements = {};
const body = new TestElement();
const storage = {
  getItem: () => null,
  setItem: noop,
  removeItem: noop,
  key: () => null,
  length: 0
};
const document = {
  documentElement: { lang: 'en', dataset: {}, addEventListener: noop },
  addEventListener: noop,
  getElementById: id => elements[id] || null,
  querySelector: () => null,
  querySelectorAll: () => [],
  body,
  createElement: () => new TestElement()
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
  getComputedStyle: element => ({
    backgroundColor: element?.style?.background || '#353535'
  }),
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
  GRID_COLS: 6,
  GRID_ROWS: 4,
  TILES_PER_GRID: 24,
  ADMIN_WEB_SESSION_TOKEN: 'test',
  TILE_TYPE_REGISTRY: {
    2: {css: 'scene', fields: 'scene', preview: 'scene', load: 'loadSceneFields', save: 'saveSceneFields', reset: 'resetSceneFields'},
    5: {
      css: 'switch',
      fields: 'switch',
      preview: 'switch',
      load: 'loadSwitchFields',
      save: 'saveSwitchFields',
      reset: 'resetSwitchFields'
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
  APP_LOCALE: 'en',
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
  folder1_tile_title: 'Light',
  folder1_tile_icon: 'lightbulb',
  folder1_tile_color: '#2A2A2A',
  folder1_tile_col: '0',
  folder1_tile_row: '0',
  folder1_tile_span_w: '1',
  folder1_tile_span_h: '1',
  folder1_tile_type: '5',
  folder1_switch_entity: 'light.test',
  folder1_switch_style: '1'
})) {
  elements[id] = new TestElement(value);
}
elements['folder1-tile-3'] = new TestElement();

const result = await vm.runInContext(`(async () => {
  currentTileIndex = 3;
  currentTileTab = 'folder1';
  tilesData.folder1 = [];
  sensorMetaCache.loaded = true;
  sensorMetaCache.values['light.test'] = JSON.stringify({
    state: 'on',
    hs_color: [32, 90]
  });
  updateTilePreview('folder1');
  await Promise.resolve();
  await Promise.resolve();

  const tile = document.getElementById('folder1-tile-3');
  const initialIcon = tile.querySelector('.tile-icon');
  const initialSwitch = tile.querySelector('.tile-switch');
  const initial = {
    hasIcon: !!initialIcon,
    hasSwitch: !!initialSwitch,
    isOn: initialSwitch?.classList.contains('is-on') || false,
    stateText: tile.querySelector('.tile-switch-state')?.textContent || ''
  };

  sensorMetaCache.values['light.test'] = JSON.stringify({
    state: 'unavailable',
    available: true,
    hs_color: [32, 90]
  });
  updateSwitchValuePreview('folder1');
  await Promise.resolve();
  await Promise.resolve();

  const unavailableIcon = tile.querySelector('.tile-icon');
  const unavailableSwitch = tile.querySelector('.tile-switch');
  return {
    initial,
    unavailableText: tile.querySelector('.tile-switch-state')?.textContent || '',
    parsed: parseSwitchPayload(sensorMetaCache.values['light.test']),
    unavailableIconColor: unavailableIcon?.style.color || '',
    unavailableIsOn:
      unavailableSwitch?.classList.contains('is-on') || false
  };
})()`, sandbox);

if (!result.initial.hasIcon || !result.initial.hasSwitch ||
    !result.initial.isOn || result.initial.stateText !== 'On') {
  throw new Error(
    'Real Light preview path did not render its active state: ' +
    JSON.stringify(result.initial));
}
if (result.parsed.available !== false) {
  throw new Error('Light unavailable state did not override stale metadata');
}
if (result.unavailableIconColor !== '#B0B0B0' ||
    result.unavailableIsOn || result.unavailableText !== 'Unavailable') {
  throw new Error('Unavailable Light preview retained an active visual state');
}

elements.folder1_switch_popup_open_mode = new TestElement('1');
elements.folder1_scene_alias = new TestElement('');
for (const domain of ['switch', 'input_boolean', 'automation', 'fan', 'humidifier', 'remote', 'siren']) {
  sandbox.testDomain = domain;
  const result = await vm.runInContext(`(async () => {
    const entity = testDomain + '.desk';
    document.getElementById('folder1_tile_type').value = '5';
    loadSwitchFields('folder1', {sensor_entity: entity, popup_open_mode: 0, switch_style: 1});
    document.getElementById('folder1_tile_title').value = 'Desk ' + testDomain;
    const payload = new FormData(); saveSwitchFields('folder1', payload);
    const snapshot = buildTileSnapshotFromInputs('folder1');
    sensorMetaCache.values[entity] = JSON.stringify({state:'on', available:true});
    updateTilePreview('folder1'); await Promise.resolve(); await Promise.resolve();
    const tile = document.getElementById('folder1-tile-3');
    const active = tile.querySelector('.tile-switch').classList.contains('is-on');
    sensorMetaCache.values[entity] = JSON.stringify({state:null,available:false});
    updateSwitchValuePreview('folder1'); await Promise.resolve(); await Promise.resolve();
    return {entity:payload.get('switch_entity'),mode:payload.get('popup_open_mode'), snapshot, active,
      inactive:!tile.querySelector('.tile-switch').classList.contains('is-on')};
  })()`, sandbox);
  assert.equal(result.entity, `${domain}.desk`);
  assert.equal(result.mode, '0');
  assert.equal(result.snapshot.switch_entity, `${domain}.desk`);
  assert.equal(result.snapshot.title, `Desk ${domain}`);
  assert(result.active && result.inactive);
}
for (const alias of ['scene_desk', 'script_desk', 'button_desk', 'input_button_desk']) {
  sandbox.testAlias = alias;
  const result = vm.runInContext(`(() => {
    document.getElementById('folder1_tile_type').value = '2';
    loadSceneFields('folder1', {scene_alias:testAlias});
    document.getElementById('folder1_tile_title').value = 'Press ' + testAlias;
    const payload = new FormData();saveSceneFields('folder1',payload);
    updateTilePreview('folder1');
    return {alias:payload.get('scene_alias'),snapshot:buildTileSnapshotFromInputs('folder1'),html:document.getElementById('folder1-tile-3').innerHTML};
  })()`, sandbox);
  assert.equal(result.alias, alias);
  assert.equal(result.snapshot.scene_alias, alias);
  assert(result.html.includes(`Press ${alias}`));
}
console.log('Light and compatible Switch/Scene WebUI contract tests passed.');
