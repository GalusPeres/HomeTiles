// The Web Admin weather preview must look like the panel (user 2026-10-02:
// "es wär gut wenn überall es genau in der Vorschau matcht"). The real weather
// renderer and its state update (LVGL on the host) and the real preview script
// (headless Chrome) draw the same tile from the same payload; every label must
// sit at the device position, scaled like the rest of the preview.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import {extractFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {findBrowser} from '../../lib/headless-dom.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const strip = s => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const fn = (s, n) => { const f = cppFunctionDefinitions(s).find(f => f.name === n); assert(f, n); return f.source; };
const host = await lvglHost(root);
const browser = findBrowser();
if (!host || !browser) {
  console.log('SKIP: the weather preview comparison needs LVGL, a host compiler and Chrome');
  process.exit(0);
}
const out = path.join(root, 'build/tests/weather-preview-device');
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
fs.writeFileSync(path.join(out, 'FS.h'), '#pragma once\nnamespace fs { class FS {}; }\n');

// Today and the next days, so that both sides see the same forecast dates.
const iso = offset => {
  const day = new Date();
  day.setDate(day.getDate() + offset);
  return day.getFullYear() + '-' + String(day.getMonth() + 1).padStart(2, '0') + '-' +
    String(day.getDate()).padStart(2, '0');
};
const forecast = [
  ['partlycloudy', 18.5, 12], ['rainy', 20.5, 9], ['sunny', 19.6, 6.9], ['cloudy', 19.2, 8.5],
  ['snowy', -1, -7.5], ['fog', 4, 1], ['windy', 10, 3], ['hail', 2, 0],
].map(([condition, temperature, templow], i) => ({
  datetime: iso(i) + 'T00:00:00+02:00', date_local: iso(i), condition, temperature, templow,
}));
const payload = JSON.stringify({
  state: 'cloudy', name: 'Munich', icon: 'mdi:weather-cloudy', temperature: 18.5,
  units: {temperature: '°C'}, forecast,
});
fs.writeFileSync(path.join(out, 'payload.json'), payload);

const runtime = read('src/tiles/runtime/tile_renderer.cpp');
const runtimeFns = [
  'fnv1a_hash', 'extract_json_string_field', 'extract_json_number_field', 'extract_json_number_or_string_field',
  'extract_json_object_field', 'extract_json_array_field', 'decode_basic_json_escapes',
  'weather_icon_from_condition', 'weather_condition_display_label', 'format_weather_temp',
  'format_weather_temp_value', 'weather_unit_gap', 'format_weather_temp_unit', 'position_tile_value_unit_centered',
  'parse_iso_date', 'weekday_from_iso', 'weather_today_tile_label', 'get_local_today_date', 'iso_date_add_days',
  'iso_date_day_offset', 'weather_condition_room', 'update_weather_tile_state',
].map(name => fn(runtime, name)).join('\n');

