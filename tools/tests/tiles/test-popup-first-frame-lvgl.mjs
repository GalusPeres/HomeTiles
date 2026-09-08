import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const host=await lvglHost(root);
if(!host){console.log('SKIP: First-frame lifecycle needs LVGL and a host compiler');process.exit(0);}
const read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const popup=read('src/ui/popups/sensor/sensor_popup.cpp');
const queueProcess=cppFunctionDefinitions(popup).find(f=>f.name==='process_sensor_popup_queue').source;
const pending=queueProcess.slice(queueProcess.indexOf('  if (g_pending_history.valid &&'),queueProcess.indexOf('  if (g_sensor_popup_ctx->state_history_mode &&'));
const interacting=cppFunctionDefinitions(read('src/types/value/value_control.cpp')).find(f=>f.name==='editable_control_is_interacting').source;
const out=path.join(root,'build/tests/popup-first-frame-lvgl');fs.mkdirSync(out,{recursive:true});
const cpp=String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include "src/ui/popups/popup_first_frame.h"
struct String:std::string{using std::string::string;bool equalsIgnoreCase(const String&s)const{return *this==s;}};
struct EditableControl{bool active=true,dragging=false,editing=false;lv_obj_t*pressed=nullptr;lv_obj_t*dropdown=nullptr;};
${interacting}
struct Context{EditableControl*control;String entity_id="number.test";bool visible=true;}context;
auto*g_sensor_popup_ctx=&context;PopupFirstFrame g_sensor_first_frame;
struct Pending{bool valid=true;String entity_id="number.test",payload="history";}g_pending_history;
int applied=0,flushed=0;
bool is_popup_visible(Context*c){return c->visible;}
void apply_history_payload(Context*,const char*){assert(flushed>0);++applied;}
void service(){${pending}}
int main(){lv_init();g_sensor_first_frame.begin();assert(!g_sensor_first_frame.pending());
 auto*display=lv_display_create(480,480);std::vector<uint32_t>pixels(480*480);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*4,LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*,uint8_t*){++flushed;lv_display_flush_ready(d);});
 EditableControl c;c.dropdown=lv_dropdown_create(lv_screen_active());context.control=&c;
 g_sensor_first_frame.begin();service();assert(applied==0&&g_pending_history.valid);lv_display_send_event(display,LV_EVENT_REFR_START,nullptr);service();assert(applied==0);
 lv_obj_invalidate(lv_screen_active());lv_refr_now(display);service();assert(applied==1&&!g_pending_history.valid);
 for(int mode=0;mode<4;++mode){g_pending_history.valid=true;if(mode==0)c.dragging=true;if(mode==1)c.editing=true;if(mode==2)c.pressed=c.dropdown;if(mode==3)lv_dropdown_open(c.dropdown);service();assert(g_pending_history.valid);c.dragging=c.editing=false;c.pressed=nullptr;lv_dropdown_close(c.dropdown);service();assert(!g_pending_history.valid);}
 for(int i=0;i<100;++i){g_sensor_first_frame.begin();g_sensor_first_frame.begin();assert(g_sensor_first_frame.pending());g_sensor_first_frame.cancel();assert(!g_sensor_first_frame.pending());lv_display_send_event(display,LV_EVENT_REFR_READY,nullptr);}
 g_sensor_first_frame.begin();lv_display_delete(display);assert(!g_sensor_first_frame.pending());g_sensor_first_frame.cancel();lv_deinit();
}
`;
const source=path.join(out,'test.cpp'),binary=path.join(out,process.platform==='win32'?'test.exe':'test');fs.writeFileSync(source,cpp);
let result=spawnSync(host.cxx,[...host.flags,'-std=c++17',source,host.archive,'-o',binary],{encoding:'utf8'});assert.equal(result.status,0,result.stdout+result.stderr);
result=spawnSync(binary,[],{encoding:'utf8'});assert.equal(result.status,0,result.stdout+result.stderr);
console.log('History waits for the first rendered frame and control release; repeated begin/cancel/display teardown passed.');
