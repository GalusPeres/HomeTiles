// Sensor, Number, Select, Date/Time, Binary Sensor and Energy popups show the
// entity's current value in the shared header: a smaller white title above
// the white value, both next to the icon disc. Every other popup keeps the
// classic header. The value is copied from a hidden holder label, so live
// updates repaint only the header line and an unchanged header never redraws.
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

// ---- Contract ---------------------------------------------------------------
assert.match(read('src/ui/popups/popup_shell.h'),
  /void show_popup_shell\([^;]*void \(\*dismiss\)\(\) = nullptr, lv_obj_t\* value = nullptr\);/);
const layout = code(read('src/ui/popups/popup_layout.h'));
for (const marker of [
  'inline const lv_font_t* headerCompactTitleFont() { return font20(); }',
  'inline const lv_font_t* headerValueFont() { return font28(); }',
  'alignHeader(card, nullptr, icon, icon_disc);',
  'const int top = std::max(0, center - (title_height + value_height) / 2);',
  'lv_obj_align(label, LV_ALIGN_TOP_LEFT, kHeaderTitleX, y);',
]) assert.ok(layout.includes(marker), `popup_layout.h: ${marker}`);
const shellSource = code(read('src/ui/popups/popup_shell.cpp'));
for (const marker of [
  'const bool with_value = sync_header_value(shell.active->value);',
  'with_value ? popup_layout::headerCompactTitleFont() : nullptr);',
  'set_title_single_line(shell.title, with_value);',
  'popup_layout::alignHeaderWithValue(shell.header, shell.title, shell.value, shell.icon,',
  'popup_layout::alignHeader(shell.header, shell.title, shell.icon, shell.icon_disc);',
  'lv_obj_set_style_text_color(shell.value, lv_color_white(), 0);',
]) assert.ok(shellSource.includes(marker), `popup_shell.cpp: ${marker}`);

// Only the value popups pass a holder; every other popup keeps its header.
const shellCalls = file => [...code(read(file)).matchAll(/show_popup_shell\(([^;]*)\);/g)].map(match => {
  let depth = 0; let count = 1;
  for (const character of match[1]) {
    if (character === '(') ++depth;
    else if (character === ')') --depth;
    else if (character === ',' && depth === 0) ++count;
  }
  return {count, args: match[1].replace(/\s+/g, ' ')};
});
for (const file of ['src/ui/popups/sensor/sensor_popup.cpp', 'src/ui/popups/energy/energy_popup.cpp']) {
  const calls = shellCalls(file);
  assert.ok(calls.length > 0, file);
  for (const call of calls)
    assert.ok(call.count === 7 && /nullptr, g_\w+_popup_ctx->value_label$/.test(call.args), `${file} passes its value holder`);
}
for (const popup of ['light', 'climate', 'cover', 'media', 'camera', 'pin', 'weather']) {
  const file = `src/ui/popups/${popup}/${popup}_popup.cpp`;
  for (const call of shellCalls(file)) assert.ok(call.count <= 6, `${popup} keeps the classic header`);
}
for (const call of shellCalls('src/ui/tabs/settings/tab_settings.cpp'))
  assert.ok(call.count <= 6, 'Settings keeps the classic header');

// Sensor: one hidden holder, set by every existing value path.
const sensor = read('src/ui/popups/sensor/sensor_popup.cpp');
assert.match(code(fn(sensor, 'build_popup_shell')),
  /ctx->value_label = lv_label_create\(parts\.card\);[\s\S]*lv_obj_add_flag\(ctx->value_label, LV_OBJ_FLAG_HIDDEN\);/,
  'The Sensor value holder is hidden and exists before the first frame');
