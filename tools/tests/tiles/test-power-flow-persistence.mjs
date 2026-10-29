// Power flow node entities (Tile::flow_entities) live in the
// /_tile_flow_entities sidecar like the Weather sensors (PackedTileV7 holds
// one entity). Compiles the real tile_config.cpp sidecar functions and
// tile_config.h helpers on the host: normalize (empty nodes kept, later
// lines kept for further consumers), options, save, reboot, unchanged no-op,
// clear, .tmp/.bak recovery, foreign records, other tile types and failed
// writes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/tiles/config/tile_config.cpp'), 'utf8');
const header = fs.readFileSync(path.join(root, 'src/tiles/config/tile_config.h'), 'utf8');
const fnIn = (text, name) => {
  const f = cppFunctionDefinitions(text).find(candidate => candidate.name === name);
  assert(f, name);
  return f.source;
};
const fn = name => fnIn(source, name);

// Every save, load, delete and no-op path carries the sidecar; the entity
// slots (subscriptions, the Bridge declaration) list the nodes.
const defs = cppFunctionDefinitions(source);
assert(defs.some(f => f.name === 'TileConfig::loadGrid' && f.source.includes('applyFlowEntitiesFromSd(folder_id, grid);')));
assert(defs.some(f => f.name === 'TileConfig::saveGridInPlace' &&
  f.source.includes('normalizeFlowEntities(working.tiles[i]);') &&
  f.source.includes('writeFlowEntitiesSd(folder_id, grid_idx, tile.flow_entities)')));
assert(defs.some(f => f.name === 'TileConfig::deleteFolder' && f.source.includes('writeFlowEntitiesSd(id, i, "");')));
assert(fn('gridSidecarsMatchStored').includes('readFlowEntitiesSd(folder_id, index, stored_flow)'));
const slots = fn('TileConfig::loadFolderGridEntitiesOnly');
assert(slots.includes('out[i].extra_entities = flow.flow_entities;'));
assert(slots.includes('full.tiles[i].type == TILE_POWER_FLOW ? full.tiles[i].flow_entities : sensors'));
assert(fn('sidecarTextsFor').includes('g_flow_entity_sidecar_texts'));
assert(fn('ensureSidecarIndexBuilt').includes('scanSidecarDir(kFlowEntityPathDir, g_flow_entity_sidecar_keys);'));
assert(fnIn(header, 'tileContentEquals').includes('a.flow_entities == b.flow_entities'));
assert(fnIn(header, 'tileTransparent').includes('powerFlowOption(tile, kFlowNoCardBit)'));
const handler = fs.readFileSync(path.join(root, 'src/web/server/handlers/web_admin_tiles.cpp'), 'utf8');
assert(handler.includes('normalizeFlowEntities(tile);'), 'the save handler normalizes the record');
assert(handler.includes('out += ",\\"flow_entities\\":\\"";'), 'GET /api/tiles returns the record');
assert(handler.includes('if (tile.type == TILE_POWER_FLOW && tile.flow_entities.length()) entity += "|" + tile.flow_entities;'),
  'a changed node reloads the routes');

const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Power flow persistence needs a native C++ compiler'); process.exit(0); }

