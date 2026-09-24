import assert from 'node:assert/strict';
import fs from 'node:fs';
import {extractDeliveredFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
// Use the actual firmware-emitted opening tag so a wrong/missing CSS class fails here.
const noteSourceLine=readRepoFile('src/web/server/render/web_admin_html.cpp').split('\n').find(line=>line.includes('_tile_size_note'));
const noteParts=Array.from(noteSourceLine.matchAll(/"(?:\\.|[^"\\])*"/g),match=>JSON.parse(match[0]));
assert.equal(noteParts.length,2);
const noteOpening=noteParts[0]+'test'+noteParts[1];
const helpers=[
 'clampInt','clampHalf','isCompactSensorType','supportedTileLayout','applyCompactSensorPreview','supportsHalfSize','markOccupied','slotFits','firstFreeSlot','fitCompactClockPreview',
 'normalizeLayoutForTileType','normalizeTileLayout','constrainLayoutToTab','setGridItemPosition','setTileGridPosition',
 'getTileElementLayout','layoutTiles','normalizeLayoutInputs','applyLayoutInputsFromLayout','updateLayoutFromInputs',
 'rectsOverlap','canPlaceGridLayout','canPlaceTileLayout','cloneLayout','simulateGridReorderLayouts','manhattanDistance','buildGridPlacementCandidates',
 'normalizeSnapshotLayout','syncTileSizePolicy','buildResizeCandidate','parseGridTrackSizes','getGridElementMetrics',
 'getTileGrid','getTileGridMetrics','getRawGridCellFromPointer'
].map(extractDeliveredFunction).join('\n');
const html=`<!doctype html><html><head><style>${readRepoFile('src/web/assets/admin.css')}
:root{--grid-cols:7;--grid-rows:5;--preview-cell-w:168px;--preview-cell-h:145px;--preview-gap:16px;--preview-pad:4px;--tile-radius:32px;--compact-inset:4px;--compact-title-font:20px;--compact-title-line:26px;--compact-value-line:31px;--compact-value-font:24px;--compact-text-gap:2px;--compact-icon-font:48px;}
</style></head><body><section id="tab-tiles-test"><div class="tile-grid">
${[0,1,2,3].map(i=>`<div class="tile sensor" id="test-tile-${i}" data-index="${i}" data-type="1"><i class="tile-icon">i</i><div class="tile-title"><span class="tile-title-lines"><span class="tile-title-line">Room</span><span class="tile-title-line">Upstairs</span></span></div><div class="tile-value">22.5 C</div></div>`).join('')}
</div></section><select id="test_tile_type"><option value="1">Sensor</option><option value="20">Binary</option><option value="14">Energy</option><option value="5">Switch</option><option value="7">Settings</option><option value="0">Empty</option></select>
${['col','row','span_w','span_h'].map(n=>`<input type="number" id="test_tile_${n}" value="1">`).join('')}
${noteOpening}Hint</p><pre id="result"></pre><script>
${inlineScriptSafe(helpers)}
const GRID_COLS=7,GRID_ROWS=5,MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
let currentTileTab='test',currentTileIndex=0,dragSource=null;
const tiles=[{type:1,col:0,row:0,span_w:2,span_h:.5},{type:20,col:0,row:.5,span_w:2,span_h:.5},{type:1,col:2,row:0,span_w:2,span_h:1},{type:0,col:4,row:0,span_w:1,span_h:1}];
function getTilesData(){return tiles;}function firstAllowedGridRow(){return 0;}
const drafts={};function persistDrafts(){}
function getTileLayoutFromData(tab,index){return normalizeTileLayout(tiles[index],index,tab);}
const check=(value,message)=>{if(!value)throw Error(message);};
try {
 layoutTiles('test',tiles);
 tiles.forEach((tile,i)=>applyCompactSensorPreview(document.getElementById('test-tile-'+i),tile.type,tile));
 const rect=i=>document.getElementById('test-tile-'+i).getBoundingClientRect();
 check(rect(0).width===352 && rect(0).height===64.5,'half tile dimensions');
 check(rect(1).y-rect(0).bottom===16,'gap is preserved');
 check(rect(1).bottom===rect(2).bottom,'two halves plus gutter equal a full tile');
 check(!document.getElementById('test-tile-2').classList.contains('sensor-compact'),'2x1 retains its original layout');
 const value=document.querySelector('#test-tile-0 .tile-value').getBoundingClientRect();
 check(parseFloat(getComputedStyle(document.querySelector('#test-tile-0 .tile-icon')).borderTopLeftRadius)===28,'inner radius is outer radius minus inset');
 check(value.bottom<=rect(0).bottom && value.left>rect(0).left,'compact value stays inside');
 const card=document.getElementById('test-tile-0'), valueLabel=card.querySelector('.tile-value');
 card.style.setProperty('--fs20','20px');card.style.setProperty('--compact-value-line-20','26px');
 valueLabel.classList.add('sensor-value-size-40');
 check(getComputedStyle(valueLabel).fontSize==='24px','explicit value sizes never change the half-height font');
 valueLabel.classList.remove('sensor-value-size-40');
 check(getComputedStyle(valueLabel).fontSize==='24px','automatic font keeps the compact default');
 const snapshot=normalizeSnapshotLayout({type:1,col:'1.5',row:'2.5',span_w:'2',span_h:'0.5'},0,'test');
 check(snapshot.col===.5&&snapshot.row===1.5&&snapshot.span_h===.5,'draft reload retains half steps');
 for(const width of [1,1.5,2,2.5,3]) check(supportedTileLayout(14,{...snapshot,span_w:width}),'Energy compact widths');
 check(!supportedTileLayout(5,snapshot)&&supportedTileLayout(20,snapshot),'type policy');
 check(supportedTileLayout(1,{...snapshot,span_w:1}),'minimum 1x0.5 size is supported');
 check(!supportedTileLayout(1,{...snapshot,span_w:.5}),'width below one cell is rejected');
 for(const width of [1,1.5,2,2.5,3]) check(supportedTileLayout(1,{...snapshot,span_w:width}),'compact width expands in half steps');
 check(supportedTileLayout(1,{...snapshot,span_w:1.5}) && supportedTileLayout(20,{...snapshot,span_w:1.5}),'1.5x0.5 supported for both sensor types');
 applyLayoutInputsFromLayout('test',tiles[0],false);syncTileSizePolicy('test');
 check(document.querySelector('#test_tile_type option[value="5"]').disabled,'incompatible type is disabled');
 check(!document.querySelector('#test_tile_type option[value="20"]').disabled,'binary sensor stays selectable');
 check(!document.querySelector('#test_tile_type option[value="14"]').disabled,'Energy stays selectable');
 check(!document.getElementById('test_tile_size_note').hidden,'type hint visible');
 check(getComputedStyle(document.getElementById('test_tile_size_note')).fontSize==='12px','size hint uses the existing small helper text style');
 const old={col:0,row:0,span_w:2,span_h:1};
 const grid=document.querySelector('.tile-grid').getBoundingClientRect();
 const candidate=buildResizeCandidate(old,'s',grid.x+100,grid.y+40,'test');
 check(candidate.span_h===.5,'dragging the lower edge snaps to half height');
 const narrow=buildResizeCandidate({col:0,row:0,span_w:2,span_h:.5},'e',grid.x+4+(168+16)*1.1,grid.y+30,'test');
 check(narrow.span_w===1.5,'half-height tile resizes to 1.5 columns');
 applyCompactSensorPreview(document.getElementById('test-tile-0'),1,narrow);
 check(document.getElementById('test-tile-0').classList.contains('sensor-compact'),'narrow half tile keeps compact preview');
 check(!canPlaceTileLayout('test',0,{...tiles[0],row:.5}),'overlapping lower half rejected');
 const base=tiles.slice(0,3).map(tile=>({...tile}));
 check(simulateGridReorderLayouts(base,new Set([0,1,2]),0,4.5,1.5,7,5).layouts[0].col===4.5,'drag supports half steps in both axes');
 const swapped=simulateGridReorderLayouts(base,new Set([0,1,2]),0,0,.5,7,5);
 check(swapped && swapped.layouts[1].row===0 && swapped.layouts[0].row===.5,'half tiles swap automatically');
 document.getElementById('test_tile_type').value='5';
 document.getElementById('test_tile_span_w').value=1;document.getElementById('test_tile_span_h').value=1;
 syncTileSizePolicy('test');
 check(document.getElementById('test_tile_col').step==='0.5' && document.getElementById('test_tile_row').step==='0.5','switch position fields allow half steps');
 check(document.getElementById('test_tile_span_h').step==='0.5','switch size resizes in half steps');
 document.getElementById('test_tile_type').value='7';syncTileSizePolicy('test');
 check(document.getElementById('test_tile_span_h').step==='1','settings size remains whole');
 document.getElementById('test_tile_type').value='5';syncTileSizePolicy('test');
 dragSource={tab:'test',type:5};
 const metrics=getTileGridMetrics('test');
 const halfPointer=getRawGridCellFromPointer('test',metrics.rect.x+metrics.padLeft+(metrics.cellW+metrics.gapX)*.6,metrics.rect.y+metrics.padTop+(metrics.cellH+metrics.gapY)*.6);
 check(halfPointer.col===.5 && halfPointer.row===.5,'switch drag snaps in half steps');
 const switchResize=buildResizeCandidate({col:0,row:0,span_w:1,span_h:1},'s',metrics.rect.x+50,metrics.rect.y+metrics.padTop+metrics.cellH*1.5,'test');
 check(switchResize.span_h===1.5,'switch resize snaps in half steps');
 dragSource=null;
 const oldRect=rect(3);check(oldRect.width===168&&Math.abs(oldRect.height-145)<1,'new-tile placeholder rests at 1x1 where a whole cell fits');
 document.body.dataset.result='pass';document.getElementById('result').textContent='Half-grid editor passed';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
fs.mkdirSync('build/tests/half-grid-editor',{recursive:true});fs.writeFileSync('build/tests/half-grid-editor/index.html',html);
runDomHarness({label:'Half-grid editor',html,tmpPrefix:'hometiles-half-grid-'});
const tr=readRepoFile('src/core/i18n/i18n.cpp');
for(const text of ['Für andere Kacheltypen zuerst mindestens eine ganze Zeile Höhe wählen.','Choose a height of at least one cell before switching to another tile type.','Choisissez une hauteur d\x27au moins une cellule avant de changer de type de tuile.']) assert(tr.includes(text));
assert(readRepoFile('src/web/server/render/web_admin_html.cpp').includes('appendHtmlEscaped(html, tr.tile_fractional_type_hint)'));
