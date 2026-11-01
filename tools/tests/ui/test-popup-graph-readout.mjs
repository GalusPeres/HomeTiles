// Finger readout in the Sensor, Number, Select, Binary Sensor and Energy
// popups. The old value row shows the time and value under the cursor.
// Sensor, Binary Sensor and Energy open on the newest point, keep the cursor
// where the finger leaves it and follow the newest point while they show it;
// a range change starts at the newest point again. Editors (Number, Select)
// show it only while touching. The readout must never scroll, swipe or click
// anything else, and dragging changes only the cursor and two static labels,
// once per display refresh.
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
import {valueUnitSource} from '../../lib/value-unit-source.mjs';
import {radiusPolicyHost, toneColorHost, popupSurfaceHost} from '../../lib/surface-style-host.mjs';

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
// Release shows the last position and lets the owner keep it; cancel always
// ends a kept readout too.
const release = helper.slice(helper.indexOf('void release()'), helper.indexOf('static void input('));
assert.match(release, /if \(apply_last\) apply_\(owner_, target, point_\);\s*if \(end_\) end_\(owner_, true\);/);
assert.match(input, /if \(self->active_ && self->target_ == target\) self->release\(\);/);
const cancel = helper.slice(helper.indexOf('void cancel()'), helper.indexOf('void release()'));
assert.match(cancel, /if \(end_\) end_\(owner_, false\);/);
assert.doesNotMatch(cancel, /was_active/, 'cancel() also ends a kept readout');

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
for (const marker of ['line.bg_color = lv_color_hex(ui_theme::text());', 'dot.bg_color = lv_color_hex(ui_theme::text());',
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
// Lifecycle: every close, hide, open, range change and teardown path ends the
// readout, so the next data starts at the newest point.
for (const name of ['on_close_click', 'hide_sensor_popup', 'show_sensor_popup', 'on_range_click'])
  assert.ok(code(fn(sensor, name)).includes('readout.cancel();'), `${name} ends the readout`);
// Sensors keep it after release; editors keep their controls.
const sensorEnd = code(fn(sensor, 'on_sensor_readout_end'));
assert.match(sensorEnd, /if \(keep && graph && readout_target\(ctx\) == graph\) \{\s*ctx->readout_pin_x = ctx->readout_x;\s*ctx->readout_latest = sensor_readout_is_latest\(ctx\);\s*return;/);
assert.match(sensorEnd, /if \(!keep\) \{\s*ctx->readout_latest = true;\s*ctx->readout_touched = false;/);
// Issue #139: the popup opens with the newest point as a dot in the line's
// color and its value above; the cursor line comes with the first touch.
const untouched = cursor.slice(0, cursor.indexOf('lv_draw_rect_dsc_t line;'));
assert.match(untouched, /if \(!ctx->readout_touched\) \{[\s\S]*if \(ctx->readout_kind != kReadoutChart\) return;/,
  'Before a touch no cursor line, on the timeline nothing');
assert.ok(untouched.includes('dot.bg_color = ctx->icon_label ? lv_obj_get_style_text_color(ctx->icon_label, LV_PART_MAIN)'),
  "The untouched dot takes the line's color");
assert.match(code(fn(sensor, 'on_sensor_readout_apply')),
  /if \(!ctx->readout_touched\) \{\s*ctx->readout_touched = true;\s*invalidate_readout_cursor\(ctx\);\s*\}\s*apply_sensor_readout\(ctx, target, point\);/,
  'A finger turns the line on, also on the point already shown');
assert.ok(code(fn(sensor, 'refresh_sensor_readout')).includes('apply_sensor_readout(ctx, graph, point);') &&
  !code(fn(sensor, 'refresh_sensor_readout')).includes('on_sensor_readout_apply'),
  'Opening and new data do not count as a touch');
assert.match(code(fn(sensor, 'sensor_readout_graph')), /if \(!ctx \|\| ctx->editable\) return nullptr;/);
const sensorRefresh = code(fn(sensor, 'refresh_sensor_readout'));
assert.match(sensorRefresh, /ctx->readout\.active\(\)/, 'A finger on the graph reads new data itself');
assert.match(sensorRefresh, /ctx->readout_latest \? area\.x2 : area\.x1 \+ ctx->readout_pin_x/);
// New data, a live state and a warm opening show the kept readout again.
const historyApply = code(fn(sensor, 'apply_history_payload'));
assert.match(historyApply, /apply_binary_history_payload\(ctx, doc\);\s*refresh_sensor_readout\(ctx\);/);
assert.match(historyApply, /apply_state_history_payload\(ctx, doc\);\s*refresh_sensor_readout\(ctx\);\s*return;/);
assert.match(historyApply, /lv_chart_refresh\(ctx->chart\);\s*refresh_sensor_readout\(ctx\);/);
assert.match(code(fn(sensor, 'prepend_state_history_activity')), /ctx->readout\.request_apply\(\);\s*refresh_sensor_readout\(ctx\);/);
assert.match(code(fn(sensor, 'finish_sensor_popup_open')), /readout_refresh_pending = reuse_history;/);
assert.match(code(fn(sensor, 'process_sensor_popup_queue')),
  /if \(g_sensor_first_frame\.pending\(\)\) return;\s*if \(g_sensor_popup_ctx->readout_refresh_pending\) \{\s*g_sensor_popup_ctx->readout_refresh_pending = false;\s*refresh_sensor_readout\(g_sensor_popup_ctx\);/);
// Cleared data takes its kept readout along.
assert.match(code(fn(sensor, 'clear_chart')), /if \(ctx->readout_kind == kReadoutChart\) clear_sensor_readout\(ctx\);/);
assert.match(code(fn(sensor, 'clear_binary_history')), /if \(ctx->readout_kind == kReadoutTimeline\) clear_sensor_readout\(ctx\);/);
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
const energyUi = code(fn(energy, 'build_popup_ui')) + code(fn(energy, 'build_chart_view'));
for (const marker of ['ctx->readout.attach(chart_wrap);', 'lv_obj_remove_flag(x_axis, LV_OBJ_FLAG_CLICKABLE);',
  'popup_graph_readout::create_band_label(\n      value_box, popup_layout::font20(), ctx->readout_time_text);',
  'popup_graph_readout::create_band_label(\n      value_box, value_font(), ctx->readout_value_text);'])
  assert.ok(energyUi.includes(marker), `Energy readout: ${marker}`);
for (const name of ['apply_energy_readout', 'show_energy_slot', 'on_energy_cursor_draw', 'invalidate_energy_cursor'])
  assert.doesNotMatch(code(fn(energy, name)), /lv_label_set_text\(|lv_obj_set_(?:pos|size|x|y)\(|lv_chart_|String\(|lv_obj_update_layout\(/,
    `Dragging over Energy bars changes only bar colors, the cursor line and static labels (${name})`);
assert.ok(energyUi.includes('lv_obj_add_event_cb(chart_wrap, on_energy_cursor_draw, LV_EVENT_DRAW_MAIN, ctx);'));
assert.match(code(fn(energy, 'on_energy_readout_end')),
  /if \(keep && ctx->readout_slot >= 0\) \{\s*ctx->readout_pin_slot = ctx->readout_slot;\s*ctx->readout_latest = ctx->readout_slot == latest_energy_slot\(ctx\);/);
assert.match(code(fn(energy, 'apply_entry_to_chart')), /ctx->plot_w = plot_w;\s*ctx->zero_y = zero_y;\s*if \(shown\) refresh_energy_readout\(ctx\);\s*\}$/);
// The marker's dot sits above the plot, outside chart_wrap's own box.
assert.match(energyUi, /lv_obj_add_event_cb\(chart_wrap, on_energy_cursor_ext_draw, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr\);\s*lv_obj_refresh_ext_draw_size\(chart_wrap\);/);
assert.match(code(fn(energy, 'show_energy_slot')),
  /ctx->readout_line_bottom =\s*bar && !lv_obj_has_flag\(bar, LV_OBJ_FLAG_HIDDEN\) \? lv_obj_get_style_y\(bar, LV_PART_MAIN\) : ctx->zero_y;/);
// The time axis holds a full line of its labels on every layout.
assert.match(code(fn(energy, 'time_axis_height')), /lv_font_get_line_height\(popup_layout::font20\(\)\)/);
assert.doesNotMatch(code(energy).replace(/constexpr int kTimeAxisHeight[^\n]*/g, '').replace(/return line > kTimeAxisHeight \? line : kTimeAxisHeight;/, ''),
  /kTimeAxisHeight/, 'Energy geometry uses time_axis_height()');
for (const name of ['on_close_click', 'on_overlay_delete', 'on_period_click', 'hide_energy_popup',
  'show_energy_popup', 'apply_entry_to_chart'])
  assert.ok(code(fn(energy, name)).includes('readout.cancel();'), `Energy ${name} ends the readout`);
assert.match(code(fn(energy, 'process_energy_popup_queue')),
  /if \(!g_pending_refresh\.any\(\)\) return;\s*if \(g_energy_popup_ctx->readout\.active\(\)\) return;/,
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
#include <lvgl_private.h>
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
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
#include "src/ui/popups/popup_graph_readout.h"
extern "C" {LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(ui_font_48);LV_FONT_DECLARE(ui_font_56);LV_FONT_DECLARE(ui_font_64);LV_FONT_DECLARE(ui_font_72);LV_FONT_DECLARE(ui_font_80);LV_FONT_DECLARE(ui_font_96);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_40);LV_FONT_DECLARE(mdi_icons_48);}
${valueUnitSource(root)}
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
${toneColorHost(root)}
namespace ui_surface_style {
template<class T> void apply_radius(lv_obj_t* obj,T radius,lv_style_selector_t selector){lv_obj_set_style_radius(obj,static_cast<int32_t>(radius),selector);}
inline void apply_global_tile_border(lv_obj_t*){}inline void apply_popup_border(lv_obj_t*,lv_color_t,lv_opa_t){}inline uint8_t icon_glow_percent(){return 25;}inline bool icon_discs_shown(){return true;}
inline lv_color_t border_hint(lv_color_t c){return lv_color_mix(lv_color_white(),c,128);}
}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
// Period switch timing lines; their content is not part of this test.
uint32_t millis(){static uint32_t now=0;return now+=1;}
struct TestSerial{template<class... T> void printf(const char*,T...){}} Serial;
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
const char* weather_today_button_label(const char*){return "Today";}
struct Strings{const char* loading="Loading";};const Strings& strings(const char*){static Strings s;return s;}
String format_number(const char* language,float value,uint8_t decimals,bool=false){if(!std::isfinite(value))return "--";char text[48];snprintf(text,sizeof(text),"%.*f",decimals>6?6:decimals,value);String result=text;if(locale(language).decimal_separator[0]==',')std::replace(result.begin(),result.end(),'.',',');return result;}
String weather_weekday_short(const char*,const String& iso){return iso.substr(5);}
enum class EnergyPeriodLabel:uint8_t{Year,Month,Week,Count};
const char* energy_period_label(const char*,EnergyPeriodLabel label){static const char* text[]={"Year","Month","Week"};return text[static_cast<int>(label)];}
const char* weather_month_short(const char*,int month){static const char* names[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};return month>=1&&month<=12?names[month-1]:"";}
const char* month_name(const char*,int month){static const char* names[]={"January","February","March","April","May","June","July","August","September","October","November","December"};return month>=1&&month<=12?names[month-1]:"";}
String format_short_date(const char*,int day,const char* month){return String(month)+" "+std::to_string(day);}
}
// The panel's first weekday (tm_wday), Settings > Localization.
namespace week_start {int first=1;inline uint8_t panel(){return static_cast<uint8_t>(first);}}
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
bool energy_request_waiting(){return false;}bool energy_period_fresh(const char*){return true;}
void set_label_style(lv_obj_t*obj,lv_color_t color,const lv_font_t*font){lv_obj_set_style_text_color(obj,color,0);lv_obj_set_style_text_font(obj,font,0);}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'), 'disable_pressed_button_animation')}
void hide_pin_popup(){}void hide_camera_popup(){}void hide_climate_popup(){}void hide_cover_popup(){}void hide_light_popup(){}void hide_sensor_popup(){}void hide_weather_popup(){}void hide_media_popup(){}void hide_device_popup(){}void hide_calendar_popup(){}void hide_vacuum_popup(){}
void viewNavigationPopupShown(lv_obj_t*,const char*){}
${radiusPolicyHost(root)}
namespace Device { constexpr int kGridGap = SCREEN_WIDTH == 480 ? 10 : 16; }  // the profile's grid gap (popup header rule)
${strip(read('src/ui/popups/popup_layout.h'))}
${strip(read('src/ui/popups/popup_open.h'))}
${strip(read('src/ui/popups/popup_shell.h'))}
${popupSurfaceHost(root, {surface: false})}
${strip(read('src/ui/popups/popup_open.cpp'))}
${strip(read('src/ui/shared/ui_pulse.h'))}
${strip(read('src/tiles/icons/mdi_bar_icons.h'))}
${strip(read('src/ui/shared/icon_lock_mark.h'))}
${strip(read('src/ui/popups/popup_shell.cpp'))}
${strip(read('src/ui/popups/energy/energy_popup.h'))}
${strip(read('src/ui/popups/energy/energy_popup.cpp'))}
lv_point_t touch_point{};bool touch_down=false;lv_indev_t* touch=nullptr;
void touch_at(int x,int y){touch_point={x,y};touch_down=true;lv_indev_read(touch);}
void touch_release(){touch_down=false;lv_indev_read(touch);}
EnergyEntryData day_entry(){EnergyEntryData e;e.id="sensor.energy";e.name="Energy";e.unit="kWh";e.period="day";e.start="2026-09-25T00:00:00";e.total=12.34f;e.value_count=24;for(int i=0;i<24;++i){e.values[i]=i*0.25f+(i==0?0.1f:0.0f);e.value_valid[i]=true;}e.value_valid[5]=false;return e;}
EnergyEntryData week_entry(){EnergyEntryData e=day_entry();e.period="week";e.start="2026-09-21";e.total=28;e.value_count=7;for(int i=0;i<7;++i)e.values[i]=i+1.0f;return e;}
// The bars' colors: the circle tone with the value's edge, the read bar all
// icon color.
uint32_t rgb(lv_color_t color){return lv_color_to_u32(color)&0xFFFFFF;}
bool read_bar(EnergyPopupContext* ctx,int i){return rgb(lv_obj_get_style_bg_color(ctx->bars[i],LV_PART_MAIN))==ctx->bar_edge&&lv_obj_get_style_border_width(ctx->bars[i],LV_PART_MAIN)==0;}
bool normal_bar(EnergyPopupContext* ctx,int i){return rgb(lv_obj_get_style_bg_color(ctx->bars[i],LV_PART_MAIN))==ctx->bar_body&&lv_obj_get_style_border_width(ctx->bars[i],LV_PART_MAIN)==kBarEdgeWidth&&
 rgb(lv_obj_get_style_border_color(ctx->bars[i],LV_PART_MAIN))==ctx->bar_edge;}
bool bars_restored(EnergyPopupContext* ctx){for(int i=0;i<ENERGY_VALUES_MAX;++i)if(ctx->bars[i]&&!normal_bar(ctx,i))return false;return true;}
EnergyEntryData month_entry(){EnergyEntryData e=day_entry();e.period="month";e.start="2026-09-01";e.total=90;e.value_count=29;for(int i=0;i<29;++i){e.values[i]=1.0f+i%5;e.value_valid[i]=true;}return e;}
EnergyEntryData year_entry(){EnergyEntryData e=day_entry();e.period="year";e.start="2026-01-01";e.total=1200;e.value_count=10;for(int i=0;i<10;++i){e.values[i]=100.0f+i*10;e.value_valid[i]=true;}return e;}
std::vector<std::string> axis_texts(EnergyPopupContext* ctx){std::vector<std::string> out;for(auto* label:ctx->x_labels)if(label&&!lv_obj_has_flag(label,LV_OBJ_FLAG_HIDDEN))out.push_back(lv_label_get_text(label));return out;}
bool shown(lv_obj_t* obj){return !lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN);}
void settle(lv_display_t* display){lv_refr_now(display);process_popup_open();sync_popup_shell();lv_refr_now(display);}
// Rendered frame, to check the cursor line.
std::vector<uint32_t> pixels;
bool white_at(int x,int y){return x>=0&&y>=0&&x<SCREEN_WIDTH&&y<SCREEN_HEIGHT&&(pixels[y*SCREEN_WIDTH+x]&0xffffff)==0xffffff;}
int main(){
 lv_init();auto* display=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);pixels.assign(SCREEN_WIDTH*SCREEN_HEIGHT,0);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*4,LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t* d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
 lv_theme_default_init(display,lv_color_hex(0x26A69A),lv_color_hex(0xC14444),false,&ui_font_20);
 touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);
 lv_indev_set_read_cb(touch,[](lv_indev_t*,lv_indev_data_t* data){data->point=touch_point;data->state=touch_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
 cache={day_entry(),week_entry(),month_entry(),year_entry()};
 preload_energy_popup();
 EnergyPopupInit init;init.entity_id="sensor.energy";init.title="Energy today";init.icon_name="flash";init.unit="kWh";init.decimals=2;init.bg_color=0x335577;init.icon_color=0xFF9800;
 show_energy_popup(init);auto* ctx=g_energy_popup_ctx;
 // The first frame already shows the current value in the shared header.
 assert(lv_obj_has_flag(ctx->value_label,LV_OBJ_FLAG_HIDDEN)&&strcmp(lv_label_get_text(ctx->value_label),"12.34")==0&&strcmp(value_unit::unit_text(ctx->value_label),"kWh")==0);
 sync_popup_shell();assert(shown(shell.value)&&strcmp(hometiles_title::text(shell.value),"12.34")==0&&strcmp(value_unit::unit_text(shell.value),"kWh")==0);
 // settle() with a queue pass between the content and the next frame: the
 // hidden 7D chart waits until the content frame is drawn.
 lv_refr_now(display);process_popup_open();sync_popup_shell();
 process_energy_popup_queue();assert(hidden_view(ctx,EnergyPeriod::Week)->shown_slots==0&&g_pending_refresh.period[1]&&"The hidden charts wait for the content frame");
 lv_refr_now(display);
 // Opening reads the newest bar.
 assert(ctx->shown_slots==24&&ctx->plot_w>0&&ctx->readout_slot==23&&ctx->readout_latest&&shown(ctx->readout_time_label)&&shown(ctx->readout_value_label));
 assert(strcmp(ctx->readout_time_text,"23:00 \xE2\x80\x93 00:00")==0&&strcmp(ctx->readout_value_text,"5.75")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0);
 // The cursor dot keeps room below the readout value: on 480x480 it touched
 // the value's descenders (a comma, user 2026-10-29 on the S3), the 600 and
 // 720 px layouts kept 12-15 px. The plot's bottom stays where it was.
 {lv_area_t value,chart;lv_obj_get_coords(ctx->readout_value_label,&value);lv_obj_get_coords(ctx->chart,&chart);
  const int dot_top=chart.y1-kReadoutDotLift-kReadoutDotSize/2;
  assert(dot_top-value.y2>=popup_layout::scale(16)&&"Room between the readout value and the dot");}
 // The axis labels: the number alone, its unit a step smaller after it like
 // the Sensor popup's (user 2026-10-29), the units in one column at the plot.
 {assert(strcmp(lv_label_get_text(ctx->y_max_label),"5.75")==0&&strcmp(value_unit::unit_text(ctx->y_max_label),"kWh")==0);
  assert(strcmp(lv_label_get_text(ctx->y_min_label),"0")==0&&strcmp(value_unit::unit_text(ctx->y_min_label),"kWh")==0);
  lv_obj_t* max_unit=lv_obj_get_child(ctx->y_max_label,0);lv_obj_t* min_unit=lv_obj_get_child(ctx->y_min_label,0);
  // The unit label (value_unit.h picks its font a step smaller where the
  // layout has one).
  assert(max_unit&&min_unit&&"the unit beside the number");
  lv_area_t a,b;lv_obj_get_coords(max_unit,&a);lv_obj_get_coords(min_unit,&b);assert(a.x2==b.x2&&"units in one column");}
 // Year, Month, Week, Today from left to right; the day button names today.
 {lv_area_t area[kPeriodCount];for(uint8_t p=0;p<kPeriodCount;++p)lv_obj_get_coords(ctx->period_btns[p],&area[p]);
  assert(area[3].x2<area[2].x1&&area[2].x2<area[1].x1&&area[1].x2<area[0].x1&&"Year, Month, Week, Today");
  assert(strcmp(lv_label_get_text(ctx->period_labels[0]),"Today")==0&&strcmp(lv_label_get_text(ctx->period_labels[3]),"Year")==0&&
         strcmp(lv_label_get_text(ctx->period_labels[2]),"Month")==0&&strcmp(lv_label_get_text(ctx->period_labels[1]),"Week")==0);
  lv_area_t row;lv_obj_get_coords(ctx->range_row,&row);lv_area_t card;lv_obj_get_coords(ctx->card,&card);
  assert(row.x1>=card.x1&&row.x2<=card.x2&&"The four buttons fit the card");}
 // The bars: circle tone with the icon-colored edge, the read bar icon color.
 assert(ctx->bar_edge==0xFF9800&&ctx->bar_body!=ctx->bar_edge&&read_bar(ctx,23)&&normal_bar(ctx,22)&&
        lv_obj_get_style_border_side(ctx->bars[22],LV_PART_MAIN)==LV_BORDER_SIDE_TOP);
 // A response that repeats the shown data keeps the chart as it is.
 lv_obj_add_flag(ctx->bars[0],LV_OBJ_FLAG_HIDDEN);queue_energy_popup_refresh("day");process_energy_popup_queue();
 assert(!g_pending_refresh.period[0]&&!shown(ctx->bars[0])&&"Identical data is not redrawn");
 lv_obj_remove_flag(ctx->bars[0],LV_OBJ_FLAG_HIDDEN);
 lv_area_t wrap,chart,axis,nav;lv_obj_get_coords(ctx->chart_wrap,&wrap);lv_obj_get_coords(ctx->chart,&chart);lv_obj_get_coords(ctx->x_axis,&axis);lv_obj_get_coords(ctx->range_row,&nav);
 // Hour labels keep their full line inside the axis and above the buttons.
 int axis_labels=0;
 for(auto* label:ctx->x_labels){if(!label||!shown(label))continue;++axis_labels;lv_area_t area;lv_obj_get_coords(label,&area);
  assert(area.y1>=axis.y1&&area.y2<=axis.y2&&area.y2<=wrap.y2&&lv_area_get_height(&area)>=lv_font_get_line_height(popup_layout::font20())&&"Time axis labels are never clipped");}
 assert(axis_labels==5&&wrap.y2<nav.y1&&"The axis stays above the period buttons");
 auto bar_x=[&](int slot,int slots){return wrap.x1+ctx->plot_left+static_cast<int>((slot+0.5f)*ctx->plot_w/slots);};
 const int y=(chart.y1+chart.y2)/2;const auto display_events=lv_display_get_event_count(display);
 auto* value_box=lv_obj_get_parent(ctx->readout_value_label);const int band_y=lv_obj_get_y(value_box);
 touch_at(bar_x(11,24),y);
 assert(ctx->readout.active()&&ctx->readout_slot==23&&"A press is applied with the next display refresh; the kept bar stays until then");
 lv_refr_now(display);
 assert(ctx->readout_slot==11&&strcmp(ctx->readout_time_text,"11:00 \xE2\x80\x93 12:00")==0&&strcmp(ctx->readout_value_text,"2.75")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0);
 for(int i=0;i<ENERGY_VALUES_MAX;++i)if(ctx->bars[i])assert(i==11?read_bar(ctx,i):normal_bar(ctx,i));
 assert(shown(ctx->readout_time_label)&&shown(ctx->readout_value_label)&&lv_obj_get_y(value_box)==band_y);
 assert(lv_obj_get_style_text_font(ctx->readout_time_label,LV_PART_MAIN)==popup_layout::font20()&&lv_obj_get_style_text_font(ctx->readout_value_label,LV_PART_MAIN)==value_font());
 lv_area_t time_area,value_area,band_area;lv_obj_get_coords(ctx->readout_time_label,&time_area);lv_obj_get_coords(ctx->readout_value_label,&value_area);lv_obj_get_coords(value_box,&band_area);
 assert(time_area.y2<=value_area.y1&&std::abs((time_area.y1+value_area.y2)-(band_area.y1+band_area.y2))<=2&&"Time above value, centered in the old value row");
 // A white dot above the plot marks the read bar, with a thin line from the
 // bar's top edge up to it, centered on the bar.
 {lv_area_t bar;lv_obj_get_coords(ctx->bars[11],&bar);const int cursor_x=wrap.x1+ctx->readout_x;
  assert(std::abs(cursor_x-(bar.x1+bar.x2)/2)<=1&&"The marker is centered on the bar");
  assert(ctx->readout_line_bottom==lv_obj_get_y(ctx->bars[11])&&"The line ends at the bar's top edge");
  assert(white_at(cursor_x,chart.y1+2)&&white_at(cursor_x,bar.y1-2)&&"The line runs from the bar up through the plot top");
  const int dot_y=chart.y1-kReadoutDotLift;
  assert(dot_y<chart.y1&&white_at(cursor_x,dot_y)&&white_at(cursor_x-kReadoutDotSize/2+2,dot_y)&&"The dot sits above the plot");
  assert(white_at(cursor_x,dot_y-kReadoutDotSize/2+1)&&"The dot is not clipped at the top of the chart area");
  assert(!white_at(cursor_x+kReadoutDotSize/2+2,dot_y)&&!white_at(cursor_x+kReadoutLineWidth+2,chart.y1+2)&&"Dot and line stay small");}
 // Several moves within one frame apply only the latest position.
 touch_at(bar_x(23,24),y);touch_at(bar_x(20,24),y);assert(ctx->readout_slot==11);
 lv_refr_now(display);
 assert(ctx->readout_slot==20&&strcmp(ctx->readout_time_text,"20:00 \xE2\x80\x93 21:00")==0);
 assert(normal_bar(ctx,11)&&read_bar(ctx,20));
 assert(!white_at(bar_x(11,24),chart.y1+2)&&white_at(wrap.x1+ctx->readout_x,chart.y1+2)&&"The line moves with the finger");
 touch_at(bar_x(5,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_value_text,"--")==0&&!value_unit::unit_text(ctx->readout_value_label)[0]&&"A missing bar value is not zero");
 touch_at(bar_x(23,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"23:00 \xE2\x80\x93 00:00")==0);
 configManager.cfg.global_time_format=clock_tile::TIME_FORMAT_12H;
 touch_at(bar_x(0,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"12 AM \xE2\x80\x93 1 AM")==0&&strcmp(ctx->readout_value_text,"0.10")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0);
 touch_at(bar_x(12,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"12 PM \xE2\x80\x93 1 PM")==0);
 configManager.cfg.global_time_format=clock_tile::TIME_FORMAT_24H;configManager.cfg.language="de";
 touch_at(bar_x(1,24),y);lv_refr_now(display);assert(strcmp(ctx->readout_value_text,"0,25")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0&&"Values use the header's localized format");
 configManager.cfg.language="en";
 // The press stays with the chart outside its bounds.
 touch_at(SCREEN_WIDTH-1,SCREEN_HEIGHT-1);lv_refr_now(display);assert(ctx->readout.active()&&ctx->readout_slot==23);
 // New chart data waits for the release.
 cache[0].values[23]=9.0f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 assert(g_pending_refresh.period[0]&&ctx->shown_entry.values[23]!=9.0f&&"Bars stay unchanged while touched");
 touch_release();
 assert(!ctx->readout.active()&&ctx->readout_slot==23&&ctx->readout_latest&&shown(ctx->readout_time_label)&&shown(ctx->readout_value_label)&&"Release keeps the read bar");
 assert(lv_display_get_event_count(display)==display_events&&"The refresh hook exists only while a finger is down");
 process_energy_popup_queue();assert(!g_pending_refresh.period[0]&&ctx->shown_entry.values[23]==9.0f);
 assert(ctx->readout_slot==23&&strcmp(ctx->readout_value_text,"9.00")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0&&"The newest bar follows new data");
 lv_refr_now(display);assert(white_at(wrap.x1+ctx->readout_x,chart.y1-kReadoutDotLift)&&"New bars keep the marker (the read bar now reaches the top)");
 // Axis labels belong to the chart surface. A bar the finger left stays.
 touch_at(bar_x(6,24),(axis.y1+axis.y2)/2);lv_refr_now(display);assert(ctx->readout_slot==6);touch_release();
 assert(ctx->readout_slot==6&&!ctx->readout_latest);
 cache[0].values[6]=4.0f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 assert(ctx->shown_entry.values[6]==4.0f&&ctx->readout_slot==6&&strcmp(ctx->readout_value_text,"4.00")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0&&"New data keeps the bar the finger left");
 // A tap reads its bar at once and keeps it.
 touch_at(bar_x(3,24),y);touch_release();assert(ctx->readout_slot==3&&shown(ctx->readout_value_label)&&strcmp(ctx->readout_time_text,"03:00 \xE2\x80\x93 04:00")==0);
 lv_refr_now(display);
 // A period change starts at its newest bar; the header keeps today's value.
 // The hidden charts are filled from the cache, one per pass; a switch only
 // swaps charts.
 for(int pass=0;pass<4;++pass)process_energy_popup_queue();
 assert(!g_pending_refresh.any()&&hidden_view(ctx,EnergyPeriod::Week)->shown_slots==7&&hidden_view(ctx,EnergyPeriod::Month)->shown_slots==30&&
        hidden_view(ctx,EnergyPeriod::Year)->shown_slots==12&&"Week, Month (September: 30 days) and Year are prepared");
 lv_obj_send_event(ctx->period_btns[1],LV_EVENT_CLICKED,nullptr);settle(display);
 assert(ctx->shown_period==EnergyPeriod::Week&&ctx->period==EnergyPeriod::Week&&ctx->shown_slots==7);
 assert(shown(ctx->chart_wrap)&&!shown(hidden_view(ctx,EnergyPeriod::Day)->chart_wrap)&&hidden_view(ctx,EnergyPeriod::Day)->shown_slots==24&&"Today's chart waits hidden");
 assert(ctx->readout_slot==6&&ctx->readout_latest&&strcmp(ctx->readout_time_text,"Sunday")==0);
 touch_at(bar_x(2,7),y);lv_refr_now(display);
 assert(strcmp(ctx->readout_time_text,"Wednesday")==0&&strcmp(ctx->readout_value_text,"3.00")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0);
 sync_popup_shell();assert(strcmp(hometiles_title::text(shell.value),"12.34")==0&&strcmp(value_unit::unit_text(shell.value),"kWh")==0&&"The header keeps today's value");
 touch_release();
 // This month by the day: the whole month on the axis, a line and the day's
 // number at each first weekday (Monday: September 7, 14, 21, 28), the date
 // in the readout.
 lv_obj_send_event(ctx->period_btns[2],LV_EVENT_CLICKED,nullptr);settle(display);
 assert(ctx->shown_period==EnergyPeriod::Month&&ctx->shown_slots==30&&ctx->readout_slot==28&&strcmp(ctx->readout_time_text,"Sep 29")==0);
 assert((axis_texts(ctx)==std::vector<std::string>{"7","14","21","28"})&&"Month: the first weekdays");
 {int lines=0;for(auto* line:ctx->x_lines)if(line&&shown(line))++lines;assert(lines==4);}
 {int bars=0;for(auto* bar:ctx->bars)if(bar&&shown(bar))++bars;assert(bars==29&&"Bars up to today, the rest of the month empty");}
 touch_at(bar_x(0,30),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"Sep 1")==0&&read_bar(ctx,0));touch_release();
 // This year by the month: twelve months on the axis, bars up to October.
 lv_obj_send_event(ctx->period_btns[3],LV_EVENT_CLICKED,nullptr);settle(display);
 assert(ctx->shown_period==EnergyPeriod::Year&&ctx->shown_slots==12&&ctx->readout_slot==9&&strcmp(ctx->readout_time_text,"October")==0);
 {const auto texts=axis_texts(ctx);assert((texts==std::vector<std::string>{"Jan","Mar","May","Jul","Sep","Nov"})&&"Year: every second month on every panel");
  int lines=0;for(auto* line:ctx->x_lines)if(line&&shown(line))++lines;assert(lines==11&&"a line between every two months");}
 touch_at(bar_x(2,12),y);lv_refr_now(display);assert(strcmp(ctx->readout_time_text,"March")==0&&strcmp(ctx->readout_value_text,"120.00")==0);touch_release();
 lv_obj_send_event(ctx->period_btns[0],LV_EVENT_CLICKED,nullptr);settle(display);
 assert(ctx->shown_period==EnergyPeriod::Day&&ctx->readout_slot==23&&ctx->readout_latest&&strcmp(ctx->readout_value_text,"9.00")==0&&strcmp(value_unit::unit_text(ctx->readout_value_label),"kWh")==0);
 // A negative bar hangs below the zero line; the line starts at the zero line.
 cache[0].values[3]=-2.0f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 touch_at(bar_x(3,24),y);touch_release();lv_refr_now(display);
 assert(ctx->readout_slot==3&&shown(ctx->y_zero_line)&&ctx->readout_line_bottom==lv_obj_get_y(ctx->y_zero_line)&&
        ctx->readout_line_bottom==lv_obj_get_y(ctx->bars[3])&&"Negative bars: the line starts at the zero line");
 assert(lv_obj_get_style_border_side(ctx->bars[3],LV_PART_MAIN)==LV_BORDER_SIDE_BOTTOM&&"A negative bar's edge is at its bottom");
 assert(white_at(wrap.x1+ctx->readout_x,wrap.y1+ctx->readout_line_bottom-3)&&white_at(wrap.x1+ctx->readout_x,chart.y1-kReadoutDotLift)&&"Line and dot above a negative bar");
 cache[0].values[3]=0.75f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 // A measured 0 shows its edge on the zero line, so it does not look like a
 // missing hour (user 2026-10-29); a missing hour shows nothing.
 cache[0].values[4]=0.0f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 assert(shown(ctx->bars[4])&&lv_obj_get_style_height(ctx->bars[4],LV_PART_MAIN)==kBarEdgeWidth&&
        lv_obj_get_style_y(ctx->bars[4],LV_PART_MAIN)+kBarEdgeWidth==ctx->zero_y&&"a zero hour: its edge on the zero line");
 assert(!shown(ctx->bars[5])&&"a missing hour: nothing");
 touch_at(bar_x(4,24),y);touch_release();lv_refr_now(display);
 assert(ctx->readout_slot==4&&strcmp(ctx->readout_value_text,"0.00")==0&&ctx->readout_line_bottom==lv_obj_get_style_y(ctx->bars[4],LV_PART_MAIN)&&
        read_bar(ctx,4)&&"the line ends on the zero hour's edge");
 cache[0].values[4]=1.0f;queue_energy_popup_refresh("day");process_energy_popup_queue();
 // Hiding while touching ends the readout and restores the bars.
 touch_at(bar_x(4,24),y);lv_refr_now(display);
 hide_energy_popup();assert(!ctx->readout.active()&&ctx->readout_slot<0&&bars_restored(ctx)&&lv_display_get_event_count(display)==display_events);
 touch_release();
 for(int cycle=0;cycle<10;++cycle){show_energy_popup(init);settle(display);assert(ctx->readout_slot==23&&ctx->readout_latest&&"Every opening starts at the newest bar");touch_at(bar_x(cycle,24),y);lv_refr_now(display);assert(ctx->readout_slot==cycle);touch_release();assert(ctx->readout_slot==cycle);hide_energy_popup();}
 assert(g_energy_popup_ctx==ctx&&lv_display_get_event_count(display)==display_events);
 lv_deinit();
 std::cout<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<": Energy readout, header value, lifecycle and deferred data passed\n";
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
for (const [name, width, height, define] of [['square', 480, 480, 'DEVICE_LAYOUT_480X480'],
  ['wide', 1024, 600, 'DEVICE_LAYOUT_1024X600'], ['ws8', 1280, 800, ''], ['tab5', 1280, 720, '']]) {
  const binary = path.join(out, name + (process.platform === 'win32' ? '.exe' : ''));
  let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', out, `-DSCREEN_WIDTH=${width}`,
    `-DSCREEN_HEIGHT=${height}`, ...(define ? ['-D' + define] : []), source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  result = spawnSync(binary, [], {encoding: 'utf8', timeout: 60000});
  assert.equal(result.status, 0, name + ': ' + result.stdout + result.stderr);
  process.stdout.write(result.stdout);
}
console.log('Popup graph readout: shared surface, Sensor contract and real Energy popup passed.');
