import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const vendor = read('src/devices/guition_jc8012p4a1_v2/vendor/gsl3680_touch.cpp');
const board = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(entry => entry.name === name);
  assert(found, name);
  return found.source;
};
const constants = (source, names) => names.map(name => {
  const match = source.match(new RegExp(`^constexpr [^\\n]*\\b${name}\\s*=[^;]*;`, 'm'));
  assert(match, name);
  return match[0];
}).join('\n');
const state = vendor.match(/struct TouchState\s*\{[\s\S]*?\n\};/);
assert(state);
const host = await lvglHost(root);
if (!host) { console.log('SKIP: Guition V2 touch needs native LVGL and a C++ compiler'); process.exit(0); }
const out = path.join(root, 'build/tests/guition-v2-touch-bounds');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
using esp_err_t = int;
using esp_lcd_touch_handle_t = void*;
using i2c_master_bus_handle_t = void*;
using i2c_master_dev_handle_t = void*;
constexpr esp_err_t ESP_OK = 0;
#define ESP_LOGW(...) ((void)0)
struct Logger { template<class... T> void printf(const char*, T...) {} } Serial;
${constants(vendor, ['kMaxContacts', 'kRawMaxX', 'kRawMaxY', 'kNativeWidth', 'kNativeHeight'])}
${constants(board, ['kTouchSamplePointCount', 'kTouchReleaseDebounceReads', 'kTouchJitterThresholdPx', 'kInvalidTouchTrackId'])}
${state[0]}
TouchState g_state;
std::array<uint8_t,24> packet{};
esp_err_t read_error = ESP_OK;
esp_err_t read_register(uint8_t reg, uint8_t* data, size_t size) {
  assert(reg == 0x80 && size == packet.size());
  std::memcpy(data, packet.data(), size); return read_error;
}
${fn(vendor, 'scale_and_clamp')}
${fn(vendor, 'esp_lcd_touch_read_data')}
${fn(vendor, 'esp_lcd_touch_get_coordinates')}
esp_lcd_touch_handle_t g_touch = &g_state;
struct { int width=800, height=1280; } display_cfg;
bool g_touch_active=false;
uint8_t g_touch_release_reads=0, g_touch_active_track_id=kInvalidTouchTrackId, g_rotation=0;
int32_t g_touch_stable_x=0, g_touch_stable_y=0;
${fn(board, 'DeviceGuitionJC8012P4A1V2::getTouch').replace('DeviceGuitionJC8012P4A1V2::getTouch', 'read_board_touch')}
void contact(int index, uint16_t x, uint16_t y, uint8_t id=1) {
  const int offset=4+index*4;
  packet[offset]=y; packet[offset+1]=y>>8;
  packet[offset+2]=x; packet[offset+3]=(id<<4)|((x>>8)&15);
}
void sample(uint16_t x, uint16_t y) {
  packet.fill(0); packet[0]=1; contact(0,x,y);
}
std::vector<lv_point_t> delivered;
void record(lv_event_t* event) {
  const auto code=lv_event_get_code(event);
  if(code!=LV_EVENT_PRESSED && code!=LV_EVENT_PRESSING && code!=LV_EVENT_RELEASED) return;
  lv_point_t point; lv_indev_get_point(lv_indev_get_act(),&point);
  delivered.push_back(point);
}
void poll(lv_indev_t* input) { lv_tick_inc(20); lv_indev_read(input); }
int main() {
  g_state.initialized=true;
  lv_init(); auto* display=lv_display_create(1280,800);
  static uint16_t band[1280*28];
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,band,nullptr,sizeof(band),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,[](lv_display_t* d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
  auto* input=lv_indev_create(); lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input,[](lv_indev_t*,lv_indev_data_t* data){
    int16_t x=0,y=0;
    if(read_board_touch(x,y)){data->state=LV_INDEV_STATE_PRESSED;data->point={x,y};}
    else data->state=LV_INDEV_STATE_RELEASED;
  });
  auto* slider=lv_slider_create(lv_screen_active());
  lv_obj_set_pos(slider,620,150); lv_obj_set_size(slider,40,500);
  lv_obj_add_flag(slider,LV_OBJ_FLAG_PRESS_LOCK);
  lv_slider_set_range(slider,0,100);
  lv_obj_add_event_cb(slider,record,LV_EVENT_ALL,nullptr);
  lv_obj_update_layout(slider);
  for(auto rotation:{0,2}) {
    g_rotation=rotation;
    for(int tap=0;tap<30;++tap) {
      packet.fill(0); poll(input);
      lv_slider_set_value(slider,50,LV_ANIM_OFF);
      sample(825,445); poll(input); poll(input);
      const int before=lv_slider_get_value(slider);
      assert(before>=48 && before<=52);
      const lv_point_t middle=delivered.back();
      const auto start=delivered.size();
      // Valid horizontal position, invalid Y: previously forced one endpoint.
      sample(825,tap%2 ? 891 : 65535); poll(input);
      assert(lv_slider_get_value(slider)==before && "Invalid raw Y must not move the slider to 0/100");
      packet.fill(0); poll(input);
      for(size_t i=start;i<delivered.size();++i)
        assert(delivered[i].x==middle.x && delivered[i].y==middle.y &&
               "Press/release consumers, including the color field, must retain the last valid point");
    }
    // Genuine drags to both edges must remain immediate and reach the endpoints.
    sample(825,445); poll(input);
    sample(825,0); poll(input); poll(input);
    assert(lv_slider_get_value(slider)==(rotation==0 ? 100 : 0));
    sample(825,890); poll(input); poll(input);
    assert(lv_slider_get_value(slider)==(rotation==0 ? 0 : 100));
    packet.fill(0); poll(input);
  }
  // Decode all contacts atomically; an invalid later contact cannot expose a
  // partially updated packet or become a replacement pointer.
  for(auto bad: {std::array<uint16_t,2>{1651,445}, {4095,445}, {825,891}, {825,65535}}) {
    packet.fill(0); packet[0]=2; contact(0,825,445); contact(1,bad[0],bad[1],2);
    esp_lcd_touch_read_data(g_touch);
    uint16_t x[5]{},y[5]{};uint8_t count=0;
    assert(!esp_lcd_touch_get_coordinates(g_touch,x,y,nullptr,nullptr,&count,5) && count==0);
  }
  packet.fill(0); packet[0]=5;
  for(int i=0;i<5;++i) contact(i,i==4?1650:i*200,i==4?890:i*100,i+1);
  esp_lcd_touch_read_data(g_touch);
  uint16_t x[5]{},y[5]{};uint8_t count=0;
  assert(esp_lcd_touch_get_coordinates(g_touch,x,y,nullptr,nullptr,&count,5) && count==5);
  assert(x[0]==0 && y[0]==0 && x[4]==1279 && y[4]==799);
  sample(825,445); read_error=1;
  esp_lcd_touch_read_data(g_touch);
  assert(!esp_lcd_touch_get_coordinates(g_touch,x,y,nullptr,nullptr,&count,5));
  lv_deinit();
  std::cout<<"Repeated taps, both rotations, LVGL slider endpoints, release coordinates, raw boundaries and atomic contacts: PASS"<<std::endl;
}
`);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding:'utf8'});
assert.equal(result.status,0,result.stdout+result.stderr);
result=spawnSync(binary,[],{encoding:'utf8'});
assert.equal(result.status,0,result.stdout+result.stderr);
console.log(result.stdout.trim());
