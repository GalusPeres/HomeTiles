import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = f => fs.readFileSync(path.join(root, f), 'utf8');
const host = await lvglHost(root);
if (!host) { console.log('SKIP: Global radius needs native LVGL'); process.exit(0); }
const out = path.join(root, 'build/tests/global-radius');
fs.mkdirSync(out, {recursive:true});
fs.writeFileSync(path.join(out, 'FS.h'), '#pragma once\nnamespace fs {class FS{};}\n');
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
const save = cppFunctionDefinitions(read('src/core/config/config_manager.cpp')).find(f => f.name === 'ConfigManager::saveTileRadius').source;
const load = read('src/core/config/config_manager.cpp').match(/config\.tile_radius = tile_radius::clamp\(prefs.getUShort[^;]+;/)[0];
const cpp = `
#include <lvgl.h>
#include <atomic>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include "src/core/config/tile_radius.h"
#include "src/types/climate/layout.h"
namespace Device {void storageWriteBegin(){} void storageWriteEnd(){}}
namespace BatchedNvsWrite {
constexpr bool kNeedsDisplayGuard=true;
std::map<std::string,uint16_t> stored;
bool fail_open=false,fail_write=false; int writes=0;
struct Preferences {
 bool begin(const char*,bool){return !fail_open;}
 size_t putUShort(const char* key,uint16_t value){if(fail_write)return 0;stored[key]=value;++writes;return 2;}
 uint16_t getUShort(const char* key,uint16_t fallback){return stored.count(key)?stored[key]:fallback;}
};
bool finish(Preferences&){return true;}
}
struct Logger{void println(const char*){}}Serial;
const char* PREF_NAMESPACE="tab5_config";
struct Config{bool tile_borders=true;uint16_t tile_radius=tile_radius::kMinimum;};
struct ConfigManager {
 Config config; const Config& getConfig(){return config;}
 bool saveTileRadius(uint16_t);
 void load(){BatchedNvsWrite::Preferences prefs;${load}}
}configManager;
${save}
${surfaceStyleHost(root)}
int main(){
 using namespace BatchedNvsWrite;
 configManager.load();assert(configManager.config.tile_radius==tile_radius::kMinimum);
 assert(tile_radius::kMaximum==(Device::kGridCellH-Device::kGridGap+2)/4);
 lv_init(); auto*d=lv_display_create(Device::kScreenWidth,Device::kScreenHeight);
 std::vector<uint16_t> buffer(Device::kScreenWidth*16);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(d,buffer.data(),nullptr,buffer.size()*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(d,[](lv_display_t*d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 // A hidden, separate cached screen must update too, without reconstruction.
 auto*cached=lv_obj_create(nullptr);auto*outer=lv_obj_create(cached);auto*inner=lv_obj_create(outer);
 lv_obj_set_size(outer,Device::kGridCellW,Device::kGridCellH);
 lv_obj_set_style_pad_all(outer,0,0);lv_obj_set_style_border_width(outer,0,0);
 lv_obj_set_size(inner,Device::kGridCellW-2*climate_layout::kOuterInset,50);
 lv_obj_set_pos(inner,climate_layout::kOuterInset,climate_layout::kOuterInset);
 ui_surface_style::apply_radius(outer,tile_radius::kMinimum);
 ui_surface_style::apply_radius(inner,climate_layout::kControlRadius);
 auto*popup=lv_obj_create(lv_layer_top());ui_surface_style::apply_radius(popup,tile_radius::kMinimum);
 for(int r=tile_radius::kMinimum;r<=tile_radius::kMaximum;++r){
   ui_surface_style::preview_radius(r);ui_surface_style::process_pending_updates();
   assert(writes==0 && configManager.config.tile_radius==tile_radius::kMinimum);
   assert(lv_obj_get_style_radius(outer,LV_PART_MAIN)==r && lv_obj_get_style_radius(popup,LV_PART_MAIN)==r);
   assert(lv_obj_get_style_radius(inner,LV_PART_MAIN)+climate_layout::kOuterInset==r);
   lv_obj_add_state(outer,LV_STATE_PRESSED);assert(lv_obj_get_style_radius(outer,LV_PART_MAIN)==r);lv_obj_remove_state(outer,LV_STATE_PRESSED);
 }
 assert(configManager.saveTileRadius(tile_radius::kMaximum));
 ui_surface_style::request_global_radius_refresh();ui_surface_style::process_pending_updates();
 assert(writes==1);assert(configManager.saveTileRadius(tile_radius::kMaximum));assert(writes==1);
 ConfigManager reboot;reboot.load();assert(reboot.config.tile_radius==tile_radius::kMaximum);
 fail_write=true;assert(!configManager.saveTileRadius(tile_radius::kMinimum));assert(configManager.config.tile_radius==tile_radius::kMaximum);fail_write=false;
 fail_open=true;assert(!configManager.saveTileRadius(tile_radius::kMinimum));fail_open=false;
 assert(configManager.saveTileRadius(0));ui_surface_style::request_global_radius_refresh();ui_surface_style::process_pending_updates();
 assert(lv_obj_get_style_radius(inner,LV_PART_MAIN)==climate_layout::kControlRadius);
 stored["tile_radius"]=65535;reboot.load();assert(reboot.config.tile_radius==tile_radius::kMaximum);
 lv_obj_delete(cached);lv_deinit();
}
`;
const source=path.join(out,'test.cpp');fs.writeFileSync(source,cpp);
for(const {buildProfile,define} of JSON.parse(read('tools/device-profiles.json')).profiles){
  const binary=path.join(out,buildProfile+(process.platform==='win32'?'.exe':''));
  const built=spawnSync(host.cxx,[...host.flags,'-std=c++17','-I',out,'-DHOMETILES_CI_TARGET',`-D${define}`,source,host.archive,'-o',binary],{encoding:'utf8'});
  assert.equal(built.status,0,built.stdout+built.stderr);
  const run=spawnSync(binary,[],{encoding:'utf8'});assert.equal(run.status,0,buildProfile+run.stdout+run.stderr);
}
console.log('Global radius: all profile limits, cached LVGL surfaces, concentric Climate corners, NVS reload and failed writes pass');
