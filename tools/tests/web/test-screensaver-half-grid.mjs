import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
const fixture=(valueType)=>`<!doctype html><html lang="de"><head><style>:root {--grid-cols:7;--grid-rows:5;--preview-cell-w:100px;--preview-cell-h:90px;--preview-gap:12px;--preview-pad:12px;--screensaver-image-inset:8px;--tile-radius:16px;--compact-inset:2px;--compact-title-font:12px;--compact-title-line:16px;--compact-value-font:12px;--compact-value-line:16px;--compact-text-gap:0px;--compact-value-line-32:20px;--icon-size:20px;} ${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-screensaver" class="tile-tab tab-content active"><div id="screensaverGrid" class="tile-grid screensaver-tile-grid"><div class="screensaver-grid-image-frame"><img id="screensaverPreviewImage" hidden></div><div id="screensaverClock" class="screensaver-grid-clock"></div>
<div class="tile" id="screensaver-tile-0" data-index="0" onclick="selectTile(0,'screensaver')" data-type="20" data-col="1.5" data-row="0" data-span-w="1.5" data-span-h="0.5"></div>
<div class="tile" id="screensaver-tile-1" data-index="1" onclick="selectTile(1,'screensaver')" data-type="1" data-col="3" data-row="0" data-span-w="1.5" data-span-h="0.5"></div></div></div>
<div id="screensaverSettings" class="tile-settings"><div class="tile-specific-settings">
<select id="screensaver_tile_type"><option value="1">Sensor</option><option value="14">Energy</option><option value="20">Binary</option><option value="17">Climate</option></select>
<input id="screensaver_tile_title"><input id="screensaver_tile_icon"><input type="color" id="screensaver_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="screensaver_tile_${n}" type="number" step="0.5">`).join('')}
<div class="type-fields" id="screensaver_sensor_fields"><select id="screensaver_sensor_entity"><option value=""></option><option value="sensor.upstairs">Upstairs</option></select><input id="screensaver_sensor_value_font"></div>
<div class="type-fields" id="screensaver_binary_sensor_fields"><select id="screensaver_binary_sensor_entity"><option value=""></option><option value="binary_sensor.desk">Desk</option></select><select id="screensaver_binary_sensor_value_font"><option value="0">Default</option><option value="1">20</option><option value="2">24</option><option value="3">32</option><option value="4">40</option></select></div>
<div class="type-fields" id="screensaver_energy_fields"><select id="screensaver_energy_entity"><option value=""></option><option value="sensor.upstairs">Upstairs</option></select><input id="screensaver_energy_value_font"></div>
<div class="type-fields" id="screensaver_climate_fields"></div>
</div></div><pre id="result"></pre><script>
// Skip unrelated boot/network timers; execute the complete delivered editor below.
const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={14:{css:'energy',fields:'energy',preview:'sensor',load:'loadEnergyFields',save:'saveEnergyFields',reset:'resetEnergyFields'},1:{fields:'sensor',preview:'sensor',load:'loadSensorFields',save:'saveSensorFields',reset:'resetSensorFields'},17:{fields:'climate',preview:'climate',load:'loadClimateFields',save:'saveClimateFields',reset:'resetClimateFields'},20:{fields:'binary_sensor',preview:'binary_sensor',load:'loadBinarySensorFields',save:'saveBinarySensorFields',reset:'resetBinarySensorFields'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const posts=[];window.fetch=async(url,options)=>{if(options?.method==='POST'){posts.push(Object.fromEntries(typeof options.body==='string'?new URLSearchParams(options.body):options.body));return {json:async()=>({success:true})};}throw Error('Unexpected network access '+url);};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 folderByTab.screensaver=65535;tileDataLoadedTabs.add('screensaver');
 tilesData.screensaver=[{type:20,col:1.5,row:4.5,span_w:1.5,span_h:.5,title:'Desk',icon_name:'thermometer',sensor_entity:'binary_sensor.desk'}, {type:${valueType},col:3,row:4,span_w:2,span_h:1,title:'Upstairs',icon_name:'thermometer',sensor_entity:'sensor.upstairs',sensor_value_font:3}];
 const check=(v,m)=>{if(!v)throw Error(m);};
 layoutTiles('screensaver',tilesData.screensaver);
 selectTile(0,'screensaver');
 check(document.getElementById('screensaver_tile_row').value==='5.5','Selecting the last half-row must not move the tile up');
 check(tilesData.screensaver[0].row===4.5,'Selection preserves cached bottom-half position');
 const snapshot=normalizeSnapshotLayout({type:20,col:'2.5',row:'5.5',span_w:'1.5',span_h:'0.5'},0,'screensaver');
 check(snapshot.row===4.5,'Save snapshot preserves the last half-row');
 check(document.getElementById('screensaver_tile_row').max==='5.5','Row input permits the last half-row');
 bindScreensaverEditor();enableTileResize('screensaver');enableTileDrag('screensaver');
 const card=document.getElementById('screensaver-tile-1');
 renderTileFromData('screensaver',1,tilesData.screensaver[1],sensorMetaCache);layoutTiles('screensaver',tilesData.screensaver);
 for(const selector of ['.tile-title','.tile-value']){
   const child=card.querySelector(selector), bounds=child.getBoundingClientRect();
   const target=document.elementFromPoint(bounds.left+bounds.width/2,bounds.top+bounds.height/2);
   check(target?.closest('.tile')===card,'Text hit belongs to its tile: '+selector+' '+JSON.stringify(bounds.toJSON())+' target '+target?.outerHTML?.slice(0,160));
   target.dispatchEvent(new MouseEvent('click',{bubbles:true,composed:true}));
   check(currentTileIndex===1 && screensaverSelected.kind==='tile','Clicking tile text must not select the wallpaper after the editor rebuilds it');
 }

 check(currentTileIndex===1 && screensaverSelected.kind==='tile','Screensaver tile click selects its own editor');
 const metrics=getTileGridMetrics('screensaver');
 const x=metrics.rect.left+metrics.padLeft+(metrics.cellW+metrics.gapX)*4.1;
 const y=metrics.rect.top+metrics.padTop+(metrics.cellH+metrics.gapY)*4.1;
 const handle=card.querySelector('.tile-resize-handle-se');
 handle.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,clientX:x,clientY:y,pointerId:1}));
 window.dispatchEvent(new PointerEvent('pointermove',{clientX:x,clientY:y,pointerId:1}));
 check(resizeState?.lastValidLayout.span_w===1.5 && resizeState.lastValidLayout.span_h===.5,'Corner resize goes directly from 2x1 to 1.5x0.5');
 window.dispatchEvent(new PointerEvent('pointerup',{clientX:x,clientY:y,pointerId:1}));
 check(tilesData.screensaver[1].span_w===1.5 && tilesData.screensaver[1].span_h===.5,'Resize release commits half geometry');
 for(const index of [0,1]){
   const half=document.getElementById('screensaver-tile-'+index);
   renderTileFromData('screensaver',index,tilesData.screensaver[index],sensorMetaCache);layoutTiles('screensaver',tilesData.screensaver);
   for(const selector of ['.tile-icon','.tile-title','.tile-value']){
     const child=half.querySelector(selector), bounds=child.getBoundingClientRect();
     const target=document.elementFromPoint(bounds.left+bounds.width/2,bounds.top+bounds.height/2);
     check(target?.closest('.tile')===half,'Half tile hit test: '+selector);
     target.dispatchEvent(new MouseEvent('click',{bubbles:true,composed:true}));
     check(currentTileIndex===index && screensaverSelected.kind==='tile','Half tile child click preserves selection: '+selector);
   }
 }
 const wallpaper=document.querySelector('.screensaver-grid-image-frame');
 wallpaper.dispatchEvent(new MouseEvent('click',{bubbles:true,composed:true}));
 check(currentTileIndex===-1 && screensaverSelected.kind==='background','Wallpaper remains selectable');
 card.querySelector('.tile-title').dispatchEvent(new MouseEvent('click',{bubbles:true,composed:true}));
 check(currentTileIndex===1,'Tile remains selectable after wallpaper selection');
 const rect=card.getBoundingClientRect();
 check(document.elementFromPoint(rect.left+rect.width/2,rect.top+rect.height/2)?.closest('.tile')===card,'Small tile is reachable above the wallpaper');
 const transfer=new DataTransfer();
 card.dispatchEvent(new DragEvent('dragstart',{bubbles:true,dataTransfer:transfer,clientX:rect.left+10,clientY:rect.top+10}));
 const dropY=metrics.rect.top+metrics.padTop+(metrics.cellH+metrics.gapY)*4.6;
 const grid=getTileGrid('screensaver');
 grid.dispatchEvent(new DragEvent('dragover',{bubbles:true,dataTransfer:transfer,clientX:rect.left+10,clientY:dropY}));
 grid.dispatchEvent(new DragEvent('drop',{bubbles:true,dataTransfer:transfer,clientX:rect.left+10,clientY:dropY}));
 card.dispatchEvent(new DragEvent('dragend',{bubbles:true,dataTransfer:transfer}));
 await new Promise(resolve=>setTimeout(resolve,350));
 card.click();
 check(tilesData.screensaver[1].row===4.5,'Dragging then reselecting preserves the bottom half-row');
 const title=document.getElementById('screensaver_tile_title');title.value='Changed';title.dispatchEvent(new Event('input',{bubbles:true}));
 await new Promise(resolve=>setTimeout(resolve,350));
 const saved=posts.filter(post=>post.index==='1').at(-1);
 check(saved?.row==='4.5' && saved?.span_w==='1.5' && saved?.span_h==='0.5','Autosave preserves dragged and resized geometry');
 check(saved?.sensor_entity==='sensor.upstairs' && saved?.sensor_value_font==='3','Energy/Sensor fields survive half-grid autosave');
 check(card.classList.contains('sensor-compact') && card.querySelector('.sensor-value-size-32'),'Live compact preview retains selected font');
 check(posts.some(post=>post.target_row==='4.5'),'Reorder sends the half-row to the device');
 document.body.dataset.result='pass';document.getElementById('result').textContent='Screensaver half-grid interactions passed';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
for (const type of [1,14]) try { runDomHarness({label:'Screensaver half-grid type '+type,html:fixture(type),tmpPrefix:'hometiles-screensaver-half-',extraArgs:['--virtual-time-budget=2000','--window-size=1280,1000']}); }
catch(error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0,200)); }