const cpp = String.raw`
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
class String:public std::string {public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}bool startsWith(const String&s)const{return rfind(s,0)==0;}String substring(size_t a,size_t b)const{return String(substr(a,b-a));}};
std::map<String,String> files; std::set<String> directories; bool ready=true,fail_write=false;int writes=0;
constexpr int FILE_READ=0,FILE_WRITE=1;
struct File {String path;std::vector<String> entries;size_t cursor=0;bool valid=false,dir=false;
 explicit operator bool()const{return valid;} bool isDirectory()const{return dir;}
 const char* name()const{return path.c_str()+path.find_last_of('/')+1;}
 File openNextFile(){if(cursor>=entries.size())return {};return {entries[cursor++],{},0,true,false};}
 size_t size()const{return files[path].size();} String readString(){return files[path];}
 size_t print(const String&s){++writes;files[path]=fail_write?String(s.substr(0,3)):s;return files[path].size();}
 void close(){}void flush(){}
};
struct FS {bool exists(const String&p){return files.count(p)||directories.count(p);}bool mkdir(const String&p){directories.insert(p);return true;}
 bool remove(const String&p){return files.erase(p)>0;}
 bool rename(const String&a,const String&b){if(!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
 File open(const String&p,int mode=FILE_READ){
 if(directories.count(p)){File f;f.path=p;f.valid=true;f.dir=true;for(const auto&e:files)if(e.first.startsWith(p+"/"))f.entries.push_back(e.first);return f;}
 if(mode==FILE_WRITE)files[p]="";
 return {p,{},0,files.count(p)>0,false};
 }} filesystem;
FS& storageFS(){return filesystem;}bool storageReady(){return ready;}
constexpr size_t TILES_PER_GRID=4;enum TileType{TILE_EMPTY=0,TILE_SENSOR=1,TILE_WEATHER=12,TILE_POWER_FLOW=28};
const char*kTitlePathDir="/_tile_titles",*kImagePathDir="/_tile_images",*kEntityPathDir="/_tile_entities",*kIconColorPathDir="/_tile_icon_colors",*kWeatherSensorPathDir="/_tile_weather_sensors",*kFlowEntityPathDir="/_tile_flow_entities";
bool g_sidecar_index_built=false;std::vector<uint32_t> g_title_sidecar_keys,g_image_sidecar_keys,g_entity_sidecar_keys,g_icon_color_sidecar_keys,g_weather_sensor_sidecar_keys,g_flow_entity_sidecar_keys;
using PsString=std::string;struct SidecarText{uint32_t key;PsString text;};using SidecarTexts=std::vector<SidecarText>;
SidecarTexts g_image_sidecar_texts,g_entity_sidecar_texts,g_title_sidecar_texts,g_icon_color_sidecar_texts,g_weather_sensor_sidecar_texts,g_flow_entity_sidecar_texts;
struct SidecarTextsGuard{SidecarTextsGuard(){}~SidecarTextsGuard(){}};
struct Tile {TileType type=TILE_EMPTY;uint8_t key_code=0;String flow_entities;};
struct TileGridConfig{Tile tiles[TILES_PER_GRID];};
static constexpr size_t kWeatherSensorEntityMax = 128;
` + fnIn(header, 'normalizeWeatherSensorEntity') +
  '\nstatic constexpr size_t kFlowEntityLinesMax = 12;\n' +
  ['normalizeFlowEntities', 'flowEntityAt'].map(name => fnIn(header, name)).join('\n') +
  '\nstatic constexpr uint8_t kFlowHeaderBit = 0x01;\nstatic constexpr uint8_t kFlowNoCardBit = 0x02;\n' +
  fnIn(header, 'powerFlowOption') +
  '\nstatic constexpr size_t kFlowEntityRecordMax = kFlowEntityLinesMax * kWeatherSensorEntityMax;\n' +
