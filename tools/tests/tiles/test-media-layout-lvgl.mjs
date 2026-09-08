import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const renderer=read('src/types/media/renderer.cpp');
const fn=(src,name)=>{const f=cppFunctionDefinitions(src).find(f=>f.name===name);assert(f,name);return f.source;};
const host=await lvglHost(root);
if(!host){console.log('SKIP: Media layout rendering needs LVGL and a host compiler');process.exit(0);}
const out=path.join(root,'build/tests/media-layout-lvgl');fs.mkdirSync(out,{recursive:true});
const control=fn(renderer,'create_media_control_button');
// ESP32 uses long for int32_t/LVGL coordinates, while the host uses int.
// Preserve that return-type distinction at template-expression boundaries.
const fonts=read('src/tiles/runtime/tile_renderer_fonts.h').replace(/^#include.*$/gm,'').replace('#pragma once','').replaceAll('constexpr lv_coord_t','constexpr long');
const cpp=String.raw`
#include <lvgl.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include "src/ui/shared/title_label.h"
#include "src/types/media/cover_geometry.h"
extern "C" { LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_48); }
${fonts}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
class String:public std::string{public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}void trim(){auto a=find_first_not_of(" \r\n");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \r\n")-a+1);}};
${read('src/types/media/widgets.h').replace(/^#.*$/gm,'')}
${read('src/types/media/content_layout.cpp').replace(/^#include.*$/gm,'')}
${renderer.slice(renderer.indexOf('#if defined(DEVICE_WAVESHARE_4B)'),renderer.indexOf('struct MediaEventData'))}
enum class GridType{TAB0,SCREENSAVER};constexpr int TILES_PER_GRID=16;
using TileType=int;constexpr int TILE_MEDIA=7,GRID_COLS=4,GRID_ROWS=4;
${read('src/tiles/config/tile_config.h').match(/static constexpr uint8_t MEDIA_TILE_MIN_SPAN[^]*?(?=\/\/ A media tile must)/)[0]}
struct Tile{String title,icon_name,sensor_entity;int span_w=2,span_h=2;};
MediaTileWidgets widgets[16];
MediaTileWidgets* tile_renderer_get_media_widgets(GridType){return widgets;}
struct Logger{void println(const char*){}}Serial;
struct Bridge{String findEntityIcon(const String&){return "speaker";}String findSensorName(const String&){return "Player";}String findSensorInitialValue(const String&){return "";}}haBridgeConfig;
struct Config{const char*language="en";};struct Manager{Config cfg;const Config&getConfig(){return cfg;}}configManager;
namespace i18n{struct Strings{const char*media_no_playback="No playback";};const Strings&strings(const char*){static Strings s;return s;}}
uint32_t tileBgColorOrDefault(const Tile&,uint32_t d){return d;}
uint32_t brighten_rgb_color(uint32_t c,uint32_t){return c;}
String normalizeMdiIconName(const String&s){return s;}
String getMdiChar(const String&s){return s=="play"?"\xF3\xB0\x8F\xA8":s=="skip-next"?"\xF3\xB0\x92\xAD":s=="skip-previous"?"\xF3\xB0\x92\xAE":"\xF3\xB0\x92\x83";}
String media_friendly_name_from_entity(const String&){return "Player";}
void set_label_style(lv_obj_t*l,lv_color_t c,const lv_font_t*f){lv_obj_set_style_text_color(l,c,0);lv_obj_set_style_text_font(l,f,0);}
void enable_event_bubble(lv_obj_t*){}void apply_media_text_scroll_style(lv_obj_t*){}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'),'disable_pressed_button_animation')}
void set_tile_grid_cell(lv_obj_t*c,int,int,int w,int h){lv_obj_set_size(c,w*CELL_W+(w-1)*GAP,h*CELL_H+(h-1)*GAP);lv_obj_center(c);}
void cover_ref_delete_cb(lv_event_t*e){delete static_cast<MediaCoverRef*>(lv_event_get_user_data(e));}
${renderer.match(/struct MediaPopupEventData \{[\s\S]*?\n};/)[0]}
void show_media_popup_event_cb(lv_event_t*){}
void media_popup_event_data_delete_cb(lv_event_t*e){delete static_cast<MediaPopupEventData*>(lv_event_get_user_data(e));}
void update_media_tile_state(GridType,uint8_t,const char*){}
${control.slice(0,control.indexOf('  MediaEventData* data'))}return label;}
${fn(renderer,'render_media_tile')}
void snapshot(const char*file,const std::vector<uint32_t>&pixels){std::ofstream out(file,std::ios::binary);auto u16=[&](uint16_t v){out.write(reinterpret_cast<char*>(&v),2);};auto u32=[&](uint32_t v){out.write(reinterpret_cast<char*>(&v),4);};out.write("BM",2);u32(54+pixels.size()*4);u32(0);u32(54);u32(40);u32(SCREEN_WIDTH);u32(-SCREEN_HEIGHT);u16(1);u16(32);u32(0);u32(pixels.size()*4);u32(0);u32(0);u32(0);u32(0);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4);}
int main(int argc,char**argv){lv_init();auto*display=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t>pixels(SCREEN_WIDTH*SCREEN_HEIGHT);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*4,LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});lv_theme_default_init(display,lv_color_hex(0x26A69A),lv_color_hex(0xC14444),false,&ui_font_20);
 uint8_t min_w=1,min_h=1;clamp_media_tile_span(TILE_MEDIA,min_w,min_h);assert(min_w==2&&min_h==2);min_w=min_h=9;clamp_media_tile_span(TILE_MEDIA,min_w,min_h);assert(min_w==3&&min_h==3);
 int previous=0;
 for(int h=2;h<=4;++h)for(int w=2;w<=4;++w){Tile t;t.span_w=w;t.span_h=h;t.title="Living room player with a very long title";t.sensor_entity="media_player.test";auto*card=render_media_tile(lv_screen_active(),0,0,t,0,GridType::TAB0);auto&m=widgets[0];
  for(bool subtitle:{true,false})for(bool cover:{true,false}){lv_label_set_text(m.media_title_label,"A long song title that scrolls within the space beside its artwork");lv_label_set_text(m.media_subtitle_label,"Artist with a long name");if(subtitle)lv_obj_remove_flag(m.media_subtitle_label,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(m.media_subtitle_label,LV_OBJ_FLAG_HIDDEN);if(cover)lv_obj_remove_flag(m.cover_clip,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(m.cover_clip,LV_OBJ_FLAG_HIDDEN);set_media_cover_text_layout(m,cover);lv_obj_update_layout(card);
   lv_area_t content,art,title,sub,buttons,header;lv_obj_get_content_coords(card,&content);lv_obj_get_coords(m.cover_clip,&art);lv_obj_get_coords(m.media_title_label,&title);lv_obj_get_coords(m.media_subtitle_label,&sub);lv_obj_get_coords(lv_obj_get_parent(m.play_pause_label),&buttons);lv_obj_get_coords(m.title_label,&header);
   assert(art.x2-art.x1==art.y2-art.y1&&lv_obj_get_width(m.cover_clip)<=240);assert(art.y1>header.y2&&art.y2<buttons.y1);assert(title.y1>=art.y1&&title.y2<buttons.y1);assert(title.x2<=content.x2);if(cover)assert(title.x1>art.x2);if(subtitle)assert(sub.y1>title.y2&&sub.y2<buttons.y1&&sub.x1==title.x1);
   if(cover&&subtitle&&w==h){const int side=lv_obj_get_width(m.cover_clip);assert(side>previous);previous=side;std::cout<<w<<"x"<<h<<": cover="<<side<<", title width="<<lv_obj_get_width(m.media_title_label)<<"\n";lv_obj_set_style_bg_color(m.cover_clip,lv_color_hex(0x447D7A),0);lv_obj_set_style_bg_opa(m.cover_clip,LV_OPA_COVER,0);lv_refr_now(display);if(argc>1)snapshot((String(argv[1])+"-"+std::to_string(w)+".bmp").c_str(),pixels);}
  }
  lv_obj_delete(card);widgets[0]={};
 }
 lv_deinit();std::cout<<"Real Media renderer: supported 2..3 by 2..3 geometry, defensive 4-cell layout, 1x1 normalization, long titles, missing values and growing artwork passed\n";
}
`;
// Media's persisted size contract normalizes a requested 1x1 to at least 2x2.
const normalize=fn(read('src/tiles/config/tile_config.h'),'clamp_media_tile_span');
assert(normalize.includes('MEDIA_TILE_MIN_SPAN'));
const source=path.join(out,'test.cpp');fs.writeFileSync(source,cpp);
for(const [name,width,height,cellW,cellH,gap,define] of [['s3',480,480,111,111,10,'DEVICE_LAYOUT_480X480'],['p4',1280,800,168,145,16,'']]){
 const binary=path.join(out,name+(process.platform==='win32'?'.exe':''));
 let result=spawnSync(host.cxx,[...host.flags,'-std=c++17',`-DSCREEN_WIDTH=${width}`,`-DSCREEN_HEIGHT=${height}`,`-DCELL_W=${cellW}`,`-DCELL_H=${cellH}`,`-DGAP=${gap}`,...(define?['-D'+define]:[]),source,host.archive,'-o',binary],{encoding:'utf8'});assert.equal(result.status,0,result.stdout+result.stderr);
 result=spawnSync(binary,[path.join(out,name)],{encoding:'utf8'});assert.equal(result.status,0,name+': '+result.stdout+result.stderr);fs.writeFileSync(path.join(out,name+'.log'),result.stdout+result.stderr);
}
console.log('Media tile geometry and rendered snapshots passed on P4 and S3.');
