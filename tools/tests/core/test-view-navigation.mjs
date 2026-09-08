import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const root = resolve(fileURLToPath(new URL('../../..', import.meta.url)));
const read = path => readFileSync(resolve(root, path), 'utf8');
const navigation = read('src/ui/navigation/view_navigation.cpp');
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
const persistence = read('src/tiles/config/tile_config.cpp');
const renderer = read('src/tiles/runtime/tile_renderer_shared.h');
function definition(source, signature) {
  const start = source.indexOf(signature);
  assert(start >= 0, signature);
  const open = source.indexOf('{', start);
  let depth = 1, end = open + 1;
  while (depth) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
    assert(end < source.length, signature);
  }
  return source.slice(start, end);
}

// Run the real firmware state machine and identity allocation with fake I/O.
// These fakes expose the navigation outcomes, not a second implementation.
const harness = String.raw`
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "src/ui/navigation/view_protocol.h"
struct String : std::string {
  using std::string::string;
  String() = default;
  String(const std::string& value):std::string(value) {}
  String(unsigned value):std::string(std::to_string(value)) {}
  String(int value):std::string(std::to_string(value)) {}
  String substring(size_t from) const { return substr(from); }
  String substring(size_t from, size_t end) const { return substr(from, end - from); }
};
String operator+(const String& a,const String& b){return String(std::string(a)+std::string(b));}
String operator+(const String& a,const char* b){return String(std::string(a)+b);}
String operator+(const char* a,const String& b){return String(a+std::string(b));}
enum TileType { TILE_EMPTY, TILE_NUMBER, TILE_SELECT, TILE_DATETIME, TILE_SENSOR, TILE_BINARY_SENSOR, TILE_SWITCH,
  TILE_WEATHER, TILE_ENERGY, TILE_MEDIA, TILE_CLIMATE, TILE_COVER, TILE_CAMERA, TILE_SCENE };
constexpr size_t TILES_PER_GRID = 6;
struct Tile { TileType type = TILE_EMPTY; uint16_t view_id = 0; String sensor_entity; int popup = 0; };
struct TileGridConfig { Tile tiles[TILES_PER_GRID]; };
struct FolderEntry { uint16_t id, parent_id; const char* name; const char* icon_name; bool pin; };
struct TileConfig {
  static constexpr uint16_t kScreensaverGridStorageId = 0xfffe;
  static uint16_t rootFolderId() { return 0; }
  uint16_t active = 0;
  uint32_t revision = 1;
  TileGridConfig grid;
  std::vector<FolderEntry> folders{{0,0,"Home","home",false},{1,0,"Room","folder",true},{2,1,"Room","folder",false}};
  const FolderEntry* getFolder(uint16_t id) const { for (const auto& f:folders) if(f.id==id)return &f; return nullptr; }
  bool isFolderPinEnabled(uint16_t id) const { const auto* f=getFolder(id); return f && f->pin; }
  const TileGridConfig& getActiveGrid() const { return grid; }
  uint16_t getActiveFolderId() const { return active; }
  uint32_t viewRevision() const { return revision; }
} tileConfig;
struct lv_obj_t { lv_obj_t* parent=nullptr; bool hidden=false; uint16_t source=0; };
struct lv_event_t { void* data=nullptr; };
struct lv_display_t {};
void* lv_event_get_user_data(lv_event_t* event){return event->data;}
using lv_event_code_t = int;
constexpr int LV_EVENT_SHORT_CLICKED=1, LV_EVENT_LONG_PRESSED=2, LV_EVENT_PRESSED=3;
constexpr int LV_EVENT_DELETE=4, LV_OBJ_FLAG_HIDDEN=1;
constexpr int TILE_POPUP_OPEN_SHORT_PRESS=1;
int getTilePopupOpenMode(const Tile& tile) { return tile.popup; }
bool pin_visible=false, sleeping=false, screensaver=false, connected=true, switch_pending=false;
uint16_t requested_folder=0;
unsigned opens=0, toggles=0, camera_stops=0;
std::vector<uint16_t> requests;
lv_obj_t card, object;
bool is_pin_popup_visible(){return pin_visible;}
bool is_image_screensaver_visible(){return screensaver;}
bool lv_obj_has_flag(lv_obj_t* obj,int){return obj->hidden;}
void lv_obj_remove_event_cb(lv_obj_t*,void(*)(lv_event_t*)){}
void lv_obj_add_event_cb(lv_obj_t*,void(*)(lv_event_t*),int,void*){}
void forgetPopup(lv_event_t*){}
struct {bool isInSleep(){return sleeping;}} powerManager;
struct {bool isMqttConnected(){return connected;}} networkManager;
struct {
  unsigned tab=0;
  unsigned activeTab(){return tab;}
  void requestFolderAccess(uint16_t id,const char*,const char*){
    requests.push_back(id); requested_folder=id;
    if(tileConfig.isFolderPinEnabled(id)) pin_visible=true;
    else {tileConfig.active=id;switch_pending=false;}
  }
} uiManager;
struct {struct Config{int language=0;} cfg; const Config& getConfig(){return cfg;}} configManager;
namespace i18n {struct Text{const char* home="Home";}; Text strings(int){return {};}}
constexpr size_t kMaxFolders=128;
String popup_entity;
lv_obj_t* popup_card=nullptr;
uint16_t popup_source=0;
bool remote_opening=false;
bool g_tiles_loaded[1]={true}, g_tiles_reload_requested[1]={false}, g_folder_switch_pending=false;
lv_obj_t* g_tiles_objs[1][TILES_PER_GRID]={&object};
uint16_t tiles_view_id_for_object(lv_obj_t* obj){return obj?obj->source:0;}
bool tiles_folder_switch_pending(){return switch_pending;}
void tiles_cancel_folder_switch(uint16_t){switch_pending=false;}
void hide_settings_popup(){}
void hide_camera_popup(){++camera_stops;card.hidden=true;}
void hide_light_popup(){card.hidden=true;}
void hide_sensor_popup(){card.hidden=true;}
void hide_weather_popup(){card.hidden=true;}
void hide_energy_popup(){card.hidden=true;}
void hide_media_popup(){card.hidden=true;}
void hide_climate_popup(){card.hidden=true;}
void hide_cover_popup(){card.hidden=true;}
void viewNavigationSource(lv_obj_t*);
void viewNavigationPopupShown(lv_obj_t*,const char*);
void lv_obj_send_event(lv_obj_t* obj,int event,void*){
  if(event==LV_EVENT_PRESSED)return;
  const auto& tile=tileConfig.grid.tiles[0];
  const int popup_event=tile.type==TILE_CAMERA||tile.type==TILE_MEDIA||tile.type==TILE_WEATHER||tile.popup==1 ? 1:2;
  if(event==popup_event){++opens;card.hidden=false;viewNavigationSource(obj);viewNavigationPopupShown(&card,tile.sensor_entity.c_str());}
  else ++toggles;
}
struct ScopedStorageWriteDisplayGuard {};
using SemaphoreHandle_t=int;
int xSemaphoreCreateMutex(){return 1;}
int xSemaphoreTake(int,int){return 1;}
void xSemaphoreGive(int){}
constexpr int pdTRUE=1;
int pdMS_TO_TICKS(int value){return value;}
struct {void println(const char*){}} Serial;
std::map<std::string,uint32_t> counters;
bool write_ok=true;
struct Preferences {
  bool begin(const char*,bool){return true;}
  uint32_t getUInt(const char* key,uint32_t fallback){return counters.count(key)?counters[key]:fallback;}
  size_t putUInt(const char* key,uint32_t value){if(!write_ok)return 0;counters[key]=value;return 4;}
  void end(){}
};
` + definition(navigation, 'struct Pending') + ' pending;\n' + definition(navigation, 'bool popupSupported(')
  + definition(navigation, 'String folderTarget(')
  + definition(navigation, 'String boundedLabel(')
  + definition(navigation, 'String folderLabel(')
  + definition(navigation, 'void cancelPending(')
  + definition(tiles, 'bool tiles_open_view_popup(')
  + definition(navigation, 'size_t firstRequiredFolderStep(')
  + definition(navigation, 'void servicePending(')
  + definition(navigation, 'struct DisplayedView') + ';\n' + definition(navigation, 'DisplayedView displayedView(')
  + definition(navigation, 'void viewNavigationClosePopups(')
  + definition(navigation, 'void viewNavigationSource(')
  + definition(navigation, 'void viewNavigationPopupShown(')
  + definition(persistence, 'static uint16_t reserveNavigationId(')
  + definition(persistence, 'static bool ensureNavigationIds(')
  + String.raw`
void prepare(uint16_t folder,uint16_t tile,std::vector<uint16_t> path){
  pending={};pending.active=true;pending.folder=folder;pending.tile=tile;
  pending.revision=tileConfig.revision;pending.path=path;
  pending.previous_folder=tileConfig.active;
}
int main(){
  using namespace hometiles_view;
  CommandGate gate;
  assert(gate.accept("new","new",1,100,200));
  assert(!gate.accept("new","new",1,100,200));
  assert(!gate.accept("old","new",2,100,200));
  assert(!gate.accept("new","new",2,300,200));
  assert(!gate.accept("new","new",2,100,10101));
  assert(gate.accept("new","new",2,0xfffffff0,20));
  bool changed=false;
  tileConfig.grid.tiles[0].type=TILE_CAMERA;
  tileConfig.grid.tiles[0].sensor_entity="camera.door";
  assert(ensureNavigationIds(tileConfig.grid,changed)&&changed);
  auto id=tileConfig.grid.tiles[0].view_id;assert(id==1);
  changed=false;assert(ensureNavigationIds(tileConfig.grid,changed)&&!changed);
  std::swap(tileConfig.grid.tiles[0],tileConfig.grid.tiles[1]);
  assert(tileConfig.grid.tiles[1].view_id==id);
  std::swap(tileConfig.grid.tiles[0],tileConfig.grid.tiles[1]);
  tileConfig.grid.tiles[0].type=TILE_EMPTY;
  assert(ensureNavigationIds(tileConfig.grid,changed));assert(!tileConfig.grid.tiles[0].view_id);
  tileConfig.grid.tiles[0].type=TILE_CAMERA;
  assert(ensureNavigationIds(tileConfig.grid,changed));assert(tileConfig.grid.tiles[0].view_id>id);
  id=tileConfig.grid.tiles[0].view_id;object.source=id;
  write_ok=false;assert(reserveNavigationId("tile_next")==0);write_ok=true;
  assert(reserveNavigationId("folder_next",3)==3);
  assert(reserveNavigationId("folder_next",1)==4);
  assert(folderLabel(2)=="Home / Room / Room");
  assert(folderLabel(999).empty());
  assert(boundedLabel("A\xc3\xbcZ", 2) == "A");
  assert(boundedLabel("A\xc3\xbcZ", 2, true) == "Z");
  assert(boundedLabel("A\xc3\xbcZ", 3) == "A\xc3\xbc");
  assert(boundedLabel("A\xc3\xbcZ", 3, true) == "\xc3\xbcZ");
  // Select options use line breaks as separators; visual title wrapping must
  // never leak into the navigation catalog consumed by the Bridge.
  const String visual_title = "Waveshare Touch\nLcd 8 Ansicht";
  assert(boundedLabel(visual_title, 80) == "Waveshare Touch Lcd 8 Ansicht");
  assert(visual_title == "Waveshare Touch\nLcd 8 Ansicht");
  assert(boundedLabel("First\r\nSecond", 80) == "First Second");
  assert(boundedLabel("First\rSecond", 80) == "First Second");
  assert(boundedLabel("A\n\xc3\xbcZ", 4) == "A \xc3\xbc");
  tileConfig.folders.push_back({4,0,"Two\nLines","folder",false});
  assert(folderLabel(4) == "Home / Two Lines");
  prepare(2,id,{0,1,2});
  servicePending(100);servicePending(200);
  assert(pin_visible&&tileConfig.active==0&&opens==0);
  servicePending(300);assert(opens==0);
  // Successful local PIN verification commits through the existing folder path.
  pin_visible=false;tileConfig.active=1;
  servicePending(400);servicePending(500);
  assert(tileConfig.active==2&&opens==1&&!pending.active);
  assert(displayedView().current==String("tile:")+String(id));
  card.hidden=true;assert(displayedView().current=="folder:2");
  sleeping=true;assert(std::string(displayedView().mode)=="sleep");sleeping=false;
  screensaver=true;assert(displayedView().current.empty());screensaver=false;
  prepare(1,id,{1});servicePending(600);pin_visible=false;tileConfig.active=0;
  servicePending(700);assert(!pending.active&&opens==1);
  prepare(2,id,{0,2});servicePending(800);tileConfig.revision++;
  servicePending(900);assert(!pending.active&&opens==1);
  prepare(2,id,{0,2});connected=false;servicePending(1000);
  assert(!pending.active&&opens==1);connected=true;
  prepare(2,id,{0,2});viewNavigationSource(&object);assert(!pending.active);
  prepare(2,id,{0,2});servicePending(61000);assert(!pending.active);
  for(auto type:{TILE_CAMERA,TILE_MEDIA,TILE_WEATHER,TILE_SENSOR,TILE_SWITCH,TILE_CLIMATE,TILE_COVER,TILE_ENERGY,TILE_BINARY_SENSOR,TILE_NUMBER,TILE_SELECT,TILE_DATETIME}){
    tileConfig.grid.tiles[0].type=type;
    for(int mode:{0,1}){tileConfig.grid.tiles[0].popup=mode;assert(tiles_open_view_popup(id));}
  }
  assert(toggles==0);
  assert(!tiles_open_view_popup(60000));
  g_folder_switch_pending=true;assert(!tiles_open_view_popup(id));g_folder_switch_pending=false;
  g_tiles_reload_requested[0]=true;assert(!tiles_open_view_popup(id));g_tiles_reload_requested[0]=false;
  viewNavigationClosePopups();assert(camera_stops==1&&card.hidden);
  auto before=opens;

  // A remote popup in the visible folder must not leave and rebuild its path.
  tileConfig.active=2;requests.clear();
  prepare(2,id,{0,1,2});pending.step=firstRequiredFolderStep(pending.path);
  before=opens;servicePending(100);
  assert(requests.empty()&&tileConfig.active==2&&opens==before+1&&!pending.active);
  // Selecting that folder closes the popup without another folder request.
  viewNavigationClosePopups();prepare(2,0,{0,1,2});
  pending.step=firstRequiredFolderStep(pending.path);servicePending(100);
  assert(requests.empty()&&displayedView().current=="folder:2"&&!pending.active);
  // Reopening an already unlocked protected folder also needs no new PIN.
  tileConfig.active=1;prepare(1,id,{0,1});
  pending.step=firstRequiredFolderStep(pending.path);servicePending(100);
  assert(requests.empty()&&!pin_visible&&tileConfig.active==1);
  // Only the missing descendant is traversed; its own PIN still applies.
  tileConfig.folders.push_back({3,2,"Protected","folder",true});
  tileConfig.active=2;prepare(3,id,{0,1,2,3});
  pending.step=firstRequiredFolderStep(pending.path);before=opens;
  servicePending(100);assert(requests==std::vector<uint16_t>{3}&&pin_visible);
  servicePending(200);assert(opens==before&&tileConfig.active==2);
  pin_visible=false;tileConfig.active=3;servicePending(300);
  assert(opens==before+1&&!pending.active);
  // Unrelated routes retain the complete existing access path.
  tileConfig.active=3;assert(firstRequiredFolderStep({0,4})==0);
  // Hidden, locked and transitioning screens cannot authorize a shortcut.
  for(int mode=0;mode<5;++mode){
    uiManager.tab=mode==0?3:0;pin_visible=mode==1;sleeping=mode==2;
    screensaver=mode==3;switch_pending=mode==4;
    assert(firstRequiredFolderStep({0,1,2,3})==0);
  }
  uiManager.tab=0;pin_visible=false;sleeping=false;screensaver=false;switch_pending=false;
  // A local move after receipt must not reuse access to the previous branch.
  tileConfig.active=2;requests.clear();prepare(3,id,{0,1,2,3});
  pending.step=firstRequiredFolderStep(pending.path);tileConfig.active=0;
  before=opens;servicePending(100);
  assert(!pending.active&&requests.empty()&&opens==before);
  tileConfig.active=2;prepare(3,id,{0,1,2,3});
  pending.step=firstRequiredFolderStep(pending.path);sleeping=true;servicePending(100);
  assert(!pending.active&&requests.empty()&&opens==before);sleeping=false;
  prepare(3,id,{0,1,2,3});pending.step=firstRequiredFolderStep(pending.path);
  switch_pending=true;servicePending(100);
  assert(!pending.active&&requests.empty()&&switch_pending);switch_pending=false;
  // A full route after waking still tolerates the earlier lock-to-Home switch.
  prepare(3,id,{0,1,2,3});tileConfig.active=0;servicePending(100);
  assert(pending.active&&requests==std::vector<uint16_t>{0});cancelPending();
}
`;

