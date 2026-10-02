// The screensaver clock sits on the image frame like on the panel's screen
// (user 2026-10-02: the preview clock touched the top-right image edge while
// the panel kept a margin). The grid around the frame is wider than the
// screen (editor padding and gaps), so placing and scaling the clock on the
// grid moved it up and right and made it slightly too large.
import {readAdminDeliverySource,readRepoFile,inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const inputs = ['screensaverUseWallpapers','screensaverShuffle','screensaverTileShadow','screensaverTileBorder',
  'screensaverShowTime','screensaverShowDate','screensaverShowWeekday','screensaverClockShadow']
  .map(id => `<input type="checkbox" id="${id}">`).join('') +
  ['screensaverTimeFont','screensaverDateFont','screensaverTimeAlignment','screensaverDateAlignment',
   'screensaverTimeFormat','screensaverDateFormat','screensaverWallpaperDuration','screensaverWallpaperZoom',
   'screensaverFocusX','screensaverFocusY'].map(id => `<input id="${id}">`).join('');

const html = `<!doctype html><html lang="en"><head><style>:root {--grid-cols:7;--grid-rows:5;--preview-cell-w:85px;--preview-cell-h:73px;--preview-gap:10px;--preview-pad:12px;--screensaver-image-inset:10px;--tile-radius:16px;--screensaver-fs96:96px;--screensaver-lh96:117px;--screensaver-ldy96:0px;--screensaver-fs48:48px;--screensaver-lh48:58px;--screensaver-ldy48:0px;--screensaver-clock-gap:6px;} ${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-screensaver" class="tile-tab tab-content active"><div id="screensaverGrid" class="tile-grid screensaver-tile-grid"><div class="screensaver-grid-image-frame"><img id="screensaverPreviewImage" hidden></div>
<div id="screensaverClock" class="screensaver-grid-clock"><div id="screensaverClockTime">--:--</div><div id="screensaverClockDate">--.--.----</div><span class="screensaver-clock-resize-handle"></span></div></div></div>
${inputs}<div id="screensaverWallpaperControls"></div><div id="screensaverWallpaperList"></div>
<pre id="result"></pre><script>
const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={},TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
window.fetch=async()=>({json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const grid=document.getElementById('screensaverGrid');
 grid.style.width='679px';grid.style.height='429px';
 const frame=grid.querySelector('.screensaver-grid-image-frame').getBoundingClientRect();
 const gridRect=grid.getBoundingClientRect();
 check(frame.width<gridRect.width-10,'The frame is narrower than the grid, so the test can tell them apart');
 const clock=document.getElementById('screensaverClock');
 const center=()=>{const r=clock.getBoundingClientRect();return {x:r.left+r.width/2,y:r.top+r.height/2};};
 screensaverDraft={screen_width:1280,screen_height:800,clock_x:1000,clock_y:0,show_time:true,show_date:true,show_weekday:false,
   time_font_size:96,date_font_size:48,time_alignment:2,date_alignment:2,time_format:1,date_format:0,
   clock_shadow:true,tile_shadow:true,tile_border:true,use_wallpapers:false,shuffle:false,duration_seconds:60,wallpapers:[]};
 for(const [x,y] of [[1000,0],[250,750],[500,500],[0,1000]]){
   screensaverDraft.clock_x=x;screensaverDraft.clock_y=y;renderScreensaverEditor();
   const c=center(),wantX=frame.left+x*frame.width/1000,wantY=frame.top+y*frame.height/1000;
   check(Math.abs(c.x-wantX)<0.6&&Math.abs(c.y-wantY)<0.6,'Clock center '+x+'/'+y+' on the frame: '+JSON.stringify(c)+' want '+wantX+','+wantY);
 }
 const scale=frame.width/1280;
 const fontPx=parseFloat(document.getElementById('screensaverClockTime').style.fontSize);
 check(Math.abs(fontPx-96*scale)<0.01,'Time font scales with the frame: '+fontPx+' want '+96*scale);
 const lineH=parseFloat(document.getElementById('screensaverClockTime').style.lineHeight);
 check(Math.abs(lineH-117*scale)<0.01,'Time line height scales with the frame: '+lineH);
 // Dragging maps the pointer back onto the frame.
 screensaverDraft.clock_x=500;screensaverDraft.clock_y=500;renderScreensaverEditor();
 bindScreensaverEditor();
 const start=center();
 clock.dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,clientX:start.x,clientY:start.y,pointerId:7}));
 clock.dispatchEvent(new PointerEvent('pointermove',{bubbles:true,clientX:start.x+frame.width/4,clientY:start.y-frame.height/4,pointerId:7}));
 check(screensaverDraft.clock_x===750&&screensaverDraft.clock_y===250,'Drag maps onto the frame: '+screensaverDraft.clock_x+'/'+screensaverDraft.clock_y);
 clock.dispatchEvent(new PointerEvent('pointerup',{bubbles:true,clientX:start.x,clientY:start.y,pointerId:7}));
 document.body.dataset.result='pass';document.getElementById('result').textContent='Screensaver clock placed and scaled on the image frame';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
try { runDomHarness({label:'Screensaver clock on the image frame',html,tmpPrefix:'hometiles-screensaver-clock-',extraArgs:['--virtual-time-budget=2000','--window-size=1280,1000']}); }
catch(error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0,400)); }
