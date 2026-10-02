// While a tile is resized only its target card shows (user 2026-10-02):
// shrinking keeps the pointer over the old card, and the selected-hover rule
// brought it back in full; in the screensaver it also lay above the target.
// :hover is replaced by a class so headless Chrome can hold the pointer there.
import {readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const css = readRepoFile('src/web/assets/admin.css').replaceAll(':hover', '.fake-hover');
const card = (id, classes) =>
  `<div id="${id}" class="tile sensor active resizing fake-hover ${classes}" data-selected="1"><div class="tile-resize-handle tile-resize-handle-se"></div></div>`;
const html = `<!doctype html><html><head><style>${css}</style></head><body class="tile-resize-active">
<div class="tile-grid"><div class="tile-grid-inner">${card('home', '')}${card('pill', 'sensor-compact')}<div id="homeTarget" class="tile-resize-placeholder show"></div></div></div>
<div class="tile-grid screensaver-tile-grid tiles-shadowed">${card('saver', '')}${card('saverPill', 'sensor-compact')}<div id="saverTarget" class="tile-resize-placeholder show"></div></div>
<pre id="result"></pre><script>
try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const style=id=>getComputedStyle(document.getElementById(id));
 for(const id of ['home','pill','saver','saverPill']){
   check(style(id).opacity==='0','Resized card '+id+' is hidden under the pointer: opacity '+style(id).opacity);
 }
 const z=id=>Number(style(id).zIndex);
 check(z('saverTarget')>z('saver'),'Screensaver target card above the old card: '+z('saverTarget')+' vs '+z('saver'));
 const clock=document.createElement('div');clock.className='screensaver-grid-clock';document.querySelector('.screensaver-tile-grid').appendChild(clock);
 check(Number(getComputedStyle(clock).zIndex)>z('saverTarget'),'The screensaver clock stays above the tiles');
 document.body.dataset.result='pass';document.getElementById('result').textContent='Resizing hides the old card under the pointer on every page';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
try { runDomHarness({label:'Resize hides the old card', html, tmpPrefix:'hometiles-resize-old-card-'}); }
catch(error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0,400)); }
