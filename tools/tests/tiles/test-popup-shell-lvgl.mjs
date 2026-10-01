import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const host=await lvglHost(root);
if(!host){console.log('SKIP: Shared popup lifecycle needs LVGL and a host compiler');process.exit(0);}
const out=path.join(root,'build/tests/popup-shell-lvgl');fs.mkdirSync(out,{recursive:true});
const withoutIncludes=s=>s.replace(/^#include.*$/gm,'').replaceAll('#pragma once','');
const cpp=String.raw`
#include <lvgl.h>
#include <lvgl_private.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <new>
#include <algorithm>
#include <iostream>
#include <fstream>
#include "src/ui/shared/title_label.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
extern "C" {LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(ui_font_48);LV_FONT_DECLARE(ui_font_56);LV_FONT_DECLARE(ui_font_64);LV_FONT_DECLARE(ui_font_72);LV_FONT_DECLARE(ui_font_80);LV_FONT_DECLARE(ui_font_96);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_48);}
#if defined(DEVICE_LAYOUT_480X480)
#define FONT_MDI_ICONS (&mdi_icons_32)
#else
#define FONT_MDI_ICONS (&mdi_icons_48)
#endif
std::string getMdiChar(const char*){return "\xF3\xB0\x96\xAD";}
${radiusPolicyHost(root)}
#include "src/core/config/icon_glow.h"
struct Config {bool tile_borders=true;bool icon_discs=true;uint8_t icon_glow=icon_glow::kDefault;int tile_radius=tile_radius::kMinimum;};
struct Manager{Config cfg;const Config& getConfig(){return cfg;}}configManager;
${surfaceStyleHost(root)}
bool fail_alloc=false;int allocations=0;
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(fail_alloc)return nullptr;++allocations;return malloc(n);}
void heap_caps_free(void*p){if(p){--allocations;free(p);}}
${withoutIncludes(read('src/ui/popups/popup_layout.h'))}
${withoutIncludes(read('src/ui/popups/popup_open.h'))}
${withoutIncludes(read('src/ui/popups/popup_shell.h'))}
${withoutIncludes(read('src/ui/popups/popup_open.cpp'))}
${withoutIncludes(read('src/ui/popups/popup_shell.cpp'))}
struct Popup{lv_obj_t*owner,*body,*title,*icon,*close,*content;bool allow_close=true,back=false;int closed=0;};
Popup make(){
 const auto parts=create_popup_body([](lv_event_t*){},nullptr,0x223344);
 Popup p{};p.owner=parts.overlay;p.body=parts.card;p.title=parts.title;p.icon=parts.icon;p.close=parts.close;
 lv_label_set_text(p.icon,getMdiChar("").c_str());
 p.content=lv_button_create(p.body);lv_obj_set_size(p.content,180,100);lv_obj_center(p.content);
 lv_obj_set_style_bg_color(p.content,lv_color_hex(0xFF0000),0);lv_obj_add_flag(p.body,LV_OBJ_FLAG_HIDDEN);return p;
}
Popup* disposable=nullptr;int dismissed=0;
void dismiss_form(){++dismissed;hide_popup_shell(disposable->body);lv_obj_delete(disposable->owner);}
void wire(Popup&p){lv_obj_add_event_cb(p.close,[](lv_event_t*e){auto*p=static_cast<Popup*>(lv_event_get_user_data(e));if(p->back){p->back=false;lv_label_set_text(lv_obj_get_child(p->close,0),"X");return;}if(!p->allow_close)return;++p->closed;hide_popup_shell(p->body);lv_obj_add_flag(p->body,LV_OBJ_FLAG_HIDDEN);},LV_EVENT_CLICKED,&p);}
struct Init{Popup*p;std::string value;};int applied=0,flushed=0,first_flush_y=-2;
void apply(const Init&i){assert(flushed>0);++applied;assert(!lv_obj_has_flag(i.p->content,LV_OBJ_FLAG_HIDDEN));}
void show(Popup&p,const char*title,bool cached=false){hometiles_title::set(p.title,title);lv_obj_remove_flag(p.body,LV_OBJ_FLAG_HIDDEN);assert(defer_popup_body(p.body,p.title,p.icon,p.close,Init{&p,title},apply,cached));show_popup_shell(p.owner,p.body,p.title,p.icon,p.close);}
// Execute Weather's actual deferred opener with the shared shell. Transport
// and forecast parsing are injected; temporary body hiding and ownership are real.
struct String:std::string{using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}bool equalsIgnoreCase(const String&s)const{return *this==s;}void remove(size_t i){erase(i);}};
struct WeatherPopupInit{String entity_id;bool colored_icons=true,icon_forced=false;};
struct WeatherPopupContext{lv_obj_t*overlay=nullptr,*card=nullptr;bool has_rendered_data=false;String rendered_entity_id,rendered_language;uint32_t rendered_payload_hash=0;size_t rendered_payload_length=0;uint8_t rendered_icon_key=1;};
uint8_t icon_style_key(bool colored,bool forced){return (colored?1:0)|(forced?2:0);}
WeatherPopupContext*g_weather_popup_ctx=nullptr;bool g_weather_open_pending=false;WeatherPopupInit g_pending_weather_init;PopupBody g_weather_body;
struct WeatherPending{bool valid=false,parse_hourly_pending=false,build_ui_pending=false;String entity_id,build_entity_id;int pending_day_nav=-1;}g_pending_weather;
struct Logger{template<class...T>void printf(const char*,T...){}}Serial;
bool is_popup_visible(WeatherPopupContext*c){return !lv_obj_has_flag(c->card,LV_OBJ_FLAG_HIDDEN);}
void reset_weather_popup_content(WeatherPopupContext*){}
void apply_init_to_context(WeatherPopupContext*,const WeatherPopupInit&){}
void hide_weather_model_widgets(WeatherPopupContext*){}
void build_popup_ui(WeatherPopupContext*,const WeatherPopupInit&){assert(false);}
void reset_pending_weather_update(){g_pending_weather={};}
bool tiles_get_cached_entity_payload_signature(const char*,uint32_t&,size_t&){return false;}
bool weather_popup_rendered_payload_matches(const char*,uint32_t,size_t){return false;}
bool tiles_get_cached_entity_payload(const char*,String&){return false;}
void request_weather_for_context(WeatherPopupContext*){assert(flushed>0);}
void apply_weather_header(WeatherPopupContext*,const String&){}
void queue_weather_popup_payload(const char*,const char*){}
${cppFunctionDefinitions(read('src/ui/popups/weather/weather_popup.cpp')).find(f=>f.name==='finish_weather_popup_open').source}
// Execute the Settings shell, flex form and close/back callback from production.
// Only form data/network actions are injected; layout and event delivery are real.
enum class SettingsPopupKind { Wifi };
SettingsPopupKind settings_popup_kind=SettingsPopupKind::Wifi;
lv_obj_t *settings_popup_overlay=nullptr,*settings_popup_card=nullptr,*settings_popup_title=nullptr,*settings_popup_close_icon=nullptr,*settings_popup_content=nullptr,*wifi_entry_view=nullptr;
namespace Device {constexpr int kGridPad=4;}
constexpr int kPopupCardPad=popup_layout::scale(20);
int settings_built=0,settings_closed=0;
void reset_popup_refs(){}
const char* popup_icon_for_kind(SettingsPopupKind){return "wifi";}
const char* popup_title_for_kind(SettingsPopupKind){return "Wi-Fi";}
uint32_t settings_tile_color(){return 0x2A2A2A;}  // The global tile color in production.
void style_plain_container(lv_obj_t*o){lv_obj_remove_style_all(o);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);}
void build_popup_content(SettingsPopupKind,lv_obj_t*parent){++settings_built;wifi_entry_view=lv_obj_create(parent);lv_obj_set_size(wifi_entry_view,100,100);}
void close_settings_popup(){++settings_closed;hide_popup_shell(settings_popup_card);lv_obj_delete(settings_popup_overlay);settings_popup_overlay=nullptr;settings_popup_card=nullptr;settings_popup_content=nullptr;wifi_entry_view=nullptr;}
void hide_settings_popup(){if(settings_popup_overlay)close_settings_popup();}
void wifi_show_list_view(){lv_obj_add_flag(wifi_entry_view,LV_OBJ_FLAG_HIDDEN);lv_label_set_text(settings_popup_close_icon,"X");}
${['on_settings_popup_close_clicked','finish_settings_popup_open','open_settings_popup'].map(name=>cppFunctionDefinitions(read('src/ui/tabs/settings/tab_settings.cpp')).find(f=>f.name===name).source).join('\n')}
void save_frame(const std::string&path,const std::vector<uint32_t>&pixels){std::ofstream f(path,std::ios::binary);f<<"P6\n"<<SCREEN_WIDTH<<" "<<SCREEN_HEIGHT<<"\n255\n";for(auto p:pixels){const char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}}
int main(int argc,char**argv){lv_init();auto*d=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t>pixels(SCREEN_WIDTH*SCREEN_HEIGHT),band(SCREEN_WIDTH*16);lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(d,band.data(),nullptr,band.size()*4,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_user_data(d,&pixels);lv_display_set_flush_cb(d,[](lv_display_t*d,const lv_area_t*area,uint8_t*data){++flushed;if(first_flush_y==-1)first_flush_y=area->y1;auto&pixels=*static_cast<std::vector<uint32_t>*>(lv_display_get_user_data(d));auto*source=reinterpret_cast<uint32_t*>(data);for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x)pixels[y*SCREEN_WIDTH+x]=*source++;lv_display_flush_ready(d);});
 int outside_draws=0;auto*outside=lv_obj_create(lv_screen_active());lv_obj_set_size(outside,100,100);lv_obj_set_pos(outside,30,30);lv_obj_add_event_cb(outside,[](lv_event_t*e){++*static_cast<int*>(lv_event_get_user_data(e));},LV_EVENT_DRAW_MAIN,&outside_draws);
 auto a=make(),b=make();wire(a);wire(b);
 show(a,"Number\nLiving room");auto*frame=shell.frame;auto*header=shell.header;auto*button=shell.close;
 assert(lv_obj_get_parent(a.body)==shell.overlay);assert(PopupFirstFrame::any_pending());process_popup_open();assert(applied==0);assert(lv_obj_has_flag(a.content,LV_OBJ_FLAG_HIDDEN));
 lv_refr_now(d);if(argc>1)save_frame(std::string(argv[1])+"-shell.ppm",pixels);assert(!PopupFirstFrame::any_pending());assert(applied==0);process_popup_open();assert(applied==1);lv_refr_now(d);if(argc>1)save_frame(std::string(argv[1])+"-body.ppm",pixels);
 hide_popup_shell(a.body);lv_refr_now(d);outside_draws=0;show(a,"Number\nLiving room",true);assert(!lv_obj_has_flag(a.content,LV_OBJ_FLAG_HIDDEN)&&"Warm content must not disappear for a frame");lv_refr_now(d);process_popup_open();
 if(SCREEN_WIDTH>SCREEN_HEIGHT)assert(outside_draws==0&&"A warm popup must not repaint cached tiles outside its frame");
 lv_refr_now(d);
 const int rendered=flushed;for(int i=0;i<20;++i){sync_popup_shell();lv_refr_now(d);}assert(flushed==rendered);
 // The visible shared close highlight retains its established shape at the
 // minimum and follows live changes, including while already pressed.
 const int close_baseline = SCREEN_WIDTH == 480 ? 11 : 16;
 for(int radius : {tile_radius::kMinimum,tile_radius::kMaximum,tile_radius::kMinimum}) {
   ui_surface_style::preview_radius(radius);
   ui_surface_style::process_pending_updates();
   lv_obj_add_state(button,LV_STATE_PRESSED);
   lv_tick_inc(300);lv_refr_now(d);
   assert(lv_obj_get_style_bg_opa(button,LV_PART_MAIN)>0);
   assert(lv_obj_get_style_radius(button,LV_PART_MAIN)==close_baseline+radius-tile_radius::kMinimum);
 }
 lv_obj_remove_state(button,LV_STATE_PRESSED);
 ui_surface_style::request_global_radius_refresh();ui_surface_style::process_pending_updates();
 lv_obj_update_layout(shell.overlay);assert(lv_obj_get_width(shell.frame)==lv_obj_get_width(a.body));assert(strcmp(hometiles_title::text(shell.title),"Number\nLiving room")==0);
 // One rule for the pressed close button and the footer controls: the
 // circle's color (tone_color::fill) in the icon hue only with tile color
 // "From icon" and "Circle in icon color"; Global, Custom and popups without
 // a tile take the neutral step; no theme darkening on the press.
 auto close_press=[&](uint32_t&rgb,lv_opa_t&opa){lv_style_value_t v;
   assert(lv_obj_get_local_style_prop(button,LV_STYLE_BG_COLOR,&v,LV_STATE_PRESSED)==LV_STYLE_RES_FOUND);rgb=lv_color_to_u32(v.color)&0xFFFFFF;
   assert(lv_obj_get_local_style_prop(button,LV_STYLE_BG_OPA,&v,LV_STATE_PRESSED)==LV_STYLE_RES_FOUND);opa=static_cast<lv_opa_t>(v.num);};
 auto footer_fill=[&](uint32_t card,uint32_t icon,uint32_t&rgb,lv_opa_t&opa){lv_color_t c;popup_shell_control_fill(card,icon,c,opa);rgb=lv_color_to_u32(c)&0xFFFFFF;};
 // Controls take the circle color whenever it is tinted (user 2026-10-01);
 // only a popup showing the tile color From icon computes it for its own
 // card, every other one for the From icon card (tone_color::g_from_icon_card).
 // A tile with "From icon" at 40 % passes its strength (the Light popup).
 tone_color::g_from_icon_card=[](uint32_t,bool,uint8_t percent)->uint32_t{return percent==40?0x3A2410:0x2B1A10;};
 struct ControlCase{bool tile,glow,from_icon;uint32_t card,icon;bool tinted,family;const char*what;uint8_t strength=0;};
 for(const ControlCase&c:std::vector<ControlCase>{
     {false,true,false,0x482F10,0xEF8402,true,true,"no tile"},
     {true,true,false,0x1B1B1B,0xC62828,true,true,"Global with the circle color"},
     {true,true,false,0x3E1717,0xC62828,true,true,"Custom in the icon's hue"},
     {true,true,true,0x482F10,0xEF8402,true,false,"From icon with the circle color"},
     {true,false,true,0x482F10,0xEF8402,false,false,"From icon without the circle color"},
     {true,true,false,0x1B1B1B,0xEF8402,true,true,"Global card, From icon tile at 40 %",40}}){
   if(c.tile)popup_shell_use_tile_disc(false,false,c.glow,c.from_icon,c.strength);
   lv_obj_set_style_bg_color(b.body,lv_color_hex(c.card),0);lv_obj_set_style_text_color(b.icon,lv_color_hex(c.icon),0);show(b,"Sonos");sync_popup_shell();
   uint32_t close_rgb,footer_rgb;lv_opa_t close_opa,footer_opa;close_press(close_rgb,close_opa);footer_fill(c.card,c.icon,footer_rgb,footer_opa);
   const tone_color::Fill expected=tone_color::fill(c.family?(c.strength==40?0x3A2410:0x2B1A10):c.card,c.icon,c.tinted,configManager.cfg.icon_glow);
   if(!lv_color_eq(lv_obj_get_style_bg_color(shell.icon_disc,LV_PART_MAIN),lv_color_hex(expected.disc_color)))std::cerr<<"header circle: "<<c.what<<"\n";
   assert(lv_color_eq(lv_obj_get_style_bg_color(shell.icon_disc,LV_PART_MAIN),lv_color_hex(expected.disc_color))&&"header circle = the tile's circle");
   if(close_rgb!=expected.control_color)std::cerr<<"close press color: "<<c.what<<"\n";
   assert(close_rgb==expected.control_color&&close_opa==expected.control_opa&&close_opa==LV_OPA_COVER&&"close press: the circle's color, opaque");
   assert(close_rgb==footer_rgb&&close_opa==footer_opa&&"close press and footer controls look the same");
   {lv_style_value_t v;assert(lv_obj_get_local_style_prop(button,LV_STYLE_COLOR_FILTER_OPA,&v,LV_STATE_PRESSED)==LV_STYLE_RES_FOUND&&v.num==LV_OPA_TRANSP&&"no theme darkening");}
   hide_popup_shell(b.body);
 }
 tone_color::g_from_icon_card=nullptr;
 configManager.cfg.icon_glow=0;popup_shell_use_tile_disc(false,false,true,true);
 {uint32_t rgb;lv_opa_t opa;footer_fill(0x482F10,0xEF8402,rgb,opa);assert(opa==LV_OPA_COVER&&rgb==tone_color::fill(0x482F10,0xEF8402,true,0).control_color&&"Circle strength 0 keeps presses visible");}
 configManager.cfg.icon_glow=icon_glow::kDefault;g_next_disc={};
 lv_obj_set_style_bg_color(b.body,lv_color_hex(0x885522),0);lv_obj_set_style_text_color(b.icon,lv_color_hex(0x00FF00),0);show(b,"Weather");assert(lv_color_eq(lv_obj_get_style_bg_color(shell.frame,LV_PART_MAIN),lv_color_hex(0x885522)));assert(lv_color_eq(lv_obj_get_style_text_color(shell.icon,LV_PART_MAIN),lv_color_hex(0x00FF00)));assert(shell.frame==frame&&shell.header==header&&shell.close==button);assert(lv_obj_get_parent(a.body)==a.owner);assert(lv_obj_has_flag(a.body,LV_OBJ_FLAG_HIDDEN));assert(strcmp(hometiles_title::text(shell.title),"Weather")==0);
 hometiles_title::set(a.title,"Hidden background update");lv_obj_set_style_bg_color(a.body,lv_color_hex(0xEE0000),0);sync_popup_shell();assert(lv_color_eq(lv_obj_get_style_bg_color(shell.frame,LV_PART_MAIN),lv_color_hex(0x885522)));assert(strcmp(hometiles_title::text(shell.title),"Weather")==0);
 lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(b.closed==1&&!PopupFirstFrame::any_pending());process_popup_open();assert(applied==2);
 for(int i=0;i<50;++i){show(a,"Light");show(b,"Cover");lv_refr_now(d);process_popup_open();hide_popup_shell(b.body);assert(allocations==2);}
 show(a,"PIN");a.allow_close=false;lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(shell.active&&shell.active->body==a.body);a.allow_close=true;
 a.back=true;lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(shell.active);assert(strcmp(lv_label_get_text(lv_obj_get_child(button,0)),"X")==0);lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(!shell.active);
 fail_alloc=true;assert(!defer_popup_body(a.body,a.title,a.icon,a.close,Init{&a,"failure"},apply));fail_alloc=false;
 auto form=make();disposable=&form;show(form,"Settings");show_popup_shell(form.owner,form.body,form.title,form.icon,form.close,dismiss_form);show(b,"Media");assert(dismissed==1&&shell.active->body==b.body);hide_popup_shell(b.body);assert(allocations==2);
 WeatherPopupContext weather;weather.overlay=b.owner;weather.card=b.body;g_weather_popup_ctx=&weather;
 for(bool cached:{false,true}){
  show(b,"Weather forecast");cancel_popup_open(b.body);weather.has_rendered_data=cached;weather.rendered_entity_id="weather.test";g_pending_weather_init.entity_id="weather.test";g_weather_open_pending=true;
  g_weather_body.hide(b.body,b.title,b.icon,b.close);defer_popup_content(b.body,finish_weather_popup_open);lv_refr_now(d);process_popup_open();sync_popup_shell();
  assert(shell.active&&shell.active->body==b.body&&lv_obj_get_parent(b.body)==shell.overlay);assert(!lv_obj_has_flag(b.body,LV_OBJ_FLAG_HIDDEN));hide_popup_shell(b.body);
 }
 g_weather_popup_ctx=nullptr;
 open_settings_popup(SettingsPopupKind::Wifi);assert(settings_built==0);assert(shell.frame==frame&&shell.close==button);
 auto*settings_card=settings_popup_card;lv_refr_now(d);process_popup_open();lv_obj_update_layout(shell.overlay);assert(settings_built==1);
 assert(lv_obj_get_width(shell.frame)==lv_obj_get_width(settings_card));assert(lv_obj_get_width(settings_card)>popup_layout::kCardWidth||SCREEN_WIDTH==SCREEN_HEIGHT);
 assert(lv_obj_has_flag(settings_popup_title,LV_OBJ_FLAG_IGNORE_LAYOUT));
 lv_obj_send_event(shell.active->close,LV_EVENT_RELEASED,nullptr);assert(settings_closed==0&&!lv_obj_has_flag(wifi_entry_view,LV_OBJ_FLAG_HIDDEN));
 lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(settings_closed==0&&lv_obj_has_flag(wifi_entry_view,LV_OBJ_FLAG_HIDDEN));
 lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);assert(settings_closed==1&&!shell.active);assert(allocations==2);
 // The next tile popup after the full-screen Settings card resizes the shared
 // frame; that must not repaint the tiles outside the new frame (P4 ~100 ms).
 lv_refr_now(d);outside_draws=0;show(a,"After Settings",true);lv_refr_now(d);process_popup_open();lv_refr_now(d);
 if(SCREEN_WIDTH>SCREEN_HEIGHT)assert(outside_draws==0&&"A popup after Settings must not repaint tiles outside its frame");
 lv_obj_update_layout(shell.overlay);assert(lv_obj_get_width(shell.frame)==lv_obj_get_width(a.body));hide_popup_shell(a.body);lv_refr_now(d);

 // A tapped tile under the popup's edge marks itself on release, before its
 // popup opens. The popup's area is drawn first; drawn first, the tile's area
 // showed a flat, popup-colored square until the popup reached it (V2 video).
 {auto*tile=lv_obj_create(lv_screen_active());lv_obj_set_size(tile,160,120);
  lv_obj_set_pos(tile,(SCREEN_WIDTH+popup_layout::kCardWidth)/2-60,SCREEN_HEIGHT/2);lv_refr_now(d);
  lv_obj_invalidate(tile);show(a,"Tapped edge tile",true);first_flush_y=-1;lv_refr_now(d);
  assert(first_flush_y>=0&&first_flush_y<SCREEN_HEIGHT/2&&"The popup is drawn before the tapped tile");
  process_popup_open();hide_popup_shell(a.body);lv_obj_delete(tile);lv_refr_now(d);}
 // A cover or icon change recolors that tile first and the open popup follows.
 // Drawn in marking order, the tile's part under the popup showed a flat
 // square in the new color until the popup's bands reached it (V2 video).
 {auto*tile=lv_obj_create(lv_screen_active());lv_obj_set_size(tile,160,120);
  lv_obj_set_pos(tile,(SCREEN_WIDTH+popup_layout::kCardWidth)/2-60,SCREEN_HEIGHT/2);
  show(a,"Recolored edge tile",true);process_popup_open();lv_refr_now(d);
  lv_obj_set_style_bg_color(tile,lv_color_hex(0x3355AA),0);popup_shell_follow_tile_color(0x3355AA);sync_popup_shell();
  first_flush_y=-1;lv_refr_now(d);
  assert(first_flush_y>=0&&first_flush_y<SCREEN_HEIGHT/2&&"The recolored popup is drawn before its tile");
  hide_popup_shell(a.body);lv_obj_delete(tile);lv_refr_now(d);}
 show(a,"Screen replacement");auto*old=lv_screen_active();auto*next=lv_obj_create(nullptr);lv_screen_load(next);lv_obj_delete(old);assert(!shell.overlay&&!PopupFirstFrame::any_pending());assert(lv_obj_is_valid(a.body)&&lv_obj_get_parent(a.body)==a.owner);
 show(a,"Owner deletion");lv_obj_delete(a.owner);assert(!shell.active&&!PopupFirstFrame::any_pending());assert(allocations==1);lv_obj_delete(b.owner);assert(allocations==0);
 lv_deinit();assert(allocations==0&&!PopupFirstFrame::any_pending());std::cout<<"Shared frame identity, header, cache reuse, no redraw on unchanged sync, first-frame gate, close/back/PIN, cancellation, screen/owner deletion and allocation failure passed\n";
}
`;
const source=path.join(out,'test.cpp');fs.writeFileSync(source,cpp);
for(const [name,width,height,define] of [['s3',480,480,'DEVICE_LAYOUT_480X480'],['p4',1280,800,'']]){
 const binary=path.join(out,name+(process.platform==='win32'?'.exe':''));
 let r=spawnSync(host.cxx,[...host.flags,'-std=c++17',`-DSCREEN_WIDTH=${width}`,`-DSCREEN_HEIGHT=${height}`,...(define?['-D'+define]:[]),source,host.archive,'-o',binary],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);
 r=spawnSync(binary,[path.join(out,name)],{encoding:'utf8'});assert.equal(r.status,0,name+': '+r.stdout+r.stderr);fs.writeFileSync(path.join(out,name+'.log'),r.stdout+r.stderr);
}
console.log('One shared popup shell and deferred cached contents passed with real LVGL on P4 and S3.');
