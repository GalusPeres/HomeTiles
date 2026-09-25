// Web Admin previews show the icon discs like the device: taller tiles get a
// disc of round_diameter() centered on the unchanged icon, glow tints it with
// the icon color, and the global option and per-tile override hide it. Scene
// tiles resolve their icon through the scene alias -> entity map, as the
// device does; before, the preview had no such map and showed no scene icon.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
// Source contract: device constants reach the preview; the server supplies the map.
assert.ok(read('src/web/server/render/web_admin_styles.cpp').includes('emit_exact("icon-disc-round", tile_icon_disc::round_diameter());'));
const tiles = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /json \+= ",\\"scene_entities\\":\{";\s*bool first_scene = true;\s*for \(const auto& scene : parseSceneList\(ha\.scene_alias_text\)\)/);
const html = read('src/web/server/render/web_admin_html.cpp');
assert.match(html, /if \(tile\.type == TILE_SCENE && !iconName\.length\(\) &&[\s\S]*?haBridgeConfig\.findEntityIcon\(scene_entity\)/,
  'Server-rendered scene tiles use the scene entity icon');
assert.match(html, /tile_icon_disc::icon_color_tints\(binary_sensor_visual_color\(binary_sensor_state\)\)/);

const page = `<!doctype html><html lang="en"><head><style>${readRepoFile('src/web/assets/admin.css')}
:root{--icon-size:24px;--icon-disc-round:30px;--compact-inset:2px;--tile-radius:11px;--icon-disc-opa:0.149;--icon-disc-glow:20%;--preview-cell-w:84px;--preview-cell-h:72px;--preview-gap:5px;}
</style></head><body>
<div id="tab-tiles-test" class="tile-tab"><div class="tile-grid">
<div class="tile" id="test-tile-0" data-index="0" style="width:84px;height:72px"></div>
<div class="tile" id="test-tile-1" data-index="1" style="width:84px;height:72px"></div>
<div class="tile" id="test-tile-2" data-index="2" style="width:84px;height:72px"></div></div></div>
<div id="testSettings"><select id="test_tile_type"><option value="1">Sensor</option><option value="2">Scene</option></select>
<textarea id="test_tile_title"></textarea><input id="test_tile_icon"><input type="color" id="test_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="test_tile_${n}" value="1">`).join('')}
<select id="test_tile_icon_disc"><option value="0">G</option><option value="1">On</option><option value="2">Off</option></select>
<input type="checkbox" id="test_tile_icon_glow" checked><select id="test_scene_alias"><option value="tv">TV</option></select></div>
<pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={0:{},1:{css:'sensor',fields:'sensor',preview:'sensor',sharedBg:true,defaultBg:'#2A2A2A'},2:{css:'scene',fields:'scene',preview:'none',sharedBg:true,defaultBg:'#2A2A2A'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
window.fetch=async()=>({json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const meta=normalizeSensorMetaPayload({values:{},icons:{'scene.tv':'mdi:television','sensor.t':'mdi:thermometer'},scene_entities:{tv:'scene.tv'}});
 sensorMetaCache=meta;
 const disc=el=>getComputedStyle(el.querySelector('.tile-icon'),'::after');
 // Cached grid: a scene tile without an icon shows its scene entity's icon.
 renderTileFromData('test',0,{type:2,title:'TV',scene_alias:'tv',icon_name:''},meta);
 const scene=document.getElementById('test-tile-0');
 check(scene.querySelector('.tile-icon.mdi-television'),'Scene tile resolves the scene entity icon');
 check(disc(scene).display!=='none'&&disc(scene).width==='30px'&&disc(scene).height==='30px','Taller tile disc uses round_diameter');
 const icon=scene.querySelector('.tile-icon').getBoundingClientRect();
 check(Math.abs(icon.width/2-parseFloat(disc(scene).left))<0.6,'Disc is centered on the icon');
 const white=c=>c.startsWith('rgba(255, 255, 255, 0.1')&&Math.abs(parseFloat(c.split(',')[3])-0.149)<0.01;
 check(white(disc(scene).backgroundColor),'White icon keeps the white disc at the device opacity');
 // A colored icon tints its disc; glow off keeps it white.
 renderTileFromData('test',1,{type:1,title:'T',sensor_entity:'sensor.t',icon_name:'thermometer'},meta);
 const sensor=document.getElementById('test-tile-1');
 sensor.querySelector('.tile-icon').style.color='rgb(255, 213, 79)';applyIconDiscTint(sensor);
 check(sensor.querySelector('.tile-icon').classList.contains('tile-icon-tinted'),'Colored icon is tinted');
 check(!white(disc(sensor).backgroundColor),'Tinted disc uses the icon hue');
 renderTileFromData('test',1,{type:1,title:'T',sensor_entity:'sensor.t',icon_name:'thermometer',icon_glow:0},meta);
 sensor.querySelector('.tile-icon').style.color='rgb(255, 213, 79)';applyIconDiscTint(sensor);
 check(!sensor.querySelector('.tile-icon').classList.contains('tile-icon-tinted'),'Glow off keeps the white disc');
 // Per-tile Off and the global option.
 renderTileFromData('test',1,{type:1,title:'T',sensor_entity:'sensor.t',icon_name:'thermometer',icon_disc:2},meta);
 check(disc(sensor).display==='none','Per-tile Off hides the disc');
 document.documentElement.classList.add('icon-discs-off');
 check(disc(scene).display==='none','Global off hides global discs');
 renderTileFromData('test',1,{type:1,title:'T',sensor_entity:'sensor.t',icon_name:'thermometer',icon_disc:1},meta);
 check(disc(sensor).display!=='none','Per-tile On keeps the disc with the global option off');
 document.documentElement.classList.remove('icon-discs-off');
 // Live preview: the scene alias field resolves the same icon.
 currentTileTab='test';currentTileIndex=2;folderByTab.test=1;
 document.getElementById('test_tile_type').value='2';document.getElementById('test_scene_alias').value='tv';
 updateTilePreview('test');
 check(document.querySelector('#test-tile-2 .tile-icon.mdi-television'),'Live scene preview resolves the scene entity icon');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
try {runDomHarness({label:'Preview icon discs and scene icons',html:page,tmpPrefix:'hometiles-preview-discs-'});}
catch(error){throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1]||error.message.slice(0,400));}
console.log('Preview icon discs, glow, global/per-tile off and scene icons pass');
