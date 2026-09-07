import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const host=await lvglHost(root);
if(!host){console.log('SKIP: Title layout requires native LVGL');process.exit(0);}
for(const type of ['sensor','binary_sensor','cover','climate','weather','energy','media','camera','navigate','scene','clock','pixelanim','text','switch'])
  assert(fs.readFileSync(path.join(root,`src/types/${type}/renderer.cpp`),'utf8').includes('hometiles_title::tile('),type);
const out=path.join(root,'build/tests/title-label-lvgl');fs.mkdirSync(out,{recursive:true});
const source=String.raw`
#include "src/ui/shared/title_label.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <fstream>
#include <iostream>
extern "C" { LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16); LV_FONT_DECLARE(ui_font_20); }
void image(const char* path,const std::vector<uint32_t>& pixels,int w,int h){
 std::ofstream f(path,std::ios::binary);auto u16=[&](unsigned v){f.put(v);f.put(v>>8);};auto u32=[&](unsigned v){u16(v);u16(v>>16);};
 f.put('B');f.put('M');u32(54+w*h*4);u32(0);u32(54);u32(40);u32(w);u32(h);u16(1);u16(32);u32(0);u32(w*h*4);u32(0);u32(0);u32(0);u32(0);
 for(int y=h-1;y>=0;--y)f.write(reinterpret_cast<const char*>(pixels.data()+y*w),w*4);
}
int main(int argc,char**argv){
 lv_init();auto* display=lv_display_create(720,480);std::vector<uint32_t> pixels(720*480);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*4,LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
 int column=0;
 for(auto* font:{&ui_font_14,&ui_font_16,&ui_font_20}){
  int row=0;
  for(bool top:{true,false})for(int width:{110,220}){
   auto* card=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(card);lv_obj_set_size(card,width,100);lv_obj_set_pos(card,column*240+5,row++*116+5);
   lv_obj_set_style_bg_color(card,lv_color_hex(0x2a2a2a),0);lv_obj_set_style_bg_opa(card,LV_OPA_COVER,0);lv_obj_set_style_radius(card,12,0);lv_obj_set_style_pad_all(card,16,0);
   auto* title=lv_label_create(card);lv_obj_set_style_text_font(title,font,0);lv_obj_set_style_text_color(title,lv_color_white(),0);
   lv_obj_set_width(title,LV_PCT(top?70:100));lv_obj_set_style_text_align(title,top?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_CENTER,0);
   hometiles_title::tile(title,"Desk",top);lv_obj_align(title,top?LV_ALIGN_TOP_RIGHT:LV_ALIGN_CENTER,0,top?4:20);lv_obj_update_layout(card);
   lv_area_t one,two;lv_obj_get_coords(title,&one);
   hometiles_title::tile(title,"Room\nDesk",top);lv_obj_update_layout(card);lv_obj_get_coords(title,&two);
   assert(abs(one.y1+one.y2-two.y1-two.y2)<=1&&"Two title lines must keep the one-line vertical center");
   assert(std::string(hometiles_title::text(title))=="Room\nDesk");
   const auto count=lv_obj_get_event_count(title);
   for(int repeat=0;repeat<20;++repeat){hometiles_title::tile(title,"A very long title that must end in an ellipsis",top);lv_obj_update_layout(card);}
   assert(lv_obj_get_event_count(title)==count&&"Repeated updates must reuse the title owner");
   assert(std::string(lv_label_get_text(title)).find("...")!=std::string::npos);
   assert(std::string(hometiles_title::text(title))=="A very long title that must end in an ellipsis");
   hometiles_title::tile(title,"Long first title line\nLong second title line",top);lv_obj_update_layout(card);
   const std::string shown=lv_label_get_text(title);assert(std::count(shown.begin(),shown.end(),'\n')==1);
   assert(hometiles_title::text_width(title,shown.substr(0,shown.find('\n')))<=lv_obj_get_content_width(title));
   lv_obj_set_width(title,400);lv_obj_update_layout(card);assert(std::string(lv_label_get_text(title))=="Long first title line\nLong second title line");
   lv_obj_set_width(title,LV_PCT(top?70:100));hometiles_title::tile(title,"Room\nDesk",top);lv_obj_update_layout(card);
   assert(hometiles_title::normalize("First\r\nSecond\nThird")=="First\nSecond Third");
  }++column;
 }
 lv_refr_now(display);if(argc>1)image(argv[1],pixels,720,480);
 for(int i=0;i<100;++i){auto*l=lv_label_create(lv_screen_active());hometiles_title::set(l,"Header\nSecond line");lv_obj_update_layout(l);lv_obj_delete(l);}
 lv_deinit();std::cout<<"Native title centers, ellipsis, resize, full text and lifecycle passed\n";
}
`;
fs.writeFileSync(path.join(out,'test.cpp'),source);
const binary=path.join(out,process.platform==='win32'?'test.exe':'test');
let r=spawnSync(host.cxx,[...host.flags,'-std=c++17',path.join(out,'test.cpp'),host.archive,'-o',binary],{encoding:'utf8'});
assert.equal(r.status,0,r.stdout+r.stderr);
r=spawnSync(binary,[path.join(out,'titles.bmp')],{encoding:'utf8',timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr);
console.log(r.stdout.trim());
