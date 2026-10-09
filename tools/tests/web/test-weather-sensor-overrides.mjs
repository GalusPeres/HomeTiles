// Weather temperature/humidity sensors (Zeid 2026-10-09): a Weather tile can
// show a chosen outdoor sensor instead of the weather entity's current
// temperature and add a humidity value; the forecast stays the weather
// entity's. Runs the real delivered Web Admin preview, autosave, save, load,
// reset and import paths in Chromium, and checks the firmware storage, MQTT
// routing, runtime override and translations that carry the same field names.
import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import assert from 'node:assert/strict';

const sensorSelect = field => `<select id="test_${field}"><option value=""></option>` +
  '<option value="sensor.outdoor_temperature">Outdoor temperature</option>' +
  '<option value="sensor.outdoor_humidity">Outdoor humidity</option></select>';
const html=`<!doctype html><html lang="en"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-test" class="tile-tab"><div class="tile-grid tiles-bordered"><div class="tile weather" id="test-tile-0" data-index="0"></div></div></div>
<div id="testSettings" class="tile-settings"><div class="tile-specific-settings"><select id="test_tile_type"><option value="12">Weather</option></select><input id="test_tile_title"><input id="test_tile_icon"><input type="color" id="test_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="test_tile_${n}" type="number">`).join('')}
<div class="tile-icon-disc-fields" id="test_tile_icon_disc_fields"><label class="inline-checkbox hidden" id="test_weather_colored_icons_row"><input id="test_weather_colored_icons" type="checkbox" checked></label></div>
<div class="type-fields" id="test_weather_fields"><select id="test_weather_entity"><option value=""></option><option value="weather.home">Home</option></select>
${sensorSelect('weather_temperature_sensor')}${sensorSelect('weather_humidity_sensor')}
<select id="test_weather_popup_open_mode"><option value="0"></option><option value="1" selected></option></select></div>
</div></div><pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={12:{css:'weather',fields:'weather',preview:'weather',load:'loadWeatherFields',save:'saveWeatherFields',reset:'resetWeatherFields'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const posts=[];window.fetch=async(url,options)=>{if(options?.method==='POST'){posts.push(Object.fromEntries(typeof options.body==='string'?new URLSearchParams(options.body):options.body));return {json:async()=>({success:true})};}throw Error('Unexpected request '+url);};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const payload=JSON.stringify({state:'partlycloudy',temperature:74,units:{temperature:'°F'},
   forecast:[{datetime:'2026-10-10',temperature:80,templow:60,condition:'sunny'}]});
 const values={'sensor.outdoor_temperature':'68.2','sensor.outdoor_humidity':'45,6','sensor.dead':'unavailable'};

 // Override helper: the sensors replace the current temperature and add a
 // humidity value; a non-numeric sensor state shows "--"; the forecast stays.
 const none=applyWeatherSensorOverrides(parseWeatherPreviewPayload(payload),'','',values);
 check(none.temperature===74&&none.humidity===undefined,'Without sensors the weather entity value stays');
 const both=applyWeatherSensorOverrides(parseWeatherPreviewPayload(payload),'sensor.outdoor_temperature','sensor.outdoor_humidity',values);
 check(both.temperature===68.2,'The temperature sensor replaces the current temperature');
 check(both.humidity==='46 %','The humidity sensor adds a rounded humidity: '+both.humidity);
 check(both.forecast.length===1&&both.forecast[0].temperature===80,'The forecast stays the weather entity forecast');
 const dead=applyWeatherSensorOverrides(parseWeatherPreviewPayload(payload),'sensor.dead','sensor.missing',values);
 check(dead.temperature===null&&dead.humidity==='--','Missing or unavailable sensors show --');
 check(applyWeatherSensorOverrides(null,'sensor.outdoor_temperature','',values)===null,'A missing weather state stays missing');

 folderByTab.test=1;tileDataLoadedTabs.add('test');
 sensorMetaCache.weatherValues={'weather.home':payload};
 Object.assign(sensorMetaCache.values,values);
 tilesData.test=[{type:12,col:0,row:0,span_w:2,span_h:2,title:'Weather',sensor_entity:'weather.home',sensor_display_mode:0,
   weather_temperature_sensor:'',weather_humidity_sensor:''}];
 // WEATHER_TILE_LAYOUT comes from the firmware (web_scripts.cpp), so record
 // the state the real preview paths hand to the tile renderer.
 const shown=[];applyWeatherPreview=(el,state)=>shown.push(state);
 const last=()=>shown.at(-1)||{};
 selectTile(0,'test');updateTilePreview('test');
 check(last().temperature===74&&last().humidity===undefined,'Live preview starts with the weather entity temperature');

 // Choosing sensors updates the live preview, the draft and autosave.
 for(const [field,value] of [['weather_temperature_sensor','sensor.outdoor_temperature'],['weather_humidity_sensor','sensor.outdoor_humidity']]){
   const select=document.getElementById('test_'+field);select.value=value;select.dispatchEvent(new Event('change',{bubbles:true}));
 }
 check(last().temperature===68.2&&last().humidity==='46 %','Live preview shows the sensors: '+JSON.stringify(last()));
 check(last().forecast?.length===1,'Live preview keeps the forecast');
 await new Promise(r=>setTimeout(r,350));
 const post=posts.filter(p=>p.index==='0').at(-1);
 check(post?.weather_temperature_sensor==='sensor.outdoor_temperature'&&post?.weather_humidity_sensor==='sensor.outdoor_humidity','Autosave sends both sensors');
 check(tilesData.test[0].weather_temperature_sensor==='sensor.outdoor_temperature','Draft keeps the temperature sensor');

 // Cached grid preview from stored tile data.
 shown.length=0;renderTileFromData('test',0,tilesData.test[0],sensorMetaCache);
 check(last().temperature===68.2&&last().humidity==='46 %','Grid preview shows the sensors: '+JSON.stringify(last()));

 // Export/import keeps the sensors.
 await postTile(1,0,tilesData.test[0]);
 check(posts.at(-1).weather_temperature_sensor==='sensor.outdoor_temperature'&&posts.at(-1).weather_humidity_sensor==='sensor.outdoor_humidity','Import posts both sensors');

 // Save -> snapshot/clipboard -> load -> reset use the same field names.
 const fd=new FormData();saveWeatherFields('test',fd);
 check(fd.get('weather_temperature_sensor')==='sensor.outdoor_temperature'&&fd.get('weather_humidity_sensor')==='sensor.outdoor_humidity','Save writes both sensors');
 const sel=f=>document.getElementById('test_'+f).value;
 resetWeatherFields('test');check(!sel('weather_temperature_sensor')&&!sel('weather_humidity_sensor'),'Reset clears both sensors');
 loadWeatherFields('test',Object.fromEntries(fd));check(sel('weather_temperature_sensor')==='sensor.outdoor_temperature'&&sel('weather_humidity_sensor')==='sensor.outdoor_humidity','Load restores both sensors');
 loadWeatherFields('test',{sensor_entity:'weather.home',weather_temperature_sensor:'sensor.offline_t'});
 check(sel('weather_temperature_sensor')==='sensor.offline_t','A configured sensor missing from the list is kept');
 check(sel('weather_humidity_sensor')==='','An absent humidity sensor loads empty');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try {runDomHarness({label:'Weather temperature/humidity sensors',html,tmpPrefix:'hometiles-weather-sensors-',extraArgs:['--virtual-time-budget=3000']});}catch(error){throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1]||error.message.slice(0,300));}