assert.match(code(fn(sensor, 'update_value_label')),
  /sensor_value_display\(value, unit, ctx->decimals, categorical_state\)[\s\S]*set_label_text_if_changed\(ctx->value_label/,
  'Numeric and textual Sensors keep the established value formatting');
assert.match(code(fn(sensor, 'update_binary_state')), /lv_label_set_text\(\s*ctx->value_label,\s*i18n::binary_sensor_state_label/,
  'Binary Sensors show their translated state');
assert.match(code(fn(sensor, 'apply_init_to_context')),
  /if \(init\.editable\) \{\s*set_label_text_if_changed\(ctx->value_label,\s*editable_display_value\(editable_value\)\.c_str\(\)\);/,
  'Number, Select and Date/Time show the tile value');
assert.match(code(fn(sensor, 'process_sensor_popup_queue')),
  /parse_editable_value\([\s\S]*set_label_text_if_changed\(ctx->value_label, editable_display_value\(value\)\.c_str\(\)\);/,
  'Editor values update while the popup is open');
const show = code(fn(sensor, 'show_sensor_popup'));
assert.ok(show.indexOf('apply_sensor_header_value(ctx, init);') > 0 &&
  show.indexOf('apply_sensor_header_value(ctx, init);') < show.indexOf('show_popup_shell('),
  'The first frame shows the value of the opened entity');
const firstFrame = code(fn(sensor, 'apply_sensor_header_value'));
for (const marker of ['editable_display_value(value)', 'update_binary_state(ctx, init.value, init.binary_available,',
  'sensor_value_display(init.value, init.unit, init.decimals,'])
  assert.ok(firstFrame.includes(marker), `First frame: ${marker}`);
// Energy: the hidden holder carries today's value from the first frame on.
const energy = read('src/ui/popups/energy/energy_popup.cpp');
assert.match(code(fn(energy, 'build_popup_ui')),
  /ctx->value_label = lv_label_create\(card\);[\s\S]*lv_obj_add_flag\(ctx->value_label, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(code(fn(energy, 'apply_init_to_context')), /update_loading_header\(ctx\);\s*\}$/);

// ---- Real LVGL: shared header ----------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Header value rendering needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/popup-header-value');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>
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
std::string getMdiChar(const char*){return "\xF3\xB0\x96\xAD";}
// Surface styles are covered by the shell tests; this test needs geometry only.
namespace ui_surface_style {
template<class T> void apply_radius(lv_obj_t* obj,T radius,lv_style_selector_t selector){lv_obj_set_style_radius(obj,static_cast<int32_t>(radius),selector);}
inline void apply_global_tile_border(lv_obj_t*){}inline void apply_popup_border(lv_obj_t*,lv_color_t,lv_opa_t){}inline lv_opa_t icon_glow_opa(){return 64;}inline lv_opa_t icon_glow_border_opa(){return 115;}
}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void*p){free(p);}
${strip(read('src/ui/popups/popup_layout.h'))}
${strip(read('src/ui/popups/popup_open.h'))}
${strip(read('src/ui/popups/popup_shell.h'))}
${strip(read('src/ui/popups/popup_open.cpp'))}
${strip(read('src/ui/popups/popup_shell.cpp'))}
int flushed=0;lv_area_t flushed_area{};
struct Body{PopupShellParts parts;lv_obj_t* value=nullptr;};
Body make(const char* title,const char* value){
 Body body{create_popup_body([](lv_event_t*){},nullptr,0x335577)};
 hometiles_title::set(body.parts.title,title);lv_label_set_text(body.parts.icon,getMdiChar("").c_str());
 if(value){body.value=lv_label_create(body.parts.card);lv_label_set_text(body.value,value);lv_obj_add_flag(body.value,LV_OBJ_FLAG_HIDDEN);}
 lv_obj_add_flag(body.parts.card,LV_OBJ_FLAG_HIDDEN);return body;
}
void show(Body& body){lv_obj_remove_flag(body.parts.card,LV_OBJ_FLAG_HIDDEN);show_popup_shell(body.parts.overlay,body.parts.card,body.parts.title,body.parts.icon,body.parts.close,nullptr,body.value);}
void render(lv_display_t* display){lv_obj_update_layout(shell.overlay);lv_refr_now(display);sync_popup_shell();lv_obj_update_layout(shell.overlay);lv_refr_now(display);}
bool white(lv_obj_t* label){return lv_color_eq(lv_obj_get_style_text_color(label,LV_PART_MAIN),lv_color_white());}
int line(lv_obj_t* label){return lv_font_get_line_height(lv_obj_get_style_text_font(label,LV_PART_MAIN));}
void check_value_header(const char* expected){
 lv_area_t title,value,disc,close;lv_obj_get_coords(shell.title,&title);lv_obj_get_coords(shell.value,&value);lv_obj_get_coords(shell.icon_disc,&disc);lv_obj_get_coords(shell.close,&close);
 assert(!lv_obj_has_flag(shell.value,LV_OBJ_FLAG_HIDDEN)&&strcmp(hometiles_title::text(shell.value),expected)==0);
 assert(lv_obj_get_style_text_font(shell.title,LV_PART_MAIN)==popup_layout::headerCompactTitleFont()&&"Title about 20 px");
 assert(lv_obj_get_style_text_font(shell.value,LV_PART_MAIN)==popup_layout::headerValueFont()&&"Value about 30 px");
 assert(popup_layout::headerCompactTitleFont()==popup_layout::font20()&&popup_layout::headerValueFont()==popup_layout::font28());
 assert(white(shell.title)&&white(shell.value)&&"All header text is white");
 assert(hometiles_title::state_for(shell.title)->single_line&&!strchr(lv_label_get_text(shell.title),'\n')&&"The title keeps one ellipsized line");
 assert(title.x1==value.x1&&title.x1-disc.x2-1==popup_layout::kHeaderIconDiscGap&&"Both lines start after the icon disc");
 assert(value.y1==title.y1+line(shell.title)&&lv_area_get_height(&value)==line(shell.value)&&"The value sits directly below the title");
 assert(line(shell.title)+line(shell.value)<=popup_layout::kHeaderIconDiscSize&&"The block fits beside the disc");
 assert(std::abs((title.y1+value.y2)-(disc.y1+disc.y2))<=2&&"The block is centered on the icon disc");
 assert(title.x2<close.x1&&value.x2<close.x1&&"Close button unchanged and clear of the text");
 assert(lv_obj_get_width(shell.close)==popup_layout::kCloseButtonSize);
}
int main(){
 lv_init();auto* display=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);std::vector<uint32_t> band(SCREEN_WIDTH*16);
 lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);lv_display_set_buffers(display,band.data(),nullptr,band.size()*4,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,[](lv_display_t* d,const lv_area_t* area,uint8_t*){if(!flushed++)flushed_area=*area;else{flushed_area.x1=std::min(flushed_area.x1,area->x1);flushed_area.y1=std::min(flushed_area.y1,area->y1);flushed_area.x2=std::max(flushed_area.x2,area->x2);flushed_area.y2=std::max(flushed_area.y2,area->y2);}lv_display_flush_ready(d);});
 auto sensor=make("Living room\nTemperature","21.5 \xC2\xB0""C"),light=make("Kitchen light",nullptr);
 show(sensor);render(display);check_value_header("21.5 \xC2\xB0""C");
 assert(lv_obj_has_flag(sensor.value,LV_OBJ_FLAG_HIDDEN)&&"The holder itself is never drawn");
 // An unchanged header never redraws.
 flushed=0;for(int i=0;i<20;++i){sync_popup_shell();lv_refr_now(display);}assert(flushed==0);
 // A live value update repaints only the value line.
 lv_label_set_text(sensor.value,"22.0 \xC2\xB0""C");flushed=0;sync_popup_shell();lv_refr_now(display);
 lv_area_t value;lv_obj_get_coords(shell.value,&value);
 // LVGL adds only a label's glyph margin (a quarter of its line height).
 const int margin=line(shell.value)/4+1;
 assert(flushed>0&&flushed_area.y1>=value.y1-margin&&flushed_area.y2<=value.y2+margin&&flushed_area.x1>=value.x1-margin&&flushed_area.x2<=value.x2+margin&&"Only the value line repaints");
 check_value_header("22.0 \xC2\xB0""C");
 // Long values stay on one line.
 const std::string long_state(200,'W');lv_label_set_text(sensor.value,long_state.c_str());render(display);
 assert(lv_obj_get_height(shell.value)==line(shell.value)&&strstr(lv_label_get_text(shell.value),"...")&&"Long values are ellipsized");
 check_value_header(long_state.c_str());
 // Without a value text the classic two-line header returns.
 lv_label_set_text(sensor.value,"");render(display);
 assert(lv_obj_has_flag(shell.value,LV_OBJ_FLAG_HIDDEN)&&lv_obj_get_style_text_font(shell.title,LV_PART_MAIN)==popup_layout::headerTitleFont());
 assert(!hometiles_title::state_for(shell.title)->single_line&&strchr(lv_label_get_text(shell.title),'\n'));
 lv_label_set_text(sensor.value,"--");render(display);check_value_header("--");
 // Popups without a value holder keep the classic header, and switching back
 // restores the value line.
 for(int cycle=0;cycle<5;++cycle){
  show(light);render(display);
  assert(lv_obj_has_flag(shell.value,LV_OBJ_FLAG_HIDDEN)&&lv_obj_get_style_text_font(shell.title,LV_PART_MAIN)==popup_layout::headerTitleFont());
  assert(strcmp(hometiles_title::text(shell.title),"Kitchen light")==0&&!hometiles_title::state_for(shell.title)->single_line);
  lv_area_t title,disc;lv_obj_get_coords(shell.title,&title);lv_obj_get_coords(shell.icon_disc,&disc);
  assert(std::abs((title.y1+title.y2)-(disc.y1+disc.y2))<=2&&"The classic title stays centered on the disc");
  flushed=0;for(int i=0;i<5;++i){sync_popup_shell();lv_refr_now(display);}assert(flushed==0);
  show(sensor);render(display);check_value_header("--");
  flushed=0;for(int i=0;i<5;++i){sync_popup_shell();lv_refr_now(display);}assert(flushed==0);
 }
 hide_popup_shell(sensor.parts.card);lv_obj_delete(sensor.parts.overlay);lv_obj_delete(light.parts.overlay);
 lv_deinit();std::cout<<SCREEN_WIDTH<<"x"<<SCREEN_HEIGHT<<": header value passed\n";
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
for (const [name, width, height, define] of [['square', 480, 480, 'DEVICE_LAYOUT_480X480'],
  ['wide', 1024, 600, 'DEVICE_LAYOUT_1024X600'], ['ws8', 1280, 800, '']]) {
  const binary = path.join(out, name + (process.platform === 'win32' ? '.exe' : ''));
  let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', `-DSCREEN_WIDTH=${width}`, `-DSCREEN_HEIGHT=${height}`,
    ...(define ? ['-D' + define] : []), source, host.archive, '-o', binary], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  result = spawnSync(binary, [], {encoding: 'utf8', timeout: 60000});
  assert.equal(result.status, 0, name + ': ' + result.stdout + result.stderr);
  process.stdout.write(result.stdout);
}
console.log('Popup header value: fonts, geometry, live updates and classic headers passed.');
