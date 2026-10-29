// The Sensor popup's 30D graph (720 hourly points at first) crashed the
// emulator in LVGL's heap (2026-10-29). lv_chart draws a line chart with at
// least as many points as columns in "crowded mode": it allocates (w + 2 *
// extra) * 3 points, but the columns run from 0 to w (w + 1 of them), and with
// no line width and no bullets extra is 0, so it writes three points past its
// buffer. The graphs draw their own pictures (line width 0), so after drawing
// them they stop the chart's own pass (lv_event_stop_processing).
//
// Real LVGL with an allocator that puts guard bytes behind every block and
// checks them when the block is freed: the chart's own pass with 720 points on
// 380 columns breaks a guard, the stopped pass and 240 points do not.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8').replace(/\r\n/g, '\n');
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const drawAt = popup.indexOf('static void on_chart_area_draw(lv_event_t* event) {');
const popupDraw = popup.slice(drawAt, popup.indexOf('\n}\n', drawAt));
assert.ok(popupDraw.indexOf('lv_event_stop_processing(event);') > popupDraw.indexOf('lv_draw_image(layer, &image, &coords);'),
  'the popup stops the chart pass after drawing its picture');
const tile = read('src/types/sensor/renderer.cpp');
const tileAt = tile.indexOf('static void sensor_graph_area_cb(lv_event_t* e) {');
const tileDraw = tile.slice(tileAt, tile.indexOf('\n}\n', tileAt));
assert.ok(tileDraw.indexOf('lv_event_stop_processing(e);') > tileDraw.lastIndexOf('lv_draw_image(layer, &image, &line_at);'),
  'so does the tile graph');
for (const source of [popup, tile]) {
  assert.ok(source.includes('lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);'), 'no line of the chart own');
}

const host = await lvglHost(root, ['-DLV_USE_STDLIB_MALLOC=LV_STDLIB_CUSTOM']);
if (!host) {
  console.log('SKIP: the chart heap run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const out = path.join(root, 'build/tests/chart-crowded-heap');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>
#include <lvgl_private.h>

// LVGL's allocator with 64 guard bytes behind every block.
static int g_broken = 0;
static constexpr size_t kGuard = 64;
struct Head { size_t size; size_t pad; };
static void check(Head* head) {
  const unsigned char* guard = reinterpret_cast<unsigned char*>(head + 1) + head->size;
  for (size_t i = 0; i < kGuard; ++i) {
    if (guard[i] != 0xAB) { ++g_broken; return; }
  }
}
extern "C" {
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void*, size_t) { return nullptr; }
void lv_mem_remove_pool(lv_mem_pool_t) {}
void* lv_malloc_core(size_t size) {
  Head* head = static_cast<Head*>(malloc(sizeof(Head) + size + kGuard));
  if (!head) return nullptr;
  head->size = size;
  memset(reinterpret_cast<unsigned char*>(head + 1) + size, 0xAB, kGuard);
  return head + 1;
}
void lv_free_core(void* p) {
  if (!p) return;
  Head* head = static_cast<Head*>(p) - 1;
  check(head);
  free(head);
}
void* lv_realloc_core(void* p, size_t size) {
  if (!p) return lv_malloc_core(size);
  Head* head = static_cast<Head*>(p) - 1;
  void* next = lv_malloc_core(size);
  if (!next) return nullptr;
  memcpy(next, p, head->size < size ? head->size : size);
  lv_free_core(p);
  return next;
}
void lv_mem_monitor_core(lv_mem_monitor_t* mon) { memset(mon, 0, sizeof(*mon)); }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
}

static uint8_t g_buf[480 * 60 * 4];
static bool g_stop = false;
static void flush_cb(lv_display_t* display, const lv_area_t*, uint8_t*) { lv_display_flush_ready(display); }
// The graphs' picture callback: its picture drawn, the chart's own pass stopped.
static void picture_cb(lv_event_t* e) {
  if (g_stop) lv_event_stop_processing(e);
}

static int broken_after(uint32_t points) {
  lv_obj_t* chart = lv_chart_create(lv_screen_active());
  lv_obj_set_size(chart, 380, 215);
  lv_obj_set_pos(chart, 50, 100);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 0, 0);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
  lv_chart_series_t* series = lv_chart_add_series(chart, lv_color_hex(0xFFFFFF), LV_CHART_AXIS_PRIMARY_Y);
  lv_obj_add_event_cb(chart, picture_cb, static_cast<lv_event_code_t>(LV_EVENT_DRAW_MAIN | LV_EVENT_PREPROCESS), nullptr);
  lv_chart_set_point_count(chart, points);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
  int32_t* values = lv_chart_get_series_y_array(chart, series);
  for (uint32_t i = 0; i < points; ++i) values[i] = 500 + static_cast<int32_t>(450 * std::sin(i * 0.15));
  g_broken = 0;
  lv_obj_invalidate(chart);
  lv_refr_now(nullptr);
  const int broken = g_broken;
  lv_obj_delete(chart);
  lv_refr_now(nullptr);
  return broken;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(480, 480);
  lv_display_set_buffers(display, g_buf, nullptr, sizeof(g_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush_cb);
  lv_refr_now(display);
  g_stop = false;
  const int own720 = broken_after(720);
  const int own240 = broken_after(240);
  g_stop = true;
  const int stopped720 = broken_after(720);
  printf("{\"own720\":%d,\"own240\":%d,\"stopped720\":%d}\n", own720, own240, stopped720);
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-w', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.ok(run.own720 > 0, `LVGL's own pass with more points than columns writes past its buffer ${JSON.stringify(run)}`);
assert.equal(run.own240, 0, `fewer points than columns stay inside ${JSON.stringify(run)}`);
assert.equal(run.stopped720, 0, `the stopped pass writes nothing ${JSON.stringify(run)}`);
console.log(`PASS: the graphs stop lv_chart's crowded pass that writes past its buffer ${JSON.stringify(run)}`);
