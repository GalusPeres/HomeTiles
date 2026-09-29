// Tile color "Custom" on a tile that follows the global tile color: the
// color field starts with the default grey, and Custom must stay selected
// (regression: the live preview treated the default grey as Global, switched
// back and hid the color field, so Custom could not be selected and its
// picker opened in the top left corner of the page).
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import assert from 'node:assert/strict';

// Chrome anchors showPicker() to the box from the last layout. The color row
// was display:none until this click, so the input must be laid out first or
// the picker opens in the top left corner of the window (a script cannot see
// a pending layout, so the order is checked in the source).
const gridPreview = readRepoFile('src/web/admin/tiles/grid-preview.js');
const setMode = gridPreview.slice(gridPreview.indexOf('function setTileColorMode('));
assert.match(setMode.slice(0, setMode.indexOf('\n  }\n')),
  /syncTileColorMode\(tab\);[\s\S]*input\.getBoundingClientRect\(\);\s*if \(typeof input\.showPicker === 'function'\) input\.showPicker\(\);/,
  'the first Custom lays the shown color row out before opening the picker');

const html = `<!doctype html><html lang="de"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-test" class="tab-content tile-tab active"><div class="tile-editor"><div class="tile-editor-main"><div class="tile-grid tiles-bordered"><div class="tile weather" id="test-tile-0" data-index="0"></div></div></div>
<div id="testSettings" class="tile-settings"><div class="tile-specific-settings"><select id="test_tile_type"><option value="12">Weather</option></select><input id="test_tile_title"><input id="test_tile_icon">
${['col', 'row', 'span_w', 'span_h'].map((n) => `<input id="test_tile_${n}" type="number">`).join('')}
<div class="icon-color-segmented tile-color-modes" role="group" id="test_tile_color_modes"><button type="button" id="test_tile_color_mode_global" data-tile-color-mode="global" onclick="setTileColorMode('test','global')">Global</button><button type="button" id="test_tile_color_mode_custom" data-tile-color-mode="custom" onclick="setTileColorMode('test','custom')">Custom</button><button type="button" id="test_tile_color_mode_icon" data-tile-color-mode="icon" onclick="setTileColorMode('test','icon')">From icon</button></div>
<div class="tile-color-row no-reset color-hidden" id="test_tile_color_row"><input type="color" id="test_tile_color" value="#1A1A1A"></div>
<input type="checkbox" id="test_tile_icon_fill" hidden>
<div class="type-fields" id="test_weather_fields"><select id="test_weather_entity"><option value="weather.home">Home</option></select><select id="test_weather_popup_open_mode"><option value="1" selected></option></select></div>
</div></div></div></div><pre id="result"></pre><script>
const errors=[];window.addEventListener('error',e=>errors.push(String(e.message)));
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={12:{css:'weather',fields:'weather',preview:'weather',load:'loadWeatherFields',save:'saveWeatherFields',reset:'resetWeatherFields',defaultBg:'#1A1A1A'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
window.fetch=async()=>({ok:true,json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 folderByTab.test=1;tileDataLoadedTabs.add('test');
 tilesData.test=[{type:12,col:0,row:0,span_w:2,span_h:2,title:'Weather',sensor_entity:'weather.home',bg_color:0}];
 selectTile(0,'test');
 const input=document.getElementById('test_tile_color');
 check(tileColorMode('test')==='global','A tile without its own color starts on Global');
 check(isDefaultTileGrey(parseInt(input.value.slice(1),16)),'its color field holds the default grey');
 let picker=0;input.showPicker=()=>{picker++;check(!document.getElementById('test_tile_color_row').classList.contains('color-hidden'),'the picker opens at a visible color field');};
 document.getElementById('test_tile_color_mode_custom').click();
 check(picker===1,'the first Custom opens the picker');
 for(const ms of [50,400,1200]){
  await new Promise(r=>setTimeout(r,ms));
  check(tileColorMode('test')==='custom','Custom stays selected after '+ms+' ms');
  check(!document.getElementById('test_tile_color_row').classList.contains('color-hidden'),'the color field stays visible');
 }
 updateTilePreview('test');
 check(tileColorMode('test')==='custom','a preview update keeps Custom');
 input.value='#3366AA';input.dispatchEvent(new Event('input',{bubbles:true}));
 check(tileColorMode('test')==='custom','a picked color keeps Custom');
 document.getElementById('test_tile_color_mode_global').click();
 check(tileColorMode('test')==='global','Global still switches back');
 check(errors.length===0,'no script errors: '+errors.join('; '));
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try { runDomHarness({label: 'Custom tile color on the default grey', html, tmpPrefix: 'hometiles-custom-grey-', extraArgs: ['--virtual-time-budget=4000']}); }
catch (error) { throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 300)); }
