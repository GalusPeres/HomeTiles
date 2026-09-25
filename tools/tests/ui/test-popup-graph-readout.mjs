// Finger readout in the Sensor, Number, Select, Binary Sensor and Energy
// popups. While a finger rests on a graph, the old value row shows the touched
// time and value; release restores the normal drawing. The readout must never
// scroll, swipe or click anything else, and dragging changes only the cursor
// and two static labels, once per display refresh.
//
// The Sensor chart and timeline readout run in real LVGL in
// tools/tests/tiles/test-editable-history-lvgl.mjs. This test checks the
// shared helper and the lifecycle contract, and runs the real Energy popup.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const code = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(definition => definition.name === name);
  assert(found, name);
  return found.source;
};
const strip = source => source.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');

// ---- Shared touch surface and band labels ----------------------------------
const helper = code(read('src/ui/popups/popup_graph_readout.h'));
for (const marker of [
  'LV_OBJ_FLAG_CLICKABLE |',
  'LV_OBJ_FLAG_PRESS_LOCK',
  'LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN |',
  'LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE |',
  'for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,',
  'LV_EVENT_PRESS_LOST, LV_EVENT_INDEV_RESET})',
  'lv_obj_add_event_cb(target, input, code, this);',
  'lv_display_add_event_cb(display_, refresh, LV_EVENT_REFR_START, this);',
  'if (lv_timer_t* timer = lv_display_get_refr_timer(display_)) lv_timer_resume(timer);',
  'lv_display_remove_event_cb_with_user_data(display_, refresh, this);',
  'if (code == LV_EVENT_PRESSING && point.x == self->point_.x && point.y == self->point_.y) return;',
  'lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);',
  'lv_obj_set_size(label, LV_PCT(100), lv_font_get_line_height(font));',
  'lv_label_set_text_static(label, text);',
  'constexpr const char* kRangeSeparator = " \\xE2\\x80\\x93 ";',
]) assert.ok(helper.includes(marker), `popup_graph_readout.h: ${marker}`);
// A deleted target must never reach a released owner.
assert.doesNotMatch(helper, /lv_obj_add_event_cb\([^;]*LV_EVENT_(?:ALL|DELETE)/,
  'The graph surface registers only its input events');
// Moves are stored in input events and applied by the refresh hook.
const input = helper.slice(helper.indexOf('static void input('), helper.indexOf('void schedule()'));
assert.doesNotMatch(input, /apply_\(/, 'Input events must not apply the readout directly');

// ---- Sensor, Number, Select, Binary Sensor ---------------------------------
const sensor = read('src/ui/popups/sensor/sensor_popup.cpp');
const hot = ['apply_chart_readout', 'apply_timeline_readout', 'on_readout_cursor_draw',
  'move_readout_cursor', 'invalidate_readout_cursor'];
for (const name of hot) {
  const body = code(fn(sensor, name));
  for (const forbidden of [/lv_label_set_text\(/, /lv_obj_create\(/, /lv_label_create\(/, /\bnew\s/,
    /malloc\(/, /lv_chart_set_/, /lv_chart_refresh\(/, /lv_obj_set_(?:pos|x|y|size)\(/,
    /lv_obj_align\(/, /lv_obj_update_layout\(/, /\bString\(/, /lv_obj_invalidate\(ctx->chart\)/])
    assert.doesNotMatch(body, forbidden, `${name} must only move the cursor and labels (${forbidden})`);
}
const chartReadout = code(fn(sensor, 'apply_chart_readout'));
assert.match(chartReadout, /values\[index - distance\] != LV_CHART_POINT_NONE[\s\S]*values\[index \+ distance\] != LV_CHART_POINT_NONE/,
  'The chart readout snaps to the nearest real history point');
assert.match(chartReadout, /lv_chart_get_point_pos_by_id\(chart, ctx->series/, 'The dot sits on the drawn point');
assert.match(chartReadout, /ctx->history_values\[found\]/, 'Readouts use the exact, unscaled history value');
assert.match(chartReadout, /ctx->history_range == SensorHistoryRange::Day7/, '7D readouts include the weekday');
const timelineReadout = code(fn(sensor, 'apply_timeline_readout'));
assert.match(timelineReadout, /binary_state_priority\(bins\[candidate\]\) > binary_state_priority\(bins\[index\]\)/,
  'Binary readouts use the same pixel state as the drawn timeline');
assert.match(timelineReadout, /i18n::binary_sensor_state_label\([\s\S]*binary_state_identifier_text\(state\)/,
  'Binary readouts use the translated state label');
assert.match(timelineReadout, /write_state_history_label\(/, 'Textual states read like the Activity list');
assert.match(timelineReadout, /kRangeSeparator/, 'Timeline segments read as a time range');
const cursor = code(fn(sensor, 'on_readout_cursor_draw'));
for (const marker of ['line.bg_color = lv_color_white();', 'dot.bg_color = lv_color_white();',
  'dot.radius = LV_RADIUS_CIRCLE;', 'dot.border_color = lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN);'])
  assert.ok(cursor.includes(marker), `Cursor: ${marker}`);
const chartAttach = code(fn(sensor, 'attach_chart_readout'));
for (const marker of ['lv_obj_remove_flag(ctx->chart, LV_OBJ_FLAG_CLICKABLE);', 'ctx->readout.attach(ctx->chart_wrap);',
  'lv_obj_add_event_cb(ctx->chart_wrap, on_readout_cursor_draw, LV_EVENT_DRAW_POST, ctx);'])
  assert.ok(chartAttach.includes(marker), `Chart readout: ${marker}`);
const timelineAttach = code(fn(sensor, 'attach_timeline_readout'));
for (const marker of ['ctx->readout.attach(ctx->binary_timeline);',
  'lv_obj_set_ext_click_area(ctx->binary_timeline, kTimelineTouchSlop);'])
  assert.ok(timelineAttach.includes(marker), `Timeline readout: ${marker}`);
assert.ok(code(fn(sensor, 'ensure_binary_view')).includes('attach_timeline_readout(ctx);'));
const body = code(fn(sensor, 'build_popup_body'));
assert.ok(body.includes('build_readout_band(ctx);') && body.includes('attach_chart_readout(ctx);'));
assert.doesNotMatch(body, /lv_label_create\(value_box\)/, 'The old value row holds only the readout');
assert.match(code(fn(sensor, 'build_readout_band')),
  /create_band_label\(\s*ctx->value_box, popup_layout::font20\(\), ctx->readout_time_text\)[\s\S]*create_band_label\(\s*ctx->value_box, get_value_font\(\), ctx->readout_value_text\)/,
  'Small time above the large value, in the old value row');
// Editors keep their controls; the band covers them only while touching.
const band = code(fn(sensor, 'show_readout_band'));
assert.match(band, /if \(ctx->editable && !ctx->readout_band_raised\)/);
assert.doesNotMatch(band, /control_row, LV_OBJ_FLAG_HIDDEN|editable_control_/, 'The readout never hides or drives an editor');
const clear = code(fn(sensor, 'clear_sensor_readout'));
assert.match(clear, /lv_obj_set_style_bg_opa\(ctx->value_box, LV_OPA_TRANSP, 0\);[\s\S]*if \(ctx->editable\) lv_obj_add_flag\(ctx->value_box, LV_OBJ_FLAG_HIDDEN\);/);
// Lifecycle: every close, hide, open and teardown path ends the readout.
for (const name of ['on_close_click', 'hide_sensor_popup', 'show_sensor_popup'])
  assert.ok(code(fn(sensor, name)).includes('readout.cancel();'), `${name} ends the readout`);
const teardown = code(fn(sensor, 'on_overlay_delete'));
assert.match(teardown, /ctx->readout\.cancel\(\);[\s\S]*heap_caps_free\(ctx->history_values\);[\s\S]*delete ctx;/,
  'Teardown detaches the refresh hook and frees the history buffer');
assert.match(code(fn(sensor, 'keep_chart_history')), /MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT/,
  'Exact history values are kept in PSRAM');
// Data waits while a finger reads the graph.
assert.match(code(fn(sensor, 'process_sensor_popup_queue')),
  /g_pending_history\.valid[\s\S]*!g_sensor_popup_ctx->readout\.active\(\)\) \{[\s\S]*apply_history_payload/,
  'History is applied after the finger leaves the graph');
assert.match(code(fn(sensor, 'prepend_state_history_activity')), /ctx->readout\.request_apply\(\);/,
  'A live state change updates a resting readout');
// Other controls keep working: range buttons and Activity are not graph surfaces.
assert.doesNotMatch(sensor, /readout\.attach\((?:ctx->)?(?:range_|binary_activity|control_row|body_box|card)/);

// ---- Energy ----------------------------------------------------------------
const energy = read('src/ui/popups/energy/energy_popup.cpp');
const energyUi = code(fn(energy, 'build_popup_ui'));
for (const marker of ['ctx->readout.attach(chart_wrap);', 'lv_obj_remove_flag(x_axis, LV_OBJ_FLAG_CLICKABLE);',
  'popup_graph_readout::create_band_label(\n      value_box, popup_layout::font20(), ctx->readout_time_text);',
  'popup_graph_readout::create_band_label(\n      value_box, value_font(), ctx->readout_value_text);'])
  assert.ok(energyUi.includes(marker), `Energy readout: ${marker}`);
const energyApply = code(fn(energy, 'apply_energy_readout'));
assert.doesNotMatch(energyApply, /lv_label_set_text\(|lv_obj_set_(?:pos|size|x|y)\(|lv_chart_|String\(/,
  'Dragging over Energy bars changes only bar opacity and static labels');
for (const name of ['on_close_click', 'on_overlay_delete', 'on_period_click', 'hide_energy_popup',
  'show_energy_popup', 'apply_entry_to_chart'])
  assert.ok(code(fn(energy, name)).includes('readout.cancel();'), `Energy ${name} ends the readout`);
assert.match(code(fn(energy, 'process_energy_popup_queue')),
  /if \(!g_pending_refresh\.valid\) return;\s*if \(g_energy_popup_ctx->readout\.active\(\)\) return;/,
  'Energy data waits while a finger reads a bar');

// ---- Real LVGL: Energy popup ------------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Energy readout rendering needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/popup-graph-readout');
fs.mkdirSync(out, {recursive: true});
const energyData = read('src/types/energy/energy_data.h');
const cpp = String.raw`
#include <lvgl.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include "src/ui/shared/title_label.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
#include "src/ui/popups/popup_graph_readout.h"
extern "C" {LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(ui_font_48);LV_FONT_DECLARE(ui_font_56);LV_FONT_DECLARE(ui_font_64);LV_FONT_DECLARE(ui_font_72);LV_FONT_DECLARE(ui_font_80);LV_FONT_DECLARE(ui_font_96);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_40);LV_FONT_DECLARE(mdi_icons_48);}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#elif defined(DEVICE_LAYOUT_1024X600)
#define FONT_MDI_ICONS (&mdi_icons_40)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
class String:public std::string{public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){}
 void trim(){auto a=find_first_not_of(" \r\n");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \r\n")-a+1);}
 bool equalsIgnoreCase(const String&s)const{if(s.size()!=size())return false;for(size_t i=0;i<size();++i)if(std::tolower((unsigned char)at(i))!=std::tolower((unsigned char)s[i]))return false;return true;}};
String getMdiChar(const String&){return "\xF3\xB0\x96\xAD";}
bool isMdiIconDisabled(const String&){return false;}
// Surface styles are covered by the shell tests; this test needs geometry only.
namespace ui_surface_style {
template<class T> void apply_radius(lv_obj_t* obj,T radius,lv_style_selector_t selector){lv_obj_set_style_radius(obj,static_cast<int32_t>(radius),selector);}
inline void apply_global_tile_border(lv_obj_t*){}inline void apply_popup_border(lv_obj_t*,lv_color_t,lv_opa_t){}inline lv_opa_t icon_glow_opa(){return 64;}
inline lv_color_t border_hint(lv_color_t c){return lv_color_mix(lv_color_white(),c,128);}
}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void*p){free(p);}
struct DeviceConfig{const char* language="en";uint8_t global_time_format=1;};
struct Manager{DeviceConfig cfg;const DeviceConfig& getConfig(){return cfg;}}configManager;
namespace i18n {
struct LocaleProfile{const char* decimal_separator;const char* hour_axis_suffix;const char* weekday_names[7];};
const LocaleProfile& locale(const char* language){
 static const LocaleProfile en{".","",{"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"}};
 static const LocaleProfile de{",","",{"Sonntag","Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag"}};
 return strcmp(language,"de")==0?de:en;}
const char* weather_today_label(const char*){return "Today";}
struct Strings{const char* loading="Loading";};const Strings& strings(const char*){static Strings s;return s;}
String format_number(const char* language,float value,uint8_t decimals,bool=false){if(!std::isfinite(value))return "--";char text[48];snprintf(text,sizeof(text),"%.*f",decimals>6?6:decimals,value);String result=text;if(locale(language).decimal_separator[0]==',')std::replace(result.begin(),result.end(),'.',',');return result;}
String weather_weekday_short(const char*,const String& iso){return iso.substr(5);}
}
namespace clock_tile {
enum : uint8_t {TIME_FORMAT_AUTO=0,TIME_FORMAT_24H=1,TIME_FORMAT_12H=2};
inline uint8_t resolve_time_format(int preferred,int global,const char*){return preferred?preferred:(global?global:TIME_FORMAT_24H);}
inline const char* weekday_name(int day,const char* language){return day<0||day>6?"":i18n::locale(language).weekday_names[day];}
}
${energyData.match(/static constexpr uint8_t ENERGY_VALUES_MAX = \d+;/)[0]}
${energyData.match(/struct EnergyEntryData \{[\s\S]*?\n\};/)[0]}
std::vector<EnergyEntryData> cache;int period_requests=0;
bool energy_find_entry(const String& id,const char* period,EnergyEntryData& out){for(const auto& entry:cache)if(entry.id==id&&entry.period==period){out=entry;return true;}return false;}
bool energy_request_period(const char*,bool){++period_requests;return true;}
void set_label_style(lv_obj_t*obj,lv_color_t color,const lv_font_t*font){lv_obj_set_style_text_color(obj,color,0);lv_obj_set_style_text_font(obj,font,0);}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'), 'disable_pressed_button_animation')}
void hide_pin_popup(){}void hide_camera_popup(){}void hide_climate_popup(){}void hide_cover_popup(){}void hide_light_popup(){}void hide_sensor_popup(){}void hide_weather_popup(){}void hide_media_popup(){}
void viewNavigationPopupShown(lv_obj_t*,const char*){}
${strip(read('src/ui/popups/popup_layout.h'))}
${strip(read('src/ui/popups/popup_open.h'))}
${strip(read('src/ui/popups/popup_shell.h'))}
${strip(read('src/ui/popups/popup_open.cpp'))}
${strip(read('src/ui/popups/popup_shell.cpp'))}
${strip(read('src/ui/popups/energy/energy_popup.h'))}
${strip(read('src/ui/popups/energy/energy_popup.cpp'))}
lv_point_t touch_point{};bool touch_down=false;lv_indev_t* touch=nullptr;
void touch_at(int x,int y){touch_point={x,y};touch_down=true;lv_indev_read(touch);}
void touch_release(){touch_down=false;lv_indev_read(touch);}
EnergyEntryData day_entry(){EnergyEntryData e;e.id="sensor.energy";e.name="Energy";e.unit="kWh";e.period="day";e.start="2026-09-25T00:00:00";e.total=12.34f;e.value_count=24;for(int i=0;i<24;++i){e.values[i]=i*0.25f+(i==0?0.1f:0.0f);e.value_valid[i]=true;}e.value_valid[5]=false;return e;}
EnergyEntryData week_entry(){EnergyEntryData e=day_entry();e.period="week";e.start="2026-09-21";e.total=28;e.value_count=7;for(int i=0;i<7;++i)e.values[i]=i+1.0f;return e;}
bool bars_restored(EnergyPopupContext* ctx){for(auto* bar:ctx->bars)if(lv_obj_get_style_bg_opa(bar,LV_PART_MAIN)!=LV_OPA_COVER)return false;return true;}
bool shown(lv_obj_t* obj){return !lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);}
void settle(lv_display_t* display){lv_refr_now(display);process_popup_open();sync_popup_shell();lv_refr_now(display);}
int main(){
 lv_init();auto* display=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t> band(SCREEN_WIDTH*24);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,band.data(),nullptr,band.size()*4,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,[](lv_display_t* d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 lv_theme_default_init(display,lv_color_hex(0x26A69A),lv_color_hex(0xC14444),false,&ui_font_20);
 touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(touch,[](lv_indev_t*,lv_indev_data_t* data){data->point=touch_point;data->state=touch_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
 cache={day_entry(),week_entry()};
 preload_energy_popup();
 EnergyPopupInit init;init.entity_id="sensor.energy";init.title="Energy today";init.icon_name="flash";init.unit="kWh";init.decimals=2;init.bg_color=0x335577;
 show_energy_popup(init);auto* ctx=g_energy_popup_ctx;
 // The first frame already shows the current value in the shared header.
 assert(lv_obj_has_flag(ctx->value_label,LV_OBJ_FLAG_HIDDEN)&&strcmp(lv_label_get_text(ctx->value_label),"12.34 kWh")==0);
 sync_popup_shell();assert(shown(shell.value)&&strcmp(hometiles_title::text(shell.value),"12.34 kWh")==0);
 settle(display);
 assert(ctx->shown_slots==24&&ctx->plot_w>0&&!shown(ctx->readout_time_label)&&!shown(ctx->readout_value_label));
 lv_area_t wrap,chart,axis;lv_obj_get_coords(ctx->chart_wrap,&wrap);lv_obj_get_coords(ctx->chart,&chart);lv_obj_get_coords(ctx->x_axis,&axis);
 auto bar_x=[&](int slot,int slots){return wrap.x1+ctx->plot_left+static_cast<int>((slot+0.5f)*ctx->plot_w/slots);};
 const int y=(chart.y1+chart.y2)/2;const auto display_events=lv_display_get_event_count(display);
 auto* value_box=lv_obj_get_parent(ctx->readout_value_label);const int band_y=lv_obj_get_y(value_box);
 touch_at(bar_x(11,24),y);
 assert(ctx->readout.active()&&ctx->readout_slot<0&&"A press is applied with the next display refresh");
 lv_refr_now(display);
 assert(ctx->readout_slot==11&&strcmp(ctx->readout_time_text,"11:00 \xE2\x80\x93 12:00")==0&&strcmp(ctx->readout_value_text,"2.75 kWh")==0);
 for(int i=0;i<ENERGY_VALUES_MAX;++i)assert(lv_obj_get_style_bg_opa(ctx->bars[i],LV_PART_MAIN)==(i==11?LV_OPA_COVER:kReadoutDimmedBarOpa));
 assert(shown(ctx->readout_time_label)&&shown(ctx->readout_value_label)&&lv_obj_get_y(value_box)==band_y);
 assert(lv_obj_get_style_text_font(ctx->readout_time_label,LV_PART_MAIN)==popup_layout::font20()&&lv_obj_get_style_text_font(ctx->readout_value_label,LV_PART_MAIN)==value_font());
 lv_area_t time_area,value_area,band_area;lv_obj_get_coords(ctx->readout_time_label,&time_area);lv_obj_get_coords(ctx->readout_value_label,&value_area);lv_obj_get_coords(value_box,&band_area);
 assert(time_area.y2<=value_area.y1&&std::abs((time_area.y1+value_area.y2)-(band_area.y1+band_area.y2))<=2&&"Time above value, centered in the old value row");
 // Several moves within one frame apply only the latest position.
 touch_at(bar_x(23,24),y);touch_at(bar_x(20,24),y);assert(ctx->readout_slot==11);
 lv_refr_now(display);
 assert(ctx->readout_slot==20&&strcmp(ctx->readout_time_text,"20:00 \xE2\x80\x93 21:00")==0);
 assert(lv_obj_get_style_bg_opa(ctx->bars[11],LV_PART_MAIN)==kReadoutDimmedBarOpa&&lv_obj_get_style_bg_opa(ctx->bars[20],LV_PART_MAIN)==LV_OPA_COVER);
 touch_at(bar_x(5,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_value_text,"--")==0&&"A missing bar value is not zero");
 touch_at(bar_x(23,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"23:00 \xE2\x80\x93 00:00")==0);
 configManager.cfg.global_time_format=clock_tile::TIME_FORMAT_12H;
 touch_at(bar_x(0,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"12 AM \xE2\x80\x93 1 AM")==0&&strcmp(ctx->readout_value_text,"0.10 kWh")==0);
 touch_at(bar_x(12,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"12 PM \xE2\x80\x93 1 PM")==0);
 configManager.cfg.global_time_format=clock_tile::TIME_FORMAT_24H;configManager.cfg.language="de";
 touch_at(bar_x(1,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_value_text,"0,25 kWh")==0&&"Values use the header's localized format");
 configManager.cfg.language="en";
 // The press stays with the chart outside its bounds.
 touch_at(SCREEN_WIDTH-1,SCREEN_HEIGHT-1);lv_refr_now(display);assert(ctx->readout.active()&&ctx->readout_slot==23);
 // New chart data waits for the release.
 cache[0].values[23]=9.0f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 assert(g_pending_refresh.valid&&ctx->shown_entry.values[23]!=9.0f&&"Bars stay unchanged while touched");
 touch_release();
 assert(!ctx->readout.active()&&ctx->readout_slot<0&&bars_restored(ctx)&&!shown(ctx->readout_time_label)&&!shown(ctx->readout_value_label)&&"Release restores the normal bars at once");
 assert(lv_display_get_event_count(display)==display_events&&"The refresh hook exists only while a finger is down");
 process_energy_popup_queue();assert(!g_pending_refresh.valid&&ctx->shown_entry.values[23]==9.0f);
 // Axis labels belong to the chart surface.
 touch_at(bar_x(6,24),(axis.y1+axis.y2)/2);lv_refr_now(display);assert(ctx->readout_slot==6);touch_release();
 // A tap is released before any frame shows a readout.
 touch_at(bar_x(3,24),y);touch_release();lv_refr_now(display);assert(ctx->readout_slot<0&&!shown(ctx->readout_value_label)&&bars_restored(ctx));
 // Week bars read their weekday; the range buttons still work normally.
 lv_obj_send_event(ctx->week_btn,LV_EVENT_CLICKED,nullptr);settle(display);assert(ctx->period=="week"&&ctx->shown_slots==7);
 touch_at(bar_x(2,7),y);lv_refr_now(display);
 assert(strcmp(ctx->readout_time_text,"Wednesday")==0&&strcmp(ctx->readout_value_text,"3.00 kWh")==0);
 sync_popup_shell();assert(strcmp(hometiles_title::text(shell.value),"12.34 kWh")==0&&"The header keeps today's value");
 // Hiding while touching ends the readout and restores the bars.
 hide_energy_popup();assert(!ctx->readout.active()&&bars_restored(ctx)&&lv_display_get_event_count(display)==display_events);
 touch_release();
 for(int cycle=0;cycle<10;++cycle){show_energy_popup(init);settle(display);touch_at(bar_x(cycle,24),y);lv_refr_now(display);assert(ctx->readout_slot==cycle);touch_release();hide_energy_popup();}
 assert(g_energy_popup_ctx==ctx&&lv_display_get_event_count(display)==display_events);
 lv_deinit();
 std::cout<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<": Energy readout, header value, lifecycle and deferred data passed\n";
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
for (const [name, width, height, define] of [['square', 480, 480, 'DEVICE_LAYOUT_480X480'],
  ['wide', 1024, 600, 'DEVICE_LAYOUT_1024X600'], ['ws8', 1280, 800, '']]) {
  const binary = path.join(out, name + (process.platform === 'win32' ? '.exe' : ''));
  let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', out, `-DSCREEN_WIDTH=${width}`,
    `-DSCREEN_HEIGHT=${height}`, ...(define ? ['-D' + define] : []), source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  result = spawnSync(binary, [], {encoding: 'utf8', timeout: 60000});
  assert.equal(result.status, 0, name + ': ' + result.stdout + result.stderr);
  process.stdout.write(result.stdout);
}
console.log('Popup graph readout: shared surface, Sensor contract and real Energy popup passed.');
