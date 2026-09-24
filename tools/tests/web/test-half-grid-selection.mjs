import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
const html=`<!doctype html><html lang="de"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-folder0" class="tile-tab tab-content active"><div class="tile-grid">
<div class="tile" id="folder0-tile-0" data-type="20" data-col="1.5" data-row="0" data-span-w="1.5" data-span-h="0.5"></div>
<div class="tile" id="folder0-tile-1" data-type="1" data-col="3" data-row="0" data-span-w="1.5" data-span-h="0.5"></div></div></div>
<div id="folder0Settings" class="tile-settings"><div class="tile-specific-settings">
<select id="folder0_tile_type"><option value="1">Sensor</option><option value="20">Binary</option><option value="17">Climate</option></select>
<input id="folder0_tile_title"><input id="folder0_tile_icon"><input type="color" id="folder0_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="folder0_tile_${n}" type="number" step="0.5">`).join('')}
<div class="type-fields" id="folder0_sensor_fields"><select id="folder0_sensor_entity"><option value=""></option><option value="sensor.upstairs">Upstairs</option></select><input id="folder0_sensor_value_font"></div>
<div class="type-fields" id="folder0_binary_sensor_fields"><select id="folder0_binary_sensor_entity"><option value=""></option><option value="binary_sensor.desk">Desk</option></select><select id="folder0_binary_sensor_value_font"><option value="0">Default</option><option value="1">20</option><option value="2">24</option><option value="3">32</option><option value="4">40</option></select></div>
<div class="type-fields" id="folder0_climate_fields"></div>
</div></div><pre id="result"></pre><script>
// Skip unrelated boot/network timers; execute the complete delivered editor below.
const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={1:{fields:'sensor',preview:'sensor',load:'loadSensorFields',save:'saveSensorFields',reset:'resetSensorFields'},17:{fields:'climate',preview:'climate',load:'loadClimateFields',save:'saveClimateFields',reset:'resetClimateFields'},20:{fields:'binary_sensor',preview:'binary_sensor',load:'loadBinarySensorFields',save:'saveBinarySensorFields',reset:'resetBinarySensorFields'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const posts=[];window.fetch=async(url,options)=>{if(options?.method==='POST'){posts.push(Object.fromEntries(options.body));return {json:async()=>({success:true})};}throw Error('Unexpected network access '+url);};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 folderByTab.folder0=0;tileDataLoadedTabs.add('folder0');
 tilesData.folder0=[{type:20,col:1.5,row:0,span_w:1.5,span_h:.5,title:'Desk',sensor_entity:'binary_sensor.desk'}, {type:1,col:3,row:0,span_w:1.5,span_h:.5,title:'Upstairs',sensor_entity:'sensor.upstairs',sensor_value_font:3}];
 const check=(v,m)=>{if(!v)throw Error(m);};
 for(let cycle=0;cycle<3;cycle++){
   selectTile(0,'folder0');
   check(document.getElementById('folder0_binary_sensor_fields').classList.contains('show'),'Binary fields selected');
   selectTile(1,'folder0');
   check(document.getElementById('folder0_sensor_fields').classList.contains('show'),'Sensor fields selected');
   check(!document.getElementById('folder0_binary_sensor_fields').classList.contains('show'),'Previous binary fields hidden');
   check(document.getElementById('folder0_tile_col').value==='4','Position belongs to selected sensor');
   check(document.getElementById('folder0_sensor_entity').value==='sensor.upstairs','Correct selected entity');
 }
 const title=document.getElementById('folder0_tile_title');title.value='Upstairs changed';title.dispatchEvent(new Event('input',{bubbles:true}));
 const font=document.getElementById('folder0_sensor_value_font');font.value='4';font.dispatchEvent(new Event('change',{bubbles:true}));
 selectTile(0,'folder0');await new Promise(resolve=>setTimeout(resolve,350));
 check(posts.length===1 && posts[0].index==='1' && posts[0].title==='Upstairs changed','Autosave stays bound to the edited tile after switching');
 check(posts[0].sensor_value_font==='4','Edited value font is saved with the correct tile');
 check(posts[0].col==='3' && posts[0].span_w==='1.5' && posts[0].span_h==='0.5','Autosave keeps the selected tile geometry');
 selectTile(1,'folder0');check(title.value==='Upstairs changed','Edited title survives switching back');
 check(font.value==='4','Edited value font survives switching back');
 selectTile(0,'folder0');
 const binaryFont=document.getElementById('folder0_binary_sensor_value_font');
 check(binaryFont.value==='0','Legacy binary uses default font');
 binaryFont.value='2';binaryFont.dispatchEvent(new Event('change',{bubbles:true}));
 const binaryValue=()=>document.querySelector('#folder0-tile-0 .tile-binary-sensor-value');
 check(binaryValue().classList.contains(getSensorValueFontClass(2)),'Live binary font updates');
 check(String(getTileSnapshotForSave('folder0',0).sensor_value_font)==='2','Binary draft includes font');
 selectTile(1,'folder0');await new Promise(resolve=>setTimeout(resolve,350));
 check(posts.length===2 && posts[1].index==='0' && posts[1].sensor_value_font==='2','Binary font autosave owns correct tile');
 selectTile(0,'folder0');check(binaryFont.value==='2','Binary font survives selection');
 renderTileFromData('folder0',0,tilesData.folder0[0],sensorMetaCache);
 check(binaryValue().classList.contains(getSensorValueFontClass(2)),'Cached binary preview preserves font');
 for(const choice of ['1','3','4','0']){
   loadBinarySensorFields('folder0',{sensor_entity:'binary_sensor.desk',sensor_value_font:Number(choice)});
   const payload=new FormData();saveBinarySensorFields('folder0',payload);
   check(payload.get('sensor_value_font')===choice,'Load/save roundtrip '+choice);
 }
 resetBinarySensorFields('folder0');check(binaryFont.value==='0','Reset restores original default');
 document.body.dataset.result='pass';document.getElementById('result').textContent='Full delivered editor selection passed';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try { runDomHarness({label:'Half-grid tile selection',html,tmpPrefix:'hometiles-selection-',extraArgs:['--virtual-time-budget=2000']}); }
catch(error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0,200)); }