// Editor bindings, option lists, import and the cached grid preview.
const delivered = readAdminDeliverySource();
const editor = readRepoFile('src/web/admin/tiles/editor.js');
for (const name of ['weatherTemperatureSensor', 'weatherHumiditySensor']) {
  assert.ok(editor.includes(`'${name}'`), `editor binds ${name}`);
}
for (const field of ['weather_temperature_sensor', 'weather_humidity_sensor']) {
  assert.ok(readRepoFile('src/web/admin/tiles/registry.js').includes(`rebuildEntitySelect(tab + '_${field}', data.sensors);`));
  assert.ok(readRepoFile('src/web/admin/tiles/import-export.js').includes(`fd.append('${field}', tile.${field} || '');`));
  assert.ok(delivered.includes(field), `delivered admin.js carries ${field}`);
}

// Firmware: request fields, JSON, sidecar storage, routes, runtime override.
const handler = readRepoFile('src/types/weather/web_handler.cpp');
assert.ok(handler.includes('server.hasArg("weather_temperature_sensor")'));
assert.ok(handler.includes('server.hasArg("weather_humidity_sensor")'));
assert.ok(handler.includes('normalizeWeatherSensors(tile);'));
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tiles.includes('\\"weather_temperature_sensor\\":'));
const config = readRepoFile('src/tiles/config/tile_config.cpp');
for (const needle of [
  'static const char* kWeatherSensorPathDir = "/_tile_weather_sensors";',
  'scanSidecarDir(kWeatherSensorPathDir, g_weather_sensor_sidecar_keys);',
  'applyWeatherSensorsFromSd(folder_id, grid);',
  'writeWeatherSensorsSd(id, i, "");',
  'normalizeWeatherSensors(working.tiles[i]);',
  'e->extra_entities[i] = psramStrdupLocal(slots[i].extra_entities);'
]) {
  assert.ok(config.includes(needle), `tile_config.cpp: ${needle}`);
}
assert.ok(readRepoFile('src/tiles/config/tile_config.h').includes(
  'a.weather_temperature_sensor == b.weather_temperature_sensor'));
