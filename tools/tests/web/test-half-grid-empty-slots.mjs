import assert from 'node:assert/strict';
import {extractDeliveredFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
const helpers=['clampInt','clampHalf','normalizeLayoutForTileType','normalizeTileLayout','constrainLayoutToTab','setGridItemPosition','setTileGridPosition','layoutTiles','getTileElementLayout'].map(extractDeliveredFunction).join('\n');
const html=`<!doctype html><html><head><style>${readRepoFile('src/web/assets/admin.css')}
:root{--grid-cols:3;--grid-rows:3;--preview-cell-w:100px;--preview-cell-h:100px;--preview-gap:12px;--preview-pad:4px;--tile-radius:16px;}</style></head><body>
<div id="tab-tiles-test"><div class="tile-grid">${Array.from({length:9},(_,i)=>`<div class="tile ${i<2?'energy':'empty'}" id="test-tile-${i}" onclick="selected=${i}"></div>`).join('')}</div></div><pre id="result"></pre><script>
${inlineScriptSafe(helpers)}
const GRID_COLS=3,GRID_ROWS=3,MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
let currentTileTab='test',selected=-1,firstRow=0;
function firstAllowedGridRow(){return firstRow;}
const check=(v,m)=>{if(!v)throw Error(m);};
try{
 const tiles=[{type:14,col:0,row:0,span_w:3,span_h:.5},{type:14,col:0,row:.5,span_w:1.5,span_h:.5},...Array.from({length:7},()=>({type:0}))];
 layoutTiles('test',tiles);
 const slot=getTileElementLayout('test',2);
 check(slot.col===1.5 && slot.row===.5 && slot.span_w===1 && slot.span_h===1,'New 1x1 slot starts half a row above the old whole-row position and on a half column');
 const empty=document.getElementById('test-tile-2'),rect=empty.getBoundingClientRect();
 document.elementFromPoint(rect.left+rect.width/2,rect.top+10).click();
 check(selected===2,'Upper half of the shifted placeholder is clickable');
 const visible=tiles.map((_,i)=>document.getElementById('test-tile-'+i)).filter(el=>el.style.display!=='none');
 for(let i=0;i<visible.length;i++)for(let j=i+1;j<visible.length;j++){
  const a=visible[i].getBoundingClientRect(),b=visible[j].getBoundingClientRect();
  check(a.right<=b.left || b.right<=a.left || a.bottom<=b.top || b.bottom<=a.top,'Placeholder hit areas never overlap other tiles or placeholders');
 }
 layoutTiles('test',Array.from({length:9},()=>({type:0})));
 for(let i=0;i<9;i++){const cell=getTileElementLayout('test',i);check(cell.col===i%3 && cell.row===Math.floor(i/3),'Empty legacy grid retains whole 1x1 positions');}
 firstRow=1;
 layoutTiles('test',Array.from({length:9},()=>({type:0})));
 for(let i=0;i<9;i++){const el=document.getElementById('test-tile-'+i);if(el.style.display!=='none')check(getTileElementLayout('test',i).row>=1,'Reserved screensaver rows stay untouched');}
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
runDomHarness({label:'Half-grid empty slots',html,tmpPrefix:'hometiles-half-empty-'});
