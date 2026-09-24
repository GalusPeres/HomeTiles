import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
const html=`<!doctype html><html lang="de"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-test" class="tile-tab"><div class="tile-grid tiles-bordered"><div class="tile clock" id="test-tile-0" data-index="0"></div><div class="tile text" id="test-tile-1" data-index="1"></div></div></div>
<div id="testSettings" class="tile-settings"><div class="tile-specific-settings"><select id="test_tile_type"><option value="9">Clock</option><option value="10">Text</option></select><input id="test_tile_title"><input id="test_tile_icon"><input type="color" id="test_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="test_tile_${n}" type="number">`).join('')}
<div class="type-fields" id="test_clock_fields"><input id="test_clock_tile_border" type="checkbox" checked><input id="test_clock_show_time" type="checkbox" checked><input id="test_clock_show_date" type="checkbox">${['time_font','date_font','time_format','date_format'].map(n=>`<input id="test_clock_${n}">`).join('')}</div>
<div class="type-fields" id="test_text_fields"><input id="test_text_tile_border" type="checkbox" checked><textarea id="test_text_value"></textarea><input id="test_text_value_font"></div>
</div></div><pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={9:{css:'clock',fields:'clock',preview:'clock',load:'loadClockFields',save:'saveClockFields',reset:'resetClockFields'},10:{css:'text',fields:'text',preview:'text',load:'loadTextFields',save:'saveTextFields',reset:'resetTextFields'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const posts=[];window.fetch=async(url,options)=>{if(options?.method==='POST'){posts.push(Object.fromEntries(typeof options.body==='string'?new URLSearchParams(options.body):options.body));return {json:async()=>({success:true})};}throw Error('Unexpected request '+url);};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 folderByTab.test=1;tileDataLoadedTabs.add('test');
 tilesData.test=[{type:9,col:0,row:0,span_w:1,span_h:1,title:'Clock',sensor_decimals:1},{type:10,col:1,row:0,span_w:2,span_h:1,title:'Text',scene_alias:'Hello'}];
 const check=(v,m)=>{if(!v)throw Error(m);};
 for(const [index,kind] of [[0,'clock'],[1,'text']]){
  selectTile(index,'test');const toggle=document.getElementById('test_'+kind+'_tile_border'),card=document.getElementById('test-tile-'+index);
  check(toggle.checked,'Legacy tiles inherit enabled borders');
  toggle.checked=false;toggle.dispatchEvent(new Event('change',{bubbles:true}));
  check(card.classList.contains('tile-border-hidden'),'Immediate preview hides the border');
  await new Promise(r=>setTimeout(r,350));
  check(posts.filter(p=>p.index===String(index)).at(-1)?.tile_border==='0','Autosave sends explicit disabled border');
  check(tilesData.test[index].sensor_display_mode===1,'Draft keeps the persisted flag');
  await postTile(1,index,tilesData.test[index]);
  check(posts.at(-1).tile_border==='0','Export/import retains the disabled border');
  renderTileFromData('test',index,tilesData.test[index],{});
  check(card.classList.contains('tile-border-hidden'),'Cached preview retains the disabled border');
  check(getComputedStyle(card).outlineStyle==='none','Global border does not override tile setting');
  const fd=new FormData();window[kind==='clock'?'saveClockFields':'saveTextFields']('test',fd);
  window[kind==='clock'?'resetClockFields':'resetTextFields']('test');check(toggle.checked,'Reset restores the default');
  window[kind==='clock'?'loadClockFields':'loadTextFields']('test',Object.fromEntries(fd));check(!toggle.checked,'Copy/paste draft reload retains disabled border');
  toggle.checked=true;toggle.dispatchEvent(new Event('change',{bubbles:true}));
  await new Promise(r=>setTimeout(r,350));
  check(tilesData.test[index].sensor_display_mode===0,'Re-enabling clears the stored override');
  card.classList.remove('active');check(getComputedStyle(card).outlineStyle==='solid','Re-enabled tile follows the global border');
 }
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try {runDomHarness({label:'Clock/Text tile borders',html,tmpPrefix:'hometiles-tile-border-',extraArgs:['--virtual-time-budget=3000']});}catch(error){throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1]||error.message.slice(0,300));}