const cpp = String.raw`
#include <lvgl.h>
#include <lvgl_private.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "src/devices/device_select.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
extern "C" {LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(ui_font_48);LV_FONT_DECLARE(ui_font_56);LV_FONT_DECLARE(ui_font_64);LV_FONT_DECLARE(ui_font_72);LV_FONT_DECLARE(ui_font_80);LV_FONT_DECLARE(ui_font_96);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_40);LV_FONT_DECLARE(mdi_icons_48);}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#elif defined(DEVICE_LAYOUT_1024X600)
#define FONT_MDI_ICONS (&mdi_icons_40)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
#if defined(_WIN32)
static struct tm* localtime_r(const time_t* t, struct tm* out) { return localtime_s(out, t) == 0 ? out : nullptr; }
#endif
class String:public std::string{public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){}String(double v,int d){char b[64];snprintf(b,sizeof b,"%.*f",d,v);assign(b);}
 void trim(){auto a=find_first_not_of(" \t\r\n");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \t\r\n")-a+1);}
 void toLowerCase(){for(auto&c:*this)c=std::tolower(static_cast<unsigned char>(c));}
 bool equalsIgnoreCase(const String&s)const{String a=*this,b=s;a.toLowerCase();b.toLowerCase();return a==b;}
 int indexOf(const String&v,size_t from=0)const{auto at=find(v,from);return at==npos?-1:static_cast<int>(at);}
 int indexOf(char v,size_t from=0)const{auto at=find(v,from);return at==npos?-1:static_cast<int>(at);}
 String substring(size_t a,size_t b)const{return a>=size()?String():String(substr(a,b-a));}
 String substring(size_t a)const{return a>=size()?String():String(substr(a));}
 char charAt(size_t at)const{return at<size()?(*this)[at]:0;}
 long toInt()const{return std::atol(c_str());}
 void replace(const char*a,const char*b){size_t at=0;const size_t la=strlen(a),lb=strlen(b);while((at=find(a,at))!=npos){std::string::replace(at,la,b);at+=lb;}}
 bool startsWith(const char*p)const{return rfind(p,0)==0;}
};
#include "src/devices/device.h"
#include "src/tiles/config/tile_geometry.h"
constexpr int SCREEN_WIDTH=Device::kScreenWidth,SCREEN_HEIGHT=Device::kScreenHeight;
String getMdiChar(const String&){return "\xF3\xB0\x96\xAD";}
${radiusPolicyHost(root, 'Device::kGridCellH', 'Device::kGridGap')}
#include "src/core/config/icon_glow.h"
struct TestConfig { int tile_radius = tile_radius::kMinimum; bool tile_borders = true; bool icon_discs = true; uint8_t icon_glow = icon_glow::kDefault; const char* language = "en"; };
struct TestConfigManager { TestConfig config; const TestConfig& getConfig() const { return config; } } configManager;
${surfaceStyleHost(root)}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void*p){free(p);}
${strip(read('src/core/json_scan.h'))}
${strip(read('src/tiles/runtime/tile_renderer_fonts.h'))}
${strip(read('src/ui/popups/popup_layout.h'))}
${strip(read('src/ui/popups/popup_open.h'))}
${strip(read('src/ui/popups/popup_shell.h'))}
${strip(read('src/ui/popups/popup_open.cpp'))}
${strip(read('src/ui/shared/ui_pulse.h'))}
${strip(read('src/tiles/icons/mdi_bar_icons.h'))}
${strip(read('src/ui/shared/icon_lock_mark.h'))}
${strip(read('src/ui/popups/popup_shell.cpp'))}
${strip(read('src/types/weather/widgets.h'))}
${strip(read('src/types/weather/tile_layout.h'))}
${strip(read('src/ui/popups/weather/weather_popup.h'))}
enum class GridType{TAB0,TAB1,TAB2,SCREENSAVER};constexpr int TILES_PER_GRID=1,GRID_CELL_W=Device::kGridCellW,GRID_CELL_H=Device::kGridCellH,GRID_GAP=Device::kGridGap;
#include "src/tiles/config/tile_icon_colors.h"
struct Tile{String title="Weather\nMunich",sensor_entity="weather.home",icon_name="weather-cloudy",sensor_unit,icon_colors;float col=0,row=0,span_w=1,span_h=1;uint8_t sensor_value_font=0,sensor_display_mode=0,sensor_decimals=0xFF,popup_open_mode=1;int type=12;};
${read('src/tiles/config/tile_config.h').match(/static constexpr uint8_t SENSOR_VALUE_FONT_MAX = \d+;/)[0]}
struct TileGridConfig{Tile tiles[TILES_PER_GRID];};struct TileConfigStub{TileGridConfig grid;const TileGridConfig&getActiveGrid()const{return grid;}}tileConfig;
namespace tile_icon_source { inline lv_obj_t* card_icon(lv_obj_t*) { return nullptr; } inline void refresh_card(lv_obj_t*, const Tile&) {} inline uint32_t popup_background(lv_obj_t*, uint32_t fallback) { return fallback; } }
constexpr int TILE_POPUP_OPEN_SHORT_PRESS=1;
int getTilePopupOpenMode(const Tile&t){return t.popup_open_mode;}
struct Logger{void println(const char*){}void printf(const char*,...){}}Serial;
struct Bridge{String findSensorName(const String&){return "Home";}String findEntityIcon(const String&){return "weather-cloudy";}}haBridgeConfig;
uint32_t tileDefaultBgColor(){return 0x1A1A1A;}uint32_t tileBgColorOrDefault(const Tile&,uint32_t c){return c;}
bool isMdiIconDisabled(const String&){return false;}String normalizeMdiIconName(const String&s){return s.rfind("mdi:",0)==0?String(s.substr(4)):s;}
bool getLocalTime(struct tm*out,uint32_t){time_t now=time(nullptr);return localtime_r(&now,out)!=nullptr;}
bool weatherColoredIcons(const Tile&t){return t.sensor_display_mode!=1;}
// The texts both sides show (the Web Admin takes them from the same i18n).
namespace i18n {
struct LocaleProfile{const char* weather_weekdays_short[7];};
inline const LocaleProfile& locale(const char*){static LocaleProfile p{{"Sun","Mon","Tue","Wed","Thu","Fri","Sat"}};return p;}
inline String format_number(const char*,float v,uint8_t d,bool trim){char b[32];snprintf(b,sizeof(b),"%.*f",d,v);String t=b;if(trim&&d>0){while(!t.empty()&&t.back()=='0')t.pop_back();if(!t.empty()&&t.back()=='.')t.pop_back();}return t;}
inline String weather_condition_label(const char*,const String&c){if(c=="cloudy")return "Cloudy";if(c=="partlycloudy")return "Partly cloudy";if(c=="sunny")return "Sunny";if(c=="rainy")return "Rainy";return c.empty()?String("--"):c;}
inline String weather_weekday_short(const char*,const String&iso){if(iso.size()<10)return "";struct tm t{};t.tm_year=std::atoi(iso.substr(0,4).c_str())-1900;t.tm_mon=std::atoi(iso.substr(5,2).c_str())-1;t.tm_mday=std::atoi(iso.substr(8,2).c_str());t.tm_hour=12;mktime(&t);return locale("en").weather_weekdays_short[t.tm_wday];}
inline const char* weather_today_label(const char*){return "Today";}
}
${strip(read('src/types/weather/weather_icon_table.h'))}
${strip(read('src/types/weather/weather_icons.h'))}
${strip(read('src/types/weather/weather_icons.cpp'))}
void set_label_style(lv_obj_t*o,lv_color_t c,const lv_font_t*f){lv_obj_set_style_text_color(o,c,0);lv_obj_set_style_text_font(o,f,0);}
void set_tile_grid_cell(lv_obj_t*o,int col,int row,int w,int h){lv_obj_set_size(o,w*GRID_CELL_W+(w-1)*GRID_GAP,h*GRID_CELL_H+(h-1)*GRID_GAP);lv_obj_set_pos(o,Device::kGridPad+col*(GRID_CELL_W+GRID_GAP),Device::kGridPad+row*(GRID_CELL_H+GRID_GAP));}
WeatherTileWidgets widgets[TILES_PER_GRID];WeatherTileWidgets* tile_renderer_get_weather_widgets(GridType){return widgets;}
WeatherTileWidgets* g_tab0_weather=widgets;WeatherTileWidgets* g_tab1_weather=widgets;WeatherTileWidgets* g_tab2_weather=widgets;
void viewNavigationSource(lv_obj_t*){}
${['brighten_rgb_color','disable_pressed_button_animation','finish_press_before_popup'].map(n=>fn(read('src/tiles/runtime/tile_renderer_shared.h'),n)).join('\n')}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'),'apply_fractional_tile_geometry')}
${fn(read('src/tiles/runtime/tile_renderer_shared.h'),'place_tile_card')}
${strip(read('src/tiles/runtime/tile_icon_disc.h'))}
${strip(read('src/tiles/runtime/compact_sensor_layout.h'))}
void show_weather_popup(const WeatherPopupInit&){}
${strip(read('src/types/weather/renderer.cpp'))}
${runtimeFns}
#include "src/types/climate/layout.h"
constexpr int GRID_COLS=Device::kGridCols,GRID_ROWS=Device::kGridRows;
constexpr int GRID_PAD=Device::kGridPad;
${read('src/tiles/config/tile_config.h').match(/static constexpr int GRID_EXTRA_X =[^]*?GRID_PAD_BOTTOM = [^;]+;/)[0]}
${fn(read('src/fonts/ui_fonts.h'),'ui_font_for_size')}
${strip(read('src/ui/screensaver/screensaver_tile_shadow.h'))}
${strip(read('src/web/server/render/web_admin_styles.cpp').split('void appendAdminStyles(')[0])}
${strip(read('src/types/weather/web_scripts.cpp'))}
static void box(std::ostream&o,const char*name,lv_obj_t*obj,lv_obj_t*card,bool&first){
 if(!obj||lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
 lv_area_t a,c;lv_obj_get_coords(obj,&a);lv_obj_get_coords(card,&c);
 std::string text=lv_obj_check_type(obj,&lv_label_class)?lv_label_get_text(obj):"";
 // Text widths: what LVGL lays out, not the label box of a fixed-width label.
 lv_point_t size{};if(!text.empty()&&lv_obj_check_type(obj,&lv_label_class))lv_text_get_size(&size,text.c_str(),lv_obj_get_style_text_font(obj,LV_PART_MAIN),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 // Text labels: the glyph baseline, base_line above the bottom of the line.
 const lv_font_t*f=lv_obj_check_type(obj,&lv_label_class)?lv_obj_get_style_text_font(obj,LV_PART_MAIN):nullptr;
 o<<(first?"":",")<<"\""<<name<<"\":{\"x\":"<<a.x1-c.x1<<",\"y\":"<<a.y1-c.y1<<",\"w\":"<<lv_area_get_width(&a)<<",\"h\":"<<lv_area_get_height(&a)<<",\"textW\":"<<size.x;
 if(f)o<<",\"baseline\":"<<(a.y1-c.y1)+f->line_height-f->base_line;
 o<<"}";first=false;
}
int main(int argc,char**argv){
 lv_init();auto*d=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);(void)d;
 std::ifstream file(argv[1]);std::stringstream payload;payload<<file.rdbuf();
 String css;appendPreviewScaleVars(css);String script;append_weather_scripts(script);
 std::cout<<"CSS "<<css.substr(css.find('{')+1,css.rfind('}')-css.find('{')-1)<<"\n";
 std::string js=script;js=js.substr(js.find("<script>")+8);js=js.substr(0,js.rfind("</script>"));
 for(char&c:js)if(c=='\n')c=' ';
 std::cout<<"SCRIPT "<<js<<"\n";
 for(float span_w:{1.f,2.f,2.5f,3.f,4.f})for(float span_h:{1.f,2.f}){
  if(span_w>GRID_COLS)continue;
  Tile&tile=tileConfig.grid.tiles[0];tile=Tile();tile.span_w=span_w;tile.span_h=span_h;
  auto*card=render_weather_tile(lv_screen_active(),0,0,tile,0,GridType::TAB0);
  update_weather_tile_state(GridType::TAB0,0,payload.str().c_str());
  lv_obj_update_layout(card);
  auto&w=widgets[0];std::ostringstream o;bool first=true;
  o<<"{\"spanW\":"<<span_w<<",\"spanH\":"<<span_h<<",\"cardW\":"<<lv_obj_get_width(card)<<",\"cardH\":"<<lv_obj_get_height(card)<<",\"boxes\":{";
  box(o,"icon",w.icon_label,card,first);box(o,"condition",w.condition_label,card,first);
  box(o,"separator",w.condition_sep_label,card,first);box(o,"temp",w.temp_label,card,first);
  for(int i=0;i<WEATHER_FORECAST_MAX;++i){auto&f=w.forecast[i];std::string p="f"+std::to_string(i)+"-";
   box(o,(p+"day").c_str(),f.day_label,card,first);box(o,(p+"icon").c_str(),f.icon_label,card,first);
   box(o,(p+"high").c_str(),f.temp_high_label,card,first);box(o,(p+"highUnit").c_str(),f.temp_high_unit_label,card,first);
   box(o,(p+"low").c_str(),f.temp_low_label,card,first);box(o,(p+"lowUnit").c_str(),f.temp_low_unit_label,card,first);}
  o<<"},\"texts\":{";first=true;
  auto text=[&](const char*name,lv_obj_t*obj){if(!obj||lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;o<<(first?"":",")<<"\""<<name<<"\":\""<<lv_label_get_text(obj)<<"\"";first=false;};
  text("condition",w.condition_label);text("temp",w.temp_label);
  for(int i=0;i<WEATHER_FORECAST_MAX;++i){auto&f=w.forecast[i];std::string p="f"+std::to_string(i)+"-";text((p+"day").c_str(),f.day_label);text((p+"high").c_str(),f.temp_high_label);text((p+"low").c_str(),f.temp_low_label);}
  o<<"}}";std::cout<<"TILE "<<o.str()<<"\n";
  lv_obj_delete(card);widgets[0]={};
 }
 lv_deinit();
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);

const preview = [
  read('src/web/admin/tiles/text-baseline.js'),
  read('src/types/weather/admin-icons.js'),
  read('src/types/weather/admin.js'),
  extractFunction('escapeHtml', read('src/web/admin/tiles/state.js')),
  extractFunction('normalizeMdiIconName', read('src/web/admin/tiles/state.js')),
  extractFunction('formatLocalizedNumber', read('src/web/admin/core/localization.js')),
].join('\n');
const fontUrl = pathToFileURL(path.join(root, 'docs/assets/fonts/inter-4.1-regular.woff2')).href;
const css = readRepoFile('src/web/assets/admin.css').replace(/\r\n/g, '\n')
  .replaceAll("url('/assets/inter-4.1-regular.woff2')", `url('${fontUrl}')`);

let checked = 0;
const failures = [];
// PREVIEW_DEVICE_PROFILES=all checks every device profile (a sweep before a
// release or after a layout change); the suite runs one per layout.
const previewProfiles = process.env.PREVIEW_DEVICE_PROFILES === 'all'
  ? JSON.parse(read('tools/device-profiles.json')).profiles.map(p => p.buildProfile)
  : ['guition_jc8012p4a1_v2', 'guition_esp32_4848s040', 'waveshare_7'];
for (const profile of previewProfiles) {
  const define = JSON.parse(read('tools/device-profiles.json')).profiles.find(p => p.buildProfile === profile).define;
  const binary = path.join(out, profile + (process.platform === 'win32' ? '.exe' : ''));
  let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', out, '-DHOMETILES_CI_TARGET', `-D${define}`,
    source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  run = spawnSync(binary, [path.join(out, 'payload.json')], {encoding: 'utf8'});
  assert.equal(run.status, 0, profile + ': ' + run.stdout + run.stderr);
  const lines = run.stdout.split(/\r?\n/);
  const vars = lines.find(line => line.startsWith('CSS ')).slice(4);
  const script = lines.find(line => line.startsWith('SCRIPT ')).slice(7);
  const tiles = lines.filter(line => line.startsWith('TILE ')).map(line => JSON.parse(line.slice(5)));

  const html = `<!doctype html><html><head><meta charset="utf-8"><style>${css}
