import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const sandbox = vm.createContext({Set});
vm.runInContext(['cloneLayout', 'rectsOverlap', 'manhattanDistance', 'buildGridPlacementCandidates', 'simulateGridReorderLayouts'].map(extractDeliveredFunction).join('\n'), sandbox);
const rect = (col, row, span_w = 1, span_h = 1, type = 5) => ({col, row, span_w, span_h, type});
const cases = [
  {tiles: [rect(0,0,2,.5,1),rect(0,.5,2,.5,20)], target:[0,.5], expected:[[0,.5],[0,0]]},
  {tiles: [rect(.5,1.5,2,.5,1),rect(3.5,1.5,2,.5,20)], target:[3.5,1.5], expected:[[3.5,1.5],[.5,1.5]]},
  {tiles: [rect(0,0,2,1,1),rect(2,0,2,.5,1),rect(2,.5,2,.5,20)], target:[2,0], expected:[[2,0],[0,0],[2,1]]},
  {tiles: [rect(0,0),rect(1,0)], target:[1,0], expected:[[1,0],[0,0]]},
  {tiles: [rect(.5,.5),rect(2,0,1,1,7)], target:[2,0], expected:[[2,0],[0,0]]},
  {tiles: [rect(0,3,2,.5,1),rect(2,3,2,.5,20)], target:[2,3], firstRow:3, expected:[[2,3],[0,3]]},
  {tiles: [rect(0,0,2,.5,1),rect(2,0,2,1,1),rect(0,.5,2,1),rect(4,0,3,5),rect(0,1.5,2,3),rect(2,1,2,4),rect(0,4.5,2,.5,20)], target:[2,0], fail:true}
];
// Compare both implementations over mixed whole/half grids and displacement chains.
let seed = 531;
const random = n => {seed = (Math.imul(seed,1664525)+1013904223)>>>0;return seed%n;};
for(let trial=0;trial<100;trial++) {
  const tiles=[];
  for(let attempt=0;attempt<45 && tiles.length<20;attempt++) {
    const half=random(3)===0;
    const tile=rect(random(13)/2,random(9)/2,half?2:1,half?.5:1,half?1:5);
    if(tile.col+tile.span_w>7 || tile.row+tile.span_h>5 || tiles.some(other=>sandbox.rectsOverlap(tile,other)))continue;
    tiles.push(tile);
  }
  if(tiles.length)cases.push({tiles,target:[random(13)/2,random(9)/2]});
}
const results=cases.map((test,index)=>{
  const base=structuredClone(test.tiles);
  const result=sandbox.simulateGridReorderLayouts(base,new Set(base.map((_,i)=>i)),0,...test.target,7,5,test.firstRow||0,base.map(t=>t.type));
  assert.deepEqual(base,test.tiles,'simulation must not mutate its source');
  if(test.fail)assert.equal(result,null,`case ${index}: impossible move must roll back`);
  if(test.expected)assert.deepEqual(Array.from(result.layouts,l=>[l.col,l.row]),test.expected,`case ${index}`);
  if(result)for(let i=0;i<base.length;i++) {
    const next=result.layouts[i];
    assert.equal(next.span_w,base[i].span_w);assert.equal(next.span_h,base[i].span_h);
    assert(next.col>=0 && next.row>=(test.firstRow||0) && next.col+next.span_w<=7 && next.row+next.span_h<=5);
    for(let j=0;j<i;j++)assert(!sandbox.rectsOverlap(next,result.layouts[j]),'no overlap after reflow');
  }
  return result;
});
const compiler=['clang++','g++'].find(c=>spawnSync(c,['--version']).status===0);
if(!compiler){console.log('SKIP: Native reflow parity needs a C++ compiler');process.exit(0);}
const source=readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
const geometry=readRepoFile('src/tiles/config/tile_geometry.h').replace(/^#include.*$/gm,'').replace('#pragma once','');
const code=`#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>
#include "src/types/tile_type.h"
namespace Device {constexpr int kGridCols=7,kGridRows=5;}
${geometry}
constexpr int GRID_COLS=7, GRID_ROWS=5,TILES_PER_GRID=35;
struct Tile {TileType type=TILE_EMPTY;float col=0,row=0,span_w=1,span_h=1;};
struct TileGridConfig {Tile tiles[TILES_PER_GRID];};
void clamp_media_tile_layout(TileType,float&,float&,float&,float&){}
${source.slice(source.indexOf('struct TileRect {'),source.indexOf('static bool parseFolderIdArg'))}
int main(){int count,first;float x,y;while(std::cin>>count>>first>>x>>y){TileGridConfig grid;for(int i=0;i<count;i++){int type;auto&t=grid.tiles[i];std::cin>>type>>t.col>>t.row>>t.span_w>>t.span_h;t.type=static_cast<TileType>(type);}bool ok=applySmartReorder(grid,0,x,y,first);std::cout<<ok;for(int i=0;i<count;i++)std::cout<<' '<<grid.tiles[i].col<<' '<<grid.tiles[i].row;std::cout<<'\\n';}}
`;
const out=path.resolve('build/tests/half-grid-reflow');fs.mkdirSync(out,{recursive:true});
const cpp=path.join(out,'reflow.cpp'),bin=path.join(out,process.platform==='win32'?'reflow.exe':'reflow');fs.writeFileSync(cpp,code);
const compiled=spawnSync(compiler,['-std=c++17','-I',process.cwd(),cpp,'-o',bin],{encoding:'utf8'});
assert.equal(compiled.status,0,compiled.stdout+compiled.stderr);
const input=cases.map(t=>`${t.tiles.length} ${t.firstRow||0} ${t.target.join(' ')}\n`+t.tiles.map(l=>[l.type,l.col,l.row,l.span_w,l.span_h].join(' ')).join('\n')).join('\n');
const run=spawnSync(bin,[],{input,encoding:'utf8'});assert.equal(run.status,0,run.stderr);
run.stdout.trim().split(/\r?\n/).forEach((line,i)=>{
  const [ok,...positions]=line.trim().split(/\s+/).map(Number);assert.equal(ok,results[i]?1:0,`native/browser result ${i}`);
  const expected=(results[i]?.layouts||cases[i].tiles).flatMap(t=>[t.col,t.row]);
  assert.deepEqual(positions,Array.from(expected),`native/browser positions or rollback ${i}`);
});
console.log(`${cases.length} native/browser reflow cases passed`);
