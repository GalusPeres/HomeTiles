import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const control = read('src/types/value/value_control.cpp'), popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const fn = (source, name) => { const f = cppFunctionDefinitions(source).find(f => f.name === name); assert(f, name); return f.source; };
assert(!control.includes('ui_keyboard') && !control.includes('lv_keyboard_'));
const host = await lvglHost(root);
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')].filter(Boolean).find(p => fs.existsSync(path.join(p, 'ArduinoJson.h')));
if (!host || !jsonInclude) { console.log('SKIP: Editable visual regression needs LVGL, ArduinoJson and a host C/C++ toolchain'); process.exit(0); }
const out = path.join(root, 'build/tests/editable-controls-lvgl'); fs.mkdirSync(out, {recursive: true});
let geometry = read('src/ui/popups/popup_layout.h');
geometry = geometry.slice(geometry.indexOf('namespace popup_layout {'), geometry.indexOf('// Standard popup close button.'));
geometry += fn(read('src/ui/popups/popup_layout.h'), 'createCloseButton') + '}';
const catalog = [...read('src/core/i18n/i18n.cpp').matchAll(/\{("(?:Zahl|Number|Nombre)"[^\n]+?)\}\};/g)].map(m => m[1]);
assert.equal(catalog.length, 3); for (const labels of catalog) assert.equal(JSON.parse('[' + labels + ']').length, 19);
const styles = read('src/ui/shared/ui_control_style.h').replace(/^#.*$/gm, '');
const mdi = read('src/tiles/icons/mdi_icons.cpp');
const iconChar = name => String.fromCodePoint(parseInt(mdi.match(new RegExp('\\{"' + name + '", (0x[0-9A-F]+)\\}'))[1]));
const chartBuild = popup.slice(popup.indexOf('  // Chart wrapper: Y-axis labels'), popup.indexOf('  lv_obj_move_foreground(icon);'));
const cpp = `
#include <lvgl.h>
#include "src/types/value/value_colors.h"
#include "src/ui/shared/title_label.h"
#include <lvgl_private.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <ArduinoJson.h>
#include "src/types/value/value_editor_model.h"
extern "C" { LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16); LV_FONT_DECLARE(ui_font_20); LV_FONT_DECLARE(ui_font_24);
LV_FONT_DECLARE(ui_font_28); LV_FONT_DECLARE(ui_font_32); LV_FONT_DECLARE(ui_font_40); LV_FONT_DECLARE(ui_font_48);
LV_FONT_DECLARE(ui_font_56); LV_FONT_DECLARE(ui_font_64); LV_FONT_DECLARE(ui_font_72); LV_FONT_DECLARE(ui_font_80); LV_FONT_DECLARE(ui_font_96);
LV_FONT_DECLARE(ui_symbols_20); LV_FONT_DECLARE(ui_symbols_24);
LV_FONT_DECLARE(mdi_icons_32); LV_FONT_DECLARE(mdi_icons_40); LV_FONT_DECLARE(mdi_icons_48); }
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#elif defined(DEVICE_LAYOUT_1024X600)
#define FONT_MDI_ICONS (&mdi_icons_40)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
class String : public std::string { public: using std::string::string; using std::string::operator=; String()=default; String(const std::string& s):std::string(s){}; void replace(char a,char b){std::replace(begin(),end(),a,b);} int indexOf(char c)const{auto n=find(c);return n==npos?-1:int(n);} };
String normalizeMdiIconName(const String& name){return name;}
String getMdiChar(const String& name){if(name=="window-close")return ${JSON.stringify(iconChar('window-close'))};if(name=="clock-end")return ${JSON.stringify(iconChar('clock-end'))};return name=="plus"?${JSON.stringify(iconChar('plus'))}:${JSON.stringify(iconChar('minus'))};}
struct Config {const char* language="en";}; struct Manager {Config cfg;const Config& getConfig(){return cfg;}} configManager;
namespace i18n {
struct Profile {const char* decimal_separator;const char* editable_labels[19];};
const Profile& locale(const char* language){static Profile de{",",{${catalog[0]}}},en{".",{${catalog[1]}}},fr{",",{${catalog[2]}}}; return strcmp(language,"de")==0?de:strcmp(language,"fr")==0?fr:en;}
const char* binary_sensor_state_label(const char*,const char* state,const char*){return strcmp(state,"unknown")==0?"Unknown":"Unavailable";}
}
constexpr size_t EDITABLE_PAYLOAD_MAX=24576;
${read('src/types/value/value_control.h').match(/struct EditableValue \{[\s\S]*?\n};/)[0]}
${['label','finite_json','parse_editable_value','editable_display_value'].map(n=>fn(control,n)).join('\n')}
${geometry}
${styles}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'),'disable_pressed_button_animation')}
${read('src/types/climate/layout.h').replace(/^#include.*$/gm,'').replace('#pragma once','')}
uint32_t value_generation=1, ticks=100;
uint32_t millis(){return ticks;}
struct Logger {template<class... T> void printf(const char*,T...) {}} Serial;
String request_id(){static int id=0;return std::to_string(++id);}
#ifdef _WIN32
struct tm* localtime_r(const time_t* now,struct tm* result){return localtime_s(result,now)==0?result:nullptr;}
#endif
struct Network {bool online=true;std::vector<String> commands;bool isMqttConnected(){return online;} bool mqttEnqueuePublish(const char*,const char* payload,bool retained){assert(!retained);commands.push_back(payload);return true;}} networkManager;
struct Topics{String deviceBase(){return "test";}}mqttTopics;
struct Bridge{String payload,icon;String findEntityIcon(const String&){return icon;}String findEditableValue(const String&){return payload;}}haBridgeConfig;
template<size_t N> void serializeJson(const StaticJsonDocument<N>&doc,String&text){ArduinoJson::serializeJson(doc,static_cast<std::string&>(text));}
struct EditableControl;
void editable_control_refresh(EditableControl*);void editable_control_close(EditableControl*);
${control.slice(control.indexOf('int editable_control_height('))}
constexpr int kCardWidth=popup_layout::kCardWidth,kCardPad=popup_layout::kCardPad,kChartHeight=popup_layout::contentScale(325),kTimeAxisMarkerCount=8,kChartLineWidth=popup_layout::scale(4),kHistoryPoints24h=288;
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kContentLiftY=6,kBinaryTimelineHeight=22,kBinaryActivityRowHeight=42,kTimeAxisHeight=20;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kContentLiftY=0,kBinaryTimelineHeight=24,kBinaryActivityRowHeight=42,kTimeAxisHeight=24;
#else
constexpr int kContentLiftY=0,kBinaryTimelineHeight=30,kBinaryActivityRowHeight=50,kTimeAxisHeight=20;
#endif
enum class SensorHistoryRange {Day24,Day7};
constexpr int kBinaryVisibleActivityRows = SCREEN_HEIGHT <= 600 ? 4 : 5;
struct SensorPopupContext {
 bool editable=true,state_history_mode=true,binary_icon_override=false;String editable_kind,entity_id,editable_history_id;SensorHistoryRange editable_requested_range=SensorHistoryRange::Day24;
 lv_obj_t *overlay=nullptr,*card=nullptr,*title_label=nullptr,*icon_label=nullptr,*control_row=nullptr,*range_day_btn=nullptr,*range_week_btn=nullptr;int chart_height=kChartHeight;SensorHistoryRange history_range=SensorHistoryRange::Day24;
 lv_obj_t *body_box,*chart_wrap,*chart,*binary_body,*binary_activity_title,*binary_activity_date,*binary_activity_viewport,*binary_activity_status,*binary_history_title,*binary_timeline,*binary_history_status,*y_min_label,*y_max_label,*y_min_line,*y_max_line;
 lv_obj_t *binary_time_labels[8],*time_lines[8],*time_labels[8];lv_chart_series_t* series;
};
void clear_chart(SensorPopupContext* ctx,int points){lv_chart_set_point_count(ctx->chart,points);lv_chart_set_range(ctx->chart,LV_CHART_AXIS_PRIMARY_Y,99,101);lv_chart_set_all_value(ctx->chart,ctx->series,100);}
struct Range{int hours,points;};Range get_history_range_config(SensorHistoryRange range){return range==SensorHistoryRange::Day7?Range{168,288}:Range{24,288};}
${fn(popup,'set_label_style')}
void build_chart(SensorPopupContext* ctx){auto* body_box=ctx->body_box;${chartBuild}}
${fn(popup,'resize_editable_chart')}
void update_binary_time_axis(SensorPopupContext*);
${fn(popup,'editable_control_top')}
${fn(popup,'layout_editable_history')}
int history_requests=0,history_clears=0,header_aligns=0;
void request_history_for_context(SensorPopupContext*ctx){++history_requests;ctx->editable_history_id=std::to_string(history_requests);}
void clear_binary_history(SensorPopupContext*){++history_clears;}
${fn(popup,'style_range_button')}
${fn(popup,'update_range_buttons')}
${fn(popup,'align_header_row')}
${fn(popup,'on_range_click')}
${fn(popup,'accept_editable_history_range')}
${fn(popup,'refresh_editable_popup_icon')}

int explicit_layouts=0;
void counted_layout(lv_obj_t* obj){++explicit_layouts;lv_obj_update_layout(obj);}
#define lv_obj_update_layout counted_layout
${fn(popup,'measure_label_text_width')}
int calc_time_axis(const SensorPopupContext*,String* labels,float* fracs,int){for(int i=0;i<4;++i){labels[i]=i==0?"12 AM":i==1?"6 AM":i==2?"12 PM":"6 PM";fracs[i]=i/3.0f;}return 4;}
int calc_day7_boundary_axis(float*,int){return 0;}
${fn(popup,'update_binary_time_axis')}
${fn(popup,'update_y_axis_layout')}
#undef lv_obj_update_layout
${fn(popup,'set_sensor_popup_visible')}
lv_obj_t* box(lv_obj_t* parent){auto* o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_width(o,LV_PCT(100));lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
lv_obj_t* text(lv_obj_t* parent,const char* value){auto* o=lv_label_create(parent);lv_label_set_text(o,value);set_label_style(o,lv_color_white(),popup_layout::font20());return o;}
void settle(EditableControl* c){ticks+=601;editable_control_refresh(c);editable_control_refresh(c);}
void click(lv_obj_t* obj){lv_obj_send_event(obj,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(obj,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(obj,LV_EVENT_SHORT_CLICKED,nullptr);lv_obj_send_event(obj,LV_EVENT_CLICKED,nullptr);}
void load(EditableControl* c,const char* kind,const char* state,const char* extra=""){
 haBridgeConfig.payload=String("{\\"version\\":1,\\"kind\\":\\"")+kind+"\\",\\"state\\":\\""+state+"\\",\\"available\\":true,\\"writable\\":true,\\"session\\":\\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\\",\\"revision\\":\\"bbbbbbbbbbbbbbbb\\""+extra+"}";
 ++value_generation;editable_control_open(c,"test.entity");lv_obj_update_layout(c->card);
}
uint32_t rendered_pixel(const std::vector<uint32_t>& pixels,int index){
 const uint16_t pixel=reinterpret_cast<const uint16_t*>(pixels.data())[index];
 return (((pixel>>11)&31)*255/31)<<16 | (((pixel>>5)&63)*255/63)<<8 | (pixel&31)*255/31;
}
void control_bounds(const char* file,lv_obj_t* obj){lv_area_t a;lv_obj_get_coords(obj,&a);std::ofstream out(file);out<<a.x1<<" "<<a.y1<<" "<<a.x2+1<<" "<<a.y2+1;}
void image(const char* file,const std::vector<uint32_t>& pixels){
 std::vector<uint32_t> decoded(pixels.size());for(size_t i=0;i<pixels.size();++i)decoded[i]=rendered_pixel(pixels,i);
 std::ofstream out(file,std::ios::binary);const uint32_t size=54+SCREEN_WIDTH*SCREEN_HEIGHT*4;
 auto u16=[&](uint16_t n){out.write(reinterpret_cast<char*>(&n),2);};auto u32=[&](uint32_t n){out.write(reinterpret_cast<char*>(&n),4);};
 out.write("BM",2);u32(size);u32(0);u32(54);u32(40);u32(SCREEN_WIDTH);u32(-SCREEN_HEIGHT);u16(1);u16(32);u32(0);u32(size-54);u32(0);u32(0);u32(0);u32(0);out.write(reinterpret_cast<const char*>(decoded.data()),decoded.size()*4);
}
int main(int argc,char**argv){
 lv_init();auto* display=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t> pixels(SCREEN_WIDTH*SCREEN_HEIGHT);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*2,LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 lv_theme_default_init(display,lv_color_hex(0x26A69A),lv_color_hex(0xC14444),false,&ui_font_20);
 lv_obj_set_style_bg_color(lv_screen_active(),lv_color_black(),0);
 auto* card=lv_obj_create(lv_screen_active());lv_obj_set_size(card,popup_layout::kCardWidth,popup_layout::kCardHeight);lv_obj_center(card);lv_obj_set_style_bg_color(card,lv_color_hex(0x2A2A2A),0);lv_obj_set_style_radius(card,popup_layout::kCardRadius,0);lv_obj_set_style_border_width(card,0,0);lv_obj_set_style_pad_all(card,popup_layout::kCardPad,0);lv_obj_remove_flag(card,LV_OBJ_FLAG_SCROLLABLE);
 auto* title=text(card,"L10s Ultra Volume");lv_obj_set_style_text_font(title,popup_layout::headerTitleFont(),0);lv_obj_set_width(title,LV_PCT(38));
 auto* close=popup_layout::createCloseButton(card,[](lv_event_t*){},nullptr);
 auto* header_icon=text(card,getMdiChar("clock-end").c_str());lv_obj_set_style_text_font(header_icon,FONT_MDI_ICONS,0);popup_layout::applyIconScale(header_icon);align_header_row(card,title,header_icon);
 auto* row=box(card);lv_obj_set_pos(row,0,popup_layout::kValueY);lv_obj_add_flag(row,LV_OBJ_FLAG_OVERFLOW_VISIBLE);auto* c=editable_control_create(row,card);
#if defined(DEVICE_GUITION_ESP32_4848S040)
 assert(lv_obj_get_style_text_font(c->dropdown,LV_PART_INDICATOR)==&ui_symbols_20);
#else
 assert(lv_obj_get_style_text_font(c->dropdown,LV_PART_INDICATOR)==&ui_symbols_24);
#endif
 SensorPopupContext ctx{};ctx.card=card;ctx.title_label=title;ctx.icon_label=header_icon;ctx.control_row=row;ctx.body_box=box(card);ctx.binary_body=box(ctx.body_box);
 ctx.binary_history_title=text(ctx.binary_body,"History");ctx.binary_timeline=box(ctx.binary_body);lv_obj_set_size(ctx.binary_timeline,LV_PCT(100),kBinaryTimelineHeight);lv_obj_set_y(ctx.binary_timeline,popup_layout::scale(42));lv_obj_set_style_bg_color(ctx.binary_timeline,lv_color_hex(0x4C74D9),0);lv_obj_set_style_bg_opa(ctx.binary_timeline,LV_OPA_COVER,0);
 ctx.binary_activity_title=text(ctx.binary_body,"Activity");ctx.binary_activity_date=text(ctx.binary_body,"Today · 09/06/2026");ctx.binary_activity_status=text(ctx.binary_body,"");ctx.binary_history_status=text(ctx.binary_body,"");ctx.binary_activity_viewport=box(ctx.binary_body);
 for(int i=0;i<5;++i){auto* entry=text(ctx.binary_activity_viewport,i==0?"100 %                       8:01:00 PM":"80 %                         7:59:32 PM");lv_obj_set_y(entry,i*kBinaryActivityRowHeight+popup_layout::scale(8));}
 for(auto*& label:ctx.binary_time_labels){label=text(ctx.binary_body,"Text");lv_obj_add_flag(label,LV_OBJ_FLAG_HIDDEN);}build_chart(&ctx);
 lv_label_set_text(ctx.y_max_label,"101 %");lv_label_set_text(ctx.y_min_label,"99 %");
 for(int i=0;i<4;++i){lv_label_set_text(ctx.time_labels[i],i==0?"12 AM":i==1?"6 AM":i==2?"12 PM":"6 PM");lv_obj_set_x(ctx.time_labels[i],popup_layout::scale(100+i*155));lv_obj_remove_flag(ctx.time_labels[i],LV_OBJ_FLAG_HIDDEN);}
 auto* footer=text(card,"24H       7D");lv_obj_set_style_text_font(footer,popup_layout::font28(),0);lv_obj_align(footer,LV_ALIGN_BOTTOM_MID,0,-popup_layout::scale(25));
 const char* number=",\\"min\\":0,\\"max\\":100,\\"step\\":1,\\"mode\\":\\"slider\\",\\"unit\\":\\"%\\"";
 for(const char* kind:{"number","select","time","date","datetime"}){
  const char* state=strcmp(kind,"number")==0?"100":strcmp(kind,"select")==0?"Home":strcmp(kind,"time")==0?"17:00:00":strcmp(kind,"date")==0?"2024-02-29":"2024-02-29 17:00:00";
  load(c,kind,state,strcmp(kind,"number")==0?number:strcmp(kind,"select")==0?",\\"options_complete\\":true,\\"options\\":[\\"Home\\",\\"Home / Lighting / Desk\\",\\"Home / Weather\\",\\"Home / Energy\\",\\"Home / Camera\\",\\"Home / Living room\\",\\"Home / Upstairs\\",\\"Home / Downstairs\\",\\"Home / Garage\\",\\"Home / Bedroom\\",\\"Home / Garden\\"]":"");
  ctx.editable_kind=kind;layout_editable_history(&ctx);
  for(const char* caption:{"View","Heating schedule\\nEnd time","Heating\\nSchedule\\nSecond floor\\nEnd time"}){
   hometiles_title::set(title,caption);align_header_row(card,title,header_icon);layout_editable_history(&ctx);lv_obj_update_layout(card);
   for(bool pressed:{false,true}){
    if(pressed)lv_obj_add_state(close,LV_STATE_PRESSED);else lv_obj_remove_state(close,LV_STATE_PRESSED);
    lv_tick_inc(300);lv_timer_handler();lv_obj_update_layout(card);
    lv_area_t controls,close_area;lv_obj_get_coords(row,&controls);lv_obj_get_coords(close,&close_area);
    assert(controls.y1>close_area.y2+popup_layout::kCloseButtonClickArea&&"Editable controls must remain below the complete close-button touch area");
    for(auto* label:{title,header_icon}){lv_area_t header;lv_obj_get_coords(label,&header);assert(controls.y1-header.y2>=popup_layout::scale(6)&&"The header must retain breathing room above every editable control kind");}
   }
  }
  lv_obj_remove_state(close,LV_STATE_PRESSED);hometiles_title::set(title,strcmp(kind,"select")==0?"View":strcmp(kind,"number")==0?"L10s Ultra Volume":"Heating schedule\\nEnd time");align_header_row(card,title,header_icon);layout_editable_history(&ctx);
  for(int i=4;i<8;++i)assert(lv_obj_has_flag(ctx.binary_time_labels[i],LV_OBJ_FLAG_HIDDEN)&&"Unused axis labels must stay hidden");update_y_axis_layout(&ctx);lv_obj_update_layout(card);
  assert(!lv_obj_has_flag(ctx.body_box,LV_OBJ_FLAG_SCROLLABLE));
  lv_area_t body,view,control_area;lv_obj_get_coords(ctx.body_box,&body);lv_obj_get_coords(ctx.binary_activity_viewport,&view);lv_obj_get_coords(row,&control_area);
  assert(view.y2<=body.y2&&view.y2-view.y1+1>=kBinaryActivityRowHeight);assert(body.y1>control_area.y2);
  if(strcmp(kind,"number")==0||strcmp(kind,"select")==0)
    assert(body.y1-control_area.y2<=popup_layout::scale(12)&&"History must directly follow the shared control band without an empty status row");
  const bool temporal=strcmp(kind,"number")!=0&&strcmp(kind,"select")!=0;
  auto* heading=temporal?ctx.binary_activity_title:ctx.binary_history_title;
  lv_obj_set_style_text_font(heading,popup_layout::font24(),0);
  for(unsigned status:{7,8,9}){
    status_text(c,status);lv_obj_update_layout(card);lv_area_t status_area,heading_area;lv_obj_get_coords(c->status,&status_area);lv_obj_get_coords(heading,&heading_area);
    assert(status_area.y1>control_area.y2&&status_area.x1>heading_area.x2);
    assert(abs(status_area.y1+status_area.y2-heading_area.y1-heading_area.y2)<=2&&"Command status must share the heading baseline without reserving a blank row");
    assert(lv_obj_get_y(ctx.body_box)==body.y1-lv_obj_get_y(card)-popup_layout::kCardPad);
  }visible(c->status,false);

  if(strcmp(kind,"number")==0){assert(lv_obj_get_width(c->slider)==(popup_layout::kCardWidth>=760?popup_layout::scale(410):popup_layout::scale(330)));lv_area_t track;lv_obj_get_coords(c->slider,&track);assert(abs((track.x1+track.x2)/2-SCREEN_WIDTH/2)<=1);lv_area_t value_area,row_area;lv_obj_get_coords(c->number_box,&value_area);lv_obj_get_coords(c->row,&row_area);
   assert(lv_obj_get_style_text_font(c->field,LV_PART_MAIN)==popup_layout::headerTitleFont());
   assert(abs(value_area.x1+value_area.x2-track.x1-track.x2)<=2);
   assert(value_area.y2<track.y1-popup_layout::scale(10));
   const int group_bottom=(track.y1+track.y2+popup_layout::scale(36))/2;
   assert(abs(value_area.y1+group_bottom-row_area.y1-row_area.y2)<=3);
   assert(lv_obj_get_width(c->number_box)==lv_obj_get_width(c->slider));
   assert(lv_obj_get_height(c->slider)==popup_layout::scale(16));assert(lv_obj_get_style_width(c->slider,LV_PART_KNOB)==popup_layout::scale(36));assert(lv_obj_get_style_text_color(c->field,LV_PART_MAIN).red==255);assert(ctx.chart_height<kChartHeight&&ctx.chart_height>=40);std::cout<<"Layout "<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<" editor="<<editable_control_height(kind)<<" chart="<<ctx.chart_height<<" activity="<<lv_obj_get_height(ctx.binary_activity_viewport)<<" row="<<kBinaryActivityRowHeight<<"\\n";assert(lv_obj_has_flag(c->up,LV_OBJ_FLAG_HIDDEN)&&lv_obj_has_flag(c->down,LV_OBJ_FLAG_HIDDEN));assert(lv_obj_get_style_bg_opa(c->number_box,LV_PART_MAIN)==LV_OPA_TRANSP);}
  if(strcmp(kind,"select")==0){lv_area_t a,b;lv_obj_get_coords(c->dropdown,&a);lv_obj_get_coords(row,&b);assert(a.y1>=b.y1&&a.y2<b.y2);assert(lv_obj_get_style_text_font(c->dropdown,LV_PART_MAIN)==popup_layout::headerTitleFont());auto color=lv_obj_get_style_bg_color(c->dropdown,LV_PART_MAIN);assert(color.red==color.green&&color.green==color.blue);for(auto state:{LV_STATE_PRESSED,LV_STATE_FOCUSED,LV_STATE_DISABLED}){lv_obj_add_state(c->dropdown,state);auto actual=lv_obj_get_style_bg_color(c->dropdown,LV_PART_MAIN);assert(actual.red==actual.green&&actual.green==actual.blue);lv_obj_remove_state(c->dropdown,state);}}
  if(strcmp(kind,"time")==0){assert(!lv_obj_has_flag(c->fields[5].box,LV_OBJ_FLAG_HIDDEN));assert(lv_obj_has_flag(c->apply,LV_OBJ_FLAG_HIDDEN));assert(lv_obj_get_width(c->clock_box)<popup_layout::kContentWidth);assert(lv_obj_get_style_text_font(c->fields[3].roller,LV_PART_MAIN)==popup_layout::font40());
    for(int i=3;i<6;++i){assert(!c->fields[i].up&&!c->fields[i].down);lv_area_t parent,value;lv_obj_get_coords(c->fields[i].box,&parent);lv_obj_get_coords(c->fields[i].roller,&value);assert(value.y1>=parent.y1&&value.y2<=parent.y2);}}

  if(argc>1){lv_obj_invalidate(lv_screen_active());lv_tick_inc(200);lv_timer_handler();lv_refr_now(display);image((String(argv[1])+"-"+kind+".bmp").c_str(),pixels);
    auto* control_widget=strcmp(kind,"time")==0?c->clock_box:strcmp(kind,"select")==0?c->dropdown:c->number_box;
    control_bounds((String(argv[1])+"-"+kind+".bounds").c_str(),control_widget);
    if(strcmp(kind,"select")==0||strcmp(kind,"time")==0){lv_area_t area;lv_obj_get_coords(control_widget,&area);auto pixel=rendered_pixel(pixels,(area.y1+popup_layout::scale(12))*SCREEN_WIDTH+area.x1+popup_layout::scale(24));int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;assert(abs(r-g)<=2&&abs(g-b)<=2&&"The rendered RGB565 surface must be neutral");}}
  if(strcmp(kind,"select")==0){for(int cycle=0;cycle<3;++cycle){lv_dropdown_open(c->dropdown);lv_obj_update_layout(card);auto* list=lv_dropdown_get_list(c->dropdown);assert(lv_obj_get_style_text_font(list,LV_PART_MAIN)==popup_layout::headerTitleFont());assert(lv_obj_get_style_clip_corner(list,LV_PART_MAIN));auto selected=lv_obj_get_style_bg_color(list,LV_PART_SELECTED);assert(selected.red==255&&selected.green==255&&selected.blue==255);lv_area_t a;lv_obj_get_coords(list,&a);assert(a.x1>=0&&a.x2<SCREEN_WIDTH);if(argc>1&&cycle==0){lv_obj_invalidate(lv_screen_active());lv_tick_inc(200);lv_timer_handler();lv_refr_now(display);image((String(argv[1])+"-options.bmp").c_str(),pixels);}lv_dropdown_close(c->dropdown);}}
 }
 if(argc>1){lv_refr_now(display);for(auto* icon:{header_icon,lv_obj_get_child(close,0)}){lv_area_t area;lv_obj_get_coords(icon,&area);int bright=0;for(int y=area.y1;y<=area.y2;++y)for(int x=area.x1;x<=area.x2;++x){auto pixel=rendered_pixel(pixels,y*SCREEN_WIDTH+x);if((pixel&255)>200&&((pixel>>8)&255)>200&&((pixel>>16)&255)>200)++bright;}assert(bright>10);}}
 // Exercise delayed metadata while the existing popup remains open.
 ctx.icon_label=header_icon;ctx.entity_id="time.end";haBridgeConfig.icon="";
 refresh_editable_popup_icon(&ctx);assert(lv_obj_has_flag(header_icon,LV_OBJ_FLAG_HIDDEN));
 haBridgeConfig.icon="clock-end";refresh_editable_popup_icon(&ctx);
 assert(!lv_obj_has_flag(header_icon,LV_OBJ_FLAG_HIDDEN));assert(strcmp(lv_label_get_text(header_icon),getMdiChar("clock-end").c_str())==0);
 ctx.binary_icon_override=true;haBridgeConfig.icon="";refresh_editable_popup_icon(&ctx);assert(!lv_obj_has_flag(header_icon,LV_OBJ_FLAG_HIDDEN));
 lv_obj_add_flag(header_icon,LV_OBJ_FLAG_HIDDEN);haBridgeConfig.icon="clock-end";refresh_editable_popup_icon(&ctx);assert(lv_obj_has_flag(header_icon,LV_OBJ_FLAG_HIDDEN));
 ctx.binary_icon_override=false;refresh_editable_popup_icon(&ctx);
 // Range buttons react immediately while existing history waits for a matching reply.
 ctx.range_day_btn=lv_button_create(card);ctx.range_week_btn=lv_button_create(card);
 for(auto*button:{ctx.range_day_btn,ctx.range_week_btn}){lv_obj_add_event_cb(button,on_range_click,LV_EVENT_CLICKED,&ctx);lv_obj_add_flag(button,LV_OBJ_FLAG_HIDDEN);}
 ctx.history_range=SensorHistoryRange::Day24;ctx.editable_requested_range=SensorHistoryRange::Day24;
 update_range_buttons(&ctx);assert(lv_obj_get_style_bg_opa(ctx.range_day_btn,LV_PART_MAIN)==LV_OPA_COVER);
 auto old_height=lv_obj_get_height(ctx.body_box);String old_date=lv_label_get_text(ctx.binary_activity_date);
 lv_obj_send_event(ctx.range_week_btn,LV_EVENT_CLICKED,nullptr);assert(history_requests==1&&history_clears==0&&ctx.history_range==SensorHistoryRange::Day24);
 assert(lv_obj_get_style_bg_opa(ctx.range_week_btn,LV_PART_MAIN)==LV_OPA_COVER&&lv_obj_get_style_bg_opa(ctx.range_day_btn,LV_PART_MAIN)==LV_OPA_TRANSP);
 assert(lv_obj_get_height(ctx.body_box)==old_height&&old_date==lv_label_get_text(ctx.binary_activity_date));
 DynamicJsonDocument reply(512);reply["request_id"]="old";reply["hours"]=168;assert(!accept_editable_history_range(&ctx,reply));
 reply["request_id"]=ctx.editable_history_id.c_str();reply["hours"]=24;assert(!accept_editable_history_range(&ctx,reply));
 reply["hours"]=168;assert(accept_editable_history_range(&ctx,reply)&&ctx.history_range==SensorHistoryRange::Day7);
 lv_obj_send_event(ctx.range_day_btn,LV_EVENT_CLICKED,nullptr);assert(ctx.history_range==SensorHistoryRange::Day7&&!accept_editable_history_range(&ctx,reply));
 assert(lv_obj_get_style_bg_opa(ctx.range_day_btn,LV_PART_MAIN)==LV_OPA_COVER&&lv_obj_get_style_bg_opa(ctx.range_week_btn,LV_PART_MAIN)==LV_OPA_TRANSP);
 reply["request_id"]=ctx.editable_history_id.c_str();reply["hours"]=24;assert(accept_editable_history_range(&ctx,reply)&&ctx.history_range==SensorHistoryRange::Day24);
 for(auto*button:{ctx.range_day_btn,ctx.range_week_btn})lv_obj_delete(button);
 assert(editable_control_height("number")==editable_control_height("select"));assert(editable_control_height("time")>editable_control_height("select"));
 assert(networkManager.commands.empty());
 // Remote state must not rebuild or scroll an open native options list.
 String select_extra=",\\\"options_complete\\\":true,\\\"options\\\":[";
 for(int i=0;i<25;++i){if(i)select_extra+=",";select_extra+="\\\"Option "+std::to_string(i)+"\\\"";}select_extra+="]";
 load(c,"select","Option 0",select_extra.c_str());lv_dropdown_open(c->dropdown);
 auto* open_list=lv_dropdown_get_list(c->dropdown);lv_obj_update_layout(open_list);
 lv_obj_scroll_to_y(open_list,200,LV_ANIM_OFF);lv_obj_update_layout(open_list);
 const auto old_scroll=lv_obj_get_scroll_y(open_list);const char* old_options=lv_dropdown_get_options(c->dropdown);
 std::string selected_payload=haBridgeConfig.payload;selected_payload.replace(selected_payload.find("Option 0"),8,"Option 1");
 haBridgeConfig.payload=selected_payload;++value_generation;editable_control_refresh(c);lv_obj_update_layout(open_list);
 assert(lv_dropdown_is_open(c->dropdown)&&lv_obj_get_scroll_y(open_list)==old_scroll);
 assert(lv_dropdown_get_options(c->dropdown)==old_options&&"State updates must not reallocate unchanged options during a gesture");
 assert(lv_dropdown_get_selected(c->dropdown)==0&&"Incoming state must not move the list being browsed");
 lv_dropdown_close(c->dropdown);editable_control_refresh(c);assert(lv_dropdown_get_selected(c->dropdown)==1);
 // Exercise the native pointer path: one visible row must still accept swipes.
 load(c,"time","17:00:00");
 struct Touch {lv_point_t point;lv_indev_state_t state=LV_INDEV_STATE_RELEASED;} touch;
 auto* pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(pointer,&touch);
 lv_indev_set_read_cb(pointer,[](lv_indev_t* input,lv_indev_data_t* data){auto* t=static_cast<Touch*>(lv_indev_get_user_data(input));data->point=t->point;data->state=t->state;});

 lv_area_t minute_area;lv_obj_get_coords(c->fields[4].roller,&minute_area);
 touch.point={(minute_area.x1+minute_area.x2)/2,(minute_area.y1+minute_area.y2)/2};touch.state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);
 const int distance=lv_font_get_line_height(popup_layout::font40())+lv_obj_get_style_text_line_space(c->fields[4].roller,LV_PART_MAIN);
 for(int step=0;step<4;++step){touch.point.y-=distance/4;lv_tick_inc(20);lv_indev_read(pointer);lv_obj_update_layout(card);}
 for(int pause=0;pause<8;++pause){lv_tick_inc(20);lv_indev_read(pointer);}
 touch.state=LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(pointer);
 assert(c->editing&&c->submit_scheduled&&c->calendar.values[4]>0&&c->calendar.values[5]==0&&networkManager.commands.empty());
 settle(c);assert(networkManager.commands.size()==1);
 assert(lv_anim_get(lv_obj_get_child(c->fields[4].roller,0),nullptr)!=nullptr);
 // A half swipe must show the next hour without a large empty band.
 load(c,"time","17:00:00");lv_tick_inc(250);lv_timer_handler();lv_obj_update_layout(card);
 lv_area_t hour;lv_obj_get_coords(c->fields[3].roller,&hour);touch.point={(hour.x1+hour.x2)/2,(hour.y1+hour.y2)/2};touch.state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);
 const int hour_pitch=lv_font_get_line_height(popup_layout::font40())+lv_obj_get_style_text_line_space(c->fields[3].roller,LV_PART_MAIN);
 for(int move=1;move<=8;++move){touch.point.y=(hour.y1+hour.y2)/2-hour_pitch*move/16;lv_tick_inc(20);lv_indev_read(pointer);}
 lv_refr_now(display);
 bool started=false;int gap=0,max_gap=0,bands=0;bool in_band=false;
 for(int y=hour.y1;y<=hour.y2;++y){bool ink=false;for(int x=hour.x1;x<=hour.x2;++x){auto px=rendered_pixel(pixels,y*SCREEN_WIDTH+x);if((px&255)>210&&((px>>8)&255)>210&&((px>>16)&255)>210){ink=true;break;}}
   if(ink){if(!in_band)++bands;if(started)max_gap=std::max(max_gap,gap);gap=0;started=true;}else if(started)++gap;in_band=ink;}
 if(argc>1){image((String(argv[1])+"-clock-midway.bmp").c_str(),pixels);control_bounds((String(argv[1])+"-clock-midway.bounds").c_str(),c->clock_box);}
 assert(bands==2&&"Both neighboring clock values must be visible during a half swipe");
 assert(max_gap<=lv_font_get_line_height(popup_layout::font40())/3&&"Adjacent clock values must not have a large blank band");
 touch.state=LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(pointer);settle(c);
 // Each clock field wraps in both directions with the normal pointer inertia.
 auto swipe=[&](int field,int direction){lv_tick_inc(250);lv_timer_handler();lv_obj_update_layout(card);lv_area_t area;lv_obj_get_coords(c->fields[field].roller,&area);touch.point={(area.x1+area.x2)/2,(area.y1+area.y2)/2};touch.state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);const int pitch=lv_font_get_line_height(popup_layout::font40())+lv_obj_get_style_text_line_space(c->fields[4].roller,LV_PART_MAIN);for(int step=1;step<=8;++step){touch.point.y=(area.y1+area.y2)/2+direction*pitch*step/8;lv_tick_inc(20);lv_indev_read(pointer);lv_obj_update_layout(card);}for(int pause=0;pause<8;++pause){lv_tick_inc(20);lv_indev_read(pointer);}touch.state=LV_INDEV_STATE_RELEASED;lv_tick_inc(20);lv_indev_read(pointer);};
 for(int field=3;field<6;++field){load(c,"time","00:00:00");swipe(field,1);assert(c->calendar.values[field]==(field==3?23:59));settle(c);swipe(field,-1);assert(c->calendar.values[field]==0);settle(c);}
 lv_indev_delete(pointer);

 load(c,"number","50",number);const auto slider_before=networkManager.commands.size();
 lv_obj_send_event(c->slider,LV_EVENT_PRESSED,nullptr);lv_slider_set_value(c->slider,7550,LV_ANIM_OFF);lv_obj_send_event(c->slider,LV_EVENT_VALUE_CHANGED,nullptr);assert(networkManager.commands.size()==slider_before);
 lv_obj_send_event(c->slider,LV_EVENT_RELEASED,nullptr);assert(c->draft==76&&networkManager.commands.size()==slider_before+1);
 lv_obj_send_event(c->slider,LV_EVENT_RELEASED,nullptr);assert(networkManager.commands.size()==slider_before+1);
 lv_obj_send_event(c->slider,LV_EVENT_PRESSED,nullptr);lv_slider_set_value(c->slider,1250,LV_ANIM_OFF);lv_obj_send_event(c->slider,LV_EVENT_PRESS_LOST,nullptr);assert(networkManager.commands.size()==slider_before+1);
 load(c,"time","17:00:00");lv_obj_send_event(c->fields[4].roller,LV_EVENT_PRESSED,nullptr);lv_roller_set_selected(c->fields[4].roller,2,LV_ANIM_OFF);lv_obj_send_event(c->fields[4].roller,LV_EVENT_VALUE_CHANGED,nullptr);assert(c->editing);
 std::string revised=haBridgeConfig.payload;revised.replace(revised.find("bbbbbbbbbbbbbbbb"),16,"cccccccccccccccc");haBridgeConfig.payload=revised;
 ++value_generation;editable_control_refresh(c);assert(c->value.writable&&!c->editing&&!c->dragging&&c->command_id.empty());
 const auto count=networkManager.commands.size();
 load(c,"number","0.2",",\\"min\\":-1,\\"max\\":100,\\"step\\":0.1,\\"mode\\":\\"box\\"");
 assert(!c->number_roller_enabled);click(c->up);settle(c);assert(networkManager.commands.size()==count+1);assert(fabs(c->draft-0.3)<1e-10);
 c->command_id="";click(c->down);settle(c);assert(fabs(c->draft-0.2)<1e-10);
 lv_obj_send_event(c->up,LV_EVENT_PRESSED,nullptr);for(int i=0;i<2000;++i)lv_obj_send_event(c->up,LV_EVENT_LONG_PRESSED_REPEAT,nullptr);assert(c->draft==100);const auto held=networkManager.commands.size();lv_obj_send_event(c->up,LV_EVENT_RELEASED,nullptr);settle(c);assert(networkManager.commands.size()==held+1);

 load(c,"number","20.5",",\\"min\\":16,\\"max\\":30,\\"step\\":0.5,\\"mode\\":\\"auto\\",\\"unit\\":\\"°C\\"");
 assert(!c->number_roller_enabled&&lv_obj_has_flag(c->slider,LV_OBJ_FLAG_HIDDEN));assert(lv_obj_get_width(c->up)==lv_obj_get_width(c->number_box)/2);assert(lv_obj_get_style_text_font(lv_obj_get_child(c->up,0),LV_PART_MAIN)==popup_layout::font24());assert(!lv_obj_has_flag(c->up,LV_OBJ_FLAG_HIDDEN)&&!lv_obj_has_flag(c->down,LV_OBJ_FLAG_HIDDEN));assert(lv_obj_get_style_bg_opa(c->number_box,LV_PART_MAIN)==LV_OPA_COVER);assert(lv_obj_get_style_border_width(c->number_box,LV_PART_MAIN)==0);assert(lv_obj_get_style_radius(c->number_box,LV_PART_MAIN)==climate_layout::kControlRadius);
 ctx.editable_kind="number";layout_editable_history(&ctx);update_y_axis_layout(&ctx);
 hometiles_title::set(title,"Wolf Fhs280 T Min\\nEinstellen");align_header_row(card,title,header_icon);lv_obj_update_layout(card);
 lv_area_t temperature_area,history_area;lv_obj_get_coords(c->number_box,&temperature_area);lv_obj_get_coords(ctx.binary_history_title,&history_area);
 const int temperature_gap=history_area.y1-temperature_area.y2-1;
 assert(temperature_gap>=popup_layout::scale(8)&&temperature_gap<=popup_layout::scale(24)&&"The temperature pill must not leave a large gap above History");
 std::cout<<"Temperature spacing "<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<": gap="<<temperature_gap<<", history_y="<<history_area.y1<<", activity_y="<<lv_obj_get_y(ctx.body_box)+lv_obj_get_y(ctx.binary_activity_title)<<"\\n";
 if(argc>1){lv_obj_invalidate(lv_screen_active());lv_tick_inc(200);lv_timer_handler();lv_refr_now(display);image((String(argv[1])+"-temperature.bmp").c_str(),pixels);control_bounds((String(argv[1])+"-temperature.bounds").c_str(),c->number_box);
   for(auto* button:{c->up,c->down}){lv_area_t icon;lv_obj_get_coords(lv_obj_get_child(button,0),&icon);int bright=0;for(int y=icon.y1;y<=icon.y2;++y)for(int x=icon.x1;x<=icon.x2;++x){auto pixel=rendered_pixel(pixels,y*SCREEN_WIDTH+x);if((pixel&255)>200&&((pixel>>8)&255)>200&&((pixel>>16)&255)>200)++bright;}assert(bright>0);}
 }
 lv_font_glyph_dsc_t glyph{};assert(lv_font_get_glyph_dsc(FONT_MDI_ICONS,&glyph,0xF0415,0)&&glyph.box_w>0);
 const auto temperature_before=networkManager.commands.size();click(c->up);settle(c);assert(c->draft==21&&networkManager.commands.size()==temperature_before+1);
 load(c,"number","20.5",",\\"min\\":16,\\"max\\":30,\\"step\\":0.5,\\"mode\\":\\"box\\"");
 assert(c->number_roller_enabled&&lv_roller_get_option_count(c->number_roller)==29);
 const auto roller_before=networkManager.commands.size();
 lv_obj_send_event(c->number_roller,LV_EVENT_PRESSED,nullptr);lv_roller_set_selected(c->number_roller,10,LV_ANIM_OFF);lv_obj_send_event(c->number_roller,LV_EVENT_VALUE_CHANGED,nullptr);assert(networkManager.commands.size()==roller_before);
 lv_obj_send_event(c->number_roller,LV_EVENT_RELEASED,nullptr);settle(c);assert(c->draft==21&&networkManager.commands.size()==roller_before+1);
 lv_obj_send_event(c->number_roller,LV_EVENT_PRESSED,nullptr);lv_roller_set_selected(c->number_roller,11,LV_ANIM_OFF);lv_obj_send_event(c->number_roller,LV_EVENT_PRESS_LOST,nullptr);assert(networkManager.commands.size()==roller_before+1);
 // A service acknowledgement may arrive before HA reports the new device state.
 load(c,"time","17:00:07");
 lv_obj_send_event(c->fields[4].roller,LV_EVENT_PRESSED,nullptr);lv_roller_set_selected(c->fields[4].roller,1,LV_ANIM_OFF);lv_obj_send_event(c->fields[4].roller,LV_EVENT_VALUE_CHANGED,nullptr);lv_obj_send_event(c->fields[4].roller,LV_EVENT_RELEASED,nullptr);settle(c);
 assert(c->calendar.values[4]==1&&c->calendar.values[5]==7);
 String ack=String(R"({"entity_id":"test.entity","id":")")+c->command_id+R"(","status":"ok"})";
 editable_handle_ack("test/stat/value",ack.c_str(),ack.length());editable_control_refresh(c);
 assert(c->calendar.values[4]==1 && "A successful ACK must not restore the old time");
 const auto delayed_count=networkManager.commands.size();
 // Old/intermediate state after ACK must not replace a newer pending value.
 auto report=[&](const char* state){std::string payload=haBridgeConfig.payload;auto start=payload.find("\\\"state\\\":\\\"")+9;auto end=payload.find('"',start);payload.replace(start,end-start,state);haBridgeConfig.payload=payload;++value_generation;editable_control_refresh(c);};
 report("17:00:08");assert(c->calendar.values[4]==1&&!c->command_id.empty());
 report("17:01:07");assert(c->calendar.values[4]==1&&c->command_id.empty());
 assert(networkManager.commands.size()==delayed_count);
 // Temperature taps edit locally and publish only after the last pause.
 load(c,"number","20",",\\\"min\\\":16,\\\"max\\\":30,\\\"step\\\":0.5,\\\"mode\\\":\\\"box\\\",\\\"unit\\\":\\\"°C\\\"");
 const auto rapid_count=networkManager.commands.size();click(c->up);ticks+=200;editable_control_refresh(c);click(c->up);ticks+=200;editable_control_refresh(c);click(c->up);
 assert(c->draft==21.5&&networkManager.commands.size()==rapid_count);settle(c);assert(networkManager.commands.size()==rapid_count+1);
 ack=String(R"({"entity_id":"test.entity","id":")")+c->command_id+R"(","status":"ok"})";editable_handle_ack("test/stat/value",ack.c_str(),ack.length());editable_control_refresh(c);assert(c->draft==21.5);
 report("20.5");assert(c->draft==21.5);report("21.5");assert(c->draft==21.5&&c->command_id.empty());
 // A disconnected or closed editor must never publish a delayed draft later.
 click(c->up);const auto offline_pending=networkManager.commands.size();networkManager.online=false;settle(c);assert(!c->submit_scheduled);networkManager.online=true;settle(c);assert(networkManager.commands.size()==offline_pending);
 click(c->up);editable_control_close(c);settle(c);assert(networkManager.commands.size()==offline_pending);
 load(c,"date","2024-02-29");click(c->fields[0].up);assert(c->calendar.values[0]==2025&&c->calendar.values[2]==28);
 for(const char* language:{"en","de","fr"}){
    configManager.cfg.language=language;auto* translated_row=box(card);auto* translated=editable_control_create(translated_row,card);
    for(int i=0;i<6;++i)assert(std::string(lv_label_get_text(lv_obj_get_child(translated->fields[i].box,0)))==label(13+i));
    assert(std::string(lv_label_get_text(lv_obj_get_child(translated->apply,0)))==label(6));
    load(translated,"number","20.5",",\\"min\\":16,\\"max\\":30,\\"step\\":0.5,\\"mode\\":\\"auto\\",\\"unit\\":\\"°C\\"");
    const std::string translated_options=lv_label_get_text(translated->field);
    assert(translated_options.find(strcmp(language,"en")==0?"20.5":"20,5")!=std::string::npos);

    editable_control_delete(translated);lv_obj_delete(translated_row);
  }
 load(c,"time","unknown");assert(!c->draft_valid&&c->fields[3].unknown_options&&lv_roller_get_selected(c->fields[3].roller)==0);lv_obj_send_event(c->fields[3].roller,LV_EVENT_PRESSED,nullptr);lv_roller_set_selected(c->fields[3].roller,2,LV_ANIM_OFF);lv_obj_send_event(c->fields[3].roller,LV_EVENT_VALUE_CHANGED,nullptr);assert(c->editing&&c->draft_valid);
 networkManager.online=false;editable_control_refresh(c);assert(!c->editing&&lv_obj_has_state(c->fields[3].roller,LV_STATE_DISABLED));const auto offline=networkManager.commands.size();lv_obj_send_event(c->fields[3].roller,LV_EVENT_PRESSED,nullptr);lv_roller_set_selected(c->fields[3].roller,2,LV_ANIM_OFF);lv_obj_send_event(c->fields[3].roller,LV_EVENT_VALUE_CHANGED,nullptr);assert(networkManager.commands.size()==offline);networkManager.online=true;editable_control_refresh(c);assert(networkManager.commands.size()==offline);
 for(int i=0;i<20;++i){editable_control_close(c);load(c,"select","unknown",",\\"options_complete\\":true,\\"options\\":[\\"First\\",\\"Second\\"]");assert(c->option_offset==1);lv_dropdown_open(c->dropdown);editable_control_close(c);assert(!lv_dropdown_is_open(c->dropdown));}
 haBridgeConfig.payload="";++value_generation;editable_control_open(c,"missing.entity");assert(!c->value.writable&&c->command_id.empty());
 // Exercise actual axis measurement without per-label screen layout passes.
 explicit_layouts=0;update_y_axis_layout(&ctx);assert(explicit_layouts==0);
 explicit_layouts=0;update_binary_time_axis(&ctx);assert(explicit_layouts<=1);
 auto* measured=ctx.y_max_label;lv_obj_set_width(measured,LV_SIZE_CONTENT);lv_obj_update_layout(measured);
 assert(measure_label_text_width(measured)==lv_obj_get_width(measured));

 // Press feedback must end on release, even while the native list stays open.
 load(c,"select","Home",",\\\"options_complete\\\":true,\\\"options\\\":[\\\"Home\\\",\\\"Office\\\"]");
 assert(c->value.writable&&c->value.options.size()==2);
 ctx.editable_kind="select";layout_editable_history(&ctx);lv_obj_update_layout(card);
 lv_area_t feedback_area;lv_obj_get_coords(c->dropdown,&feedback_area);
 const int feedback_pixel=((feedback_area.y1+feedback_area.y2)/2)*SCREEN_WIDTH+feedback_area.x1+popup_layout::scale(4);
 auto feedback_frame=[&](const char* name){
  lv_tick_inc(300);lv_timer_handler();lv_refr_now(display);
  if(argc>1)image((String(argv[1])+"-select-"+name+".bmp").c_str(),pixels);
  return rendered_pixel(pixels,feedback_pixel);
 };
 const uint32_t resting_background=feedback_frame("normal");
 pointer=lv_indev_create();lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(pointer,&touch);
 lv_indev_set_read_cb(pointer,[](lv_indev_t* input,lv_indev_data_t* data){auto* t=static_cast<Touch*>(lv_indev_get_user_data(input));data->point=t->point;data->state=t->state;});
 touch.state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);
 // The dropdown's upper-right touch target must not hit the header close area.
 touch.point={feedback_area.x2-popup_layout::scale(12),feedback_area.y1+popup_layout::scale(6)};
 touch.state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);
 assert(lv_obj_has_state(c->dropdown,LV_STATE_PRESSED));
 assert(!lv_obj_has_state(close,LV_STATE_PRESSED));
 const auto pressed_background=feedback_frame("pressed");
  for(int shift:{0,8,16}){const int base=(resting_background>>shift)&255,pressed=(pressed_background>>shift)&255;assert(base<30&&pressed>base&&pressed-base<=10&&"Press feedback must be subtle on a dark Settings surface");}
  const auto border=lv_obj_get_style_border_color(c->dropdown,LV_PART_MAIN);assert(border.red==0x55&&border.green==0x55&&border.blue==0x55&&"Press feedback must preserve the Settings gray border");
 touch.state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);
 assert(lv_dropdown_is_open(c->dropdown)&&lv_obj_has_state(c->dropdown,LV_STATE_CHECKED));
 assert(feedback_frame("open")==resting_background&&"An expanded dropdown must not retain press feedback");
 auto* feedback_list=lv_dropdown_get_list(c->dropdown);lv_area_t feedback_list_area;lv_obj_get_coords(feedback_list,&feedback_list_area);
 const int selected_pixel=(feedback_list_area.y1+lv_obj_get_style_pad_top(feedback_list,LV_PART_MAIN)+lv_font_get_line_height(popup_layout::headerTitleFont())/2)*SCREEN_WIDTH+feedback_list_area.x2-popup_layout::scale(24);
 const uint32_t selected_color=rendered_pixel(pixels,selected_pixel);
 const int selected_r=(selected_color>>16)&255,selected_g=(selected_color>>8)&255,selected_b=selected_color&255;
 assert(selected_r==255&&selected_g==255&&selected_b==255&&"Editable dropdown selection must be white on every tile color");
 JsonDocument unavailable;deserializeJson(unavailable,haBridgeConfig.payload);
 unavailable["state"]="unavailable";unavailable["available"]=false;unavailable["writable"]=false;
 ArduinoJson::serializeJson(unavailable,static_cast<std::string&>(haBridgeConfig.payload));
 ++value_generation;editable_control_refresh(c);
 assert(!lv_dropdown_is_open(c->dropdown)&&lv_obj_has_state(c->dropdown,LV_STATE_DISABLED));
 assert(feedback_frame("unavailable")==resting_background&&"The disabled theme must not bleach the dropdown background");
 const auto disabled_commands=networkManager.commands.size();
 touch.state=LV_INDEV_STATE_PRESSED;lv_indev_read(pointer);feedback_frame("disabled-pressed");
 touch.state=LV_INDEV_STATE_RELEASED;lv_indev_read(pointer);
 assert(!lv_dropdown_is_open(c->dropdown)&&networkManager.commands.size()==disabled_commands);
 for(auto state:{LV_STATE_CHECKED,LV_STATE_PRESSED,static_cast<lv_state_t>(LV_STATE_CHECKED|LV_STATE_PRESSED)}){
  lv_obj_add_state(c->dropdown,state);
  assert(feedback_frame("disabled-combined")==resting_background);
  lv_obj_remove_state(c->dropdown,state);
 }
 load(c,"select","Home",",\\\"options_complete\\\":true,\\\"options\\\":[\\\"Home\\\",\\\"Office\\\"]");
 assert(feedback_frame("recovered")==resting_background&&!lv_obj_has_state(c->dropdown,LV_STATE_DISABLED));
 lv_indev_delete(pointer);
 std::cout<<"Dropdown press feedback: native release, unavailable, combined disabled states and recovery passed\\n";

 // Reuse the same controls across dark, saturated and light tile colors.
 // No widget recreation, option replacement or command is needed to recolor.
 auto same=[](lv_color_t a,lv_color_t b){return a.red==b.red&&a.green==b.green&&a.blue==b.blue;};
 auto luminance=[](lv_color_t c){auto linear=[](int x){double v=x/255.0;return v<=0.04045?v/12.92:pow((v+0.055)/1.055,2.4);};return .2126*linear(c.red)+.7152*linear(c.green)+.0722*linear(c.blue);};
 auto contrast=[&](lv_color_t a,lv_color_t b){double x=luminance(a)+.05,y=luminance(b)+.05;return std::max(x,y)/std::min(x,y);};
 for(int r=0;r<=255;r+=17)for(int g=0;g<=255;g+=17)for(int b=0;b<=255;b+=17){
  const auto p=editable_colors::from(lv_color_make(r,g,b));
  assert(contrast(p.surface,lv_color_white())>=4.5);
  assert(contrast(p.pressed,lv_color_white())>=4.5&&contrast(p.raised,lv_color_white())>=4.5);
  assert(contrast(p.field,lv_color_white())>=4.5);
 }
 const auto commands_before_colors=networkManager.commands.size();
 for(uint32_t rgb:{0x2A2A2Au,0x184A78u,0x8A283Cu,0x237053u,0xEBDCB8u,0xFFFFFFu,0x010101u}){
  lv_obj_set_style_bg_color(card,lv_color_hex(rgb),0);
  for(const char* kind:{"number","temperature","select","time","date"}){
   const bool temperature=strcmp(kind,"temperature")==0;
   load(c,temperature?"number":kind,temperature?"20":strcmp(kind,"number")==0?"50":strcmp(kind,"select")==0?"Home":strcmp(kind,"time")==0?"17:00:00":"2026-09-07",
    temperature?",\\"min\\":5,\\"max\\":40,\\"step\\":0.5,\\"unit\\":\\"°C\\"":strcmp(kind,"number")==0?number:strcmp(kind,"select")==0?",\\"options_complete\\":true,\\"options\\":[\\"Home\\",\\"Office\\"]":"");
   ctx.editable_kind=temperature?"number":kind;layout_editable_history(&ctx);lv_obj_update_layout(card);
   assert(same(lv_obj_get_style_text_color(title,LV_PART_MAIN),lv_color_white()));
   assert(same(lv_obj_get_style_text_color(ctx.binary_history_status,LV_PART_MAIN),lv_color_white()));
   assert(same(lv_obj_get_style_text_color(lv_obj_get_child(close,0),LV_PART_MAIN),lv_color_white()));
   assert(same(lv_obj_get_style_bg_color(c->clock_box,LV_PART_MAIN),c->colors.raised));
   if(strcmp(kind,"select")==0){
    const char* options=lv_dropdown_get_options(c->dropdown);apply_control_colors(c);
    assert(options==lv_dropdown_get_options(c->dropdown));
    lv_dropdown_open(c->dropdown);lv_obj_update_layout(card);auto* list=lv_dropdown_get_list(c->dropdown);
    assert(same(lv_obj_get_style_bg_color(list,LV_PART_MAIN),c->colors.surface));
    assert(same(lv_obj_get_style_text_color(list,LV_PART_MAIN),lv_color_white()));
    assert(same(lv_obj_get_style_bg_color(list,LV_PART_SELECTED),lv_color_white()));
    assert(same(lv_obj_get_style_text_color(list,LV_PART_SELECTED),c->colors.surface));
    lv_dropdown_close(c->dropdown);
    lv_obj_add_state(c->dropdown,LV_STATE_DISABLED);
    assert(same(lv_obj_get_style_bg_color(c->dropdown,LV_PART_MAIN),c->colors.surface));
    lv_obj_remove_state(c->dropdown,LV_STATE_DISABLED);
   }
   if(argc>1&&(SCREEN_WIDTH==480||SCREEN_WIDTH==1280)&&(rgb==0x184A78u||rgb==0xEBDCB8u)){
    lv_obj_set_style_text_color(footer,lv_color_white(),0);
    for(uint32_t i=0;i<lv_obj_get_child_count(ctx.binary_activity_viewport);++i)lv_obj_set_style_text_color(lv_obj_get_child(ctx.binary_activity_viewport,i),lv_color_white(),0);
    lv_tick_inc(300);lv_timer_handler();lv_refr_now(display);image((String(argv[1])+"-color-"+std::to_string(rgb)+"-"+kind+".bmp").c_str(),pixels);
   }
  }
 }
 assert(networkManager.commands.size()==commands_before_colors);
 lv_obj_set_style_bg_color(card,lv_color_hex(0x2A2A2A),0);
 std::cout<<"Editable palette: 4096 colors, reused controls, white selection, loading text and contrast passed\\n";

 // Use the actual popup visibility path with partial rendering, as on the
 // device. An opaque dropdown must not redraw covered grid tiles.
 ctx.overlay=box(lv_layer_top());lv_obj_set_size(ctx.overlay,LV_PCT(100),LV_PCT(100));ctx.card=card;
 lv_obj_set_parent(card,ctx.overlay);lv_obj_center(card);set_sensor_popup_visible(&ctx,true);
 String many_options=",\\"options_complete\\":true,\\"options\\":[";
 for(int i=0;i<64;++i){if(i)many_options+=",";many_options+="\\"Home / Lighting / Office / Desk "+std::to_string(i)+" [t:"+std::to_string(i)+"]\\"";}many_options+="]";
 load(c,"select","Home / Lighting / Office / Desk 0 [t:0]",many_options.c_str());
 ctx.editable_kind="select";layout_editable_history(&ctx);lv_obj_update_layout(card);
 lv_dropdown_open(c->dropdown);lv_obj_update_layout(card);
 for(auto state:{LV_STATE_CHECKED,static_cast<lv_state_t>(LV_STATE_CHECKED|LV_STATE_PRESSED)}){
  lv_obj_add_state(c->dropdown,state);lv_tick_inc(300);lv_timer_handler();auto color=lv_obj_get_style_bg_color(c->dropdown,LV_PART_MAIN);
  const uint8_t expected=(state&LV_STATE_PRESSED)?0x23:0x1b;
  assert(color.red==expected&&color.green==expected&&color.blue==expected);
  auto arrow=lv_obj_get_style_text_color(c->dropdown,LV_PART_INDICATOR);assert(arrow.red==255&&arrow.green==255&&arrow.blue==255);
  lv_obj_remove_state(c->dropdown,LV_STATE_PRESSED);
 }
 auto* list=lv_dropdown_get_list(c->dropdown);assert(lv_obj_get_parent(list)==lv_screen_active());
 lv_area_t list_area;lv_obj_get_coords(list,&list_area);
 lv_area_t dropdown_area,card_area;lv_obj_get_coords(c->dropdown,&dropdown_area);lv_obj_get_coords(card,&card_area);
 assert(list_area.y1>dropdown_area.y2&&"The open list must stay below the control and header");
 assert(list_area.y2<card_area.y1+popup_layout::kNavY-popup_layout::kCardPad&&"The open list must stop above the range buttons after header clearance changes");
 auto* covered=lv_obj_create(lv_screen_active());lv_obj_set_size(covered,80,40);
 lv_obj_set_pos(covered,list_area.x1+20,list_area.y1+20);int covered_draws=0;
 lv_obj_add_event_cb(covered,[](lv_event_t* e){++*static_cast<int*>(lv_event_get_user_data(e));},LV_EVENT_DRAW_MAIN,&covered_draws);
 lv_obj_move_to_index(covered,0);
 int history_draws=0;lv_obj_add_event_cb(ctx.binary_timeline,[](lv_event_t* e){++*static_cast<int*>(lv_event_get_user_data(e));},LV_EVENT_DRAW_MAIN,&history_draws);
 std::vector<uint16_t> composed(SCREEN_WIDTH*SCREEN_HEIGHT);
 lv_display_set_user_data(display,&composed);
 lv_display_set_flush_cb(display,[](lv_display_t* d,const lv_area_t* area,uint8_t* data){
  auto& image=*static_cast<std::vector<uint16_t>*>(lv_display_get_user_data(d));auto* source=reinterpret_cast<uint16_t*>(data);
  const int width=lv_area_get_width(area);for(int y=area->y1;y<=area->y2;++y)
    std::copy(source+(y-area->y1)*width,source+(y-area->y1+1)*width,image.begin()+y*SCREEN_WIDTH+area->x1);
  lv_display_flush_ready(d);
 });
 lv_display_set_buffers(display,pixels.data(),nullptr,SCREEN_WIDTH*28*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_obj_invalidate(lv_screen_active());lv_refr_now(display);int previous_history_draws=0,corrected_history_draws=0;
 for(int i=0;i<12;++i){
  covered_draws=history_draws=0;lv_obj_scroll_to_y(list,i*5,LV_ANIM_OFF);lv_refr_now(display);
  // Compact history may intersect the list's rounded top corners. Those
  // uncovered pixels must still render; fully covered grid content must not.
  assert(covered_draws==0);corrected_history_draws+=history_draws;const auto expected=composed;
  history_draws=0;
  lv_obj_remove_event_cb_with_user_data(list,dropdown_cover_check,nullptr);
  lv_obj_invalidate(list);lv_refr_now(display);previous_history_draws+=history_draws;
  assert(composed==expected&&"Cover culling must preserve every RGB565 pixel, including rounded corners");
  lv_obj_add_event_cb(list,dropdown_cover_check,LV_EVENT_COVER_CHECK,nullptr);
 }
 assert(previous_history_draws>corrected_history_draws);
 std::cout<<"Dropdown workload: options=64, previous_history_draws="<<previous_history_draws<<", corrected_history_draws="<<corrected_history_draws<<", identical_pixels=true\\n";
 // Transparent surfaces and corner areas cannot claim opaque coverage.
 for(bool translucent:{false,true}){
  lv_area_t probe_area=translucent?lv_area_t{list_area.x1+30,list_area.y1+30,list_area.x1+40,list_area.y1+40}:
                                  lv_area_t{list_area.x1,list_area.y1,list_area.x1+1,list_area.y1+1};
  if(translucent)lv_obj_set_style_bg_opa(list,LV_OPA_50,LV_PART_MAIN);
  lv_cover_check_info_t info{LV_COVER_RES_COVER,&probe_area};lv_obj_send_event(list,LV_EVENT_COVER_CHECK,&info);
  assert(info.res!=LV_COVER_RES_COVER);
 }
 lv_obj_set_style_bg_opa(list,LV_OPA_COVER,LV_PART_MAIN);
 editable_control_close(c);set_sensor_popup_visible(&ctx,false);
 assert(lv_obj_get_parent(list)==c->dropdown);
 assert(lv_obj_get_parent(ctx.overlay)==lv_layer_top());
 auto* previous_screen=lv_screen_active();auto* next_screen=lv_obj_create(nullptr);lv_screen_load(next_screen);lv_obj_delete(previous_screen);
 assert(lv_obj_is_valid(list));set_sensor_popup_visible(&ctx,true);assert(lv_obj_get_parent(ctx.overlay)==next_screen);
 load(c,"select","First",",\\"options_complete\\":true,\\"options\\":[\\"First\\",\\"Second\\"]");
 lv_dropdown_open(c->dropdown);assert(lv_dropdown_is_open(c->dropdown));editable_control_close(c);set_sensor_popup_visible(&ctx,false);
 auto* slider_after_delete=c->slider;auto* roller_after_delete=c->number_roller;auto* arrow_after_delete=c->fields[0].up;auto* clock_roller_after_delete=c->fields[4].roller;
 editable_control_delete(c);
 assert(lv_display_remove_event_cb_with_user_data(display,dropdown_render_event,c)==0);
 assert(lv_obj_remove_event_cb_with_user_data(slider_after_delete,input_event,c)==0);
 assert(lv_obj_remove_event_cb_with_user_data(roller_after_delete,input_event,c)==0);
 assert(lv_obj_remove_event_cb_with_user_data(arrow_after_delete,input_event,c)==0);assert(lv_obj_remove_event_cb_with_user_data(clock_roller_after_delete,input_event,c)==0);
 lv_obj_delete(ctx.overlay);lv_deinit();std::cout<<"Real LVGL controls, axis layout, covered-grid culling, screen replacement and responsive geometry passed\\n";
}
`;
const source = path.join(out, 'test.cpp'); fs.writeFileSync(source, cpp);
for (const [profile,width,height,define] of [['square',480,480,'DEVICE_LAYOUT_480X480'],['wide',1024,600,'DEVICE_LAYOUT_1024X600'],['ws8',1280,800,''],['portrait',720,1280,''],['base',720,720,''],['landscape',1280,720,''],['compact-wide',800,480,'DEVICE_LAYOUT_480X480']]) {
  const binary=path.join(out,profile+(process.platform==='win32'?'.exe':''));
  let result=spawnSync(host.cxx,[...host.flags,'-std=c++17','-Wno-deprecated-declarations','-I',root,'-I',jsonInclude,'-DSCREEN_WIDTH='+width,'-DSCREEN_HEIGHT='+height,...(define?['-D'+define]:[]),...(profile==='square'?['-DDEVICE_GUITION_ESP32_4848S040']:[]),source,host.archive,'-o',binary],{encoding:'utf8'});
  assert.equal(result.status,0,result.stdout+result.stderr);
  result=spawnSync(binary,[path.join(out,profile)],{encoding:'utf8',timeout:45000});assert.equal(result.status,0,profile+': '+result.stdout+result.stderr);
  fs.writeFileSync(path.join(out,profile+'.log'),result.stdout+result.stderr);
}
console.log('Real LVGL controls: seven layouts, neutral Settings dropdowns, clock wrap, coalescing, delayed ACK/state, live icons, stable history ranges and lifecycle passed.');
