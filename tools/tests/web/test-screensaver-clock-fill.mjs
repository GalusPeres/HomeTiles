// Screensaver readability (user 2026-10-10): the clock was unreadable on a
// light image, so it gets an optional card (fill color and opacity). Panel,
// Web Admin preview and storage must agree. The per-tile Text position that
// came with it was removed again (user 2026-10-15): a half-height tile's lone
// value always keeps the title line above it, on the panel and in previews.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// Text position is gone everywhere: field, storage, endpoint, editor, CSS,
// translations and the panel layout.
const compact = read('src/tiles/runtime/compact_sensor_layout.h');
assert.doesNotMatch(compact, /value_only_top|center_text|ss_text_align/);
assert.match(compact, /tile_geometry::extent\(tile\.col, tile\.span_w, GRID_CELL_W, GRID_GAP\),\n\s*tile\.sensor_value_font\);/);
const config = read('src/ui/screensaver/screensaver_config.cpp');
for (const file of ['src/tiles/config/tile_config.h', 'src/ui/screensaver/screensaver_tile_look.h',
                    'src/ui/screensaver/screensaver_config.cpp', 'src/web/server/handlers/web_admin_tiles.cpp',
                    'src/web/server/render/web_admin_html.cpp', 'src/web/server/render/web_admin_styles.cpp',
                    'src/web/admin/tiles/editor.js', 'src/web/admin/tiles/snapshots.js',
                    'src/web/admin/tiles/import-export.js', 'src/web/assets/admin.css',
                    'src/core/i18n/i18n.h', 'src/core/i18n/i18n.cpp']) {
  assert.doesNotMatch(read(file), /ss_text_align|text_position|ss-text-centered|value-only-top|kTextAlign|tile-look-align|"Centered on icon"/, file);
}

// Clock fill: stored with the clock settings, off by default, drawn as a card
// with the shared padding and radius.
const header = read('src/ui/screensaver/screensaver_config.h');
assert.match(header, /bool clock_fill = false;\n\s*uint32_t clock_fill_color = 0x000000;\n\s*uint8_t clock_fill_opacity = 128;/);
assert.match(fn(config, 'ScreensaverConfigStore::loadPath'), /loaded\.clock_fill = doc\["clock_fill"\] \| false;/);
assert.match(fn(config, 'ScreensaverConfigStore::loadPath'), /constrain\(doc\["clock_fill_opacity"\] \| 128, 0, 255\)/);
const toJson = config.slice(config.indexOf('String ScreensaverConfigStore::toJson('),
                            config.indexOf('bool ScreensaverConfigStore::save()'));
