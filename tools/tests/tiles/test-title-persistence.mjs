import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const source=fs.readFileSync(path.join(root,'src/tiles/config/tile_config.cpp'),'utf8');
const fn=name=>{const f=cppFunctionDefinitions(source).find(f=>f.name===name);assert(f,name);return f.source;};
const compiler=[process.env.CXX,'clang++','g++'].filter(Boolean).find(c=>spawnSync(c,['--version']).status===0);
if(!compiler){console.log('SKIP: Title persistence needs a native C++ compiler');process.exit(0);}
assert(cppFunctionDefinitions(source).some(f=>f.name==='TileConfig::loadGrid'&&f.source.includes('applyLongTitlesFromSd')));
assert(source.includes('writeLongTitleSd(id, i, "")'));
assert(source.includes('stored_title != tile.title'));
const cpp=String.raw`
#include "src/core/text/title_text.h"
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
class String:public std::string {public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}bool startsWith(const String&s)const{return rfind(s,0)==0;}};
std::map<String,String> files; std::set<String> directories; bool ready=true,fail_write=false,fail_rename=false;int writes=0;
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
 bool rename(const String&a,const String&b){if(fail_rename||!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
 File open(const String&p,int mode=FILE_READ){
 if(directories.count(p)){File f;f.path=p;f.valid=true;f.dir=true;for(const auto&e:files)if(e.first.startsWith(p+"/"))f.entries.push_back(e.first);return f;}
 if(mode==FILE_WRITE)files[p]="";
 return {p,{},0,files.count(p)>0,false};
 }} filesystem;
FS& storageFS(){return filesystem;}bool storageReady(){return ready;}
constexpr size_t TITLE_MAX=32,TILES_PER_GRID=4;constexpr int TILE_EMPTY=0;
const char*kTitlePathDir="/_tile_titles",*kImagePathDir="/_tile_images",*kEntityPathDir="/_tile_entities";
bool g_sidecar_index_built=false;std::vector<uint32_t> g_title_sidecar_keys,g_image_sidecar_keys,g_entity_sidecar_keys;
struct Tile {int type=0;String title;};struct TileGridConfig{Tile tiles[TILES_PER_GRID];};
`+['sidecarKey','sidecarKeyPresent','sidecarKeyAdd','sidecarKeyRemove','scanSidecarDir','ensureSidecarIndexBuilt',
 'tmpPathFor','backupPathFor','replaceFileWithPreparedTmp','titlePathFile','readLongTitleSd','writeLongTitleSd','applyLongTitlesFromSd'].map(fn).join('\n')+String.raw`
void reboot(){g_sidecar_index_built=false;g_title_sidecar_keys.clear();}
int main(){
 String full="A deliberately long first line\nSecond title line with temperature ";full+="\xC2\xB0";full+="C";
 assert(hometiles_title::normalize("First\\nSecond\r\nThird")=="First\nSecond Third");
 std::string unicode;for(int i=0;i<200;++i)unicode+="\xC3\x84";assert(hometiles_title::normalize(unicode.c_str()).size()==254);
 for(int type=1;type<=23;++type){
  assert(writeLongTitleSd(1,0,full));TileGridConfig grid;grid.tiles[0]={type,full.substr(0,31)};
  reboot();applyLongTitlesFromSd(1,grid);assert(grid.tiles[0].title==full);
  const int before=writes;assert(writeLongTitleSd(1,0,full));assert(writes==before);
  assert(writeLongTitleSd(1,0,"Short\nTitle"));reboot();String ignored;assert(!readLongTitleSd(1,0,ignored));
 }
 assert(writeLongTitleSd(1,0,full));assert(writeLongTitleSd(7,2,full));assert(writeLongTitleSd(1,0,""));
 TileGridConfig moved;moved.tiles[2]={21,full.substr(0,31)};reboot();applyLongTitlesFromSd(7,moved);assert(moved.tiles[2].title==full);
 auto path=titlePathFile(7,2);files[tmpPathFor(path)]=full;files.erase(path);reboot();String restored;assert(readLongTitleSd(7,2,restored)&&restored==full);
 files[backupPathFor(path)]=full;files.erase(tmpPathFor(path));reboot();assert(readLongTitleSd(7,2,restored)&&restored==full);
 files[path]="corrupt";assert(readLongTitleSd(7,2,restored)&&restored==full);
 moved.tiles[2]={21,"Changed"};applyLongTitlesFromSd(7,moved);assert(moved.tiles[2].title=="Changed");
 moved.tiles[2]={0,full.substr(0,31)};applyLongTitlesFromSd(7,moved);assert(moved.tiles[2].title!=full);
 assert(writeLongTitleSd(7,2,""));reboot();assert(!readLongTitleSd(7,2,restored));
 assert(writeLongTitleSd(8,1,full));fail_write=true;assert(!writeLongTitleSd(8,1,full+" changed"));fail_write=false;assert(readLongTitleSd(8,1,restored)&&restored==full);
 fail_rename=true;assert(!writeLongTitleSd(8,1,full+" changed"));fail_rename=false;assert(readLongTitleSd(8,1,restored)&&restored==full);
 assert(!writeLongTitleSd(8,1,String(256,'x')));ready=false;assert(!writeLongTitleSd(8,1,full));
 std::cout<<"Title sidecars: 23 types, reboot, no-op, move/copy, shorten/delete, recovery, UTF-8 and failed writes passed\n";
}
`;
const out=path.join(root,'build/tests/title-persistence');fs.mkdirSync(out,{recursive:true});const cppPath=path.join(out,'test.cpp'),binary=path.join(out,process.platform==='win32'?'test.exe':'test');fs.writeFileSync(cppPath,cpp);
let r=spawnSync(compiler,['-std=c++17','-I',root,cppPath,'-o',binary],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
r=spawnSync(binary,[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);console.log(r.stdout.trim());
