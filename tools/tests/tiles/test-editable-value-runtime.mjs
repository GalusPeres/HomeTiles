import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const control=read('src/types/value/value_control.cpp');
const header=read('src/types/value/value_control.h');
const popup=read('src/ui/popups/sensor/sensor_popup.cpp');
const fn=(source,name)=>{const found=cppFunctionDefinitions(source).find(f=>f.name===name);assert(found,name);return found.source};
const compiler=[process.env.CXX,'clang++','g++'].filter(Boolean).find(c=>spawnSync(c,['--version']).status===0);
const jsonInclude=[process.env.ARDUINOJSON_INCLUDE,path.join(os.homedir(),'Documents/Arduino/libraries/ArduinoJson/src'),path.join(root,'third_party/ArduinoJson/src')].filter(Boolean).find(p=>fs.existsSync(path.join(p,'ArduinoJson.h')));
if(!compiler||!jsonInclude){console.log('SKIP: Editable runtime tests need a host C++ compiler and ArduinoJson headers');process.exit(0)}
const out=path.join(root,'build/tests/editable-runtime');fs.mkdirSync(out,{recursive:true});
let geometry=read('src/ui/popups/popup_layout.h');
geometry=geometry.slice(geometry.indexOf('namespace popup_layout {'),geometry.indexOf('// Standard popup close button.'));
for(const f of cppFunctionDefinitions(geometry).reverse()) if(f.name.startsWith('font')||['headerTitleFont','applyIconScale','alignHeader'].includes(f.name)) geometry=geometry.slice(0,f.start)+geometry.slice(f.end);
geometry+='inline const int* headerTitleFont(){static const int height=scale(24);return &height;}\ninline const int* font20(){static const int height=scale(20);return &height;}\ninline const int* font40(){static const int height=scale(40);return &height;}\n}';
const valueStruct=header.match(/struct EditableValue \{[\s\S]*?\n};/)[0];
const source=`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <iostream>
#include <ArduinoJson.h>
#include "src/types/tile_type_policy.h"
#include "src/types/value/value_editor_model.h"
class String: public std::string {
public:
 using std::string::string; using std::string::operator=;
 String()=default; String(const std::string& s):std::string(s){}
 int indexOf(char c)const { auto n=find(c);return n==npos?-1:static_cast<int>(n); }
 String substring(size_t a,size_t b)const{return substr(a,b-a);}
 void replace(char a,char b){std::replace(begin(),end(),a,b);}
};
struct Config {const char* language="en";};
struct Manager {Config cfg;const Config& getConfig()const{return cfg;}} configManager;
namespace i18n {
struct Profile{const char* decimal_separator;};
const Profile& locale(const char* language){static Profile dot{"."},comma{","};return strcmp(language,"en")==0?dot:comma;}
const char* binary_sensor_state_label(const char* language,const char* state,const char*){
 const bool unknown=strcmp(state,"unknown")==0;
 if(strcmp(language,"de")==0)return unknown?"Unbekannt":"Nicht verfügbar";
 if(strcmp(language,"fr")==0)return unknown?"Inconnu":"Indisponible";
 return unknown?"Unknown":"Unavailable";
}
}
constexpr size_t EDITABLE_PAYLOAD_MAX=24576;
${valueStruct}
${['finite_json','editable_entity_matches','parse_editable_value','editable_display_value'].map(n=>fn(control,n)).join('\n')}
${geometry}
struct Obj {int y=0,w=0,h=0;unsigned flags=0;};using lv_obj_t=Obj;
struct lv_display_t;
constexpr unsigned LV_OBJ_FLAG_HIDDEN=1,LV_OBJ_FLAG_SCROLLABLE=2,LV_OBJ_FLAG_OVERFLOW_VISIBLE=4;
constexpr int LV_ANIM_OFF=0,LV_DIR_VER=1,LV_SCROLLBAR_MODE_AUTO=1,LV_SCROLLBAR_MODE_OFF=0,LV_ALIGN_TOP_MID=0;
int LV_PCT(int n){return n;}
void lv_obj_set_height(Obj*o,int h){o->h=h;}void lv_obj_set_y(Obj*o,int y){o->y=y;}
long lv_obj_get_y(Obj*o){return o->y;}long lv_obj_get_height(Obj*o){return o->h;}
bool lv_obj_has_flag(Obj*o,unsigned f){return (o->flags&f)!=0;}
void lv_obj_set_size(Obj*o,int w,int h){o->w=w;o->h=h;}
void lv_obj_align(Obj*o,int,int,int y){o->y=y;}void lv_obj_center(Obj*o){o->y=0;}
void lv_obj_scroll_to_y(Obj*,int,int){} void lv_obj_set_scroll_dir(Obj*,int){}
void lv_obj_set_scrollbar_mode(Obj*,int){} void lv_obj_move_foreground(Obj*){}
void lv_obj_update_layout(Obj*){} void lv_obj_add_flag(Obj*o,unsigned f){o->flags|=f;}
// ESP32's int32_t is long, unlike the host's int32_t. Match the firmware
// signature here so mixed-type template arguments fail before a full build.
void lv_obj_remove_flag(Obj*o,unsigned f){o->flags&=~f;} long lv_font_get_line_height(const int*h){return *h;}
constexpr int kChartHeight=popup_layout::contentScale(325),kTimeAxisMarkerCount=8;
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kContentLiftY=6,kBinaryTimelineHeight=22,kBinaryActivityRowHeight=42,kTimeAxisHeight=20;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kContentLiftY=0,kBinaryTimelineHeight=24,kBinaryActivityRowHeight=42,kTimeAxisHeight=24;
#else
constexpr int kContentLiftY=0,kBinaryTimelineHeight=30,kBinaryActivityRowHeight=50,kTimeAxisHeight=20;
#endif
constexpr int kBinaryVisibleActivityRows = SCREEN_HEIGHT <= 600 ? 4 : 5;
struct SensorPopupContext {
 bool editable=false,state_history_mode=true;String editable_kind;
 Obj *title_label=nullptr,*icon_label=nullptr,*control_row=nullptr;
 Obj *body_box,*chart_wrap,*binary_body,*binary_activity_title,*binary_activity_date,*binary_activity_viewport,*binary_activity_status,*binary_history_title,*binary_timeline,*binary_history_status;
 Obj* binary_time_labels[8];
 Obj* chart=nullptr; Obj* y_min_label=nullptr; Obj* y_min_line=nullptr; Obj* time_lines[8]={}; Obj* time_labels[8]={}; int chart_height=kChartHeight;
};
${fn(control,'editable_control_height')}
${fn(popup,'resize_editable_chart')}
void update_binary_time_axis(SensorPopupContext*){}
${fn(popup,'editable_control_top')}
${fn(popup,'layout_editable_history')}
// Styling is exercised with real LVGL in test-editable-controls-lvgl.mjs.
namespace editable_colors {struct Palette {};}
${control.match(/struct EditableControl \{[\s\S]*?\n};/)[0]}
struct Network {bool online=true;int count=0;String payload;bool isMqttConnected(){return online;}
 bool mqttEnqueuePublish(const char*,const char* text,bool retained){assert(!retained);++count;payload=text;return true;}} networkManager;
struct Topics{String deviceBase(){return "device/test";}}mqttTopics;
uint32_t millis(){return 100;}
String request_id(){return "fresh-command";}
unsigned last_status=0;
void status_text(EditableControl*,unsigned value){last_status=value;}
void finish_editing(EditableControl*c){c->editing=false;}
template<size_t N> void serializeJson(const StaticJsonDocument<N>&doc,String&text){ArduinoJson::serializeJson(doc,static_cast<std::string&>(text));}
${fn(control,'submit')}
EditableControl* active_control=nullptr;
void editable_control_close(EditableControl*c){c->active=false;c->command_id="";}
void editable_control_refresh(EditableControl*){}
void apply_control_colors(EditableControl*){}
void visible(Obj*,bool){}
${fn(control,'editable_control_open')}
int main(){
 const String base=R"({"version":1,"kind":"number","state":"0","available":true,"writable":true,"session":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","revision":"bbbbbbbbbbbbbbbb","min":-1,"max":1,"step":0.1,"unit":"m²"})";
 EditableControl component;component.active=true;component.entity="number.test";component.value=parse_editable_value(base);component.editing=true;
 submit(&component,"0.15");assert(networkManager.count==0&&component.command_id.empty()&&component.editing&&last_status==7);
 submit(&component,"2");assert(networkManager.count==0&&component.command_id.empty());
 submit(&component,"nan");assert(networkManager.count==0&&component.command_id.empty());
 submit(&component,"0,2");assert(networkManager.count==1&&!component.command_id.empty()&&!component.editing&&last_status==8);
 assert(networkManager.payload.find("fresh-command")!=std::string::npos);
 component.command_id="";networkManager.online=false;submit(&component,"0.3");assert(networkManager.count==1&&component.command_id.empty());
 networkManager.online=true;component.value.writable=false;submit(&component,"0.3");assert(networkManager.count==1);
 editable_control_open(&component,"number.missing");assert(!component.value.valid&&!component.value.writable&&component.command_id.empty()&&component.payload!="");
 auto value=parse_editable_value(base);assert(value.valid&&value.writable&&value.state=="0"&&fabs(value.step-0.1)<1e-8);
 assert(editable_display_value(value)=="0 m²");
 for(const char* language:{"en","de","fr"}){configManager.cfg.language=language;value.state="1.5";
   assert(editable_display_value(value)==(std::string(language)=="en"?"1.5 m²":"1,5 m²"));
   value.state="unknown";assert(editable_display_value(value)==i18n::binary_sensor_state_label(language,"unknown",""));
   value.available=false;assert(editable_display_value(value)==i18n::binary_sensor_state_label(language,"unavailable",""));value.available=true;
 }
 for(const char* bad:{"", "[]", "{", "null", R"({"version":2})"})assert(!parse_editable_value(bad).valid);
 assert(!parse_editable_value(String(24577,'x')).valid);
 for(const char* state:{"null","false","0"}){std::string input=base;input.replace(input.find("\\"state\\":\\"0\\""),11,std::string("\\"state\\":")+state);auto parsed=parse_editable_value(input);if(strcmp(state,"null")==0)assert(parsed.valid&&!parsed.has_state&&!parsed.available);else assert(!parsed.valid);}
 for(auto type:{TILE_NUMBER,TILE_SELECT,TILE_DATETIME})assert(editable_entity_matches(type,""));
 for(const char* entity:{"number.x","input_number.x"})assert(editable_entity_matches(TILE_NUMBER,entity));
 for(const char* entity:{"sensor.x","number.","number.X","number.x/y"})assert(!editable_entity_matches(TILE_NUMBER,entity));
 assert(editable_entity_matches(TILE_SELECT,"input_select.x"));assert(editable_entity_matches(TILE_DATETIME,"input_datetime.x"));
 Obj objects[18];SensorPopupContext ctx;ctx.body_box=&objects[0];ctx.chart_wrap=&objects[1];ctx.chart=&objects[1];ctx.binary_body=&objects[2];ctx.binary_activity_title=&objects[3];ctx.binary_activity_date=&objects[4];ctx.binary_activity_viewport=&objects[5];ctx.binary_activity_status=&objects[6];ctx.binary_history_title=&objects[7];ctx.binary_timeline=&objects[8];ctx.binary_history_status=&objects[9];for(int i=0;i<8;++i)ctx.binary_time_labels[i]=&objects[10+i];
 for(int cycle=0;cycle<20;++cycle){
  for(const char* kind:{"number","select","time","date","datetime"}){
   ctx.editable=true;ctx.editable_kind=kind;layout_editable_history(&ctx);
   assert(ctx.body_box->h>=2*kBinaryActivityRowHeight);
   assert(ctx.body_box->y+ctx.body_box->h<popup_layout::kNavY);
   assert(ctx.body_box->y>=popup_layout::kValueY-kContentLiftY+popup_layout::kValueHeight);
   assert(ctx.binary_activity_viewport->h>=kBinaryActivityRowHeight);
   if(ctx.editable_kind=="number"){
    assert(!(ctx.body_box->flags&LV_OBJ_FLAG_SCROLLABLE));
    assert(ctx.binary_activity_viewport->y+ctx.binary_activity_viewport->h<=ctx.body_box->h);
    assert(!(ctx.chart_wrap->flags&LV_OBJ_FLAG_HIDDEN));
    assert(ctx.binary_activity_title->y>=ctx.chart_wrap->y+ctx.chart_height+kTimeAxisHeight);
   }else{
    assert(!(ctx.body_box->flags&LV_OBJ_FLAG_SCROLLABLE));
    assert(ctx.binary_activity_viewport->y+ctx.binary_activity_viewport->h<=ctx.body_box->h);
    if(ctx.editable_kind!="select"){assert(ctx.binary_activity_title->y==popup_layout::scale(8));assert(ctx.binary_timeline->flags&LV_OBJ_FLAG_HIDDEN);}
   }
  }
  ctx.editable=false;ctx.state_history_mode=false;layout_editable_history(&ctx);
  assert(!(ctx.chart_wrap->flags&LV_OBJ_FLAG_HIDDEN));assert(!(ctx.body_box->flags&LV_OBJ_FLAG_SCROLLABLE));assert(ctx.body_box->h==popup_layout::kBodyHeight);
  ctx.state_history_mode=true;layout_editable_history(&ctx);assert(ctx.chart_wrap->flags&LV_OBJ_FLAG_HIDDEN);assert(!(ctx.binary_timeline->flags&LV_OBJ_FLAG_HIDDEN));
 }
 std::cout<<"Editable parser and responsive popup layout passed\\n";
}
`;
const cpp=path.join(out,'test.cpp');fs.writeFileSync(cpp,source);
for(const [profile,width,height,define] of [['square',480,480,'DEVICE_LAYOUT_480X480'],['wide',1024,600,'DEVICE_LAYOUT_1024X600'],['ws8',1280,800,''],['portrait',720,1280,''],['base',720,720,'']]){
 const binary=path.join(out,profile+(process.platform==='win32'?'.exe':''));
 const args=['-std=c++17','-Wno-deprecated-declarations','-I',root,'-I',jsonInclude,'-DSCREEN_WIDTH='+width,'-DSCREEN_HEIGHT='+height,...(define?['-D'+define]:[]),cpp,'-o',binary];
 let result=spawnSync(compiler,args,{encoding:'utf8'});assert.equal(result.status,0,result.stdout+result.stderr);
 result=spawnSync(binary,[],{encoding:'utf8'});assert.equal(result.status,0,profile+': '+result.stdout+result.stderr);
}
console.log('Actual editable parser and popup geometry: 480×480, 1024×600, 1280×800, 720×1280, 720×720 passed.');
