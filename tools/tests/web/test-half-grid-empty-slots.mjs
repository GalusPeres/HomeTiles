import assert from 'node:assert/strict';
import {extractDeliveredFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

// One free slot (1x1, or 1x0.5 where no whole cell fits) is centred under the pointer in half-cell steps; a new
// tile grows when a type needs more room. Other empty tiles stay hidden.
const helpers=['clampInt','clampHalf','isCompactSensorType','supportsHalfSize','supportedTileLayout',
  'normalizeLayoutForTileType','normalizeTileLayout','constrainLayoutToTab','setGridItemPosition',
  'setTileGridPosition','layoutTiles','markOccupied','slotFits','freeSlotNear','pointerGridPoint','firstFreeSlot',
  'occupiedFromGrid','freeSlotElement','enableFreeSlotHover','getTileElementLayout',
  'minimumTileSize','grownNewTileLayout','canPlaceTileLayout','canPlaceGridLayout','rectsOverlap','getTileLayoutFromData'].map(extractDeliveredFunction).join('\n');
const html=`<!doctype html><html><head><style>${readRepoFile('src/web/assets/admin.css')}
:root{--grid-cols:3;--grid-rows:3;--preview-cell-w:100px;--preview-cell-h:100px;--preview-gap:12px;--preview-pad:4px;--tile-radius:16px;}</style></head><body>
<div id="tab-tiles-test"><div class="tile-grid">${[14,14,5,0,0,0,0,0,0].map((type,i)=>`<div class="tile ${type?'filled':'empty'}" id="test-tile-${i}" data-index="${i}" data-type="${type}" onclick="selected=${i}"></div>`).join('')}</div></div><pre id="result"></pre><script>
${inlineScriptSafe(helpers)}
const GRID_COLS=3,GRID_ROWS=3,MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3,FREE_SLOT_SIZES=[[1,1],[1,.5]];
let currentTileTab='test',currentTileIndex=-1,selected=-1,firstRow=0,dragSource=null,resizeState=null,newTileSpot=null;
function firstAllowedGridRow(){return firstRow;}
function getTileGrid(){return document.querySelector('#tab-tiles-test .tile-grid');}
function getTilesData(){return window.testTiles||[];}
function getTileGridMetrics(){const grid=getTileGrid(),rect=grid.getBoundingClientRect();return {rect,padLeft:4,padTop:4,cellW:100,cellH:100,gapX:12,gapY:12};}
function selectTile(index){selected=index;}
const check=(v,m)=>{if(!v)throw Error(m);};
const visibleEmpty=()=>[...document.querySelectorAll('.tile.empty')].filter(el=>el.style.display!=='none');
const at=(col,row)=>{const r=getTileGrid().getBoundingClientRect();return {clientX:r.left+4+col*112+6,clientY:r.top+4+row*112+6};};
const move=(col,row)=>{const p=at(col,row);getTileGrid().dispatchEvent(new PointerEvent('pointermove',{bubbles:true,pointerType:'mouse',...p}));};
try{
 const tiles=[{type:14,col:0,row:0,span_w:3,span_h:.5},{type:14,col:0,row:.5,span_w:1.5,span_h:.5},{type:5,col:1,row:1,span_w:2,span_h:1},...Array.from({length:6},()=>({type:0}))];
 window.testTiles=tiles;
 layoutTiles('test',tiles);
 check(visibleEmpty().length===1,'Exactly one free slot is shown');
 const rest=getTileElementLayout('test',Number(visibleEmpty()[0].dataset.index));
 check(rest.col===0 && rest.row===1 && rest.span_w===1 && rest.span_h===1,'The resting slot is the first aligned 1x1 spot');
 enableFreeSlotHover('test');
 move(2,0.5);
 const half=getTileElementLayout('test',Number(document.querySelector('.tile.empty[data-free-slot="1"]').dataset.index));
 check(half.col===1.5 && half.row===.5 && half.span_w===1 && half.span_h===.5,'Only half a row is free there, so the slot is 1x0.5 on a half column');
 move(1.2,2.2);
 const whole=getTileElementLayout('test',Number(document.querySelector('.tile.empty[data-free-slot="1"]').dataset.index));
 check(whole.row===2 && whole.span_w===1 && whole.span_h===1 && whole.col<=1.2 && 1.2<whole.col+1,'Where a whole cell fits the slot is 1x1, centred under the pointer '+JSON.stringify(whole));
 check(visibleEmpty().length===1,'Hover never leaves a second placeholder behind');
 const slotEl=document.querySelector('.tile.empty.free-slot-hover');
 const r=slotEl.getBoundingClientRect();
 document.elementFromPoint(r.left+r.width/2,r.top+r.height/2).click();
 check(selected===Number(slotEl.dataset.index),'Clicking the hovered slot selects that new tile');
 // A picked new tile keeps its spot when a re-render recreates its element.
 const picked=Number(slotEl.dataset.index);
 currentTileIndex=picked;newTileSpot={tab:'test',index:picked,layout:{col:1.5,row:.5,span_w:1,span_h:.5}};
 ['col','row','spanW','spanH'].forEach(k=>delete slotEl.dataset[k]);
 layoutTiles('test',tiles);
 const kept=getTileElementLayout('test',picked);
 check(kept.col===1.5 && kept.row===.5 && kept.span_h===.5,'A picked 1x0.5 new tile never jumps back to a 1x1 slot');
 check(visibleEmpty().filter(el=>el!==slotEl).length===1,'Another free slot remains for the next new tile');
 // A new 1x0.5 tile grows for types that need a whole cell, or blocks them.
 newTileSpot={tab:'test',index:picked,layout:{col:0,row:1,span_w:1,span_h:.5}};
 const grown=grownNewTileLayout('test',5);
 check(grown && grown.col===0 && grown.row===1 && grown.span_h===1,'A switch grows the new tile downwards to 1x1');
 check(grownNewTileLayout('test',1).span_h===.5,'A sensor keeps the half height');
 newTileSpot.layout={col:1.5,row:.5,span_w:1,span_h:.5};
 check(grownNewTileLayout('test',5)===null,'Without room a larger type cannot be chosen');
 currentTileIndex=-1;newTileSpot=null;
 check(supportedTileLayout(9,{col:0,row:0,span_w:1,span_h:.5}),'Clocks accept the half-height size');
 check(!supportedTileLayout(5,{col:0,row:0,span_w:1,span_h:.5}),'Switches still need a whole cell height');
 check(supportedTileLayout(5,{col:.5,row:0,span_w:1.5,span_h:1}) && supportedTileLayout(12,{col:0,row:0,span_w:2.5,span_h:1.5}),'Every type resizes in half steps');
 check(!supportedTileLayout(7,{col:0,row:0,span_w:1.5,span_h:1}),'Settings stays whole');
 document.querySelectorAll('.tile-grid > .tile').forEach(el=>{el.className='tile empty';el.dataset.type='0';delete el.dataset.selected;});
 layoutTiles('test',Array.from({length:9},()=>({type:0})));
 check(visibleEmpty().length===1,'An empty grid shows one resting slot');
 firstRow=1;
 layoutTiles('test',Array.from({length:9},()=>({type:0})));
 for(const el of visibleEmpty())check(getTileElementLayout('test',Number(el.dataset.index)).row>=1,'Reserved screensaver rows stay untouched');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
runDomHarness({label:'Half-grid free slot',html,tmpPrefix:'hometiles-half-empty-'});
