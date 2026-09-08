import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const runtime = read('src/tiles/runtime/tile_renderer.cpp');
const fn = name => {
  const matches = cppFunctionDefinitions(runtime).filter(f => f.name === name);
  assert(matches.length, name); return matches.map(f => f.source).join('\n');
};
const host = await lvglHost(root);
if (!host) { console.log('SKIP: Media artwork regression needs LVGL and a C++ compiler'); process.exit(0); }
const out = path.join(root, 'build/tests/media-artwork-runtime'); fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <misc/cache/instance/lv_image_cache.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include <iostream>
#include "src/core/json_scan.h"
#include "src/types/media/update_schedule.h"
#include "src/types/media/cover_geometry.h"
extern "C" { LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40); }
${read('src/tiles/runtime/tile_renderer_fonts.h').replace(/^#include.*$/gm,'').replace('#pragma once','')}
class String : public std::string {public:
 using std::string::string; using std::string::operator=; String()=default; String(const std::string&s):std::string(s){}
 void trim(){auto a=find_first_not_of(" \r\n\t");if(a==npos){clear();return;}*this=substr(a,find_last_not_of(" \r\n\t")-a+1);}
 void toLowerCase(){std::transform(begin(),end(),begin(),[](unsigned char c){return std::tolower(c);});}
 bool equalsIgnoreCase(String b)const{String a=*this;a.toLowerCase();b.toLowerCase();return a==b;}
 void replace(const char*a,const char*b){size_t pos=0;while((pos=find(a,pos))!=npos){std::string::replace(pos,strlen(a),b);pos+=strlen(b);}}
 bool equals(const char*s)const{return *this==s;}
 bool startsWith(const char*s)const{return rfind(s,0)==0;}
 char charAt(size_t i)const{return at(i);}
};
${read('src/types/media/widgets.h').replace(/^#.*$/gm,'')}
${read('src/types/media/artwork_payload.h').replace(/^#.*$/gm,'')}
uint32_t ticks=100;uint32_t millis(){return ticks;}
struct Logger{void println(const char*){}template<class...T>void printf(const char*,T...){}}Serial;
std::map<void*,size_t> allocations;bool fail_pixels=false,fail_descriptor=false;size_t pixel_peak=0;
void* tracked_malloc(size_t n){if(fail_descriptor)return nullptr;auto*p=std::malloc(n);if(p)allocations[p]=n;return p;}
void tracked_free(void*p){if(!p)return;assert(allocations.erase(p)==1&&"Double free or borrowed source freed");std::free(p);}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int caps){assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(fail_pixels)return nullptr;auto*p=std::malloc(n);if(p)allocations[p]=n;pixel_peak=std::max(pixel_peak,n);return p;}
void* alloc_media_cover_memory(size_t n){return tracked_malloc(n);}
#define malloc tracked_malloc
#define free tracked_free
${read('src/types/media/artwork_scale.cpp').replace(/^#include.*$/gm,'')}
${fn('free_media_cover_dsc')}
${fn('clone_media_cover_dsc')}
enum class GridType{TAB0,TAB1,TAB2,SCREENSAVER};constexpr int TILES_PER_GRID=4;
MediaTileWidgets g_tab0_media[4],g_tab1_media[4],g_tab2_media[4],g_screensaver_media[4];
MediaTileWidgets* tile_renderer_get_media_widgets(GridType grid){return grid==GridType::TAB0?g_tab0_media:grid==GridType::TAB1?g_tab1_media:grid==GridType::TAB2?g_tab2_media:g_screensaver_media;}
${fn('find_decoded_media_cover_sibling')}
${fn('set_media_cover_visible')}
${fn('media_cover_has_hidden_ancestor')}
// Only decoding/network I/O are injected. Use actual layout and the exact
// profile's HTTPS policy so state_fast/full updates expose cover flicker.
${read('src/types/media/content_layout.cpp').replace(/^#include.*$/gm,'')}
uint32_t fnv1a_hash(const char*s){uint32_t h=2166136261u;while(*s){h^=uint8_t(*s++);h*=16777619u;}return h;}
lv_image_dsc_t* source_image(int w=240,int h=240,int stride=0){if(!stride)stride=w*2;auto*d=static_cast<lv_image_dsc_t*>(tracked_malloc(sizeof(lv_image_dsc_t)));assert(d);*d={};d->header.magic=LV_IMAGE_HEADER_MAGIC;d->header.cf=LV_COLOR_FORMAT_RGB565_SWAPPED;d->header.w=w;d->header.h=h;d->header.stride=stride;d->data_size=stride*h;d->data=static_cast<uint8_t*>(heap_caps_malloc(d->data_size,3));assert(d->data);for(int y=0;y<h;++y)for(int x=0;x<w;++x){uint16_t v=y*w+x;memcpy(const_cast<uint8_t*>(d->data)+y*stride+2*x,&v,2);}return d;}
int decode_count=0;bool fail_decode=false;
lv_image_dsc_t* make_media_cover_dsc_from_base64(const String&){++decode_count;return fail_decode?nullptr:source_image();}
${fn('update_media_cover_from_base64')}
constexpr bool kMediaCoverDownloadsEnabled=true;constexpr uint32_t kMediaCoverRetryCooldownMs=1000;
int request_count=0;bool request_queue_full=false;
${fn('media_cover_download_allowed')}
void log_media_cover_download_blocked(){}
void queue_media_cover_request(GridType,uint8_t,MediaCoverRef*r,const String&,uint32_t hash){if(request_queue_full)return;++request_count;r->requested_url_hash=hash;}
${fn('update_media_cover')}
${runtime.match(/struct MediaCoverResult \{[\s\S]*?\n};/)[0]}
std::vector<MediaCoverResult> results;void*g_media_cover_result_queue=&results;
constexpr int pdTRUE=1;
int xQueueReceive(void*,MediaCoverResult*r,int){if(results.empty())return 0;*r=results.front();results.erase(results.begin());return 1;}
int uxQueueMessagesWaiting(void*){return results.size();}
String media_entity_for_grid_index(GridType,uint8_t){return "media_player.test";}
${fn('tile_renderer_find_media_cover')}
const lv_image_dsc_t* popup_received=nullptr;
void update_media_popup_cover(const char*,const lv_image_dsc_t*d,uint32_t){popup_received=d;}
${fn('process_media_cover_results')}
${['extract_json_string_field_cstr','extract_json_number_field_cstr','extract_json_bool_field_cstr','decode_basic_json_escapes','media_first_non_empty','media_text_same','sanitize_media_display_text','set_label_text_if_changed','set_label_long_mode_if_changed','restart_visible_media_text_scroll','media_icon_for_state'].map(fn).join('\n')}
String media_empty_title_label(const String&){return "No playback";}String getMdiChar(const String&s){return s;}
void update_media_popup_from_widgets(GridType,uint8_t,MediaTileWidgets&,const String&){}
${fn('update_media_tile_state')}
${runtime.match(/struct MediaUpdate \{[\s\S]*?\n};/)[0]}
constexpr int MEDIA_QUEUE_SIZE=24;MediaUpdate g_media_queue[24];uint8_t g_media_head=0,g_media_tail=0;
int retry_scans=0;void process_pending_media_cover_retries(){++retry_scans;}
${['process_media_state_updates','process_media_update_queue','process_idle_media_updates'].map(fn).join('\n')}
int main(){
 lv_init();auto*display=lv_display_create(1280,800);
 MediaCoverRef refs[4];
 for(int i=0;i<4;++i){auto&w=g_tab0_media[i];w.cover_ref=&refs[i];w.cover_clip=lv_obj_create(lv_screen_active());lv_obj_set_size(w.cover_clip,80+i*50,80+i*50);w.cover_image=lv_image_create(w.cover_clip);w.media_title_label=lv_label_create(lv_screen_active());w.media_subtitle_label=lv_label_create(lv_screen_active());}
 lv_obj_update_layout(lv_screen_active());
 auto*source=source_image(8,4,20);auto*thumb=make_media_tile_cover_dsc(source,4);assert(thumb&&thumb->header.w==4&&thumb->header.h==2&&thumb->header.stride==8);
 for(int y=0;y<2;++y)for(int x=0;x<4;++x){uint16_t value;memcpy(&value,thumb->data+y*8+x*2,2);assert(value==y*16+x*2);}
 auto original=std::vector<uint8_t>(source->data,source->data+source->data_size);
 free_media_cover_dsc(thumb);assert(!make_media_tile_cover_dsc(source,20));
 fail_pixels=true;assert(!make_media_tile_cover_dsc(source,2));fail_pixels=false;
 const auto owned=allocations.size();fail_descriptor=true;assert(!make_media_tile_cover_dsc(source,2));fail_descriptor=false;assert(allocations.size()==owned);
 source->data_size=1;assert(!make_media_tile_cover_dsc(source,2));source->data_size=80;
 assert(std::equal(original.begin(),original.end(),source->data));free_media_cover_dsc(source);
 auto*portrait=source_image(120,240);thumb=make_media_tile_cover_dsc(portrait,80);assert(thumb&&thumb->header.w==40&&thumb->header.h==80);free_media_cover_dsc(thumb);free_media_cover_dsc(portrait);
 assert(update_media_cover_from_base64(g_tab0_media[0],"art-a"));assert(decode_count==1&&refs[0].dsc->header.w==80&&refs[0].popup_dsc->header.w==240);
 lv_obj_set_size(g_tab0_media[1].cover_clip,155,155);
 assert(update_media_cover_from_base64(g_tab0_media[1],"art-a"));assert(decode_count==1&&refs[1].dsc->header.w==155&&refs[1].popup_dsc->header.w==240&&"Thumbnail size must use pending geometry before LVGL's next layout pass");
 assert(refs[0].popup_dsc->data!=refs[1].popup_dsc->data);
 const auto cached=allocations.size();assert(update_media_cover_from_base64(g_tab0_media[1],"art-a"));assert(allocations.size()==cached&&decode_count==1);
 fail_pixels=true;fail_decode=true;auto*keep=refs[2].dsc;assert(!update_media_cover_from_base64(g_tab0_media[2],"art-a"));assert(refs[2].dsc==keep);fail_pixels=false;fail_decode=false;
 // Test the real state-to-artwork branch: missing, changed URL, null, empty,
 // Base64 plus URL and malformed data never turn a JSON key into a request.
 update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"A","entity_picture_data":"art-a"})");
 const auto artwork_hash=refs[0].url_hash;
 update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"B","volume_level":0.5})");assert(refs[0].url_hash==artwork_hash&&!lv_obj_has_flag(g_tab0_media[0].cover_clip,LV_OBJ_FLAG_HIDDEN));
 // Bridge publishes URL-only state_fast before full state, even during the
 // same song. Check each intermediate update, before the next image arrives.
 update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"B","entity_picture":"https://test/current.jpg","entity_picture_data":"art-a"})");
 auto*visible=refs[0].dsc;const auto title_x=lv_obj_get_style_x(g_tab0_media[0].media_title_label,LV_PART_MAIN);const auto title_width=lv_obj_get_style_width(g_tab0_media[0].media_title_label,LV_PART_MAIN);
 for(int i=0;i<20;++i){
  update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"B","volume_level":0.5,"entity_picture":"https://test/current.jpg"})");
  assert(!lv_obj_has_flag(g_tab0_media[0].cover_clip,LV_OBJ_FLAG_HIDDEN)&&"URL-only updates must not hide the loaded MQTT cover during a song");
  assert(refs[0].dsc==visible&&request_count==0&&"Already delivered MQTT artwork must not be downloaded again");
  assert(lv_obj_get_style_x(g_tab0_media[0].media_title_label,LV_PART_MAIN)==title_x&&lv_obj_get_style_width(g_tab0_media[0].media_title_label,LV_PART_MAIN)==title_width);
  update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"B","entity_picture":"https://test/current.jpg","entity_picture_data":"art-a"})");
 }
 update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"Next song","entity_picture":"https://test/next.jpg"})");
 assert(!lv_obj_has_flag(g_tab0_media[0].cover_clip,LV_OBJ_FLAG_HIDDEN)&&refs[0].dsc==visible&&"Keep the last cover until its replacement is ready");
 assert(lv_obj_get_style_x(g_tab0_media[0].media_title_label,LV_PART_MAIN)==title_x);
 update_media_tile_state(GridType::TAB0,0,R"({"state":"playing","media_title":"Next song","entity_picture":"https://test/next.jpg","entity_picture_data":"art-next"})");
 assert(!lv_obj_has_flag(g_tab0_media[0].cover_clip,LV_OBJ_FLAG_HIDDEN)&&refs[0].url_hash==fnv1a_hash("art-next")&&refs[0].requested_url_hash==0);
 request_count=0;
 update_media_tile_state(GridType::TAB0,0,R"({"media_title":"B","entity_picture":"http://test/one.jpg"})");assert(refs[0].source_url=="http://test/one.jpg"&&request_count==1);
 update_media_tile_state(GridType::TAB0,0,R"({"media_title":"B","entity_picture":"http://test/two.jpg"})");assert(refs[0].source_url=="http://test/two.jpg"&&request_count==2);
 update_media_tile_state(GridType::TAB0,0,R"({"entity_picture":null,"entity_picture_data":null,"media_title":"After null"})");assert(refs[0].source_url.empty()&&refs[0].requested_url_hash==0&&request_count==2&&lv_obj_has_flag(g_tab0_media[0].cover_clip,LV_OBJ_FLAG_HIDDEN));
 update_media_tile_state(GridType::TAB0,0,R"({"entity_picture":"","media_title":"Empty"})");assert(request_count==2);
 update_media_tile_state(GridType::TAB0,0,R"({"entity_picture":"https://test/three.jpg","entity_picture_data":"art-b"})");assert(refs[0].source_url=="mqtt"&&request_count==2);
 // URL adoption uses the full source for a different-sized sibling.
 refs[0].url_hash=fnv1a_hash("http://test/shared.jpg");
 update_media_cover(GridType::TAB0,2,g_tab0_media[2],"http://test/shared.jpg");assert(refs[2].dsc->header.w==180&&refs[2].popup_dsc->header.w==240&&request_count==2);
 auto result=[&](uint32_t hash){MediaCoverResult r{};r.grid_type=GridType::TAB0;r.grid_index=3;r.url_hash=hash;r.ok=true;r.dsc=source_image();return r;};
 refs[3].requested_url_hash=123;results.push_back(result(122));const auto before=allocations.size();process_media_cover_results();assert(!refs[3].dsc&&allocations.size()==before-2);
 results.push_back(result(123));process_media_cover_results();assert(refs[3].dsc->header.w==230&&popup_received==refs[3].popup_dsc&&refs[3].popup_dsc->header.w==240);
 // Failed HTTP replacements must leave both the tile and popup untouched.
 const auto displayed=refs[3].dsc;const auto displayed_popup=popup_received;
 refs[3].requested_url_hash=126;MediaCoverResult failed{};failed.grid_type=GridType::TAB0;failed.grid_index=3;failed.url_hash=126;results.push_back(failed);process_media_cover_results();
 assert(refs[3].dsc==displayed&&popup_received==displayed_popup&&!lv_obj_has_flag(g_tab0_media[3].cover_clip,LV_OBJ_FLAG_HIDDEN));
 assert(refs[3].failed_url_hash==126&&refs[3].failed_at_ms==ticks);
 // A new URL supersedes an in-flight request even if the one-slot queue is full.
 refs[3].requested_url_hash=127;request_queue_full=true;update_media_cover(GridType::TAB0,3,g_tab0_media[3],"http://test/newest.jpg");request_queue_full=false;
 assert(refs[3].requested_url_hash==0);results.push_back(result(127));process_media_cover_results();assert(refs[3].dsc==displayed&&popup_received==displayed_popup);
 // MQTT pixels also supersede a pending HTTP result, including identical pixels
 // returned from the descriptor cache with a different paired URL.
 update_media_cover_from_base64(g_tab0_media[3],"final-mqtt","http://test/newest.jpg");refs[3].requested_url_hash=128;
 update_media_cover_from_base64(g_tab0_media[3],"final-mqtt","http://test/final.jpg");const auto final_mqtt=refs[3].dsc;
 results.push_back(result(128));process_media_cover_results();assert(refs[3].dsc==final_mqtt&&refs[3].embedded_url_hash==fnv1a_hash("http://test/final.jpg")&&refs[3].failed_url_hash==0);
 // Missing optional PSRAM keeps the owned original usable, with no extra
 // internal pixel allocation. Published descriptor deletion stays on the loop.
 refs[3].requested_url_hash=124;results.push_back(result(124));fail_pixels=true;process_media_cover_results();fail_pixels=false;assert(refs[3].dsc->header.w==240&&!refs[3].popup_dsc);
 ticks=1000;process_idle_media_updates();assert(retry_scans==0&&g_media_tail==0);
 for(int i=0;i<3;++i)g_media_queue[i]={GridType::TAB0,0,"paused",true};g_media_head=3;
 refs[3].requested_url_hash=125;results.push_back(result(125));process_idle_media_updates();assert(results.empty()&&g_media_tail==0&&refs[3].url_hash==125);
 ticks=1099;process_idle_media_updates();assert(g_media_tail==0);ticks=1100;process_idle_media_updates();assert(g_media_tail==1&&retry_scans==0);ticks=1200;process_idle_media_updates();assert(g_media_tail==2);ticks=1300;process_idle_media_updates();assert(g_media_tail==3&&retry_scans==0);
 uint32_t resolved_hash=0;auto*resolved=tile_renderer_find_media_cover("media_player.test",resolved_hash);assert(resolved&&resolved_hash);assert(!tile_renderer_find_media_cover("media_player.other",resolved_hash)&&resolved_hash==0);
 for(auto&r:refs){free_media_cover_dsc(r.dsc);free_media_cover_dsc(r.popup_dsc);}
 assert(!tile_renderer_find_media_cover("media_player.test",resolved_hash)&&resolved_hash==0);assert(allocations.empty());assert(pixel_peak<=240*240*2);
 uint32_t last=0;assert(!media_updates::idle_batch_due(false,10000,last)&&last==0);assert(!media_updates::idle_batch_due(true,99,last));assert(media_updates::idle_batch_due(true,100,last));assert(!media_updates::idle_batch_due(true,199,last));last=UINT32_MAX-49;assert(!media_updates::idle_batch_due(true,49,last));assert(media_updates::idle_batch_due(true,50,last));
 lv_display_delete(display);lv_deinit();std::cout<<"Artwork pixels, stride, allocation failures, sibling sizes, fast/full continuity, failed replacements, late results, ownership and idle rollover passed\n";
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
for(const profile of ['p4','s3']) {
 const binary=path.join(out,profile+(process.platform==='win32'?'.exe':''));
 let result = spawnSync(host.cxx, [...host.flags, ...(profile==='p4'?['-DCONFIG_IDF_TARGET_ESP32P4']:['-DDEVICE_LAYOUT_480X480']), '-I', path.join(host.library, 'src'), '-std=c++17', source, host.archive, '-o', binary], {encoding:'utf8'});
 assert.equal(result.status, 0, result.stdout + result.stderr);
 result = spawnSync(binary, [], {encoding:'utf8'}); assert.equal(result.status, 0, profile+': '+result.stdout + result.stderr);
 console.log(profile+': '+result.stdout.trim());
}
