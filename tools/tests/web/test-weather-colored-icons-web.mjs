// Weather tile setting "Colored weather icons": default on, autosave sends the
// explicit choice, drafts, copy/paste and import keep it in the display mode
// byte (0 colored, 1 white outlines), and reset restores it.
import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
const html=`<!doctype html><html lang="de"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-test" class="tile-tab"><div class="tile-grid tiles-bordered"><div class="tile weather" id="test-tile-0" data-index="0"></div></div></div>
<div id="testSettings" class="tile-settings"><div class="tile-specific-settings"><select id="test_tile_type"><option value="12">Weather</option></select><input id="test_tile_title"><input id="test_tile_icon"><input type="color" id="test_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="test_tile_${n}" type="number">`).join('')}
<div class="tile-icon-disc-fields" id="test_tile_icon_disc_fields"><label class="inline-checkbox hidden" id="test_weather_colored_icons_row"><input id="test_weather_colored_icons" type="checkbox" checked></label></div>
<div class="type-fields" id="test_weather_fields"><select id="test_weather_entity"><option value=""></option><option value="weather.home">Home</option></select><select id="test_weather_popup_open_mode"><option value="0"></option><option value="1" selected></option></select></div>
</div></div><pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={12:{css:'weather',fields:'weather',preview:'weather',load:'loadWeatherFields',save:'saveWeatherFields',reset:'resetWeatherFields'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const posts=[];window.fetch=async(url,options)=>{if(options?.method==='POST'){posts.push(Object.fromEntries(typeof options.body==='string'?new URLSearchParams(options.body):options.body));return {json:async()=>({success:true})};}throw Error('Unexpected request '+url);};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 folderByTab.test=1;tileDataLoadedTabs.add('test');
 tilesData.test=[{type:12,col:0,row:0,span_w:2,span_h:2,title:'Weather',sensor_entity:'weather.home',sensor_display_mode:0}];
 const check=(v,m)=>{if(!v)throw Error(m);};
 selectTile(0,'test');const toggle=document.getElementById('test_weather_colored_icons');
 check(toggle.checked,'Existing weather tiles show colored icons');
 syncIconDiscFields('test');check(!document.getElementById('test_weather_colored_icons_row').classList.contains('hidden'),'The icon section shows the setting for weather tiles');
 toggle.checked=false;toggle.dispatchEvent(new Event('change',{bubbles:true}));
 await new Promise(r=>setTimeout(r,350));
 check(posts.filter(p=>p.index==='0').at(-1)?.weather_colored_icons==='0','Autosave sends the disabled colored icons');
 check(tilesData.test[0].sensor_display_mode===1,'Draft keeps the persisted flag');
 await postTile(1,0,tilesData.test[0]);
 check(posts.at(-1).weather_colored_icons==='0','Export/import retains the white outlines');
 const fd=new FormData();saveWeatherFields('test',fd);
 check(fd.get('weather_colored_icons')==='0','Save writes the choice');
 resetWeatherFields('test');check(toggle.checked,'Reset restores colored icons');
 loadWeatherFields('test',Object.fromEntries(fd));check(!toggle.checked,'Copy/paste draft reload retains the choice');
 loadWeatherFields('test',{sensor_entity:'weather.home',sensor_display_mode:0});check(toggle.checked,'Server data 0 is colored');
 loadWeatherFields('test',{sensor_entity:'weather.home',sensor_display_mode:1});check(!toggle.checked,'Server data 1 is white outlines');
 toggle.checked=true;toggle.dispatchEvent(new Event('change',{bubbles:true}));
 await new Promise(r=>setTimeout(r,350));
 check(tilesData.test[0].sensor_display_mode===0,'Re-enabling stores colored icons');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try {runDomHarness({label:'Weather colored icons setting',html,tmpPrefix:'hometiles-weather-icons-',extraArgs:['--virtual-time-budget=3000']});}catch(error){throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1]||error.message.slice(0,300));}
