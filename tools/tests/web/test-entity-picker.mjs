// The shared entity picker replaces the per-type entity <select> lists: every
// tile field that names an entity is a hidden input the picker draws, the
// choices come only from /api/entity_options, and the value is kept even when
// the entity is missing from the list. Covers the server contract (no option
// loops left, one field helper, every list key served), the translations in
// every language, and the real Admin bundle in headless Chrome: placeholder,
// names, open, search, keyboard choice, clear, escaping, kept values, scenes,
// the icon color source and a failed load.
import assert from 'node:assert/strict';

import {inlineScriptSafe, readAdminDeliverySource, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// ---- Server: one helper, no option lists ----
const fields = {
  'src/types/sensor/web_html.cpp': ['"sensor_entity"', '"sensors"'],
  'src/types/binary_sensor/web_html.cpp': ['"binary_sensor_entity"', '"binary_sensors"'],
  'src/types/number/web_html.cpp': ['"number_entity"', '"numbers"'],
  'src/types/select/web_html.cpp': ['"select_entity"', '"selects"'],
  'src/types/datetime/web_html.cpp': ['"datetime_entity"', '"datetimes"'],
  'src/types/energy/web_html.cpp': ['"energy_entity"', '"energy"'],
  'src/types/weather/web_html.cpp': ['"weather_entity"', '"weathers"'],
  'src/types/switch/web_html.cpp': ['"switch_entity"', '"switches"'],
  'src/types/media/web_html.cpp': ['"media_entity"', '"media"'],
  'src/types/climate/web_html.cpp': ['"climate_entity"', '"climates"'],
  'src/types/cover/web_html.cpp': ['"cover_entity"', '"covers"'],
  'src/types/camera/web_html.cpp': ['"camera_entity"', '"cameras"'],
  'src/types/scene/web_html.cpp': ['"scene_alias"', '"scenes"'],
  'src/types/device/web.cpp': ['"locks"', '"alarm_panels"', '"fans"']
};
for (const [file, markers] of Object.entries(fields)) {
  const source = read(file);
  assert.match(source, /appendEntityPickerField\(html, tab_id, /, `${file} uses the shared picker field`);
  for (const marker of markers) assert.ok(source.includes(marker), `${file}: ${marker}`);
  assert.doesNotMatch(source, /_options\b|haBridgeConfig|<option value=\\"\\">/, `${file} lists no entities itself`);
}
const utils = read('src/web/server/web_admin_utils.cpp');
assert.match(utils, /<input type=\\"hidden\\" id=\\"";[\s\S]*?data-entity-picker=\\"";/,
  'The field keeps its id on a hidden input');
assert.match(read('src/web/server/render/tile_icon_colors_html.cpp'),
  /_tile_icon_source" data-icon-color="source" data-entity-picker="icon_sources">/);
assert.doesNotMatch(read('src/types/types_registry.h'), /_options = nullptr/, 'No entity lists in the type context');
assert.doesNotMatch(read('src/web/server/render/web_admin_html.cpp'), /sensorOptions|switchOptions|parseSceneList/,
  'The page and the folder tabs no longer build entity lists');

const handler = read('src/web/server/handlers/web_admin_tiles.cpp');
const options = handler.slice(handler.indexOf('void WebAdminServer::handleGetEntityOptions()'),
  handler.indexOf('// ========== Folder API'));
for (const key of ['sensors', 'binary_sensors', 'numbers', 'selects', 'datetimes', 'weathers', 'climates', 'covers',
  'locks', 'alarm_panels', 'fans', 'cameras', 'media', 'switches']) {
  assert.ok(options.includes(`appendList("${key}"`), `/api/entity_options serves ${key}`);
}
assert.match(options, /\\"energy\\":\[/);
assert.match(options, /\\"scenes\\":\[/);
assert.match(options, /\\"t\\":\\"/);
assert.match(options, /\\"i\\":\\"/);
assert.match(options, /findEntityIcon\(scene\.entity\), scene\.entity/);
assert.doesNotMatch(options, /" - "/, 'Names no longer carry the entity id');

// ---- Admin JavaScript: no select workarounds left ----
const bundle = JSON.parse(read('src/web/admin/bundle.json')).sources;
assert.ok(bundle.includes('src/web/admin/tiles/entity-picker.js'));
for (const file of bundle) {
  const source = read(file);
  assert.doesNotMatch(source, /configuredValue|rebuildEntitySelect|titleFromOption/, `${file} keeps no select workaround`);
}
assert.match(read('src/types/scene/admin.js'), /entityPickerName\(sceneSel\)/);
assert.match(read('src/web/admin/tiles/editor.js'), /bindLive\(sceneInput, 'change', 'sceneAlias'/);

// ---- Translations: every language, the documented order ----
const header = read('src/core/i18n/i18n.h');
assert.match(header, /const char\* entity_picker_labels\[8\];/);
assert.match(header, /const char\* entity_kind_labels\[22\];/);
const picker = read('src/web/admin/tiles/entity-picker.js');
const domains = picker.slice(picker.indexOf('const ENTITY_KIND_DOMAINS = ['), picker.indexOf('];', picker.indexOf('const ENTITY_KIND_DOMAINS')));
assert.equal((domains.match(/\[/g) || []).length - 1, 22, 'One domain group per entity_kind_labels entry');
const scripts = read('src/web/server/render/web_admin_scripts.cpp');
for (const key of ['entityPickerChoose', 'entityPickerSearch', 'entityPickerNoMatch', 'entityPickerLoadFailed',
  'entityPickerRetry', 'entityPickerClear', 'entityPickerNone', 'entityPickerReleased']) {
  assert.ok(scripts.includes(`"${key}"`), key);
  assert.ok(picker.includes(`'${key}'`), `${key} is used`);
}
assert.match(scripts, /const ENTITY_KIND_LABELS = \[/);

// The last three arrays of every LocaleProfile table: picker labels, kinds,
// icon picker labels.
const i18n = read('src/core/i18n/i18n.cpp');
function lastArrays(open) {
  const groups = [];
  let depth = 0;
  let start = 0;
  for (let i = open; i < i18n.length; i++) {
    const c = i18n[i];
    if (c === '"') {
      for (i++; i18n[i] !== '"'; i++) if (i18n[i] === '\\') i++;
      continue;
    }
    if (c === '/' && i18n[i + 1] === '/') { i = i18n.indexOf('\n', i); continue; }
    if (c === '{') { depth++; if (depth === 2) start = i; }
    if (c === '}') {
      if (depth === 2) groups.push(i18n.slice(start, i + 1));
      depth--;
      if (depth === 0) break;
    }
  }
  const strings = group => [...group.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  return groups.slice(-3).map(strings);
}
const locales = {};
for (const match of i18n.matchAll(/static const LocaleProfile kLocale(\w+) = \{/g)) {
  const [labels, kinds, icons] = lastArrays(match.index + match[0].length - 1);
  assert.equal(icons.length, 5, `${match[1]} icon picker labels`);
  assert.equal(labels.length, 8, `${match[1]} picker labels`);
  assert.equal(kinds.length, 22, `${match[1]} entity kinds`);
  for (const text of [...labels, ...kinds]) assert.ok(text.trim().length, `${match[1]} has no empty label`);
  assert.ok(labels[2].includes('{query}'), `${match[1]} no-match text names the search`);
  locales[match[1]] = {labels, kinds, icons};
}
assert.deepEqual(Object.keys(locales).sort(), ['De', 'En', 'Fr', 'Pl']);
assert.equal(locales.De.labels[0], 'Entität wählen');
assert.equal(locales.En.kinds[15], 'Scene');

// ---- The real bundle in headless Chrome (German) ----
const de = locales.De;
const i18nKeys = ['entityPickerChoose', 'entityPickerSearch', 'entityPickerNoMatch', 'entityPickerLoadFailed',
  'entityPickerRetry', 'entityPickerClear', 'entityPickerNone'];
const appI18n = Object.fromEntries(i18nKeys.map((key, index) => [key, de.labels[index]]));
const page = `<!doctype html><html lang="de"><head><style>${read('src/web/assets/admin.css')}</style></head><body>
<div class="tile-settings" id="tSettings" style="width:420px">
<div class="tile-settings-head"><select id="t_tile_type"><option value="1">Sensor</option></select><div class="tile-entity-slot" id="t_tile_entity_slot"></div></div>
<div class="tile-settings-body"><textarea id="t_tile_title"></textarea><input id="t_tile_icon"><input type="color" id="t_tile_icon_color" value="#FFFFFF" data-unset="1">
<div class="type-fields show" id="t_sensor_fields"><label for="t_sensor_entity_picker">Sensor</label><input type="hidden" id="t_sensor_entity" data-entity-picker="sensors"><label>Einheit</label><input id="t_sensor_unit"></div>
<div class="type-fields show" id="t_scene_fields"><label for="t_scene_alias_picker">Szene</label><input type="hidden" id="t_scene_alias" data-entity-picker="scenes"></div>
<div id="later"></div>
</div></div><pre id="result"></pre><script>
const loaded=[];const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,fn,...args)=>{if(name==='DOMContentLoaded')loaded.push(fn);else nativeListen(name,fn,...args);};
const APP_I18N=${JSON.stringify(appI18n)};
const ENTITY_KIND_LABELS=${JSON.stringify(de.kinds)};
const CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={0:{}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
let failLoad=false;let loads=0;
const OPTIONS={success:true,
  sensors:[{v:'sensor.temp_kitchen',t:'Temperatur Küche',i:'mdi:thermometer'},{v:'sensor.power',t:'Strom'},
    {v:'sensor.evil',t:'<img src=x onerror="window.__xss=1">'}],
  switches:[{v:'light.desk',t:'Schreibtisch',i:'mdi:desk-lamp'}],
  scenes:[{v:'movie',t:'Movie',e:'scene.movie_time',i:'mdi:movie'}],
  binary_sensors:[{v:'binary_sensor.door',t:'Tür'}]};
window.fetch=async url=>{loads++;if(failLoad)throw new Error('offline');return {ok:true,json:async()=>OPTIONS};};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const $=id=>document.getElementById(id);
 const tick=()=>new Promise(r=>setTimeout(r,0));
 loaded.filter(fn=>String(fn).includes('setupEntityPickers(document)')).forEach(fn=>fn());
 const sensor=$('t_sensor_entity');const field=$('t_sensor_entity_picker');
 check(field&&field.closest('.entity-picker')===sensor.nextElementSibling,'The picker follows its input');
 // The shown type's entity moves under Type, label first, unit stays.
 placeTileEntityField('t');
 const slot=$('t_tile_entity_slot');
 check(slot.firstElementChild.tagName==='LABEL'&&slot.contains(sensor)&&slot.contains(field)&&!slot.contains($('t_sensor_unit')),'Entity under Type');
 check($('t_sensor_fields').firstElementChild.textContent==='Einheit','The rest stays in the type fields');
 check(field.textContent.includes(${JSON.stringify(de.labels[0])}),'German placeholder: '+field.textContent);
 let changes=0;nativeListen('change',e=>{if(e.target===sensor)changes++;});
 // A value set by load code redraws the field, also before the list arrived.
 sensor.value='sensor.gone_device';
 check(field.textContent.includes('Gone Device')&&field.textContent.includes('sensor.gone_device'),'A missing entity is shown, not dropped');
 await fetchEntityOptions();
 sensor.value='sensor.temp_kitchen';
 check(field.textContent.includes('Temperatur Küche')&&field.querySelector('.mdi-thermometer'),'Name and icon from the Bridge list');
 check(changes===0,'Setting the value programmatically fires no change');
 check(entityPickerName(sensor)==='Temperatur Küche','The title source is the entity name');
 // Open: search focused, sorted list, kind on the right, current one selected.
 field.click();await tick();
 const pop=document.querySelector('.entity-picker-popover.open');
 check(pop&&document.activeElement===pop.querySelector('input'),'Opens with the search focused');
 check(pop.querySelector('input').placeholder===${JSON.stringify(de.labels[1])},'German search placeholder');
 let items=[...pop.querySelectorAll('.entity-picker-item')];
 check(items.length===3,'Only the sensor list: '+items.length);
 check(items.map(i=>i.querySelector('.entity-picker-context').textContent).join()==='sensor.evil,sensor.power,sensor.temp_kitchen','Sorted by name');
 check(items.every(i=>i.querySelector('.entity-picker-kind').textContent===${JSON.stringify(de.kinds[0])}),'Kind label in German');
 check(pop.querySelector('.entity-picker-item.selected .entity-picker-context').textContent==='sensor.temp_kitchen','The chosen entity is marked');
 check(!pop.querySelector('img')&&!window.__xss,'Names are escaped');
 // Search, highlight, keyboard choice.
 const search=pop.querySelector('input');
 search.value='str';search.dispatchEvent(new Event('input',{bubbles:true}));
 items=[...pop.querySelectorAll('.entity-picker-item')];
 check(items.length===1&&items[0].querySelector('mark').textContent==='Str','Search filters and highlights');
 search.value='nichts';search.dispatchEvent(new Event('input',{bubbles:true}));
 check(pop.textContent.includes(${JSON.stringify(de.labels[2].replace('{query}', 'nichts'))}),'No match names the search: '+pop.textContent);
 $('t_tile_title').value='Mein Titel';$('t_tile_icon').value='home';setIconColorInput('t','#FF0000');
 search.value='sensor';search.dispatchEvent(new Event('input',{bubbles:true}));
 search.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true}));
 search.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));
 check(sensor.value==='sensor.power'&&changes===1,'Arrow and Enter choose with one change event: '+sensor.value+' '+changes);
 check(!document.querySelector('.entity-picker-popover.open')&&field.textContent.includes('Strom'),'Closes and shows the choice');
 check($('t_tile_title').value==='Strom'&&$('t_tile_icon').value===''&&$('t_tile_icon_color').dataset.unset==='1','A chosen entity brings name, icon and icon color');
 $('t_tile_title').value='Eigener Titel';
 // Escape closes without a change, Clear empties with one.
 field.click();await tick();
 document.querySelector('.entity-picker-popover input').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}));
 check(!document.querySelector('.entity-picker-popover.open')&&changes===1,'Escape keeps the value');
 const clear=sensor.nextElementSibling.querySelector('.entity-picker-clear');
 check(!clear.hidden&&clear.getAttribute('aria-label')===${JSON.stringify(de.labels[5])},'Clear button in German');
 clear.click();
 check(sensor.value===''&&changes===2&&field.textContent.includes(${JSON.stringify(de.labels[0])})&&clear.hidden,'Clear empties the field');
 check($('t_tile_title').value==='Eigener Titel','Clearing keeps an edited title');
 // A click on an item chooses it; a click elsewhere closes.
 field.click();await tick();
 document.querySelector('.entity-picker-popover .entity-picker-item').click();
 check(sensor.value==='sensor.evil'&&changes===3,'A click chooses');
 check($('t_tile_title').value.startsWith('<img'),'Another entity replaces the edited title');
 $('t_tile_title').value='Bleibt';field.click();await tick();
 document.querySelector('.entity-picker-popover .entity-picker-item.selected').click();
 check($('t_tile_title').value.startsWith('<img')&&changes===4,'Choosing the same entity again takes its name again');
 field.click();await tick();document.body.click();
 check(!document.querySelector('.entity-picker-popover.open'),'Outside click closes');
 // Another type: its entity moves up, the sensor returns to its fields.
 $('t_sensor_fields').classList.remove('show');placeTileEntityField('t');
 check(slot.contains($('t_scene_alias'))&&$('t_sensor_fields').firstElementChild.getAttribute('for')==='t_sensor_entity_picker'&&$('t_sensor_fields').contains(field),'Type switch moves the entity');
 // Scenes keep the alias; kind and context come from the scene entity.
 const scene=$('t_scene_alias');scene.value='movie';
 const sceneField=$('t_scene_alias_picker');
 check(sceneField.textContent.includes('Movie')&&sceneField.textContent.includes('scene.movie_time'),'Scene shows its entity');
 sceneField.click();await tick();
 check(document.querySelector('.entity-picker-popover .entity-picker-kind').textContent===${JSON.stringify(de.kinds[15])},'Scene kind');
 document.body.click();
 // The icon color source merges several lists.
 check(entityPickerEntries(OPTIONS,'icon_sources').map(e=>e.value).join()==='sensor.evil,light.desk,sensor.power,sensor.temp_kitchen,binary_sensor.door','Icon sources merge the lists, sorted by name: '+entityPickerEntries(OPTIONS,'icon_sources').map(e=>e.value).join());
 // Inserted tabs (folder fragments) get their pickers.
 loaded.filter(fn=>String(fn).includes('MutationObserver')).length||check(false,'Observer registered');
 $('later').innerHTML='<input type="hidden" id="f_switch_entity" data-entity-picker="switches">';
 await tick();
 check($('f_switch_entity_picker'),'A later field gets its picker');
 // A failed load offers a retry and keeps the value.
 failLoad=true;entityOptionsCache=null;
 $('f_switch_entity').value='light.desk';
 $('f_switch_entity_picker').click();await tick();await tick();
 const state=document.querySelector('.entity-picker-popover .entity-picker-state');
 check(state&&state.textContent.includes(${JSON.stringify(de.labels[3])})&&state.querySelector('.entity-picker-retry'),'Load failure: '+(state&&state.textContent));
 check($('f_switch_entity').value==='light.desk','The value stays after a failed load');
 failLoad=false;state.querySelector('.entity-picker-retry').click();await tick();await tick();
 check(document.querySelector('.entity-picker-popover .entity-picker-item.selected'),'Retry loads the list');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
let domChecked = false;
try {
  domChecked = runDomHarness({label: 'Entity picker', html: page, tmpPrefix: 'hometiles-entity-picker-',
    extraArgs: ['--virtual-time-budget=5000']}) !== false;
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 600));
}
// ---- With a Bridge that searches (paired panel) ----
const remotePage = `<!doctype html><html lang="de"><head><style>${read('src/web/assets/admin.css')}</style></head><body>
<div class="tile-settings" style="width:420px">
<div class="type-fields show"><label for="r_switch_entity_picker">Schalter</label><input type="hidden" id="r_switch_entity" data-entity-picker="switches"></div>
<div class="type-fields show"><label for="r_sensor_entity_picker">Sensor</label><input type="hidden" id="r_sensor_entity" data-entity-picker="sensors"></div>
</div><div id="web_auth_section"></div><pre id="result"></pre><script>
const loaded=[];const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,fn,...args)=>{if(name==='DOMContentLoaded')loaded.push(fn);else nativeListen(name,fn,...args);};
const APP_I18N=${JSON.stringify(Object.assign({}, appI18n, {entityPickerReleased: de.labels[7], webAuthSet: 'Passwort setzen'}))};
const ENTITY_KIND_LABELS=${JSON.stringify(de.kinds)};
const CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={0:{}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const OPTIONS={success:true,sensors:[{v:'sensor.kitchen',t:'Temperatur Küche'}],
  switches:[{v:'light.tab5_display_brightness',t:'Display'}]};
const searches=[];let bridge=true;
const ANSWERS={
  'switches|':{full:false,more:false,r:[{v:'light.desk',t:'Schreibtisch',i:'mdi:desk-lamp',a:'EG Büro',d:'Schreibtischlampe'}]},
  'switches|desk':{full:true,more:true,r:[{v:'light.desk',t:'Schreibtisch',i:'mdi:desk-lamp',a:'EG Büro',d:'Schreibtischlampe'}]},
  'switches|desk|1':{full:true,more:false,r:[{v:'light.desk_2',t:'Schreibtisch 2 Licht',a:'EG Büro',d:'Schreibtisch 2'}]},
  'sensors|sensor.kitchen':{full:false,more:false,r:[{v:'sensor.kitchen',t:'Temperatur Küche',i:'mdi:thermometer',a:'OG Küche',d:'Heizung'}]}
};
let shown=false;document.getElementById('web_auth_section').scrollIntoView=()=>{shown=true;};
window.fetch=async(url,init)=>{
  url=String(url);
  if(url==='/api/entity_search'&&init?.method==='POST'){
    const body=new URLSearchParams(init.body);searches.push(body.get('list')+'|'+body.get('q')+(body.get('o')?'|'+body.get('o'):''));
    return {ok:true,json:async()=>bridge?{success:true,bridge:true,id:searches.length}:{success:true,bridge:false}};
  }
  if(url.startsWith('/api/entity_search?id=')){
    const key=searches[Number(url.split('=')[1])-1];
    return {ok:true,json:async()=>Object.assign({success:true,ready:true},ANSWERS[key]||{full:false,more:false,r:[]})};
  }
  return {ok:true,json:async()=>OPTIONS};
};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};const $=id=>document.getElementById(id);
 const wait=ms=>new Promise(r=>setTimeout(r,ms));
 loaded.filter(fn=>String(fn).includes('setupEntityPickers(document)')).forEach(fn=>fn());
 await fetchEntityOptions();
 // A closed field learns area and device from the Bridge.
 $('r_sensor_entity').value='sensor.kitchen';await wait(400);
 check(searches.includes('sensors|sensor.kitchen'),'The field asks once: '+searches.join());
 check($('r_sensor_entity_picker').textContent.includes('OG Küche ▸ Heizung'),'Area ▸ device: '+$('r_sensor_entity_picker').textContent);
 // Open: the Bridge's released entities with area and device, the panel's own entities after them.
 $('r_switch_entity_picker').click();await wait(400);
 const pop=document.querySelector('.entity-picker-popover.open');
 let rows=[...pop.querySelectorAll('.entity-picker-item')];
 check(rows.length===2&&rows[0].textContent.includes('EG Büro ▸ Schreibtischlampe')&&rows[1].textContent.includes('Display'),'Bridge plus own: '+rows.map(r=>r.textContent).join(' | '));
 const foot=pop.querySelector('.entity-picker-foot');
 check(!foot.hidden&&foot.textContent.includes(${JSON.stringify(de.labels[7])}),'Without a password: the hint');
 // Typing searches the Bridge again after a pause; with the password every
 // entity, page by page while the list nears its end.
 const search=pop.querySelector('input');search.value='desk';search.dispatchEvent(new Event('input',{bubbles:true}));
 rows=[...pop.querySelectorAll('.entity-picker-item')];
 check(rows.length===0||rows.every(r=>!r.textContent.includes('Display')),'The own list filters right away');
 await wait(600);
 rows=[...pop.querySelectorAll('.entity-picker-item')];
 check(searches.filter(s=>s==='switches|desk').length===1,'One search after typing: '+searches.join());
 check(searches.includes('switches|desk|1'),'The next page from match 1: '+searches.join());
 check(rows.length===2&&foot.hidden&&!pop.querySelector('.entity-picker-more'),'Full search, both pages, no hint');
 // Like Home Assistant the list leaves out the device name in front.
 check(rows[1].querySelector('.entity-picker-name').textContent==='Licht'&&
   rows[1].querySelector('.entity-picker-context').textContent==='EG Büro ▸ Schreibtisch 2','Short name: '+rows[1].textContent);
 rows[1].click();
 check($('r_switch_entity').value==='light.desk_2'&&$('r_switch_entity_picker').textContent.includes('EG Büro'),'A Bridge entity is chosen with its area');
 // The hint opens the password section.
 $('r_switch_entity_picker').click();await wait(400);
 document.querySelector('.entity-picker-password').click();
 check(shown&&!document.querySelector('.entity-picker-popover.open'),'Set password opens the section');
 // Without a Bridge search the panel's own list stays.
 bridge=false;
 const before=searches.length;
 $('r_switch_entity_picker').click();await wait(400);
 rows=[...document.querySelectorAll('.entity-picker-popover .entity-picker-item')];
 check(rows.length===1&&rows[0].textContent.includes('light.tab5_display_brightness'),'Own list without Bridge search');
 $('r_switch_entity_picker').click();await wait(50);
 $('r_switch_entity_picker').click();await wait(400);
 check(searches.length===before+1,'No more searches once the panel has none');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
let remoteChecked = false;
try {
  remoteChecked = runDomHarness({label: 'Entity picker with Bridge search', html: remotePage,
    tmpPrefix: 'hometiles-entity-picker-remote-', extraArgs: ['--virtual-time-budget=8000']}) !== false;
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 600));
}
console.log(`Entity picker: server contract, translations${domChecked ? ', editor DOM' : ''}` +
  `${remoteChecked ? ', Bridge search' : ''} pass`);
