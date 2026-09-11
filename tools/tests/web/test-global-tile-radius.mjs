import fs from 'node:fs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import {inlineScriptSafe} from '../../lib/admin-source.mjs';

const read = file => fs.readFileSync(new URL('../../../' + file, import.meta.url), 'utf8');
const css = read('src/web/assets/admin.css');
const code = read('src/web/admin/settings/display-borders.js');
const html = `<!doctype html><html><head><style>${css}
:root{--radius-preview-scale:.5;--tile-radius-device:22;--tile-radius:11px;--preview-pad:12px;--preview-gap:10px;--grid-cols:2;--grid-rows:2;--preview-cell-w:100px;--preview-cell-h:80px;--climate-margin-x:3px;--climate-control-radius:max(0px,calc(var(--tile-radius) - var(--climate-margin-x)));--screensaver-image-radius:calc(var(--tile-radius) + 2px);--screensaver-image-inset:10px;}
</style></head><body>
<div class="folder-footer-options">
<label class="inline-checkbox"><input type="checkbox"><span id="border-label">Kachel-Rahmen</span></label>
<label class="tile-radius-control"><span id="radius-label">Kachelradius</span><input class="global-tile-radius" type="range" min="22" max="32" value="22"><output class="global-tile-radius-value">22</output></label>
<label class="inline-checkbox"><input type="checkbox"><span>Kachel-Schatten</span></label></div>
<div id="grid" class="tile-grid"><div id="tile" class="tile climate"><div id="inner" class="climate-preview-cell"></div></div></div>
<div class="tile-grid screensaver-tile-grid"><div id="wallpaper" class="screensaver-grid-image-frame"></div></div>
<div id="cached" hidden><div class="tile-grid"><div class="tile"></div></div></div><pre id="result">running</pre>
<script>
${inlineScriptSafe(code)}
const requests=[], timers=new Map();let timerId=0,notices=0;
window.setTimeout=fn=>{timers.set(++timerId,fn);return timerId;};
window.clearTimeout=id=>timers.delete(id);
window.fetch=(url,options)=>new Promise(resolve=>requests.push({url,body:new URLSearchParams(options.body),resolve}));
function showNotification(){notices++;}function t(key){return key;}
const check=(ok,message)=>{if(!ok)throw Error(message);};
const radius=id=>parseFloat(getComputedStyle(document.getElementById(id)).borderTopLeftRadius);
const flush=()=>{for(const [id,fn] of [...timers]){timers.delete(id);fn();}};
const settle=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
const respond=(index,ok=true)=>requests[index].resolve({ok,json:async()=>({success:true,radius:Number(requests[index].body.get('radius'))})});
(async()=>{try{
 const center=element=>{const rect=element.getBoundingClientRect();return rect.y+rect.height/2;};
 const control=document.querySelector('.global-tile-radius');
 check(Math.abs(center(document.getElementById('border-label'))-center(document.getElementById('radius-label')))<.1,'radius caption aligns with the checkbox captions');
 check(Math.abs(center(control)-center(document.getElementById('border-label')))<.1,'slider aligns with the options');
 control.style.transition='none';control.focus();
 check(getComputedStyle(control).boxShadow==='none','slider focus must not inherit the text-field ring');
 control.blur();
 previewTileRadiusLive(26);previewTileRadiusLive(28);previewTileRadiusLive(32);
 check(radius('tile')===16,'tile updates before the request');
 check(radius('inner')===13,'Climate inner arc keeps the inset');
 check(radius('grid')===29,'black frame includes tile radius, padding and border');
 check(radius('wallpaper')===18,'wallpaper remains concentric with the frame');
 check(requests.length===0&&timers.size===1,'rapid input coalesces one preview');
 flush();check(requests.length===1&&requests[0].body.get('preview')==='1','device preview must not persist');
 previewTileRadiusLive(29);flush();check(requests.length===1,'requests serialize');
 saveTileRadius(30);check(requests.length===1,'release queues the final value');
 respond(0);await settle();
 check(requests.length===2&&requests[1].body.get('radius')==='30'&&requests[1].body.get('preview')==='0','release wins over queued previews');
 respond(1);await settle();check(tileRadiusConfirmed===30,'saved value becomes the rollback baseline');
 const lazy=document.createElement('div');lazy.innerHTML='<input class="global-tile-radius" min="22" max="32" value="22"><output class="global-tile-radius-value"></output><div class="tile"></div>';document.body.appendChild(lazy);syncTileRadiusControls(lazy);
 check(lazy.querySelector('input').value==='30'&&getComputedStyle(lazy.querySelector('.tile')).borderTopLeftRadius==='15px','lazy folders inherit the live geometry and control value');
 check(getComputedStyle(document.querySelector('#cached .tile')).borderTopLeftRadius==='15px','cached folders inherit the same radius');
 saveTileRadius(27);respond(2,false);await settle();
 check(document.querySelector('.global-tile-radius').value==='30'&&notices===1,'failed save restores confirmed radius');
 previewTileRadius(-20);check(document.querySelector('.global-tile-radius').value==='22','minimum is enforced');
 previewTileRadius(1000);check(document.querySelector('.global-tile-radius').value==='32','profile maximum is enforced');
 document.body.dataset.result='pass';document.getElementById('result').textContent='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
runDomHarness({label:'Global radius live preview, concentric corners, save ordering and rollback',html,tmpPrefix:'hometiles-radius-'});