assert.ok(readRepoFile('src/network/mqtt/mqtt_handlers.cpp').includes(
  'for (const char* extra = slot.extra_entities; *extra;)'));
const renderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('weather_sensors::apply_temperature(tile.weather_temperature_sensor, temperature, has_temp);'));
assert.ok(renderer.includes('weather_sensors::append_humidity(tile.weather_humidity_sensor, temp_text);'));
const popup = readRepoFile('src/ui/popups/weather/weather_popup.cpp');
assert.ok(popup.includes('weather_sensors::apply_temperature(ctx->temperature_sensor, temperature, has_temp);'));
assert.ok(popup.includes('void queue_weather_popup_sensor_refresh(const char* entity_id)'));
assert.ok(readRepoFile('src/types/weather/renderer.cpp').includes('init.humidity_sensor = data->humidity_sensor;'));
const dispatch = readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.ok(dispatch.includes('tile.weather_temperature_sensor.equalsIgnoreCase(entity_id)'));
assert.ok(dispatch.includes('queue_weather_popup_sensor_refresh(entity_id);'));
assert.ok(!dispatch.includes('climate_temperature_sensor'), 'Climate tiles stay unchanged');

// Every language has the three labels before its Climate tile type label.
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const locales = [...i18n.matchAll(/static const LocaleProfile kLocale(\w+) = \{/g)].map(match => match[1]);
assert.ok(locales.length >= 4);
for (const locale of locales) {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${locale} = {`);
  const end = i18n.indexOf('static const LocaleProfile kLocale', start + 10);
  const block = i18n.slice(start, end < 0 ? undefined : end);
  assert.match(block, /\n    \{"[^"\n]+", "[^"\n]+", "[^"\n]+"\},\n    "[^"\n]+",\n    "[^"\n]+",\n/,
    `${locale}: weather_sensor_labels precede the Climate tile type`);
}
assert.ok(readRepoFile('src/types/weather/web_html.cpp').includes('i18n::weather_sensor_label(language, field)'));

console.log('Weather temperature/humidity sensors: preview, editor, storage, routing and translations');
