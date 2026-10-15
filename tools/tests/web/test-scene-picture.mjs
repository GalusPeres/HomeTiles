// Scene tile picture: a Scene tile shows a JPEG from the panel's /icons
// instead of its icon (an album cover for a script that starts a playlist).
// Without a title it fills the tile, with a title it takes the icon's place.
// Runs the real delivered Web Admin list, upload, live/grid preview, draft,
// autosave, save/load/reset and import paths in Chromium, and checks the
// firmware request field, JSON, routes, upload hardening, renderer and
// translations that carry the same field name.
import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import assert from 'node:assert/strict';

const html=`<!doctype html><html lang="en"><head><style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-test" class="tile-tab"><div class="tile-grid tiles-bordered">
<div class="tile scene" id="test-tile-0" data-index="0"></div><div class="tile scene" id="test-tile-1" data-index="1"></div></div></div>
<div id="testSettings" class="tile-settings"><div class="tile-specific-settings"><select id="test_tile_type"><option value="2">Scene</option></select><input id="test_tile_title"><input id="test_tile_icon"><input type="color" id="test_tile_color">
${['col','row','span_w','span_h'].map(n=>`<input id="test_tile_${n}" type="number">`).join('')}
<div class="type-fields show" id="test_scene_fields"><input type="hidden" id="test_scene_alias" data-entity-picker="scenes">
<label for="test_scene_image">Picture</label><select id="test_scene_image" class="scene-picture-select"><option value="">No picture</option></select>
<div class="scene-picture-control"><button type="button" class="btn btn-secondary" id="test_scene_image_upload">Upload</button>
<input type="file" id="test_scene_image_file" accept="image/*" hidden><span class="settings-note scene-picture-status" id="test_scene_image_status"></span></div></div>
</div></div><pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const SCENE_I18N=Object.freeze({pictureLabel:'Bild',noPicture:'Kein Bild',uploadButton:'Bild hochladen…',uploadingStatus:'Wird hochgeladen…',uploadFailed:'Das Bild konnte nicht hochgeladen werden.'});
const TILE_TYPE_REGISTRY={2:{css:'scene',fields:'scene',preview:'scene',load:'loadSceneFields',save:'saveSceneFields',reset:'resetSceneFields'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const posts=[],uploads=[];let failUpload=false;window.__nativeFetch=window.fetch.bind(window);
window.fetch=async(url,options)=>{
  url=String(url);
  if(url.startsWith('/api/sd_icons'))return {ok:true,json:async()=>[{path:'/icons/b-cover.jpg'},{path:'/icons/a-cover.jpeg'},{path:'/icons/logo.png'},{path:'/icons/../x.jpg'}]};
  if(url==='/api/upload_icon'){const f=options.body.get('file');uploads.push({name:f.name,type:f.type,size:f.size,head:[...new Uint8Array(await f.slice(0,3).arrayBuffer())]});
    return failUpload?{ok:false,json:async()=>({ok:false,error:'Upload failed'})}:{ok:true,json:async()=>({ok:true,path:'/icons/'+f.name})};}
  if(options?.method==='POST'){posts.push(Object.fromEntries(typeof options.body==='string'?new URLSearchParams(options.body):options.body));return {json:async()=>({success:true})};}
  throw Error('Unexpected request '+url);};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const tick=ms=>new Promise(r=>setTimeout(r,ms));
 const pic=()=>document.getElementById('test_scene_image');
 const tile=i=>document.getElementById('test-tile-'+i);
 folderByTab.test=1;tileDataLoadedTabs.add('test');
 sensorMetaCache.sceneEntities={movie:'scene.movie'};
 tilesData.test=[{type:2,col:0,row:0,span_w:2,span_h:2,title:'',scene_alias:'movie',scene_image:'/icons/b-cover.jpg'},
                 {type:2,col:2,row:0,span_w:1,span_h:1,title:'Dinner',scene_alias:'movie',scene_image:'/icons/missing.jpg'}];

 // Load: the list fills from /api/sd_icons (JPEGs inside /icons only, sorted),
 // "No picture" first in the panel's language, the configured one selected.
 selectTile(0,'test');await tick(50);
 const options=[...pic().options].map(o=>o.value);
 check(JSON.stringify(options)===JSON.stringify(['','/icons/a-cover.jpeg','/icons/b-cover.jpg']),'Picture list: '+JSON.stringify(options));
 check(pic().options[0].textContent==='Kein Bild','The empty option is translated');
 check(pic().value==='/icons/b-cover.jpg','The stored picture is selected');
 check(document.getElementById('test_tile_title').value==='','A picture keeps the title empty (no automatic scene title)');

 // Live preview: without a title the picture fills the card, no icon shows.
 updateTilePreview('test');
 let img=tile(0).querySelector(':scope > img.scene-picture');
 check(tile(0).classList.contains('scene-picture-full')&&img,'Live preview fills the card with the picture');
 check(img.getAttribute('src')==='/api/tile_picture?path=%2Ficons%2Fb-cover.jpg','The picture comes from /api/tile_picture: '+img.getAttribute('src'));
 const style=getComputedStyle(img);
 check(style.objectFit==='cover'&&style.position==='absolute','The full picture covers the card');
 check(!tile(0).querySelector(':scope > .tile-icon')||getComputedStyle(tile(0).querySelector(':scope > .tile-icon')).display==='none','No icon over a full picture');

 // A title moves the picture into the icon's box, without the icon disc.
 const title=document.getElementById('test_tile_title');
 title.value='Movie';title.dispatchEvent(new Event('input',{bubbles:true}));updateTilePreview('test');
 const box=tile(0).querySelector(':scope > .tile-icon.scene-picture-icon');
 check(box&&box.querySelector('img.scene-picture-icon-img')&&!tile(0).classList.contains('scene-picture-full'),'With a title the picture takes the icon place');
 check(getComputedStyle(box,'::after').display==='none','A picture icon has no disc');
 check(tile(0).querySelector(':scope > .tile-title')?.textContent.includes('Movie'),'The title stays');
 title.value='';title.dispatchEvent(new Event('input',{bubbles:true}));

 // Choosing "No picture" goes back to the icon, through draft and autosave.
 pic().value='';pic().dispatchEvent(new Event('change',{bubbles:true}));
 check(!tile(0).querySelector('img'),'No picture: no image in the preview');
 await tick(350);
 let post=posts.filter(p=>p.index==='0').at(-1);
 check(post&&post.scene_image==='','Autosave clears the picture: '+JSON.stringify(post));
 check(tilesData.test[0].scene_image==='','The draft keeps the cleared picture');
 pic().value='/icons/a-cover.jpeg';pic().dispatchEvent(new Event('change',{bubbles:true}));
 await tick(350);
 post=posts.filter(p=>p.index==='0').at(-1);
 check(post.scene_image==='/icons/a-cover.jpeg'&&post.scene_alias==='movie','Autosave sends the picture with the scene');

 // Upload: any image is re-encoded as a JPEG of at most 512 px, stored in
 // /icons and then chosen like a picture from the list.
 const canvas=document.createElement('canvas');canvas.width=1200;canvas.height=800;
 canvas.getContext('2d').fillRect(0,0,1200,800);
 const pngUrl=canvas.toDataURL('image/png');const png=await (await window.__nativeFetch(pngUrl)).blob();
 const file=new File([png],'cover.png',{type:'image/png'});
 const uploaded=await uploadScenePicture('test',file);
 check(/^\\/icons\\/scene-[0-9a-z]+\\.jpg$/.test(uploaded),'Upload answers its /icons path: '+uploaded);
 const sent=uploads.at(-1);
 check(sent.type==='image/jpeg'&&sent.head[0]===0xFF&&sent.head[1]===0xD8,'The upload is a JPEG: '+JSON.stringify(sent));
 check(document.getElementById('test_scene_image_status').textContent==='','No status after a good upload');
 fillScenePictureOptions('test',uploaded);
 check(pic().value===uploaded&&[...pic().options].some(o=>o.value===uploaded),'The uploaded picture is listed and chosen');
 failUpload=true;
 check(await uploadScenePicture('test',file)==='','A failed upload chooses nothing');
 check(document.getElementById('test_scene_image_status').textContent==='Das Bild konnte nicht hochgeladen werden.','A failed upload says so in the panel language');
 failUpload=false;

 // Cached grid preview from stored tile data: full picture, icon picture,
 // and a stored picture the list does not know is still shown and kept.
 renderTileFromData('test',0,{...tilesData.test[0],scene_image:'/icons/b-cover.jpg',title:''},sensorMetaCache);
 check(tile(0).classList.contains('scene-picture-full')&&tile(0).querySelector('img.scene-picture'),'Grid preview fills the card');
 renderTileFromData('test',1,tilesData.test[1],sensorMetaCache);
 check(tile(1).querySelector('.tile-icon.scene-picture-icon img')?.getAttribute('src').endsWith('%2Ficons%2Fmissing.jpg'),'Grid preview shows the picture as icon with a title');
 selectTile(1,'test');await tick(50);
 check(pic().value==='/icons/missing.jpg','A configured picture missing from the list is kept');

 // Save -> load -> reset use the same field name; import posts it.
 const fd=new FormData();saveSceneFields('test',fd);
 check(fd.get('scene_image')==='/icons/missing.jpg','Save writes the picture');
 resetSceneFields('test');check(pic().value==='','Reset clears the picture');
 loadSceneFields('test',{scene_alias:'movie',scene_image:'/icons/b-cover.jpg'});check(pic().value==='/icons/b-cover.jpg','Load restores the picture');
 loadSceneFields('test',{scene_alias:'movie',scene_image:'/etc/passwd'});check(pic().value==='','A path outside /icons is not offered');
 await postTile(1,0,{...tilesData.test[0],scene_image:'/icons/a-cover.jpeg'});
 check(posts.at(-1).scene_image==='/icons/a-cover.jpeg','Import posts the picture');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try {runDomHarness({label:'Scene tile picture',html,tmpPrefix:'hometiles-scene-picture-',extraArgs:['--virtual-time-budget=5000']});}catch(error){throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1]||error.message.slice(0,300));}

// Editor bindings and delivered sources.
const editor = readRepoFile('src/web/admin/tiles/editor.js');
for (const name of ['scenePicture', 'scenePictureUpload', 'scenePictureFile']) {
  assert.ok(editor.includes(`'${name}'`), `editor binds ${name}`);
}
assert.ok(readRepoFile('src/web/admin/tiles/import-export.js').includes("fd.append('scene_image', tile.scene_image || '');"));
assert.ok(readAdminDeliverySource().includes('scene_image'), 'delivered admin.js carries scene_image');
assert.ok(readRepoFile('src/web/admin/tiles/live-preview.js').includes('applyScenePicturePreview(tileElem'));
assert.ok(readRepoFile('src/web/admin/tiles/grid-preview.js').includes("applyScenePicturePreview(el, tile.scene_image || ''"));

// Firmware: request field, JSON, routes, storage-only paths, upload hardening.
const handler = readRepoFile('src/types/scene/web_handler.cpp');
assert.match(handler, /server\.hasArg\("scene_image"\)/);
assert.match(handler, /scene_picture::valid_path\(path\)\) tile\.image_path = path;/);
assert.ok(readRepoFile('src/web/server/handlers/web_admin_tiles.cpp').includes('\\"scene_image\\":'));
const server = readRepoFile('src/web/server/web_admin.cpp');
assert.match(server, /server\.on\("\/api\/tile_picture", HTTP_GET,\s*guarded\(/);
const files = readRepoFile('src/web/server/handlers/web_admin_files.cpp');
assert.ok(files.includes('String iconUploadPath(const String& client_name)'), 'uploads stay inside /icons');
assert.ok(!files.includes('if (filename.indexOf(\'/\') < 0) filename = "/icons/" + filename;'), 'a client path no longer chooses the file');
assert.ok(files.includes('if (!g_icon_upload_ok) {'), 'a failed upload is reported');
const picture = readRepoFile('src/types/scene/picture.cpp');
// LVGL's TJpgDec has no decode-time scaling (JD_USE_SCALE 0): asking for it
// fails with JDR_PAR, so pictures decode at 1:1 and are reduced afterwards.
assert.ok(picture.includes('jd_decomp(&jd, jpeg_output, 0)'), 'pictures decode at 1:1 (no JD_USE_SCALE)');
assert.ok(picture.includes('box_reduce('), 'pictures are reduced to about their tile size');
assert.ok(picture.includes('kCacheBudgetBytes'), 'decoded pictures are cached within a budget');
const renderer = readRepoFile('src/types/scene/renderer.cpp');
assert.ok(!renderer.includes('"S:"'), 'no LVGL SD drive path (no ESP32-P4 or S3 board uses that driver)');
assert.ok(renderer.includes('scene_picture::release_on_delete'), 'cards hand their picture back');
assert.ok(renderer.includes('LV_EVENT_STATE_CHANGED'), 'the picture dims with the pressed card');

// Every language has the six picture labels right before the entity picker
// labels (the tests reading the icon picker labels count from the end).
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const locales = [...i18n.matchAll(/static const LocaleProfile kLocale(\w+) = \{/g)].map(match => match[1]);
assert.ok(locales.length >= 4);
for (const locale of locales) {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${locale} = {`);
  const pickerLine = i18n.lastIndexOf('\n    {', i18n.indexOf('{query}', start));
  const line = i18n.slice(i18n.lastIndexOf('\n    {', pickerLine - 1) + 1, pickerLine);
  assert.match(line, /^ {4}\{"[^"\n]+", "[^"\n]+", "[^"\n]+", "[^"\n]+", "[^"\n]+", "[^"\n]+"\},$/,
    `${locale}: scene_picture_labels precede the entity picker labels`);
}
assert.ok(readRepoFile('src/core/i18n/i18n.h').includes('const char* scene_picture_labels[6];'));
assert.ok(readRepoFile('src/types/scene/web_scripts.cpp').includes('const SCENE_I18N = Object.freeze({'));

console.log('Scene tile picture: list, upload, previews, draft, autosave, import, firmware routes and translations');