['sidecarKey', 'sidecarKeyPresent', 'sidecarKeyAdd', 'sidecarTextCached', 'sidecarTextStore', 'sidecarTextForget', 'sidecarTextsFor',
 'sidecarKeyRemove', 'scanSidecarDir', 'ensureSidecarIndexBuilt', 'tmpPathFor', 'backupPathFor', 'replaceFileWithPreparedTmp',
 'flowEntityPathFile', 'readFlowEntitiesSd', 'writeFlowEntitiesSd', 'applyFlowEntitiesFromSd'].map(fn).join('\n') + String.raw`
void reboot(){g_sidecar_index_built=false;g_flow_entity_sidecar_keys.clear();g_flow_entity_sidecar_texts.clear();}
Tile flow(const char* record){Tile tile;tile.type=TILE_POWER_FLOW;tile.flow_entities=record;return tile;}
int main(){
 // Normalization: trimmed ids per line, empty nodes kept in place, trailing
 // empty lines dropped, junk ids emptied, other types cleared.
 Tile t=flow(" sensor.pv_power \nsensor.grid_power\r\n\nsensor.battery_soc\nsensor.home\n\n");normalizeFlowEntities(t);
 assert(t.flow_entities=="sensor.pv_power\nsensor.grid_power\n\nsensor.battery_soc\nsensor.home");
 assert(flowEntityAt(t.flow_entities,0)=="sensor.pv_power"&&flowEntityAt(t.flow_entities,2).empty()&&flowEntityAt(t.flow_entities,4)=="sensor.home");
 assert(flowEntityAt(t.flow_entities,7).empty()&&flowEntityAt("",0).empty());
 t=flow("no_domain\nsensor.a b\nsensor.x|y");normalizeFlowEntities(t);assert(t.flow_entities.empty());
 t=flow("\n\nsensor.battery");normalizeFlowEntities(t);assert(t.flow_entities=="\n\nsensor.battery"&&flowEntityAt(t.flow_entities,2)=="sensor.battery");
 // Further consumers after the five nodes stay (user 2026-10-24: keep it open).
 t=flow("sensor.pv\n\n\n\nsensor.home\nsensor.heating");normalizeFlowEntities(t);assert(flowEntityAt(t.flow_entities,5)=="sensor.heating");
 std::string many;for(int i=0;i<20;++i){many+="sensor.n"+std::to_string(i)+"\n";}
 t=flow(many.c_str());normalizeFlowEntities(t);assert(flowEntityAt(t.flow_entities,11)=="sensor.n11"&&flowEntityAt(t.flow_entities,12).empty());
 t=flow("sensor.pv");t.type=TILE_SENSOR;normalizeFlowEntities(t);assert(t.flow_entities.empty());
 // Options: only Power flow tiles read their key_code bits.
 t=flow("");t.key_code=kFlowNoCardBit;assert(powerFlowOption(t,kFlowNoCardBit)&&!powerFlowOption(t,kFlowHeaderBit));
 t.type=TILE_SENSOR;assert(!powerFlowOption(t,kFlowNoCardBit));

 // Save, reboot and load; unchanged saves write nothing; a changed node writes.
 Tile all=flow("sensor.pv\nsensor.grid\nsensor.bat\nsensor.soc\nsensor.home");
 assert(writeFlowEntitiesSd(3,1,all.flow_entities));
 TileGridConfig grid;grid.tiles[1].type=TILE_POWER_FLOW;reboot();applyFlowEntitiesFromSd(3,grid);
 assert(grid.tiles[1].flow_entities==all.flow_entities);
 int before=writes;assert(writeFlowEntitiesSd(3,1,all.flow_entities));assert(writes==before);
 assert(writeFlowEntitiesSd(3,1,"sensor.pv2\nsensor.grid"));
 grid=TileGridConfig{};grid.tiles[1].type=TILE_POWER_FLOW;reboot();applyFlowEntitiesFromSd(3,grid);
 assert(grid.tiles[1].flow_entities=="sensor.pv2\nsensor.grid");

 // A tile of another type in the slot ignores a stale file.
 grid=TileGridConfig{};grid.tiles[1].type=TILE_SENSOR;applyFlowEntitiesFromSd(3,grid);assert(grid.tiles[1].flow_entities.empty());

 // Clearing removes the file; nothing comes back after a reboot.
 assert(writeFlowEntitiesSd(3,1,""));reboot();String record;assert(!readFlowEntitiesSd(3,1,record));
 assert(!files.count(flowEntityPathFile(3,1)));

 // Recovery from .tmp and .bak; a foreign record is normalized on load.
 auto path=flowEntityPathFile(5,0);
 files[tmpPathFor(path)]="sensor.pv\nsensor.grid";directories.insert(kFlowEntityPathDir);reboot();
 assert(readFlowEntitiesSd(5,0,record)&&record=="sensor.pv\nsensor.grid");
 files.erase(tmpPathFor(path));files[backupPathFor(path)]="sensor.pv\nsensor.grid";reboot();
 assert(readFlowEntitiesSd(5,0,record)&&record=="sensor.pv\nsensor.grid");
 files.erase(backupPathFor(path));files[path]="bad id\nsensor.ok\n";reboot();
 grid=TileGridConfig{};grid.tiles[0].type=TILE_POWER_FLOW;applyFlowEntitiesFromSd(5,grid);
 assert(grid.tiles[0].flow_entities=="\nsensor.ok");

 // A failed write keeps the previous record; oversized records are refused.
 assert(writeFlowEntitiesSd(6,2,"sensor.pv\nsensor.grid"));fail_write=true;
 assert(!writeFlowEntitiesSd(6,2,"sensor.pv2\nsensor.grid"));fail_write=false;
 assert(readFlowEntitiesSd(6,2,record)&&record=="sensor.pv\nsensor.grid");
 assert(!writeFlowEntitiesSd(6,2,String(2000,'x')));ready=false;assert(!writeFlowEntitiesSd(6,2,"sensor.a\n"));
 std::cout<<"Power flow sidecar: normalize, options, save/reboot, no-op, clear, other types, recovery and failed writes passed\n";
}
`;
const out = path.join(root, 'build/tests/power-flow-persistence');
fs.mkdirSync(out, {recursive: true});
const cppPath = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(cppPath, cpp);
let r = spawnSync(compiler, ['-std=c++17', cppPath, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
console.log(r.stdout.trim());
