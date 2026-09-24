import {radiusPolicyHost,surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root,p),'utf8');
const fn = (p,n) => cppFunctionDefinitions(read(p)).find(f=>f.name===n).source;
const strip = s=>s.replace(/^#include.*$/gm,'').replaceAll('#pragma once','');
const host = await lvglHost(root);
if (!host) { console.log('SKIP: Half-grid geometry needs native LVGL');process.exit(0); }
const out=path.join(root,'build/tests/half-grid');fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'Arduino.h'),'#pragma once\n#include <cstdint>\n#include <cstddef>\n');
fs.writeFileSync(path.join(out,'FS.h'),'#pragma once\nnamespace fs {class FS{};}\n');
const cpp=`
#include <lvgl.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <atomic>
#include <string>
#include "src/tiles/config/tile_geometry.h"
extern "C" { LV_FONT_DECLARE(ui_font_12); LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16); LV_FONT_DECLARE(ui_font_20); LV_FONT_DECLARE(ui_font_24); LV_FONT_DECLARE(ui_font_28); LV_FONT_DECLARE(ui_font_32); LV_FONT_DECLARE(ui_font_40); }
#include "src/tiles/runtime/tile_renderer_fonts.h"
constexpr int GRID_COLS=Device::kGridCols,GRID_ROWS=Device::kGridRows;
constexpr int GRID_CELL_W=Device::kGridCellW,GRID_CELL_H=Device::kGridCellH,GRID_GAP=Device::kGridGap;
${radiusPolicyHost(root,'Device::kGridCellH','Device::kGridGap')}
struct Config{int tile_radius=tile_radius::kMinimum;bool tile_borders=true;};
struct Manager{Config config;const Config& getConfig(){return config;}}configManager;
${surfaceStyleHost(root)}
struct Tile{int type=TILE_SENSOR;float col=0,row=0,span_w=1,span_h=1;uint8_t sensor_display_mode=0;};
${fn('src/tiles/config/tile_config.h','tileBorderEnabled')}
struct PackedQuarterGridV7{uint8_t version=7,quarter_index=0,reserved[2]={};};
template<class P,class S>void clamp_media_tile_layout(int,P&,P&,S&,S&){}
${fn('src/ui/tabs/tiles/tab_tiles_unified.cpp','get_tile_layout')}
${fn('src/tiles/config/tile_config.cpp','packGeometry')}
${fn('src/tiles/config/tile_config.cpp','unpackGeometry')}
${fn('src/tiles/runtime/tile_renderer_shared.h','apply_fractional_tile_geometry')}
${fn('src/tiles/runtime/tile_renderer.cpp','set_tile_grid_cell')}
${strip(read('src/tiles/runtime/compact_sensor_layout.h'))}
int main(){
 assert(tile_layout::value_font_for_choice(0,nullptr)==nullptr);
 assert(tile_layout::value_font_for_choice(255,&ui_font_20)==&ui_font_20);
 assert(tile_layout::value_font_for_choice(1,nullptr)==tile_layout::content_font_20());
 assert(tile_layout::value_font_for_choice(2,nullptr)==tile_layout::content_font_24());
 assert(tile_layout::value_font_for_choice(3,nullptr)==tile_layout::content_font_32());
 assert(tile_layout::value_font_for_choice(4,nullptr)==tile_layout::content_font_40());
 for(int type:{TILE_SENSOR,TILE_BINARY_SENSOR,TILE_ENERGY}) {
  for(float row:{0.f,0.5f,1.f,1.5f}) {
   Tile input{type,0.5f,row,2,0.5f};
   PackedQuarterGridV7 record; Tile loaded{type,0,std::floor(row),2,1};
   float col,row2,w,h;assert(get_tile_layout(input,col,row2,w,h));
   assert(col==.5f && row2==input.row && w==2 && h==.5f);
   packGeometry(input,record,3);
   assert(record.version==7 && record.quarter_index==0 && record.reserved[0]==0);
   assert(record.reserved[1]==(row==std::floor(row)?0x90:0xb0));
   unpackGeometry(record,3,loaded);
   assert(loaded.col==input.col && loaded.row==input.row && loaded.span_w==2 && loaded.span_h==0.5f);
  }
 }
 PackedQuarterGridV7 legacy;Tile full{TILE_SENSOR,1,1,2,1};packGeometry(full,legacy,0);
 assert(legacy.reserved[0]==0 && legacy.reserved[1]==0);unpackGeometry(legacy,0,full);
 assert(full.col==1 && full.row==1 && full.span_w==2 && full.span_h==1);
 assert(!tile_geometry::supported(TILE_SWITCH,0,0,2,0.5));
 assert(tile_geometry::supported(TILE_SENSOR,0,0,1,0.5));
 assert(!tile_geometry::supported(TILE_SENSOR,0,0,.5f,.5f));
 for(float width=1;width<=GRID_COLS;width+=.5f)assert(tile_geometry::supported(TILE_SENSOR,0,0,width,.5f));
 assert(tile_geometry::supported(TILE_SENSOR,0,0,1.5f,.5f));
 assert(tile_geometry::supported(TILE_BINARY_SENSOR,0,0,1.5f,.5f));
 Tile narrow{TILE_SENSOR,.5f,.5f,1.5f,.5f};PackedQuarterGridV7 packedNarrow;
 packGeometry(narrow,packedNarrow,0);Tile narrowLoaded{TILE_SENSOR,0,0,2,1};
 unpackGeometry(packedNarrow,0,narrowLoaded);
 assert(narrowLoaded.col==.5f && narrowLoaded.row==.5f && narrowLoaded.span_w==1.5f && narrowLoaded.span_h==.5f);
 assert(!tile_geometry::supported(TILE_SENSOR,0,0,2,0.25));
 assert(!tile_geometry::supported(TILE_SENSOR,0,0,2,NAN));
 assert(!tile_geometry::supported(TILE_SENSOR,Device::kGridCols-1,0,2,0.5));
 for(int cell:{GRID_CELL_W,GRID_CELL_H}) {
  assert(tile_geometry::extent(0,0.5,cell,GRID_GAP)+GRID_GAP+tile_geometry::extent(0.5,0.5,cell,GRID_GAP)==cell);
  assert(tile_geometry::extent(0,2,cell,GRID_GAP)==cell*2+GRID_GAP);
 }
 lv_init();auto*d=lv_display_create(Device::kScreenWidth,Device::kScreenHeight);
 for(int type:{TILE_CLOCK,TILE_TEXT,TILE_SENSOR}) {
  Tile tile;tile.type=type;assert(tileBorderEnabled(tile));tile.sensor_display_mode=1;
  assert(tileBorderEnabled(tile)==(type==TILE_SENSOR));
  auto*card=lv_button_create(lv_screen_active());
  if(!tileBorderEnabled(tile)) ui_surface_style::disable_tile_border(card);
  ui_surface_style::apply_global_tile_border(card);
  ui_surface_style::request_global_tile_border_refresh();ui_surface_style::process_pending_updates();
  ui_surface_style::apply_tile_border(card,true);
  lv_obj_add_state(card,LV_STATE_PRESSED);
  assert(lv_obj_get_style_outline_width(card,LV_PART_MAIN)==(type==TILE_SENSOR?1:0));
  lv_obj_delete(card);
 }

 assert(tile_geometry::supported(TILE_CLOCK,0,0,1,.5f));
 assert(tile_geometry::supported(TILE_CLOCK,0,0,2.5f,.5f));
 assert(tile_geometry::compact_clock(TILE_CLOCK,1,.5f));
 assert(!tile_geometry::compact_clock(TILE_CLOCK,2,1));
 assert(!tile_geometry::compact(TILE_CLOCK,1,.5f));
 assert(tile_geometry::compact(TILE_SENSOR,2,.5f));
 assert(!tile_geometry::compact(TILE_SENSOR,2,1));
 for(int type:{TILE_SENSOR,TILE_BINARY_SENSOR,TILE_ENERGY}) for(float width:{1.f,1.5f,2.f,2.5f}) {
  const float height=.5f;
  Tile tile{type,0,0,width,height};
  auto*card=lv_button_create(lv_screen_active());lv_obj_remove_style_all(card);
  lv_obj_set_size(card,tile_geometry::extent(0,width,GRID_CELL_W,GRID_GAP),tile_geometry::extent(0,height,GRID_CELL_H,GRID_GAP));
  auto*icon=lv_label_create(card);lv_label_set_text(icon,"i");
  auto*title=lv_label_create(card);hometiles_title::tile(title,"A very long room\\nUpstairs",true);
  auto*value=lv_label_create(card);lv_label_set_text(value,"22.5 C");
  compact_sensor_layout::apply(card,icon,title,value,tile);lv_obj_update_layout(card);
  const auto* disc=lv_obj_get_parent(icon);
  assert(lv_obj_get_style_radius(disc,LV_PART_MAIN)==tile_radius::kMinimum-compact_sensor_layout::inset());
  lv_area_t bounds,t,v;lv_obj_get_coords(card,&bounds);lv_obj_get_coords(title,&t);lv_obj_get_coords(value,&v);
  assert(std::strchr(lv_label_get_text(title),'\\n')==nullptr);
  assert(t.x1==v.x1 && t.y2<v.y1 && v.y2<=bounds.y2);
  assert(t.x2<=bounds.x2 && v.x2<=bounds.x2);
  assert(lv_obj_get_style_text_align(value,LV_PART_MAIN)==LV_TEXT_ALIGN_LEFT);
  lv_obj_add_state(card,LV_STATE_PRESSED);lv_obj_update_layout(card);
  lv_area_t pressed;lv_obj_get_coords(value,&pressed);assert(memcmp(&pressed,&v,sizeof(v))==0);
  // Half-height values always use the 20 px title size, whatever size was chosen.
  assert(lv_obj_get_style_text_font(value, LV_PART_MAIN)==tile_layout::content_font_20());
  lv_obj_delete(card);
 }
 auto* grid=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(grid);
 lv_obj_set_size(grid,Device::kScreenWidth,Device::kScreenHeight);
 lv_obj_set_style_pad_all(grid,Device::kGridPad,0);
 lv_obj_set_style_pad_row(grid,GRID_GAP,0);lv_obj_set_style_pad_column(grid,GRID_GAP,0);
 int32_t cols[]={GRID_CELL_W,GRID_CELL_W,GRID_CELL_W,GRID_CELL_W,LV_GRID_TEMPLATE_LAST};
 int32_t rows[]={GRID_CELL_H,GRID_CELL_H,LV_GRID_TEMPLATE_LAST};
 lv_obj_set_grid_dsc_array(grid,cols,rows);
 auto*upper=lv_button_create(grid);auto*lower=lv_button_create(grid);auto*fullTile=lv_button_create(grid);
 set_tile_grid_cell(upper,0,0,2,1);set_tile_grid_cell(lower,0,0,2,1);set_tile_grid_cell(fullTile,2,0,2,1);
 apply_fractional_tile_geometry(upper,Tile{TILE_SENSOR,0,0,2,.5});
 apply_fractional_tile_geometry(lower,Tile{TILE_SENSOR,0,.5,2,.5});
 lv_obj_update_layout(grid);
 lv_area_t upperRect,lowerRect,fullRect;lv_obj_get_coords(upper,&upperRect);lv_obj_get_coords(lower,&lowerRect);lv_obj_get_coords(fullTile,&fullRect);
 assert(upperRect.y1==fullRect.y1 && lowerRect.y2==fullRect.y2);
 assert(lowerRect.y1-upperRect.y2-1==GRID_GAP);
 assert(fullRect.x1-upperRect.x2-1==GRID_GAP);
 lv_deinit();
}
`;
const source=path.join(out,'test.cpp');fs.writeFileSync(source,cpp);
for(const {buildProfile,define} of JSON.parse(read('tools/device-profiles.json')).profiles){
 const binary=path.join(out,buildProfile+(process.platform==='win32'?'.exe':''));
 const compiled=spawnSync(host.cxx,[...host.flags,'-std=c++17','-I',out,'-DHOMETILES_CI_TARGET',`-D${define}`,source,host.archive,'-o',binary],{encoding:'utf8'});
 assert.equal(compiled.status,0,compiled.stdout+compiled.stderr);
 const result=spawnSync(binary,[],{encoding:'utf8'});assert.equal(result.status,0,buildProfile+result.stdout+result.stderr);
}
console.log('Half-grid: legacy headers, fractional reload, profile geometry, compact label bounds and pressed state pass');
