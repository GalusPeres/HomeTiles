import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const host = await lvglHost(root);
if (!host) { console.log('SKIP: Weather title updates require native LVGL'); process.exit(0); }
const runtime = fs.readFileSync(path.join(root, 'src/tiles/runtime/tile_renderer.cpp'), 'utf8');
const functions = cppFunctionDefinitions(runtime);
const fn = name => { const result = functions.find(item => item.name === name); assert(result, name); return result.source; };
// Execute the production title-update block, including its name selection and
// JSON decoding. Forecast parsing is independent of this label regression.
const update = fn('update_weather_tile_state');
const start = update.indexOf('if (widgets.location_label) {');
assert(start >= 0);
const masked = maskCpp(update);
let end = masked.indexOf('{', start) + 1, depth = 1;
while (depth && end < masked.length) {
  if (masked[end] === '{') ++depth;
  if (masked[end] === '}') --depth;
  ++end;
}
assert.equal(depth, 0);
const titleUpdate = update.slice(start, end);
const out = path.join(root, 'build/tests/weather-title-updates');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, String.raw`
#include "src/ui/shared/title_label.h"
#include <cassert>
#include <iostream>
extern "C" { LV_FONT_DECLARE(ui_font_14); LV_FONT_DECLARE(ui_font_16); LV_FONT_DECLARE(ui_font_20); }
class String : public std::string {
 public:
  using std::string::string; using std::string::operator=;
  String() = default; String(const std::string& value) : std::string(value) {}
  void trim() { auto first=find_first_not_of(" \t\r\n"); if(first==npos){clear();return;} *this=substr(first,find_last_not_of(" \t\r\n")-first+1); }
  int indexOf(const String& value, size_t from=0) const { auto at=find(value,from); return at==npos?-1:static_cast<int>(at); }
  int indexOf(char value, size_t from=0) const { auto at=find(value,from); return at==npos?-1:static_cast<int>(at); }
  String substring(size_t from, size_t to) const { return substr(from,to-from); }
  char charAt(size_t at) const { return at<size()?(*this)[at]:0; }
};
${fn('extract_json_string_field')}
${fn('decode_basic_json_escapes')}
struct Tile { String title; };
struct Widgets { lv_obj_t* location_label; };
void apply_update(const Tile& tile, Widgets& widgets, const String& json) {
  ${titleUpdate}
}
int main() {
  lv_init(); lv_display_create(1280,800);
  for (auto* font : {&ui_font_14,&ui_font_16,&ui_font_20}) {
    auto* weather = lv_label_create(lv_screen_active());
    auto* sensor = lv_label_create(lv_screen_active());
    for (auto* label : {weather,sensor}) {
      lv_obj_set_style_text_font(label,font,0);
      lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_RIGHT,0);
      hometiles_title::tile(label,"Wetter\nViechtach",true);
    }
    Widgets widgets{weather};
    const auto owners = lv_obj_get_event_count(weather);
    for (int width : {89,360,56,89}) {
      // 89 px is the 8-inch 1x1 header's content width; wide labels model spans.
      lv_obj_set_width(weather,width); lv_obj_set_width(sensor,width);
      lv_obj_update_layout(weather);
      for (const char* name : {"Wetter\nViechtach", "Ein langer Ort\nÜberlingen am Bodensee", ""}) {
        Tile tile; tile.title=name;
        const String payload="{\"name\":\"Wetter\\nViechtach\"}";
        const char* full = *name ? name : "Wetter\nViechtach";
        hometiles_title::tile(sensor,full,true); lv_obj_update_layout(sensor);
        const std::string expected=lv_label_get_text(sensor);
        for (int repeat=0;repeat<5;++repeat) {
          apply_update(tile,widgets,payload);
          assert(std::string(lv_label_get_text(weather))==expected && "Weather updates must preserve Sensor ellipsis immediately");
          assert(std::string(hometiles_title::text(weather))==full && "Popup titles must retain the updated full name");
          lv_obj_update_layout(weather);
          assert(std::string(lv_label_get_text(weather))==expected);
          assert(lv_obj_get_event_count(weather)==owners);
        }
        if(font==&ui_font_20 && width==89 && std::string(name)=="Wetter\nViechtach")
          std::cout<<"8-inch 1x1: "<<expected<<std::endl;
      }
      Tile empty; apply_update(empty,widgets,"{}");
      assert(std::string(hometiles_title::text(weather))=="--");
      assert(std::string(lv_label_get_text(weather))=="--");
    }
    lv_obj_delete(weather); lv_obj_delete(sensor);
  }
  lv_deinit();
}
`);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding:'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding:'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log(result.stdout.trim());
console.log('Weather runtime titles: Sensor ellipsis, two lines, resize, payload fallback and repeated updates');
