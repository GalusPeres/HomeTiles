import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import deviceCatalog from '../../device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const control = read('src/types/value/value_control.cpp');
const fn = (source, name) => {
  const result = cppFunctionDefinitions(source).find(f => f.name === name);
  assert(result, name);
  return result.source;
};
const host = await lvglHost(root);
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')]
  .filter(Boolean).find(p => fs.existsSync(path.join(p, 'ArduinoJson.h')));
if (!host || !jsonInclude) {
  console.log('SKIP: History lifecycle rendering needs LVGL, ArduinoJson and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/editable-history-lvgl');
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out,'esp_heap_caps.h'),'#pragma once\n#include <cstdlib>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\ninline void* heap_caps_malloc(size_t n,int){return malloc(n);}\ninline void heap_caps_free(void*p){free(p);}\n');
const layout = read('src/ui/popups/popup_layout.h');
const geometry = layout.slice(layout.indexOf('namespace popup_layout {'), layout.indexOf('// Standard popup close button.')) + '}';
const chartBuild = popup.slice(popup.indexOf('  // Chart wrapper: Y-axis labels'), popup.indexOf('  lv_obj_move_foreground(ctx->icon_label);'));
const rangeBuild = popup.slice(popup.indexOf('  lv_obj_t* range_row = lv_obj_create(card);'), popup.indexOf('  set_range_buttons_visible(ctx, false);', popup.indexOf('  lv_obj_t* range_row = lv_obj_create(card);')));
const cpp = `
#include <lvgl.h>
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_open.h"
void hide_popup_shell(lv_obj_t*){}
void show_popup_shell(lv_obj_t*,lv_obj_t*,lv_obj_t*,lv_obj_t*,lv_obj_t*){}
#include "src/ui/shared/title_label.h"
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
extern "C" { LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16); LV_FONT_DECLARE(ui_font_20); LV_FONT_DECLARE(ui_font_24);
LV_FONT_DECLARE(ui_font_28); LV_FONT_DECLARE(ui_font_32); LV_FONT_DECLARE(ui_font_40); LV_FONT_DECLARE(ui_font_48);
LV_FONT_DECLARE(ui_font_56); LV_FONT_DECLARE(ui_font_64); LV_FONT_DECLARE(ui_font_72); LV_FONT_DECLARE(ui_font_80); LV_FONT_DECLARE(ui_font_96); }
class String : public std::string {public:
 using std::string::string; using std::string::operator=; String()=default; String(const std::string& value):std::string(value){}
 bool isEmpty()const{return empty();} bool equalsIgnoreCase(const char* other)const{String a=*this,b=other;a.toLowerCase();b.toLowerCase();return a==b;}
 void trim(){auto a=find_first_not_of(" ");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" ")-a+1);}
 void toLowerCase(){std::transform(begin(),end(),begin(),[](unsigned char c){return std::tolower(c);});}
 char charAt(size_t i)const{return at(i);}
};
struct Config{const char* language="en";};struct Manager{Config cfg;const Config& getConfig(){return cfg;}}configManager;
namespace i18n {
 const char* binary_sensor_label(const char*,int n){static const char* labels[]={"","","History","Activity","History unavailable","No activity","24H","7D"};return labels[n];}
 const char* binary_sensor_state_label(const char*,const String& state,const String&){return state=="unknown"?"Unknown":"Unavailable";}
 struct Strings{const char* loading="Loading";};const Strings& strings(const char*){static Strings s;return s;}
 String format_number(const char*,float value,int decimals){char text[64];snprintf(text,sizeof(text),"%.*f",decimals,value);return text;}
}
// Formatting and transport are outside this test. Layout, LVGL objects,
// history parsing, visibility, timeline drawing and row reuse are real code.
String normalize_state_live_value(const String& value){return value;}
String normalize_state_history_value(const String& value){return value;}
String format_state_history_label(const String& value){return value;}
String format_state_history_date(uint64_t){return "Today · 09/07/2026";}
String format_binary_activity_time(uint64_t,bool){return "11:16:39 AM";}
uint32_t ticks=100;uint32_t millis(){return ticks;}
struct Logger{void println(const char*){}template<class... T>void printf(const char*,T...){}}Serial;
int opening_layout_calls=0;
void counted_opening_layout(lv_obj_t* object){++opening_layout_calls;lv_obj_update_layout(object);}
#define lv_obj_update_layout counted_opening_layout
${geometry}
#undef lv_obj_update_layout
${popup.slice(popup.indexOf('constexpr int kBinaryTimelineHeight') - '#if defined(DEVICE_LAYOUT_480X480)\n'.length, popup.indexOf('struct HistoryRangeConfig'))}
constexpr int kCardWidth=popup_layout::kCardWidth,kCardPad=popup_layout::kCardPad,kChartHeight=popup_layout::contentScale(325),kTimeAxisMarkerCount=8,kChartLineWidth=popup_layout::scale(4),kHistoryPoints24h=288;
constexpr int kRangeButtonWidth=popup_layout::scale(92),kRangeButtonHeight=popup_layout::kNavHeight,kRangeButtonGap=popup_layout::scale(10);
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kTimeAxisHeight=20;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kTimeAxisHeight=24;
#else
constexpr int kTimeAxisHeight=20;
#endif
constexpr size_t kBinaryMaxActivityEntries=96,kBinaryMaxSegments=96,kBinaryMaxTimelineBins=768,kStateHistoryMaxPaletteEntries=16;
struct EditableControl;
${popup.match(/struct HistoryRangeConfig \{[\s\S]*?\n};/)[0]}
${popup.match(/struct SensorPopupContext \{[\s\S]*?\n};/)[0]}
${read('src/ui/popups/sensor/sensor_popup.h').match(/struct SensorPopupInit \{[\s\S]*?\n};/)[0]}
SensorPopupContext* g_sensor_popup_ctx=nullptr;PopupFirstFrame g_sensor_first_frame;SensorPopupInit g_pending_sensor_init;bool g_sensor_open_pending=false;
struct Pending{bool valid=false;}g_pending_history,g_pending_binary_state;
struct EditableValue{String kind,state="32",unit="%";bool available=true;};
struct Bridge{String payload="number";String findEditableValue(const String&){return payload;}}haBridgeConfig;
EditableValue parse_editable_value(const String&p){EditableValue v;v.kind=String(p.substr(0,p.find('|')));return v;}
uint32_t editable_value_generation(){return 1;}
void editable_control_close(EditableControl*){}void editable_control_open(EditableControl*,const String&){}
bool isMdiIconDisabled(const String&){return false;}String getMdiChar(const String&){return "";}

void update_binary_state(SensorPopupContext*,const String&,bool,const String&,uint64_t,const String&){assert(false);}
void hide_pin_popup(){}void hide_camera_popup(){}void hide_climate_popup(){}void hide_cover_popup(){}void hide_light_popup(){}void hide_weather_popup(){}void hide_energy_popup(){}void hide_media_popup(){}
void viewNavigationPopupShown(lv_obj_t*,const char*){}
HistoryRangeConfig get_history_range_config(SensorHistoryRange r){return r==SensorHistoryRange::Day7?HistoryRangeConfig{168,35,288}:HistoryRangeConfig{24,5,288};}
${fn(control, 'editable_control_height')}
${['set_label_style','set_range_buttons_visible','style_range_button','update_range_buttons','accept_editable_history_range','clear_chart'].map(n => fn(popup,n)).join('\n')}
void build_chart(SensorPopupContext* ctx){auto* body_box=ctx->body_box;${chartBuild}}
${fn(popup,'measure_label_text_width')}
int calc_time_axis(const SensorPopupContext* ctx,String* labels,float* fracs,int){int count=ctx->history_range==SensorHistoryRange::Day7?7:4;for(int i=0;i<count;++i){labels[i]=std::to_string(i);fracs[i]=float(i)/(count-1);}return count;}
int calc_day7_boundary_axis(float*,int){return 0;}
static void refresh_binary_activity_rows(SensorPopupContext*,bool force=false);
int history_layout_calls=0;
${['update_binary_time_axis','update_y_axis_layout','resize_editable_chart','editable_control_top','layout_editable_history','extract_epoch','extract_numeric','binary_state_code','binary_state_color','binary_state_priority','binary_state_identifier','state_history_color','local_date_key','on_binary_timeline_draw','refresh_binary_labels','refresh_binary_activity_rows','on_binary_activity_scroll','clear_binary_history','ensure_binary_view','binary_timeline_hex_nibble','decode_state_timeline'].map(n => n==='layout_editable_history' ? fn(popup,n).replace('{','{ ++history_layout_calls;') : fn(popup,n)).join('\n')}
void update_value_label(SensorPopupContext*ctx,const String& value,const String& unit){ctx->unit=unit;if(ctx->value_label)lv_label_set_text(ctx->value_label,value.c_str());}
void apply_binary_history_payload(SensorPopupContext*,DynamicJsonDocument&){assert(false&&"Unexpected binary response");}
${fn(popup,'apply_state_history_payload')}
${fn(popup,'apply_history_payload')}
${fn(popup,'editable_history_fingerprint')}
int requests=0;
void request_history_for_context(SensorPopupContext*ctx){ctx->editable_history_id=std::to_string(++requests);ctx->history_request_fingerprint=editable_history_fingerprint(ctx->entity_id);}
${fn(popup,'apply_sensor_header')}
${fn(popup,'apply_init_to_context')}
void build_popup_shell(SensorPopupContext*,const SensorPopupInit&){assert(false&&"Reopening must reuse the existing shell");}
void build_popup_body(SensorPopupContext*){}
bool is_popup_visible(SensorPopupContext*c){return c&&c->card&&!lv_obj_has_flag(c->card,LV_OBJ_FLAG_HIDDEN);}
${fn(popup,'reusable_sensor_body')}
${fn(popup,'finish_sensor_popup_open')}
${['set_sensor_popup_visible','show_sensor_popup','hide_sensor_popup'].map(n=>fn(popup,n)).join('\n')}
${fn(popup,'on_range_click')}
void build_range_buttons(SensorPopupContext*ctx){auto*card=ctx->card;${rangeBuild}}
lv_obj_t* box(lv_obj_t*parent){auto*obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_width(obj,LV_PCT(100));lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);return obj;}
bool shown(lv_obj_t*obj){return !lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);}
void snapshot(const char*file,const std::vector<uint32_t>&pixels){std::ofstream out(file,std::ios::binary);auto u16=[&](uint16_t v){out.write(reinterpret_cast<char*>(&v),2);};auto u32=[&](uint32_t v){out.write(reinterpret_cast<char*>(&v),4);};out.write("BM",2);u32(54+pixels.size()*4);u32(0);u32(54);u32(40);u32(SCREEN_WIDTH);u32(-SCREEN_HEIGHT);u16(1);u16(32);u32(0);u32(pixels.size()*4);u32(0);u32(0);u32(0);u32(0);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4);}
int main(int argc,char**argv){
 lv_init();auto*display=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t>pixels(SCREEN_WIDTH*SCREEN_HEIGHT);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*4,LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 lv_theme_default_init(display,lv_color_hex(0x26A69A),lv_color_hex(0xC14444),false,&ui_font_20);
 auto*card=box(lv_screen_active());lv_obj_set_size(card,popup_layout::kCardWidth,popup_layout::kCardHeight);lv_obj_center(card);lv_obj_set_style_pad_all(card,popup_layout::kCardPad,0);lv_obj_set_style_bg_color(card,lv_color_hex(0x2A2A2A),0);lv_obj_set_style_bg_opa(card,LV_OPA_COVER,0);
 SensorPopupContext ctx;ctx.card=card;ctx.body_box=box(card);build_chart(&ctx);ctx.editable=true;ctx.state_history_mode=true;ctx.entity_id="test.entity";
 build_range_buttons(&ctx);
 ensure_binary_view(&ctx);
 auto geometry=[&](){lv_obj_update_layout(card);return std::vector<int>{lv_obj_get_y(ctx.body_box),lv_obj_get_height(ctx.body_box),lv_obj_get_y(ctx.binary_activity_title),lv_obj_get_y(ctx.binary_activity_viewport),lv_obj_get_height(ctx.binary_activity_viewport),lv_obj_get_height(ctx.chart_wrap)};};
 auto axes=[&](bool categorical){for(int i=0;i<kTimeAxisMarkerCount;++i){bool expected=categorical&&i<(ctx.history_range==SensorHistoryRange::Day7?7:4);assert(shown(ctx.binary_time_labels[i])==expected&&"Categorical axes must never enter Number or Time Activity");}};
 auto payload=[&](bool empty=false,bool error=false){auto cfg=get_history_range_config(ctx.editable_requested_range);JsonDocument doc;doc["entity_id"]="test.entity";doc["request_id"]=ctx.editable_history_id.c_str();doc["hours"]=cfg.hours;doc["period_minutes"]=cfg.period_minutes;doc["kind"]="editable";doc["unit"]="%";doc["range_start"]=1788768000;doc["range_end"]=1788854400;doc["history_available"]=!error;
  if(error)doc["error"]="recorder_unavailable";
  auto values=doc["values"].to<JsonArray>();auto activity=doc["activity"].to<JsonArray>();auto palette=doc["palette"].to<JsonArray>();palette.add("Home");palette.add("Office");
  if(!empty&&!error){for(int v:{20,60,100,40,80})values.add(v);for(int i=0;i<40;++i){auto item=activity.add<JsonObject>();item["timestamp"]=1788854400+i*60;item["state"]=ctx.editable_kind=="time"?"17:00:00":ctx.editable_kind=="select"?"Home":"32";}doc["timeline_points"]=4;doc["timeline_encoding"]="palette4-hex";doc["timeline_data"]="0011";}
  String result;serializeJson(doc,static_cast<std::string&>(result));return result;
 };
 int section_top=-1,select_activity_y=-1,select_viewport_height=-1;
 for(int cycle=0;cycle<3;++cycle)for(const char*kind:{"select","time","number","date","number","datetime","select"}){
  ctx.editable_kind=kind;ctx.history_range=ctx.editable_requested_range=SensorHistoryRange::Day24;layout_editable_history(&ctx);
  auto expected=geometry();if(section_top<0)section_top=expected[0];if(ctx.editable_kind=="number"||ctx.editable_kind=="select")assert(expected[0]==section_top&&"Number and Select must share the first section baseline");if(ctx.editable_kind=="time")assert(expected[0]>section_top&&expected[0]-section_top<=popup_layout::scale(28));
  assert(expected[3]+expected[4]==expected[1]&&"Every Activity viewport must fill the available body down to the footer gap");
  // This is the production reuse/open sequence, including the clear that
  // previously exposed an old categorical axis after layout had hidden it.
  clear_binary_history(&ctx);clear_chart(&ctx,288);request_history_for_context(&ctx);axes(ctx.editable_kind=="select");
  const bool numeric=ctx.editable_kind=="number",temporal=ctx.editable_kind!="select"&&!numeric;
  if(ctx.editable_kind=="select"){
   select_activity_y=expected[2];select_viewport_height=expected[4];
   lv_area_t heading,bar;lv_obj_get_coords(ctx.binary_history_title,&heading);lv_obj_get_coords(ctx.binary_timeline,&bar);
   assert(bar.y1>heading.y2&&bar.y1-heading.y2<=popup_layout::scale(20)&&"The categorical bar must directly follow History without a numeric graph allocation");
  }
  if(numeric){assert(expected[2]>select_activity_y&&expected[4]<=select_viewport_height&&"Compact Select history must leave more room for Activity than a numeric graph");}
  assert(geometry()==expected);assert(shown(ctx.chart_wrap)==numeric);assert(shown(ctx.binary_timeline)==(!numeric&&!temporal));assert(shown(ctx.binary_history_title)==!temporal);
  for(auto range:{SensorHistoryRange::Day24,SensorHistoryRange::Day7,SensorHistoryRange::Day24}){
   lv_obj_send_event(range==SensorHistoryRange::Day7?ctx.range_week_btn:ctx.range_day_btn,LV_EVENT_CLICKED,nullptr);
   assert(geometry()==expected);axes(ctx.editable_kind=="select");
   const String valid=payload();apply_history_payload(&ctx,valid.c_str());
   assert(geometry()==expected&&"History replies must preserve section allocations");axes(ctx.editable_kind=="select");
   assert(shown(ctx.chart_wrap)==numeric&&"Activity parsing must not hide a Number graph");
   assert(ctx.binary_activity.size()==40&&shown(ctx.binary_activity_rows[0]));
   assert(!shown(ctx.binary_history_status));
   lv_obj_update_layout(card);lv_area_t body,activity;lv_obj_get_coords(ctx.body_box,&body);lv_obj_get_coords(ctx.binary_activity_viewport,&activity);
   if(ctx.editable_kind=="select"){
    lv_area_t bar,axis,heading;lv_obj_get_coords(ctx.binary_timeline,&bar);lv_obj_get_coords(ctx.binary_time_labels[0],&axis);lv_obj_get_coords(ctx.binary_activity_title,&heading);
    assert(axis.y1>bar.y2&&axis.y1-bar.y2<=popup_layout::scale(8)&&"Time labels must stay directly below the categorical bar");
    assert(heading.y1>axis.y2&&heading.y1-axis.y2<=popup_layout::scale(16)&&"Activity must follow the compact categorical axis");
    if(cycle==0)std::cout<<"Compact Select "<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<": bar/axis gap="<<axis.y1-bar.y2<<", axis/Activity gap="<<heading.y1-axis.y2<<", Activity height="<<expected[4]<<"\\n";
   }
   assert(activity.y2<=body.y2&&activity.y2-activity.y1+1>=kBinaryActivityRowHeight);
   lv_area_t footer;lv_obj_get_coords(ctx.range_row,&footer);
   assert(footer.y1-activity.y2==popup_layout::scale(12)+1&&"All modes must use the same small gap above the real range buttons");
   const int max_scroll=40*kBinaryActivityRowHeight-lv_obj_get_height(ctx.binary_activity_viewport);
   const auto child_count=lv_obj_get_child_count(ctx.binary_activity_viewport);
   for(int offset:{0,kBinaryActivityRowHeight/2,max_scroll/2,max_scroll,0}){
    lv_obj_scroll_to_y(ctx.binary_activity_viewport,offset,LV_ANIM_OFF);lv_obj_update_layout(card);
    const int actual_scroll=lv_obj_get_scroll_y(ctx.binary_activity_viewport);
    const int first=actual_scroll/kBinaryActivityRowHeight;
    const int last=std::min(39,(actual_scroll+lv_obj_get_height(ctx.binary_activity_viewport)-1)/kBinaryActivityRowHeight);
    for(int index=first;index<=last;++index){
     const int slot=index%kBinaryActivityPoolRows;
     assert(shown(ctx.binary_activity_rows[slot])&&ctx.binary_activity_row_indices[slot]==static_cast<size_t>(index)&&"Every visible Activity row, including the bottom partial row, must be populated while scrolling");
    }
    assert(lv_obj_get_child_count(ctx.binary_activity_viewport)==child_count&&"Scrolling must reuse the existing row pool");
   }
   if(numeric){assert(lv_chart_get_point_count(ctx.chart)==5);assert(lv_chart_get_series_y_array(ctx.chart,ctx.series)[2]>0);lv_area_t graph;lv_obj_get_coords(ctx.chart_wrap,&graph);assert(graph.y2<activity.y1);lv_refr_now(display);lv_area_t chart;lv_obj_get_coords(ctx.chart,&chart);int bright=0;for(int y=chart.y1+2;y<chart.y2-2;++y)for(int x=chart.x1+2;x<chart.x2-2;++x)if((pixels[y*SCREEN_WIDTH+x]&0xffffff)==0xffffff)++bright;assert(bright>20&&"The numeric series must actually render");}
   if(cycle==0&&argc>1){lv_refr_now(display);snapshot((String(argv[1])+"-"+kind+"-"+std::to_string(get_history_range_config(range).hours)+".bmp").c_str(),pixels);}
   const auto count=ctx.binary_activity.size();apply_history_payload(&ctx,R"({"entity_id":"other.entity"})");assert(ctx.binary_activity.size()==count&&geometry()==expected);
  }
  apply_history_payload(&ctx,payload(true).c_str());assert(ctx.binary_activity.empty()&&shown(ctx.binary_activity_status)&&geometry()==expected);axes(ctx.editable_kind=="select");
  apply_history_payload(&ctx,payload(false,true).c_str());assert(shown(ctx.binary_history_status)&&geometry()==expected);axes(ctx.editable_kind=="select");
  apply_history_payload(&ctx,payload().c_str());assert(!shown(ctx.binary_history_status)&&geometry()==expected&&shown(ctx.chart_wrap)==numeric);
  if(numeric){
   JsonDocument invalid;deserializeJson(invalid,payload());invalid["period_minutes"]=99;invalid["values"]=nullptr;String json;serializeJson(invalid,static_cast<std::string&>(json));const auto count=ctx.binary_activity.size();const auto range=ctx.history_range;apply_history_payload(&ctx,json.c_str());assert(ctx.binary_activity.size()==count&&ctx.history_range==range);
   deserializeJson(invalid,payload(false,true));invalid.remove("values");json.clear();serializeJson(invalid,static_cast<std::string&>(json));apply_history_payload(&ctx,json.c_str());assert(!ctx.history_loaded&&shown(ctx.binary_history_status)&&ctx.binary_activity.empty());
  }
 }
 // Execute the actual open/rebind/hide path on the populated LVGL shell.
 // History data and rows must survive a warm reopen; changed metadata, state
 // during an in-flight request, locale, range and entity force fresh history.
 ctx.overlay=box(lv_screen_active());lv_obj_set_size(ctx.overlay,SCREEN_WIDTH,SCREEN_HEIGHT);lv_obj_set_parent(card,ctx.overlay);
 ctx.value_box=box(card);ctx.value_label=lv_label_create(ctx.value_box);ctx.control_row=box(card);
 g_sensor_popup_ctx=&ctx;SensorPopupInit init;init.editable=true;init.entity_id="test.entity";init.value="32";init.unit="%";
 auto open=[&](){const bool cached=reusable_sensor_body(&ctx,init);const int before=requests,layouts=history_layout_calls,opening_layouts=opening_layout_calls;show_sensor_popup(init);assert(opening_layout_calls==opening_layouts&&"Opening must not force a whole-screen layout before its first frame");assert(popup_open_pending(ctx.card)&&PopupFirstFrame::any_pending()&&g_sensor_open_pending&&requests==before&&history_layout_calls==layouts);assert(shown(ctx.body_box)==cached&&"Matching cached content must be visible in the first frame");finish_sensor_popup_open();assert(g_sensor_open_pending&&requests==before);lv_refr_now(display);process_popup_open();assert(!g_sensor_open_pending&&g_sensor_first_frame.pending()&&shown(ctx.body_box));lv_refr_now(display);};
 auto open_and_load=[&](){open();apply_history_payload(&ctx,payload().c_str());assert(ctx.history_loaded);};
 for(const char*kind:{"number","select","time"}){
  haBridgeConfig.payload=kind;open_and_load();const auto children=lv_obj_get_child_count(ctx.binary_activity_viewport);const auto count=ctx.binary_activity.size();const int requested=requests,layouts=history_layout_calls;
  for(int i=0;i<20;++i){hide_sensor_popup();show_sensor_popup(init);assert(requests==requested&&ctx.binary_activity.size()==count&&lv_obj_get_child_count(ctx.binary_activity_viewport)==children&&history_layout_calls==layouts);assert(shown(ctx.chart_wrap)==(ctx.editable_kind=="number"));assert(PopupFirstFrame::any_pending());hide_sensor_popup();assert(!PopupFirstFrame::any_pending());}
  haBridgeConfig.payload=String(kind)+"|state_changed";open();assert(requests==requested+1&&ctx.binary_activity.size()==count);
  haBridgeConfig.payload=String(kind)+"|state_changed_again";apply_history_payload(&ctx,payload().c_str());hide_sensor_popup();open();assert(requests==requested+2&&"A response to an old state must not mark the current state fresh");
  open_and_load();ticks+=60000;int before=requests;hide_sensor_popup();open();assert(requests==before+1&&ctx.binary_activity.size()==count);
  open_and_load();ctx.state_history_refresh_pending=true;before=requests;hide_sensor_popup();open();assert(requests==before+1);
  open_and_load();configManager.cfg.language="de";open();assert(ctx.binary_activity.empty());configManager.cfg.language="en";
  open_and_load();ctx.history_range=SensorHistoryRange::Day7;open();assert(ctx.binary_activity.empty());
  open_and_load();ctx.unit="changed";init.unit="changed";open();assert(ctx.binary_activity.empty()&&"Live metadata must not relabel the cached history");init.unit="%";
  open_and_load();ctx.editable_kind="date";haBridgeConfig.payload="date";open();assert(ctx.binary_activity.empty()&&"History belongs to the rendered kind, not subsequently changed live metadata");haBridgeConfig.payload=kind;
  open_and_load();init.entity_id="other.entity";open();assert(ctx.binary_activity.empty());init.entity_id="test.entity";
 }
 // Numeric Sensor history uses the same resident graph on every warm opening.
 hide_sensor_popup();init.editable=false;init.entity_id="sensor.temperature";init.unit="C";
 open();lv_chart_set_value_by_id(ctx.chart,ctx.series,0,123);
 const auto* sensor_graph=ctx.chart;const int sensor_layouts=history_layout_calls;
 for(int i=0;i<20;++i){hide_sensor_popup();open();assert(ctx.chart==sensor_graph&&shown(ctx.value_box)&&shown(ctx.chart_wrap));assert(lv_chart_get_y_array(ctx.chart,ctx.series)[0]==123&&"Warm Sensor opens must not clear the cached graph");assert(history_layout_calls==sensor_layouts);}
 init.entity_id="sensor.other";open();assert(lv_chart_get_y_array(ctx.chart,ctx.series)[0]==LV_CHART_POINT_NONE&&"A different entity must not inherit the previous graph");
 hide_sensor_popup();g_sensor_popup_ctx=nullptr;
 // Existing textual and numeric Sensor modes must still restore their own UI.
 ctx.editable=false;ctx.state_history_mode=true;layout_editable_history(&ctx);clear_binary_history(&ctx);axes(true);assert(!shown(ctx.chart_wrap)&&shown(ctx.binary_timeline));
 ctx.state_history_mode=false;layout_editable_history(&ctx);assert(shown(ctx.chart_wrap));
 lv_deinit();std::cout<<"History lifecycle "<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<": repeated modes, real payloads, rendered graph, ranges, empty/error/recovery and stable sections passed\\n";
}
`;
const source = path.join(out,'test.cpp');fs.writeFileSync(source,cpp);
const layouts=[['square',480,480,'DEVICE_LAYOUT_480X480'],['wide',1024,600,'DEVICE_LAYOUT_1024X600'],['ws8',1280,800,''],['portrait',720,1280,''],['base',720,720,''],['landscape',1280,720,''],['compact-wide',800,480,'DEVICE_LAYOUT_480X480']];
const selection=read('src/devices/device_select.h');
for(const device of deviceCatalog.profiles){
 const dir=`src/devices/${device.metadataDeviceKey}`;
 const header=fs.existsSync(path.join(root,dir,'profile.h'))?`${dir}/profile.h`:`${dir}/device_${device.metadataDeviceKey}.h`;
 const dimensions=read(header).match(/kProfile\s*\{\s*"[^"]*",\s*"[^"]*",\s*(\d+),\s*(\d+),/);
 assert(dimensions,`${device.buildProfile}: profile dimensions must be checked`);
 const define=['DEVICE_LAYOUT_480X480','DEVICE_LAYOUT_1024X600'].find(name=>{
  const end=selection.indexOf(`#define ${name}\n`);
  return selection.slice(selection.lastIndexOf('#if ',end),end).includes(`defined(${device.define})`);
 })||'';
 const covered=layouts.find(([,width,height,layout])=>width===Number(dimensions[1])&&height===Number(dimensions[2])&&layout===define);
 assert(covered,`${device.buildProfile}: actual dimensions and layout must have native coverage`);
 console.log(`${device.buildProfile}: ${covered[0]} (${dimensions[1]}x${dimensions[2]})`);
}
for(const [profile,width,height,define] of layouts){
 const binary=path.join(out,profile+(process.platform==='win32'?'.exe':''));
 let result=spawnSync(host.cxx,[...host.flags,'-std=c++17','-Wno-deprecated-declarations','-I',root,'-I',jsonInclude,'-DSCREEN_WIDTH='+width,'-DSCREEN_HEIGHT='+height,...(define?['-D'+define]:[]),source,path.join(root,'src/ui/popups/popup_open.cpp'),'-I',out,host.archive,'-o',binary],{encoding:'utf8'});
 assert.equal(result.status,0,result.stdout+result.stderr);
 result=spawnSync(binary,[path.join(out,profile)],{encoding:'utf8',timeout:45000});assert.equal(result.status,0,profile+': '+result.stdout+result.stderr);
 fs.writeFileSync(path.join(out,profile+'.log'),result.stdout+result.stderr);
}
console.log('History replies and popup reuse: every device layout, full Activity height, scrolling row coverage, stable ranges and unavailable recovery passed.');
