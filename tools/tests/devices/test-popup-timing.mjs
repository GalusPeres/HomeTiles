import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
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
 input(40000,LV_EVENT_PRESSED);assert(!popup_timing::sample.press_layout);lv_deinit();}
`;
const source=path.join(out,'test.cpp'),binary=path.join(out,process.platform==='win32'?'test.exe':'test');fs.writeFileSync(source,cpp);
let r=spawnSync(host.cxx,[...host.flags,'-std=c++17','-I',out,source,host.archive,'-o',binary],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
r=spawnSync(binary,[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
console.log('Popup timing: correct phase/flush aggregation, empty frames, one report per touch and long-press handling');