:root{${vars}}
body{margin:0;background:#000} .tile.weather{position:absolute;left:20px;top:20px}
</style></head><body><div id="host"></div><pre id="result"></pre><script>
const APP_LOCALE = 'en';
${inlineScriptSafe(script)}
${inlineScriptSafe(preview)}
document.fonts.load('400 20px "HomeTiles Inter"').then(() => {
  const scale = parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--radius-preview-scale'));
  // The preview grid at exactly the device proportions, so the tile sizes the
  // grid gives match the scaled device cards.
  const root = document.documentElement.style;
  root.setProperty('--preview-cell-w', WEATHER_TILE_LAYOUT.cellW * scale + 'px');
  root.setProperty('--preview-cell-h', WEATHER_TILE_LAYOUT.cellH * scale + 'px');
  root.setProperty('--preview-gap', WEATHER_TILE_LAYOUT.gap * scale + 'px');
  const results = [];
  for (const device of ${JSON.stringify(tiles)}) {
    const el = document.createElement('div');
    el.className = 'tile weather';
    // A re-rendered tile at a half position is filled before the grid places
    // it (user 2026-10-02: the forecast squeezed into the top left): render
    // it in a wrong size, then give it its grid size.
    el.style.width = '120px';
    el.style.height = '60px';
    el.innerHTML = '<i class="mdi mdi-weather-cloudy tile-icon"></i><div class="tile-title">Weather<br>Munich</div>';
    document.getElementById('host').replaceChildren(el);
    const tile = {col: 0, span_w: device.spanW, span_h: device.spanH, sensor_display_mode: 0};
    applyWeatherPreview(el, parseWeatherPreviewPayload(${JSON.stringify(payload)}), tile, 'weather-cloudy', '');
    el.style.width = device.cardW * scale + 'px';
    el.style.height = device.cardH * scale + 'px';
    const card = el.getBoundingClientRect();
    const boxes = {}, texts = {};
    const rect = (name, node, textNode = node) => {
      if (!node) return;
      const r = node.getBoundingClientRect();
      const range = document.createRange(); range.selectNodeContents(textNode);
      const textW = range.getBoundingClientRect().width;
      // The browser baseline of a text, from an empty inline-block on it.
      let baseline;
      if (!/icon/.test(name) && textNode.nodeType === 1) {
        const mark = document.createElement('span');
        mark.style.cssText = 'display:inline-block;width:0;height:0;vertical-align:baseline';
        textNode.appendChild(mark);
        baseline = mark.getBoundingClientRect().top - card.top;
        mark.remove();
      }
      boxes[name] = {x: r.left - card.left, y: r.top - card.top, w: r.width, h: r.height, textW, baseline};
    };
    const row = el.querySelector('.weather-preview-row');
    rect('condition', row.querySelector('.weather-preview-condition'));
    rect('separator', row.querySelector('.weather-preview-separator'));
    rect('temp', row.querySelector('.weather-preview-temp'));
    for (const [name, node] of [['condition', '.weather-preview-condition'], ['temp', '.weather-preview-temp']]) {
      const found = row.querySelector(node); if (found) texts[name] = found.textContent;
    }
    rect('icon', el.querySelector('.tile-weather-icon svg'));
    const days = el.querySelectorAll('.weather-preview-day');
    const icons = el.querySelectorAll('.weather-preview-icon svg');
    const values = el.querySelectorAll('.weather-preview-temp-value');
    const units = el.querySelectorAll('.weather-preview-temp-unit');
    days.forEach((node, i) => { rect('f' + i + '-day', node); texts['f' + i + '-day'] = node.textContent; });
    icons.forEach((node, i) => rect('f' + i + '-icon', node));
    values.forEach((node, i) => {
      const key = 'f' + (i >> 1) + '-' + (i % 2 ? 'low' : 'high');
      rect(key, node); texts[key] = node.textContent;
      rect(key + 'Unit', units[i]);
    });
    results.push({boxes, texts});
  }
  document.getElementById('result').textContent = JSON.stringify({scale, results});
  document.body.dataset.done = '1';
});
</script></body></html>`;
  const page = path.join(out, profile + '.html');
  fs.writeFileSync(page, html);
  run = spawnSync(browser, ['--headless=new', '--disable-gpu', '--no-first-run', '--allow-file-access-from-files',
    '--virtual-time-budget=5000', '--dump-dom', pathToFileURL(page).href], {encoding: 'utf8', timeout: 60000});
  assert.equal(run.status, 0, run.stderr);
  const match = /<pre id="result">([^<]*)<\/pre>/.exec(run.stdout);
  assert(match && match[1], profile + ': the preview harness did not finish\n' + run.stdout.slice(-2000));
  const {scale, results} = JSON.parse(match[1].replaceAll('&quot;', '"').replaceAll('&amp;', '&'));
  const report = [];
  tiles.forEach((device, index) => {
    const browserTile = results[index];
    const label = `${profile} ${device.spanW}x${device.spanH}`;
    // Same texts in the same labels.
    for (const [name, text] of Object.entries(device.texts)) {
      const shown = browserTile.texts[name];
      if (name === 'temp' || name === 'condition') assert.equal(shown, text.replace(/\s+/g, ' '), `${label} ${name}`);
      else assert.equal(shown, text, `${label} ${name}`);
    }
    assert.deepEqual(Object.keys(browserTile.boxes).sort(),
      Object.keys(device.boxes).filter(name => name !== 'icon' || browserTile.boxes.icon).sort(), `${label} labels`);
    // Positions: the device label boxes scaled to the preview. Text widths
    // differ by renderer (LVGL vs. Chrome), so a centered or right-aligned
    // text is compared by its center; texts by their baseline (the preview
    // moves the box so the glyphs sit on the LVGL baseline), icons by their top.
    for (const [name, deviceBox] of Object.entries(device.boxes)) {
      const shown = browserTile.boxes[name];
      if (!shown) continue;
      const byBaseline = deviceBox.baseline !== undefined && shown.baseline !== undefined;
      const expectTop = (byBaseline ? deviceBox.baseline : deviceBox.y) * scale;
      const dy = (byBaseline ? shown.baseline : shown.y) - expectTop;
      const centered = /day|icon/.test(name);
      const expectX = centered ? (deviceBox.x + deviceBox.w / 2) * scale : deviceBox.x * scale;
      const shownX = centered ? shown.x + shown.w / 2 : shown.x;
      const dx = shownX - expectX;
      report.push(`${label} ${name}: dx=${dx.toFixed(1)} dy=${dy.toFixed(1)}`);
      // Within 1 preview px (about 2 display px) of the device position.
      if (Math.abs(dy) > 1) failures.push(`${label} ${name} ${byBaseline ? "baseline" : "top"} ${(expectTop + dy).toFixed(2)} vs device ${expectTop.toFixed(2)}`);
      if (Math.abs(dx) > 1) {
        failures.push(`${label} ${name} x ${shownX.toFixed(2)} vs device ${expectX.toFixed(2)}`);
      }
      ++checked;
    }
  });
  fs.writeFileSync(path.join(out, profile + '.log'), report.join('\n') + '\n');
}
assert.deepEqual(failures, [], 'labels away from their device positions');
console.log(`Weather preview matches the device: ${checked} labels within 1 preview px on V2, 480x480 and 1024x600`);
