import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const failures=[];
async function check(name,run){try{await run();console.log('PASS: '+name);}catch(e){failures.push(name);console.error('FAIL: '+name+'\n'+e.message);}}
await check('An older grid GET must not replace a locally edited tile',async()=>{
 let resolveResponse;
 const before={type:1,col:0,row:0,span_w:2,span_h:.5,title:'Before',sensor_value_font:0};
 const edited={...before,title:'Changed',sensor_value_font:3};
 const c=vm.createContext({Promise,Set,tilesData:{folder0:[before]},tileDataLoadedTabs:new Set(['folder0']),tileDataLoadPromises:{},drafts:{folder0:{}},fetch:()=>new Promise(resolve=>{resolveResponse=resolve;}),getFolderIdForTab:()=>0});
 vm.runInContext('function getTilesData(tab){return tilesData[tab]||[];}\n'+extractDeliveredFunction('fetchTileGridData'),c);
 const pending=c.fetchTileGridData('folder0',true);
 c.tilesData.folder0[0]=edited;
 c.drafts.folder0[0]={type:'1',title:'Changed',sensor_value_font:'3',col:'1',row:'1',span_w:'2',span_h:'0.5',_dirty:true,_rev:1};
 resolveResponse({ok:true,json:async()=>[before]});
 await pending;
 assert.equal(c.tilesData.folder0[0].title,'Changed');
 assert.equal(c.tilesData.folder0[0].sensor_value_font,3);
});
await check('A displaced tile must not autosave its previous position',async()=>{
 const c=vm.createContext({currentTileTab:'folder0',currentTileIndex:0,tilesData:{folder0:[{type:1,col:0,row:0,span_w:2,span_h:.5},{type:20,col:0,row:.5,span_w:2,span_h:.5}]},drafts:{folder0:{1:{type:'20',title:'Edited neighbour',col:'1',row:'1.5',span_w:'2',span_h:'0.5',_dirty:true,_rev:2}}},layoutTiles(){},clearReflowPreviewClasses(){},syncSelectedLayoutInputs(){},persistDrafts(){}});
 vm.runInContext('function getTilesData(tab){return tilesData[tab]||[];}\n'+['applyLocalTileReorder','restoreLocalTileReorder','getTileSnapshotForSave'].map(extractDeliveredFunction).join('\n'),c);
 c.applyLocalTileReorder('folder0',{layouts:[{col:0,row:.5,span_w:2,span_h:.5},{col:0,row:0,span_w:2,span_h:.5}]});
 const saving=c.getTileSnapshotForSave('folder0',1);
 assert.equal(saving.row,'1');
 assert.equal(saving.title,'Edited neighbour');
 c.restoreLocalTileReorder('folder0',[{col:0,row:0},{col:0,row:.5}]);
 const restored=c.getTileSnapshotForSave('folder0',1);
 assert.equal(restored.row,'1.5','Failed reorder restores draft geometry too');
 assert.equal(restored.title,'Edited neighbour');
});
assert.equal(failures.length,0,failures.join('; '));
