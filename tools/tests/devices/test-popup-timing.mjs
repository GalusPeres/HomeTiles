import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
// The maintainer's test builds (V2 and S3 beta defines) measure every opening.
assert.match(fs.readFileSync(path.join(root,'src/core/diagnostics/popup_timing.h'),'utf8'),
  /#if \(defined\(HOMETILES_ISSUE38_BETA\) \|\| defined\(HOMETILES_TEST_BETA\)\) && !defined\(HOMETILES_POPUP_TIMING\)\s*#define HOMETILES_POPUP_TIMING\s*#endif/);
const host=await lvglHost(root);
if(!host){console.log('SKIP: Popup timing requires LVGL and a host compiler');process.exit(0);}
const out=path.join(root,'build/tests/popup-timing');fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'Arduino.h'),'#pragma once\n');
const cpp=String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
uint32_t now_us=0;uint32_t micros(){return now_us;}
struct Logger{int count=0;std::string text;void printf(const char*f,...){char buffer[1024];va_list args;va_start(args,f);vsnprintf(buffer,sizeof(buffer),f,args);va_end(args);text=buffer;++count;}}Serial;
#define HOMETILES_POPUP_TIMING
#include "src/core/diagnostics/popup_timing.h"
lv_obj_t*test_body=nullptr;lv_obj_t*popup_shell_body(){return test_body;}
int main(){lv_init();auto*d=lv_display_create(480,480);auto*i=lv_indev_create();auto*card=lv_button_create(lv_screen_active());lv_obj_set_size(card,168,145);lv_obj_update_layout(card);popup_timing::attach(d,i);
 auto input=[&](uint32_t at,lv_event_code_t event){now_us=at;lv_indev_send_event(i,event,card);};
 auto display=[&](uint32_t at,lv_event_code_t event,void*param=nullptr){now_us=at;lv_display_send_event(d,event,param);};
 lv_area_t area{0,0,19,9};
 input(1000,LV_EVENT_PRESSED);display(2100,LV_EVENT_REFR_START);display(5200,LV_EVENT_FLUSH_START,&area);display(7200,LV_EVENT_FLUSH_FINISH,&area);display(8500,LV_EVENT_REFR_READY);assert(Serial.count==0);
 input(10000,LV_EVENT_RELEASED);input(10100,LV_EVENT_SHORT_CLICKED);
 display(10200,LV_EVENT_REFR_START);display(10300,LV_EVENT_REFR_READY);assert(Serial.count==0&&"An empty timer pass is not a rendered result");
 display(10600,LV_EVENT_REFR_START);display(11000,LV_EVENT_FLUSH_START,&area);display(14000,LV_EVENT_FLUSH_FINISH,&area);display(15000,LV_EVENT_REFR_READY);assert(Serial.count==1);
 assert(Serial.text.find("source=168x145 hold_us=9000 release_to_click_us=100 click_to_frame_us=500 frame_us=4400 flush_us=3000 flushes=1 pixels=200 press_frames=1 press_frame_us=6400 press_flush_us=2000 press_max_frame_us=6400")!=std::string::npos);
 display(16000,LV_EVENT_REFR_START);display(17000,LV_EVENT_REFR_READY);assert(Serial.count==1&&"Print at most once per input");
 input(20000,LV_EVENT_PRESSED);input(23000,LV_EVENT_RELEASED);display(24000,LV_EVENT_REFR_START);display(25000,LV_EVENT_REFR_READY);assert(Serial.count==1&&"A release without a click must not report a popup");
 lv_obj_set_style_border_width(card,0,LV_STATE_PRESSED);
 input(30000,LV_EVENT_PRESSED);assert(popup_timing::sample.press_layout);
 input(35000,LV_EVENT_LONG_PRESSED);display(36000,LV_EVENT_REFR_START);display(36100,LV_EVENT_FLUSH_START,&area);display(36300,LV_EVENT_FLUSH_FINISH,&area);display(37000,LV_EVENT_REFR_READY);assert(Serial.count==2);assert(Serial.text.find("hold_us=5000 release_to_click_us=0")!=std::string::npos);assert(Serial.text.find("press_frames=0 press_frame_us=0")!=std::string::npos);assert(Serial.text.find("press_layout=1")!=std::string::npos);
 lv_obj_remove_local_style_prop(card,LV_STYLE_BORDER_WIDTH,LV_STATE_PRESSED);
 input(40000,LV_EVENT_PRESSED);assert(!popup_timing::sample.press_layout);
 // An opening's frame: the time under the popup, its overlay parts and each block of its body, over every band.
 auto*overlay=lv_obj_create(lv_screen_active());lv_obj_set_size(overlay,200,200);auto*frame=lv_obj_create(overlay);lv_obj_set_size(frame,150,150);
 test_body=lv_obj_create(overlay);lv_obj_set_size(test_body,150,150);auto*b0=lv_obj_create(test_body);lv_obj_set_size(b0,20,20);auto*b1=lv_obj_create(test_body);lv_obj_set_size(b1,100,50);lv_obj_create(b1);lv_obj_update_layout(overlay);
 auto draw=[&](uint32_t at,lv_obj_t*o){now_us=at;lv_obj_send_event(o,LV_EVENT_DRAW_MAIN_BEGIN,nullptr);};
 input(50000,LV_EVENT_PRESSED);input(50100,LV_EVENT_RELEASED);input(50200,LV_EVENT_SHORT_CLICKED);
 display(51000,LV_EVENT_REFR_START);display(51100,LV_EVENT_RENDER_START);
 draw(61100,overlay);draw(62100,frame);draw(64100,test_body);draw(64600,b0);draw(64700,b1);
 display(70700,LV_EVENT_FLUSH_START,&area);display(71700,LV_EVENT_FLUSH_FINISH,&area);display(73700,LV_EVENT_FLUSH_START,&area);display(74000,LV_EVENT_FLUSH_FINISH,&area);display(75000,LV_EVENT_REFR_READY);
 assert(Serial.count==4);
 assert(Serial.text=="[PopupDraw] under=12.0 | overlay 200x200 n=5 1.0 | o0 150x150 n=0 2.0 | body 150x150 n=3 0.5 | b1 100x50 n=1 6.0\n"&&"Every band adds to its parts; blocks under 0.5 ms are left out");
 assert(lv_obj_get_event_count(b1)==0&&lv_obj_get_event_count(overlay)==0&&"The marks go with the report");
 // A closing's frame: what the click left to draw, each object by size and place (the screensaver's tiles).
 lv_obj_add_flag(card,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
 auto plain=[&](lv_obj_t*parent,int x,int y,int w,int h){auto*o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);return o;};
 auto*grid=plain(lv_layer_top(),0,0,300,200);auto*tile=plain(grid,10,20,100,80);auto*inner=plain(tile,5,5,40,30);lv_obj_update_layout(grid);
 input(80000,LV_EVENT_PRESSED);test_body=nullptr;input(80100,LV_EVENT_RELEASED);input(80200,LV_EVENT_SHORT_CLICKED);
 display(81000,LV_EVENT_REFR_START);display(81100,LV_EVENT_RENDER_START);
 draw(84100,grid);draw(85100,tile);draw(93100,inner);
 display(96100,LV_EVENT_FLUSH_START,&area);display(96600,LV_EVENT_FLUSH_FINISH,&area);display(97000,LV_EVENT_REFR_READY);
 assert(Serial.count==6);
 assert(Serial.text=="[CloseDraw] before=3.0 | 300x200@0,0 n=2 1.0 | 100x80@10,20 n=1 8.0 | 40x30@15,25 n=0 3.0\n"&&"A closing's frame by drawn object");
 assert(lv_obj_get_event_count(tile)==0&&lv_obj_get_event_count(inner)==0&&"The closing marks go with the report");
 // Without a popup open at the press nothing is split.
 input(90000,LV_EVENT_PRESSED);input(90100,LV_EVENT_RELEASED);input(90200,LV_EVENT_SHORT_CLICKED);
 display(91000,LV_EVENT_REFR_START);display(91100,LV_EVENT_RENDER_START);draw(92000,tile);
 display(93000,LV_EVENT_FLUSH_START,&area);display(93500,LV_EVENT_FLUSH_FINISH,&area);display(94000,LV_EVENT_REFR_READY);
 assert(Serial.count==7&&lv_obj_get_event_count(tile)==0);
 lv_deinit();}
`;
const source=path.join(out,'test.cpp'),binary=path.join(out,process.platform==='win32'?'test.exe':'test');fs.writeFileSync(source,cpp);
let r=spawnSync(host.cxx,[...host.flags,'-std=c++17','-I',out,source,host.archive,'-o',binary],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
r=spawnSync(binary,[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
console.log('Popup timing: correct phase/flush aggregation, empty frames, one report per touch, long-press handling, the draw time of each popup part and of each object a closing redraws');
