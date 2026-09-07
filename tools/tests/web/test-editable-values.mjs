import assert from "node:assert/strict";
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


const run = script => vm.runInContext(script, sandbox);
for (const [id, value] of Object.entries({
  folder1_tile_title: '', folder1_tile_icon: '', folder1_tile_color: '#2A2A2A',
  folder1_tile_col: '1', folder1_tile_row: '1', folder1_tile_span_w: '1',
  folder1_tile_span_h: '1', folder1_tile_type: '21'
})) elements[id] = new TestElement(value);
elements['folder1-tile-3'] = new TestElement();
run(`currentTileIndex=3; currentTileTab='folder1'; tilesData.folder1=[];
  let saveCount=0; scheduleAutoSave=()=>{saveCount++};`);
const types = [['number','Number','number.area','20','m²'],
               ['select','Select','select.mode','Heat pump + heating rod',''],
               ['datetime','DateTime','time.start','08:00:00','']];
for (const [index,[kind, camel, entity, state, unit]] of types.entries()) {
  const type=21+index;
  sandbox.TILE_TYPE_REGISTRY[type]={label:camel,css:kind,fields:kind,preview:kind,
    load:'load'+camel+'Fields',save:'save'+camel+'Fields',reset:'reset'+camel+'Fields',defaultBg:'#2A2A2A'};
  sandbox[kind.toUpperCase()+'_I18N']={unknown:'Unknown',unavailable:'Unavailable'};
  const select=elements['folder1_'+kind+'_entity']=new TestElement();
  const popup=elements['folder1_'+kind+'_popup_open_mode']=new TestElement('1');
  const font=elements['folder1_'+kind+'_value_font']=new TestElement('2');
  select.options=[{value:'',textContent:'No selection'},{value:entity,textContent:'Entity - '+entity}];
  select.selectedOptions=[select.options[0]];
  elements.folder1_tile_title.value='';
  elements.folder1_tile_type.value=String(type);
  run(`setupLivePreview('folder1');setupLivePreview('folder1');updateTileType('folder1');
    maybeFillTitleFromEntity('folder1','_${kind}_entity');`);
  assert.equal(elements.folder1_tile_title.value,'','Placeholder must never become a title');
  select.value=entity;select.selectedOptions=[select.options[1]];
  const before=run('saveCount');select.dispatch('change');
  assert.equal(run('saveCount'),before+1,'Rebinding must replace listeners');
  elements.folder1_tile_title.value='Desk <custom>\nOffice';
  elements.folder1_tile_title.dispatch('input');
  popup.value='0';popup.dispatch('change');
  const fields=run(`(()=>{const form=new FormData();save${camel}Fields('folder1',form);return Object.fromEntries(form)})()`);
  assert.equal(fields[kind+'_entity'],entity);assert.equal(fields.sensor_entity,entity);assert.equal(fields.popup_open_mode,'0');
  const snapshot=run("getTileSnapshotForSave('folder1',3)");
  assert.equal(snapshot.title,'Desk <custom>\nOffice');
  assert.equal(snapshot[kind+'_entity'],entity);assert.equal(String(snapshot.popup_open_mode),'0');
  run(`load${camel}Fields('folder1',{sensor_entity:'${entity}_missing',popup_open_mode:0});`);
  assert.equal(select.value,entity+'_missing');assert(select.options.some(o=>o.value===entity+'_missing'));
  run(`load${camel}Fields('folder1',{sensor_entity:'${entity}',popup_open_mode:0});`);
  for (const [option,css] of [['0','default'],['1','20'],['2','24'],['3','32'],['4','40']]) {
    const saves=run('saveCount');font.value=option;font.dispatch('change');
    assert.equal(run('saveCount'),saves+1,'Font changes must autosave exactly once');
    assert.equal(run("getTileSnapshotForSave('folder1',3)").sensor_value_font,option);
    assert(elements['folder1-tile-3'].innerHTML.includes('sensor-value-size-'+css));
    run(`load${camel}Fields('folder1',{sensor_entity:'${entity}',sensor_value_font:${option},popup_open_mode:0});`);
    assert.equal(font.value,option);
  }
  font.value='2';font.dispatch('change');
  for (const [language,unknown,unavailable] of [['de','Unbekannt','Nicht verfügbar'],['en','Unknown','Unavailable'],['fr','Inconnu','Indisponible']]) {
    sandbox.APP_LOCALE=language; document.documentElement.lang=language;
    sandbox[kind.toUpperCase()+'_I18N']={unknown,unavailable};
    for (const [current,available,expected] of [[state,true,state+(unit?' '+unit:'')],['unknown',true,unknown],['unavailable',false,unavailable],[null,false,'--']]) {
      sandbox.testMeta={editable_values:{[entity]:JSON.stringify({version:1,kind:kind==='datetime'?'time':kind,state:current,available,unit})},
        values:{[entity]:'legacy-must-not-overwrite'},icons:{[entity]:'mdi:tune'},names:{[entity]:'Entity'}};
      // The periodic refresh receives an already normalized cache from fetch.
      run(`sensorMetaCache=normalizeSensorMetaPayload(normalizeSensorMetaPayload(testMeta));updateTilePreview('folder1');`);
      let html=elements['folder1-tile-3'].innerHTML;
      assert(html.includes(expected),kind+' live '+language+': '+html);
      assert(html.includes('tile-editable-value sensor-value-size-24'),'Value font must match the device');
      assert(html.includes('&lt;custom&gt;'),'Untrusted titles must be escaped');
      for (const [w,h] of [[1,1],[2,1],[2,2]]) {
        run(`renderTileFromData('folder1',3,{type:${type},title:'Desk <custom>',sensor_entity:'${entity}',sensor_value_font:2,span_w:${w},span_h:${h}},sensorMetaCache);`);
        html=elements['folder1-tile-3'].innerHTML;
        assert(html.includes(expected),kind+' cached '+language+': '+html);
      }
    }
  }
  let request;
  sandbox.fetch=async (url,options)=>{request={url,fields:Object.fromEntries(options.body)};return {json:async()=>({success:true})}};
  await run(`postTile(1,3,{type:${type},title:${JSON.stringify('Desk\nOffice')},sensor_entity:'${entity}',sensor_value_font:4,popup_open_mode:0,col:1,row:1,span_w:1,span_h:1})`);
  assert.equal(request.url,'/api/tiles');assert.equal(request.fields[kind+'_entity'],entity);assert.equal(request.fields.popup_open_mode,'0');
  assert.equal(request.fields.sensor_value_font,'4');assert.equal(request.fields.title,'Desk\nOffice');
  run(`reset${camel}Fields('folder1');`);assert.equal(select.value,'');assert.equal(popup.value,'1');assert.equal(font.value,'2');
}
console.log('Editable types: real editor, preview, drafts, rebinding, import, availability and three locales passed.');
