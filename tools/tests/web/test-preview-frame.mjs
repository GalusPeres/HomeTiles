// The preview is the scaled screen inside a black bezel (user 2026-10-02:
// "der schwarze Rahmen rundherum" was gone with the exact proportions). The
// bezel must move grid tiles, half-position tiles and the screensaver image
// frame alike, so all of them still sit where the device puts them.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const css = readRepoFile('src/web/assets/admin.css');
const vars = '--grid-cols:7;--grid-rows:5;--preview-cell-w:90.37px;--preview-cell-h:78px;--preview-gap:8.61px;' +
  '--preview-pad:2.15px;--preview-pad-left:2.15px;--preview-pad-right:2.15px;--preview-pad-top:2.69px;' +
  '--preview-pad-bottom:3.23px;--preview-frame:10px;--screensaver-image-inset:10px;--tile-radius:17px;' +
  '--screensaver-image-radius:calc(var(--tile-radius) + 2.15px);';
const html = `<!doctype html><html><head><style>:root{${vars}} ${css}</style></head><body>
<div id="grid" class="tile-grid screensaver-tile-grid"><div class="screensaver-grid-image-frame"></div>
<div id="cell" class="tile" style="grid-column:2 / span 1;grid-row:2 / span 1"></div>
<div id="half" class="tile fractional-tile" style="--tile-col:1;--tile-row:1;--tile-w:1;--tile-h:1"></div></div>
<pre id="result"></pre><script>
try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const grid=document.getElementById('grid').getBoundingClientRect();
 const border=parseFloat(getComputedStyle(document.getElementById('grid')).borderLeftWidth);
 const cell=document.getElementById('cell').getBoundingClientRect();
 const half=document.getElementById('half').getBoundingClientRect();
 check(Math.abs(cell.left-half.left)<0.05&&Math.abs(cell.top-half.top)<0.05,'Grid and half-position tiles share the bezel: '+cell.left+'/'+half.left+' '+cell.top+'/'+half.top);
 const left=cell.left-grid.left-border, top=cell.top-grid.top-border;
 check(Math.abs(left-(10+2.15+90.37+8.61))<0.05,'Column 2 sits behind the bezel and margin: '+left);
 check(Math.abs(top-(10+2.69+78+8.61))<0.05,'Row 2 sits behind the bezel and margin: '+top);
 const frame=document.querySelector('.screensaver-grid-image-frame').getBoundingClientRect();
 check(Math.abs(frame.left-grid.left-border-10)<0.05&&Math.abs(grid.right-border-frame.right-10)<0.05,'The screensaver image fills the screen inside the bezel');
 document.body.dataset.result='pass';document.getElementById('result').textContent='Preview bezel moves every tile alike';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
try { runDomHarness({label:'Preview bezel', html, tmpPrefix:'hometiles-preview-frame-'}); }
catch(error) { throw new Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0,400)); }
assert.match(readRepoFile('src/web/server/render/web_admin_styles.cpp'), /constexpr int kPreviewFramePx = 10;/);
