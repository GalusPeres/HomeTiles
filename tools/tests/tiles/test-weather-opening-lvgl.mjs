import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const strip = s => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const fn = (s, n) => { const f = cppFunctionDefinitions(s).find(f => f.name === n); assert(f, n); return f.source; };
const host = await lvglHost(root);
if (!host) { console.log('SKIP: Weather opening needs LVGL and a host compiler'); process.exit(0); }
const out = path.join(root, 'build/tests/weather-opening-lvgl');
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, 'Arduino.h'), '#pragma once\n#include <cstdint>\n#include <cstddef>\n');
fs.writeFileSync(path.join(out, 'FS.h'), '#pragma once\nnamespace fs { class FS {}; }\n');
const cpp = String.raw`
#include <lvgl.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <new>
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
class String:public std::string{public:using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){}void trim(){auto a=find_first_not_of(" \r\n");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \r\n")-a+1);}void toLowerCase(){for(auto&c:*this)c=std::tolower(static_cast<unsigned char>(c));}bool equalsIgnoreCase(const String&s)const{String a=*this,b=s;a.toLowerCase();b.toLowerCase();return a==b;}};
#include "src/devices/device.h"
constexpr int SCREEN_WIDTH=Device::kScreenWidth,SCREEN_HEIGHT=Device::kScreenHeight;
String getMdiChar(const String&){return "\xF3\xB0\x96\xAD";}
struct TestConfig { bool tile_borders = true; };
struct TestConfigManager { TestConfig config; const TestConfig& getConfig() const { return config; } } configManager;
${strip(read('src/ui/shared/ui_surface_style.h'))}
${strip(read('src/ui/shared/ui_surface_style.cpp'))}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void*p){free(p);}
${strip(read('src/tiles/runtime/tile_renderer_fonts.h'))}
${strip(read('src/ui/popups/popup_layout.h'))}
${strip(read('src/ui/popups/popup_open.h'))}
${strip(read('src/ui/popups/popup_shell.h'))}
${strip(read('src/ui/popups/popup_open.cpp'))}
${strip(read('src/ui/popups/popup_shell.cpp'))}
${strip(read('src/types/weather/widgets.h'))}
${strip(read('src/ui/popups/weather/weather_popup.h'))}
enum class GridType{TAB0,SCREENSAVER};constexpr int TILES_PER_GRID=1,GRID_CELL_W=Device::kGridCellW,GRID_CELL_H=Device::kGridCellH,GRID_GAP=Device::kGridGap;
struct Tile{String title="Weather",sensor_entity="weather.home",icon_name="weather-sunny",sensor_unit;uint8_t span_w=1,span_h=1,sensor_value_font=0,sensor_display_mode=0,sensor_decimals=0xFF,popup_open_mode=1;int type=1,sensor_gauge_min=0,sensor_gauge_max=100,sensor_gauge_arc=270,sensor_gauge_size=160,sensor_gauge_y_offset=0,sensor_graph_height=60,sensor_value_y_offset=0;};
constexpr int TILE_POPUP_OPEN_SHORT_PRESS=1;
int getTilePopupOpenMode(const Tile&t){return t.popup_open_mode;}
struct Logger{void println(const char*){}}Serial;
struct Bridge{String findSensorName(const String&){return "Home";}String findEntityIcon(const String&){return "weather-sunny";}String findSensorUnit(const String&){return "";}String findSensorInitialValue(const String&){return "98";}String findSensorStateKind(const String&){return "number";}String findEditableValue(const String&){return "";}}haBridgeConfig;
uint32_t get_tile_type_default_bg(int){return 0x2A2A2A;}uint32_t tileBgColorOrDefault(const Tile&,uint32_t c){return c;}
bool isMdiIconDisabled(const String&){return false;}String normalizeMdiIconName(const String&s){return s;}
void set_label_style(lv_obj_t*o,lv_color_t c,const lv_font_t*f){lv_obj_set_style_text_color(o,c,0);lv_obj_set_style_text_font(o,f,0);}
void set_tile_grid_cell(lv_obj_t*o,int col,int row,int w,int h){lv_obj_set_size(o,w*GRID_CELL_W+(w-1)*GRID_GAP,h*GRID_CELL_H+(h-1)*GRID_GAP);lv_obj_set_pos(o,Device::kGridPad+col*(GRID_CELL_W+GRID_GAP),Device::kGridPad+row*(GRID_CELL_H+GRID_GAP));}
WeatherTileWidgets widgets[TILES_PER_GRID];WeatherTileWidgets* tile_renderer_get_weather_widgets(GridType){return widgets;}
void viewNavigationSource(lv_obj_t*){}
${['brighten_rgb_color','disable_pressed_button_animation','finish_press_before_popup'].map(n=>fn(read('src/tiles/runtime/tile_renderer_shared.h'),n)).join('\n')}
PopupShellParts popup;int opens=0,completed_opens=0,sensor_opens=0;
// Execute the production opening function with a small resident body. Forecast
// model parsing and transport are outside this input/first-frame regression.
struct WeatherPopupContext{lv_obj_t*card=nullptr,*overlay=nullptr,*location_label=nullptr,*icon_label=nullptr,*close_button=nullptr;bool has_rendered_data=false;String rendered_entity_id,entity_id,title;uint32_t bg_color=0;};
WeatherPopupContext* g_weather_popup_ctx=nullptr;PopupBody g_weather_body;
WeatherPopupInit g_pending_weather_init;bool g_weather_open_pending=false;
void hide_pin_popup(){}void hide_camera_popup(){}void hide_climate_popup(){}void hide_cover_popup(){}void hide_light_popup(){}void hide_sensor_popup(){}void hide_energy_popup(){}void hide_media_popup(){}
void viewNavigationPopupShown(lv_obj_t*,const char*){++opens;}
void finish_weather_popup_open(){++completed_opens;g_weather_open_pending=false;g_weather_body.restore();g_weather_popup_ctx->has_rendered_data=true;g_weather_popup_ctx->rendered_entity_id=g_weather_popup_ctx->entity_id;}
void build_popup_ui(WeatherPopupContext*ctx,const WeatherPopupInit&){popup=create_popup_body([](lv_event_t*){hide_popup_shell(popup.card);},nullptr,0x223344);ctx->card=popup.card;ctx->overlay=popup.overlay;ctx->location_label=popup.title;ctx->icon_label=popup.icon;ctx->close_button=popup.close;}
${fn(read('src/ui/popups/weather/weather_popup.cpp'),'show_weather_popup')}
${strip(read('src/types/weather/renderer.cpp')).replace('render_weather_tile(', 'render_weather_tile_content(')}
// Match grid finalization: apply the production surface styles after rendering.
lv_obj_t* render_weather_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
 auto* card = render_weather_tile_content(parent,col,row,tile,index,grid_type);
 ui_surface_style::apply_global_tile_border(card);
 return card;
}
${strip(read('src/types/sensor/widgets.h'))}
${strip(read('src/ui/popups/sensor/sensor_popup.h'))}
SensorTileWidgets sensor_widgets[TILES_PER_GRID];
SensorTileWidgets* tile_renderer_get_sensor_widgets(GridType){return sensor_widgets;}
constexpr int GAUGE_ARC_STEPS=1000;
bool tileTypeIsEditableValue(int){return false;}
struct EditableValue{String state,unit,kind;};EditableValue parse_editable_value(const String&){return {};}
bool sensor_popup_should_use_state_history(const String&,const String&){return false;}
void show_sensor_popup(const SensorPopupInit&){++sensor_opens;}void request_tile_graph_history(const char*){}
${strip(read('src/types/sensor/renderer.cpp')).replace('render_sensor_tile(', 'render_sensor_tile_content(')}
lv_obj_t* render_sensor_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
 auto* card = render_sensor_tile_content(parent,col,row,tile,index,grid_type);
 ui_surface_style::apply_global_tile_border(card);
 return card;
}
#include "src/types/climate/layout.h"
constexpr int GRID_COLS=Device::kGridCols,GRID_ROWS=Device::kGridRows;
${strip(read('src/web/server/render/web_admin_styles.cpp').split('void appendAdminStyles(')[0])}
bool pressed=false;lv_point_t pointer{};bool measuring=false;int covered_draws=0;uint64_t flushed_pixels=0;
int tile_draws=0, tile_style_changes=0;
void advance_time(int duration) {
 for (int elapsed=0; elapsed<duration; elapsed+=10) {
  lv_tick_inc(10);
  lv_timer_handler();
  process_popup_open();
 }
}
void measure_press(lv_display_t* display, lv_indev_t* input, lv_obj_t* card, int span, int duration) {
 advance_time(250);
 lv_area_t area; lv_obj_get_coords(card, &area);
 pointer={area.x1+40,area.y1+80};
 const int before=opens;
 tile_draws=0; flushed_pixels=0; measuring=true;
 tile_style_changes=0;
 pressed=true; lv_indev_read(input);
 assert(tile_style_changes==0&&"The fully styled Weather tile must not rebuild descendant layout on press");
 lv_refr_now(display);
 const auto immediate=lv_color_to_u32(lv_obj_get_style_bg_color(card,LV_PART_MAIN));
 advance_time(duration);
 std::cout<<"Press "<<span<<"x"<<span<<": initial="<<immediate
          <<", final="<<lv_color_to_u32(lv_obj_get_style_bg_color(card,LV_PART_MAIN))
          <<", draws="<<tile_draws<<", pixels="<<flushed_pixels<<std::endl;
 assert(opens==before&&"Short-press mode must not open while held");
 tile_style_changes=0;
 pressed=false; lv_indev_read(input);
 assert(tile_style_changes==0&&"The fully styled Weather tile must not rebuild descendant layout before opening");
 lv_refr_now(display);
 tile_draws=0; flushed_pixels=0;
 advance_time(250);
 std::cout<<"Release "<<span<<"x"<<span<<": late draws="<<tile_draws
          <<", pixels="<<flushed_pixels<<std::endl;
 measuring=false; assert(opens==before+(duration<400?1:0));
 hide_popup_shell(popup.card); lv_refr_now(display);
 // Input cancellation during navigation must not open a popup on release.
 pressed=true; lv_indev_read(input); advance_time(600);
 lv_indev_reset(input,nullptr); pressed=false; lv_indev_read(input);
 assert(opens==before+(duration<400?1:0)&&!lv_obj_has_state(card,LV_STATE_PRESSED));
}
void check_sensor_interaction(lv_display_t* display, lv_indev_t* input) {
 for (int span:{1,2}) {
  Tile tile; tile.span_w=tile.span_h=span;
  auto* sensor=render_sensor_tile(lv_screen_active(),0,0,tile,0,GridType::SCREENSAVER);
  auto* weather=render_weather_tile(lv_screen_active(),0,0,tile,0,GridType::TAB0);
  lv_obj_update_layout(weather); advance_time(250);
  // Compare actual standard button styles throughout the press/release transition.
  for (bool down:{true,false}) {
   if(down){lv_obj_add_state(sensor,LV_STATE_PRESSED);lv_obj_add_state(weather,LV_STATE_PRESSED);}
   else{lv_obj_remove_state(sensor,LV_STATE_PRESSED);lv_obj_remove_state(weather,LV_STATE_PRESSED);}
   for (int step=0;step<20;++step) {
    assert(lv_color_eq(lv_obj_get_style_bg_color(sensor,LV_PART_MAIN),lv_obj_get_style_bg_color(weather,LV_PART_MAIN))&&"Weather must use Sensor press colors and timing");
    assert(lv_obj_get_style_recolor_opa(sensor,LV_PART_MAIN)==lv_obj_get_style_recolor_opa(weather,LV_PART_MAIN)&&"Weather must retain the standard button color filter");
    advance_time(10);
   }
  }
  // Labels and forecast containers must not intercept input before the card.
  auto check_children=[](auto&& self,lv_obj_t* parent)->void {
   for(uint32_t i=0;i<lv_obj_get_child_count(parent);++i){auto* child=lv_obj_get_child(parent,i);assert(!lv_obj_has_flag(child,LV_OBJ_FLAG_CLICKABLE));self(self,child);}
  };
  check_children(check_children,weather);
  lv_area_t area;lv_obj_get_coords(weather,&area);pointer={area.x1+40,area.y1+40};
  const int before=opens;pressed=true;lv_indev_read(input);advance_time(20);
  assert(lv_anim_count_running()>0&&"The test must release during the real theme transition");
  pressed=false;lv_indev_read(input);
  assert(opens==before+1&&shell.active&&"Popup must become visible in the release event, before any refresh or animation completion");
  const int completed_before=completed_opens;
  process_popup_open();assert(completed_opens==completed_before&&"Body work must wait for the first popup frame");
  assert(lv_anim_count_running()>0&&"Opening must not wait for the release transition");
  lv_refr_now(display);process_popup_open();assert(completed_opens==completed_before+1);
  advance_time(250);hide_popup_shell(popup.card);
  lv_obj_delete(weather);lv_obj_delete(sensor);lv_refr_now(display);
 }
 std::cout<<"Sensor interaction: identical colors; popup visible during animation, all children pass input to card"<<std::endl;
 // Both configured modes use the same input events as the actual Sensor renderer.
 for(int mode:{0,1})for(int duration:{20,120,600}) {
  Tile tile;tile.popup_open_mode=mode;
  bool sensor_opened=false;
  for(bool weather:{false,true}) {
   auto* card=weather?render_weather_tile(lv_screen_active(),0,0,tile,0,GridType::TAB0):render_sensor_tile(lv_screen_active(),0,0,tile,0,GridType::TAB0);
   lv_obj_update_layout(card);advance_time(250);lv_area_t area;lv_obj_get_coords(card,&area);pointer={area.x1+40,area.y1+40};
   const int before=weather?opens:sensor_opens;pressed=true;lv_indev_read(input);advance_time(duration);
   const bool held_open=(weather?opens:sensor_opens)>before;
   assert(held_open==(mode==0&&duration==600));
   pressed=false;lv_indev_read(input);
   const bool released_open=(weather?opens:sensor_opens)>before;
   if(!weather)sensor_opened=released_open;else assert(released_open==sensor_opened);
   advance_time(250);hide_popup_shell(popup.card);lv_obj_delete(card);lv_refr_now(display);
  }
 }
}
void watch(lv_obj_t* object) {
 lv_obj_add_event_cb(object, [](lv_event_t*) { ++tile_style_changes; }, LV_EVENT_STYLE_CHANGED, nullptr);
 lv_obj_add_event_cb(object, [](lv_event_t* event) {
  if (measuring) ++tile_draws;
  if (!measuring || !shell.active) return;
  const auto* layer = lv_event_get_layer(event);
  if (layer->parent) return;
  const auto& clip = layer->_clip_area;
  lv_area_t frame; lv_obj_get_coords(shell.frame, &frame);
  const int radius = lv_obj_get_style_radius(shell.frame, LV_PART_MAIN);
  frame.y1 += radius; frame.y2 -= radius;
  if (clip.x1 >= frame.x1 && clip.x2 <= frame.x2 &&
      clip.y1 >= frame.y1 && clip.y2 <= frame.y2) ++covered_draws;
 }, LV_EVENT_DRAW_MAIN, nullptr);
 for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
  watch(lv_obj_get_child(object, i));
}
void fill(lv_obj_t*o,const char*s){if(!o)return;lv_label_set_text(o,s);lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);}
void check_value_alignment(lv_display_t* display) {
 Tile sensor_tile; sensor_tile.title="Battery";
 auto* sensor=render_sensor_tile(lv_screen_active(),0,0,sensor_tile,0,GridType::SCREENSAVER);
 auto* value=sensor_widgets[0].value_label;
 fill(value,"98 %"); lv_obj_update_layout(sensor);
 lv_area_t sensor_value, sensor_area;
 lv_obj_get_coords(value,&sensor_value); lv_obj_get_coords(sensor,&sensor_area);
 const int expected_y=sensor_value.y1-sensor_area.y1;
 String css; appendPreviewScaleVars(css);
 lv_area_t icon,title;
 lv_obj_get_coords(lv_obj_get_child(sensor,0),&icon);
 lv_obj_get_coords(lv_obj_get_child(sensor,1),&title);
 for(const auto& item:std::vector<std::pair<const char*,int>>{
     {"title-top",title.y1-sensor_area.y1},{"title-right",sensor_area.x2-title.x2},
     {"icon-top",icon.y1-sensor_area.y1},{"icon-left",icon.x1-sensor_area.x1}}) {
  const auto property=std::string("--tile-header-")+item.first+":"+
      std::to_string(preview_scaled_exact_px(item.second))+"px;";
  assert(css.find(property)!=std::string::npos&&"Preview scale must come from the actual Sensor header");
 }
 for (int width=1;width<=Device::kGridCols;++width) {
  for (int height=1;height<=Device::kGridRows;++height) {
   Tile tile; tile.span_w=width; tile.span_h=height;
   auto* card=render_weather_tile(lv_screen_active(),0,0,tile,0,GridType::TAB0);
   auto& w=widgets[0];
   for (const char* temperature:{"--","27.8 C","-12.4 C"}) {
    fill(w.temp_label,temperature); fill(w.condition_label,"Partly cloudy");
    fill(w.condition_sep_label,"|"); lv_obj_update_layout(card);
    lv_area_t weather_value,weather_area;
    lv_obj_get_coords(w.temp_label,&weather_value); lv_obj_get_coords(card,&weather_area);
    const int actual_y=weather_value.y1-weather_area.y1;
    if(actual_y!=expected_y)std::cerr<<width<<"x"<<height<<": weather="<<actual_y<<", sensor="<<expected_y<<std::endl;
    assert(actual_y==expected_y&&"Weather must retain the 1x1 Sensor value height for every span");
    if(height>1){lv_area_t day;lv_obj_get_coords(w.forecast[0].day_label,&day);assert(day.y1>weather_value.y2&&"Forecast must start below the value");}
   }
   // Compare actual forecast coordinates across devices sharing the same grid.
   std::cout<<"Forecast layout: "<<width<<"x"<<height;
   if(height>1)for(int i=0;i<weather_forecast_count(width);++i){
    lv_area_t day,area;lv_obj_get_coords(w.forecast[i].day_label,&day);lv_obj_get_coords(card,&area);
    std::cout<<" "<<day.x1-area.x1<<","<<day.y1-area.y1<<","<<day.x2-area.x1<<","<<day.y2-area.y1;
   }
   std::cout<<std::endl;
   lv_obj_delete(card);
  }
 }
 std::cout<<"Value alignment: y="<<expected_y<<", all supported spans passed"<<std::endl;
 lv_obj_delete(sensor); lv_refr_now(display);
}
void remove_optimization(lv_obj_t*o){lv_obj_remove_event_cb(o,draw_popup_background);for(uint32_t i=0;i<lv_obj_get_child_count(o);++i)remove_optimization(lv_obj_get_child(o,i));}
int main(){lv_init();auto*d=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t>pixels(SCREEN_WIDTH*SCREEN_HEIGHT),band(SCREEN_WIDTH*28);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,band.data(),nullptr,band.size()*4,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_user_data(d,&pixels);lv_display_set_flush_cb(d,[](lv_display_t*d,const lv_area_t*a,uint8_t*data){if(measuring)flushed_pixels+=lv_area_get_size(a);auto&pixels=*static_cast<std::vector<uint32_t>*>(lv_display_get_user_data(d));auto*source=reinterpret_cast<uint32_t*>(data);for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)pixels[y*SCREEN_WIDTH+x]=*source++;lv_display_flush_ready(d);});
 lv_theme_default_init(d,lv_color_hex(0x26A69A),lv_color_hex(0xC14444),false,&ui_font_20);
 static_assert(LV_THEME_DEFAULT_TRANSITION_TIME==80);
 lv_timer_set_period(lv_display_get_refr_timer(d),17);
 lv_timer_set_period(lv_anim_get_timer(),17);
 check_value_alignment(d);
 auto*input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,[](lv_indev_t*,lv_indev_data_t*data){data->point=pointer;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
 WeatherPopupInit initial;initial.entity_id="weather.home";initial.title="Weather";show_weather_popup(initial);lv_refr_now(d);process_popup_open();hide_popup_shell(popup.card);lv_refr_now(d);
 check_sensor_interaction(d,input);
 int total_covered=0;
 const std::vector<int> columns=Device::kGridCols>=7?std::vector<int>{0,1,4}:std::vector<int>{0,1};
 for(int span:{1,2,3})for(int col:columns){Tile tile;tile.span_w=tile.span_h=span;auto*card=render_weather_tile(lv_screen_active(),col,col==0?0:1,tile,0,GridType::TAB0);auto&w=widgets[0];fill(w.temp_label,"23 C");fill(w.condition_label,"Sunny");fill(w.condition_sep_label,"|");for(int i=0;i<weather_forecast_count(span)&&span>1;++i){auto&f=w.forecast[i];fill(f.day_label,"Monday");fill(f.icon_label,getMdiChar("").c_str());fill(f.temp_high_label,"24 C");fill(f.temp_low_label,"16 C");}watch(card);lv_refr_now(d);
  if (col==0) for(int duration:{120,600}) measure_press(d,input,card,span,duration);
  const auto children=lv_obj_get_child_count(card);
  for(int cycle=0;cycle<5;++cycle){
   lv_area_t area;lv_obj_get_coords(card,&area);pointer={area.x1+40,area.y1+80};pressed=true;lv_indev_read(input);lv_tick_inc(30);lv_refr_now(d);const int before=opens;covered_draws=0;flushed_pixels=0;measuring=true;pressed=false;lv_indev_read(input);assert(opens==before+1);lv_refr_now(d);measuring=false;
   if(cycle==0)std::cout<<span<<"x"<<span<<" col="<<col<<": covered draws="<<covered_draws<<", flushed pixels="<<flushed_pixels<<std::endl;
   const auto incremental=pixels;remove_optimization(card);lv_obj_invalidate(lv_screen_active());lv_refr_now(d);assert(incremental==pixels&&"Optimized popup pixels must match an unfiltered full redraw, including shadow and exposed tile");
   total_covered+=covered_draws;register_popup_background(card);
   hide_popup_shell(popup.card);lv_refr_now(d);const auto closed=pixels;remove_optimization(card);lv_obj_invalidate(lv_screen_active());lv_refr_now(d);assert(closed==pixels&&"Closing must restore the cached Weather tile completely");register_popup_background(card);
   assert(lv_obj_get_child_count(card)==children&&"Opening must retain the existing Weather widgets");
  }
  lv_obj_delete(card);lv_refr_now(d);
 }
 assert(total_covered==0&&"Opening must not redraw Weather objects fully covered by the popup");lv_deinit();}
`;
const source=path.join(out,'test.cpp');fs.writeFileSync(source,cpp);
const forecastLayouts=new Map();
for (const {buildProfile:name,define} of JSON.parse(read('tools/device-profiles.json')).profiles) {
  const binary=path.join(out,name+(process.platform==='win32'?'.exe':''));
  let r=spawnSync(host.cxx,[...host.flags,'-std=c++17','-I',out,'-DHOMETILES_CI_TARGET',`-D${define}`,source,host.archive,'-o',binary],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
  r=spawnSync(binary,[],{encoding:'utf8'});fs.writeFileSync(path.join(out,name+'.log'),r.stdout+r.stderr);assert.equal(r.status,0,name+': '+r.stdout+r.stderr);
  const lines=r.stdout.trim().split(/\r?\n/);
  forecastLayouts.set(name,lines.filter(line=>line.startsWith('Forecast layout: ')));
  console.log(name+': '+lines.filter(line=>!line.startsWith('Forecast layout: ')).join('\n'));
}
assert.equal(forecastLayouts.get('waveshare_8').length,35);
assert.deepEqual(forecastLayouts.get('guition_jc8012p4a1'),forecastLayouts.get('waveshare_8'),
  'Guition V1 forecast must match Waveshare 8-inch coordinates for all 35 tile spans');
assert.deepEqual(forecastLayouts.get('guition_jc8012p4a1_v2'),forecastLayouts.get('waveshare_8'),
  'Guition V2 forecast must match Waveshare 8-inch coordinates for all 35 tile spans');