assert.match(navigation, /requestFolderAccess\(next/);
assert.match(navigation, /gate\.accept/);
assert.match(navigation, /doc\["revision"\].as<uint32_t>\(\) != tileConfig.viewRevision/);
assert.match(navigation, /wakeFromDisplaySleep\("view command"\)/);
assert.match(navigation, /const size_t first_step = firstRequiredFolderStep\(path\);[\s\S]*hide_pin_popup\(\);[\s\S]*pending.step = first_step/);
assert.match(persistence, /out\.reserved\[1\] = static_cast<uint8_t>\(in\.view_id\)/);
assert.match(persistence, /in\.reserved\[2\].*<< 8/);
for (const type of ['camera','climate','cover','energy','light','media','sensor','weather']) {
  const source = read(`src/ui/popups/${type}/${type}_popup.cpp`);
  assert.match(source, /viewNavigationPopupShown/);
  assert.match(source, /hide_camera_popup\(\)/);
}
const output = resolve(root, 'build/host-view-navigation');
mkdirSync(output, {recursive:true});
const source = resolve(output, 'test.cpp');
const binary = resolve(output, process.platform === 'win32' ? 'test.exe' : 'test');
writeFileSync(source, harness);
const compiler = [process.env.CXX,'clang++','g++','c++'].filter(Boolean).find(command => spawnSync(command,['--version']).status===0);
assert(compiler, 'Native C++ compiler is required');
let result=spawnSync(compiler,['-std=c++17','-I',root,source,'-o',binary],{encoding:'utf8'});
assert.equal(result.status,0,result.stdout+result.stderr);
result=spawnSync(binary,[],{encoding:'utf8'});
assert.equal(result.status,0,result.stdout+result.stderr);
console.log('View navigation and persistent identity regressions passed.');