for (const key of ['clock_fill', 'clock_fill_color', 'clock_fill_opacity']) assert.ok(toJson.includes(`doc["${key}"]`), key);
const clock = fn(read('src/ui/screensaver/image_screensaver.cpp'), 'rebuild_global_clock');
assert.match(clock, /if \(config\.clock_fill\) \{[\s\S]*lv_obj_set_style_bg_opa\(st->clock_box, config\.clock_fill_opacity, 0\);[\s\S]*screensaver_clock_fill::pad_x\(\)/);
// The clock card's corners are the tiles' fixed radius (user 2026-10-15:
// they looked rounder than the clock's former fixed 16 px).
assert.match(read('src/ui/screensaver/screensaver_clock_fill.h'), /inline int radius\(\) \{ return tile_radius::kDefault; \}/);
const styles = read('src/web/server/render/web_admin_styles.cpp');
for (const name of ['screensaver-clock-fill-pad-x', 'screensaver-clock-fill-pad-y', 'screensaver-clock-fill-radius']) {
  assert.ok(styles.includes(`"${name}"`), name);
}

const html = read('src/web/server/render/web_admin_html.cpp');
for (const needle of ['id="screensaverClockFill"', 'id="screensaverClockFillColor"', 'id="screensaverClockFillOpacity"']) {
  assert.ok(html.includes(needle), needle);
}
assert.match(read('src/web/admin/tiles/import-export.js'), /clock_fill_opacity: Math\.round\(ssClamp\(data\?\.clock_fill_opacity \?\? 128, 0, 255\)\)/);

// Browser: the real editor draws the clock card; a lone value keeps the
// title line above it (the removed Text position's class does nothing).
const inputs = ['screensaverUseWallpapers', 'screensaverShuffle', 'screensaverTileShadow', 'screensaverTileBorder',
  'screensaverShowTime', 'screensaverShowDate', 'screensaverShowWeekday', 'screensaverClockShadow', 'screensaverClockFill']
  .map(id => `<input type="checkbox" id="${id}">`).join('') +
  ['screensaverTimeFont', 'screensaverDateFont', 'screensaverTimeAlignment', 'screensaverDateAlignment',
   'screensaverTimeFormat', 'screensaverDateFormat', 'screensaverWallpaperDuration', 'screensaverWallpaperZoom',
   'screensaverFocusX', 'screensaverFocusY'].map(id => `<input id="${id}">`).join('') +
  '<input type="color" id="screensaverClockFillColor"><input type="range" min="0" max="255" id="screensaverClockFillOpacity"><output id="screensaverClockFillOpacityValue"></output>';

const page = `<!doctype html><html lang="en"><head><style>:root {--grid-cols:7;--grid-rows:5;--preview-cell-w:85px;--preview-cell-h:73px;--preview-gap:10px;--preview-pad:12px;--screensaver-image-inset:10px;--tile-radius:16px;--screensaver-fs48:48px;--screensaver-lh48:58px;--screensaver-ldy48:0px;--screensaver-fs28:28px;--screensaver-lh28:34px;--screensaver-ldy28:0px;--screensaver-clock-gap:6px;--screensaver-clock-fill-pad-x:16px;--screensaver-clock-fill-pad-y:8px;--screensaver-clock-fill-radius:16px;--compact-h:40px;--compact-inset:6px;--compact-title-line:24px;--compact-value-line:24px;--compact-value-dy:0px;--compact-title-top:4px;--compact-value-top:28px;--compact-value-top-24:29px;--compact-title-top-24:3px;--compact-value-line-step-24:29px;} ${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-screensaver" class="tile-tab tab-content active"><div id="screensaverGrid" class="tile-grid screensaver-tile-grid"><div class="screensaver-grid-image-frame"><img id="screensaverPreviewImage" hidden></div>
<div id="screensaverClock" class="screensaver-grid-clock"><div id="screensaverClockTime">--:--</div><div id="screensaverClockDate">--.--.----</div><span class="screensaver-clock-resize-handle"></span></div></div></div>
${inputs}<div id="screensaverWallpaperControls"></div><div id="screensaverWallpaperList"></div>
<div class="tile sensor-compact ss-text-centered" id="centered" style="position:relative;width:200px;height:40px"><div class="tile-value">69.9 °F</div></div>
<div class="tile sensor-compact" id="titled" style="position:relative;width:200px;height:40px"><div class="tile-title">Outside</div><div class="tile-value">69.9 °F</div></div>
<div class="tile sensor-compact" id="standard" style="position:relative;width:200px;height:40px"><div class="tile-value">69.9 °F</div></div>
<pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={},TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3,MEDIA_TILE_TYPE=15;
window.fetch=async()=>({json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const grid=document.getElementById('screensaverGrid');
 grid.style.width='679px';grid.style.height='429px';
 const frame=grid.querySelector('.screensaver-grid-image-frame').getBoundingClientRect();
 const scale=frame.width/1280;
 const clock=document.getElementById('screensaverClock');
 const center=()=>{const r=clock.getBoundingClientRect();return {x:r.left+r.width/2,y:r.top+r.height/2};};
 screensaverDraft=ssNormalizeLoaded({screen_width:1280,screen_height:800,clock_x:500,clock_y:500,show_time:true,show_date:true,show_weekday:false,
   time_font_size:48,date_font_size:28,time_alignment:1,date_alignment:1,time_format:1,date_format:0,
   clock_shadow:true,tile_shadow:false,tile_border:true,use_wallpapers:false,shuffle:false,duration_seconds:60,wallpapers:[]});
 check(screensaverDraft.clock_fill===false&&screensaverDraft.clock_fill_color==='#000000'&&screensaverDraft.clock_fill_opacity===128,'Defaults: no fill, black, 50 %');
 renderScreensaverEditor();
 check(getComputedStyle(clock).backgroundColor==='rgba(0, 0, 0, 0)','No card without fill');
 check(document.getElementById('screensaverClockFillColor').disabled,'Color waits for the fill');
 const before=center();
 screensaverDraft.clock_fill=true;screensaverDraft.clock_fill_color='#336699';screensaverDraft.clock_fill_opacity=255;
 renderScreensaverEditor();
 const cs=getComputedStyle(clock);
 check(cs.backgroundColor==='rgb(51, 102, 153)','Fill color: '+cs.backgroundColor);
 check(Math.abs(parseFloat(cs.paddingLeft)-16*scale)<0.01&&Math.abs(parseFloat(cs.paddingTop)-8*scale)<0.01,'Card padding scales with the frame: '+cs.paddingLeft+' '+cs.paddingTop);
 const after=center();
 check(Math.abs(after.x-before.x)<0.6&&Math.abs(after.y-before.y)<0.6,'The card grows around the same center');
 check(!document.getElementById('screensaverClockFillOpacity').disabled,'Opacity editable with the fill');
 screensaverDraft.clock_fill_opacity=128;renderScreensaverEditor();
 check(document.getElementById('screensaverClockFillOpacityValue').textContent==='50 %','Opacity label');
 check(ssPayload(screensaverDraft).clock_fill===true&&ssPayload(screensaverDraft).clock_fill_color==='#336699','Saved with the clock settings');
 screensaverDraft.clock_fill=false;renderScreensaverEditor();
 check(clock.style.background===''&&clock.style.padding==='','Turning the fill off removes the card');
 const top=id=>document.querySelector('#'+id+' > .tile-value').offsetTop;
 check(top('centered')===28,'An old centered class no longer moves the value: '+top('centered'));
 check(top('titled')===28,'With a title the block stays as before: '+top('titled'));
 check(top('standard')===28,'A lone value keeps the title line: '+top('standard'));
 document.body.dataset.result='pass';document.getElementById('result').textContent='Screensaver clock fill';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try {
  if (runDomHarness({label: 'Screensaver clock fill', html: page,
                     tmpPrefix: 'hometiles-screensaver-fill-', extraArgs: ['--virtual-time-budget=2000', '--window-size=1280,1000']})) {
    console.log('Screensaver clock fill: stored, served, drawn and previewed; no Text position');
  }
} catch (error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 400)); }
