// UI frame swap: LVGL draws each frame into the hidden framebuffer and the
// panel switches to it at a frame boundary, so a redraw (a song change, a
// tile recolor) never appears band by band on the Guition V2 and Guition S3.
// The harness runs the production P4 presenter against a small simulated DPI
// panel: the shown framebuffer is never written, every shown picture is a
// complete frame, LVGL's sync areas keep both framebuffers equal, and the
// camera path and a missing panel refresh fall back to direct drawing.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, `${name} is missing`);
  return found.source;
};

// Only the Guition V2 and the Guition S3 swap frames; every other board keeps
// drawing directly.
const manager = read('src/core/display/display_manager.cpp');
assert.match(manager, /#if defined\(DEVICE_GUITION_JC8012P4A1_V2\) \|\| \\\n    defined\(DEVICE_GUITION_ESP32_4848S040\)\n#define HOMETILES_UI_FRAME_SWAP 1/);
assert.match(manager, /#if defined\(HOMETILES_UI_FRAME_SWAP\)\n  lv_display_set_sync_cb\(disp, sync_cb\);/);
assert.match(fn(manager, 'DisplayManager::sync_cb'),
  /DeviceImpl::displaySyncArea\(area->x1, area->y1, area->x2 - area->x1 \+ 1,\s*area->y2 - area->y1 \+ 1\);\s*lv_display_sync_ready\(lv_disp\);/);
assert.match(fn(manager, 'commit_display_if_last'),
  /defined\(DEVICE_GUITION_JC8012P4A1_V2\)\s*if \(lv_display_flush_is_last\(lv_disp\)\) \{\s*DeviceImpl::displayCommit\(\);/);

const v2 = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
assert.match(v2, /constexpr bool kUiFrameSwap = true;/);
assert.match(fn(v2, 'init_display'), /if \(kUiFrameSwap && g_panel_fb_ready\) g_camera_presenter\.enableUiFrameSwap\(\);/);
assert.match(fn(v2, 'panel_fb'), /g_camera_presenter\.uiFramebuffer\(\)/);
assert.match(fn(v2, 'DeviceGuitionJC8012P4A1V2::displayCommit'), /g_camera_presenter\.commitUi\(\);/);
// The sync area uses the same landscape-to-panel mapping as the drawing.
const draw = fn(v2, 'draw_landscape_area');
const sync = fn(v2, 'DeviceGuitionJC8012P4A1V2::displaySyncArea');
for (const mapping of ['dst_x = y;', 'dst_y = logical_w - x - w;', 'dst_x = logical_h - y - h;', 'dst_y = x;']) {
  assert.ok(draw.includes(mapping) && sync.includes(mapping), mapping);
}
assert.match(sync, /g_camera_presenter\.syncUiArea\(dst_x, dst_y, h, w\);/);
assert.match(read('src/devices/common/p4_dsi_ui_ppa.cpp'), /uint16_t\* framebuffer = presenter\.uiFramebuffer\(\);/);
for (const other of ['guition_jc8012p4a1/device_guition_jc8012p4a1.cpp',
  'waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.cpp',
  'guition_jc1060p470c/device_guition_jc1060p470c.cpp']) {
  assert.doesNotMatch(read(`src/devices/${other}`), /enableUiFrameSwap/, `${other} keeps direct drawing`);
}

const s3 = read('src/devices/guition_esp32_4848s040/device_guition_esp32_4848s040.cpp');
assert.match(s3, /constexpr bool kUiFrameSwap = true;/);
assert.match(s3, /if \(kUiFrameSwap\) g_gfx->enableFrameSwap\(\);/);
assert.match(fn(s3, 'DeviceGuitionESP324848S040::displayPushPixels'), /g_gfx->openFrame\(\);\s*g_gfx->draw16bitRGBBitmap\(/);
assert.match(fn(s3, 'DeviceGuitionESP324848S040::displayWaitDisplay'),
  /if \(g_gfx->frameSwapEnabled\(\)\) \{\s*g_gfx->commitFrame\(\);\s*\} else \{\s*g_gfx->commitAtomicFrame\(\);/);
assert.match(fn(s3, 'DeviceGuitionESP324848S040::displayBeginAtomicFrame'), /if \(g_gfx && g_gfx->frameSwapEnabled\(\)\) return false;/);
assert.match(fn(s3, 'DeviceGuitionESP324848S040::displayFillScreen'), /g_gfx->fillBoth\(color\);/);
// The old framebuffer is written again only after two frame ends counted
// from after the switch (IDF may prefetch one more old frame).
assert.match(s3, /swap_eof_start_ = frame_complete_count_;\n    active_index_ = pending_index_;/);
assert.match(s3, /waitForFrameCompletions\(swap_eof_start_, 2,/);
assert.ok(s3.indexOf('const esp_err_t err = esp_lcd_panel_draw_bitmap(\n        panel_handle_, 0, 0, _fb_width, _fb_height,\n        framebuffers_[pending_index_]);') <
  s3.indexOf('swap_eof_start_ = frame_complete_count_;'), 'frame ends are counted after the switch');
// Storage makes fb0 canonical only from a complete, shown frame.
assert.match(s3, /bool canonicalizeForStorage\(\) \{\n    if \(!panel_handle_\) return false;\n(?:    \/\/.*\n)*    commitFrame\(\);\n    finishSwap\(\);\n    storage_transition_ = true;/);

const mocks = String.raw`
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#define CONFIG_IDF_TARGET_ESP32P4 1
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr int ESP_CACHE_MSYNC_FLAG_TYPE_DATA = 1;
constexpr int ESP_CACHE_MSYNC_FLAG_DIR_C2M = 2;
constexpr int ESP_CACHE_MSYNC_FLAG_DIR_M2C = 4;
constexpr int ESP_CACHE_MSYNC_FLAG_INVALIDATE = 8;
esp_err_t esp_cache_msync(void* addr, size_t size, int flags);
using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
struct MockSem { int count = 0; };
using SemaphoreHandle_t = MockSem*;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
inline void vTaskDelay(TickType_t) {}
using ppa_client_handle_t = void*;
enum ppa_srm_rotation_angle_t {
  PPA_SRM_ROTATION_ANGLE_0 = 0, PPA_SRM_ROTATION_ANGLE_90 = 90,
  PPA_SRM_ROTATION_ANGLE_180 = 180, PPA_SRM_ROTATION_ANGLE_270 = 270
};
constexpr int PPA_SRM_COLOR_MODE_RGB565 = 1;
constexpr int PPA_TRANS_MODE_NON_BLOCKING = 1;
struct ppa_srm_oper_config_t {
  struct { const void* buffer; uint32_t pic_w, pic_h, block_w, block_h, block_offset_x, block_offset_y; int srm_cm; } in;
  struct { void* buffer; size_t buffer_size; uint32_t pic_w, pic_h, block_offset_x, block_offset_y; int srm_cm; } out;
  ppa_srm_rotation_angle_t rotation_angle; float scale_x, scale_y; bool rgb_swap, byte_swap; int mode; void* user_data;
};
esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t, const ppa_srm_oper_config_t*);
struct MockPanel {};
using esp_lcd_panel_handle_t = MockPanel*;
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void* data);
uint32_t millis();
uint32_t micros();
struct MockSerial {
  const char* last = "";
  template <class... T> void printf(const char* format, T...) { last = format; }
  void println(const char* text) { last = text; }
};
extern MockSerial Serial;
class String {
 public:
  String() = default;
  String(const char* text) : s_(text) {}
  template <class T> explicit String(T value) : s_(std::to_string(value)) {}
  void reserve(size_t n) { s_.reserve(n); }
  String& operator+=(const char* text) { s_ += text; return *this; }
  String& operator+=(const String& other) { s_ += other.s_; return *this; }
 private:
  std::string s_;
};
`;

const files = {'mocks.h': mocks};
for (const name of ['sdkconfig.h', 'Arduino.h', 'esp_cache.h', 'driver/ppa.h', 'esp_lcd_panel_ops.h',
  'freertos/FreeRTOS.h', 'freertos/semphr.h', 'freertos/task.h']) {
  files[name] = '#pragma once\n#include "mocks.h"\n';
}
files['src/core/hardware/board_hal.h'] = '#pragma once\nnamespace BoardHAL { void restart(); }\n';
files['src/core/diagnostics/crash_log.h'] =
  '#pragma once\n#include "mocks.h"\nnamespace CrashLog { void appendDisplayPipelineTimeoutReport(const String&); }\n';

const harness = String.raw`
#include <cstdio>
#include <cstdlib>
#include "src/devices/common/p4_dsi_camera_presenter.h"
#include "src/core/display/dma2d_arbiter.h"

// Rows of 32 bytes and framebuffers on 64-byte cache lines, like the panel.
constexpr int W = 16, H = 8;
alignas(64) uint16_t fb0[W * H] = {};
alignas(64) uint16_t fb1[W * H] = {};
uint16_t* const fb[2] = {fb0, fb1};
int cur_fb = 0;      // the framebuffer the driver scans from the next frame on
int scanned = 0;     // the framebuffer the panel reads right now
bool auto_refresh = true;
bool frame_swap = true;
int draws = 0;
uint16_t model[W * H] = {};  // what LVGL believes the screen shows
MockSem refresh_sem;
MockSem ppa_sem;
MockPanel panel;
MockSerial Serial;
uint32_t millis() { return 1000; }
uint32_t micros() { return 0; }
namespace dma2d_arbiter { bool lock(uint32_t) { return true; } void unlock() {} }
namespace BoardHAL { void restart() { std::puts("FAIL restart"); std::exit(1); } }
namespace CrashLog { void appendDisplayPipelineTimeoutReport(const String&) {} }

const char* step = "init";
void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s (%s)\n", what, step); std::exit(1); }
}
bool in_fb(const void* p, int i) {
  const auto* a = reinterpret_cast<const uint8_t*>(p);
  const auto* b = reinterpret_cast<const uint8_t*>(fb[i]);
  return a >= b && a < b + sizeof(fb0);
}
void panel_refresh() { scanned = cur_fb; refresh_sem.count = 1; }
esp_err_t esp_cache_msync(void* addr, size_t, int flags) {
  // A CPU write back into the framebuffer the panel reads would tear.
  if (frame_swap && (flags & ESP_CACHE_MSYNC_FLAG_DIR_C2M)) check(!in_fb(addr, scanned), "no write into the shown framebuffer");
  return ESP_OK;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
  if (sem->count) { sem->count = 0; return pdTRUE; }
  if (!ticks) return pdFALSE;
  if (sem == &refresh_sem && auto_refresh) { panel_refresh(); sem->count = 0; return pdTRUE; }
  return pdFALSE;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void* data) {
  ++draws;
  if (in_fb(data, 0)) cur_fb = 0; else if (in_fb(data, 1)) cur_fb = 1; else return -1;
  return ESP_OK;
}
esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t, const ppa_srm_oper_config_t* op) {
  const auto* src = static_cast<const uint16_t*>(op->in.buffer);
  auto* dst = static_cast<uint16_t*>(op->out.buffer);
  for (uint32_t y = 0; y < op->in.block_h; ++y)
    for (uint32_t x = 0; x < op->in.block_w; ++x)
      dst[(op->out.block_offset_y + y) * op->out.pic_w + op->out.block_offset_x + x] = src[y * op->in.pic_w + x];
  static_cast<MockSem*>(op->user_data)->count = 1;
  return ESP_OK;
}

p4_dsi_camera_presenter::Presenter presenter;

// One LVGL band: written where the presenter says, then reported.
void draw(int x, int y, int w, int h, uint16_t color) {
  uint16_t* target = presenter.uiFramebuffer();
  check(target != nullptr, "target");
  if (frame_swap) check(target != fb[scanned], "bands never go to the shown framebuffer");
  for (int row = y; row < y + h; ++row)
    for (int col = x; col < x + w; ++col) { target[row * W + col] = color; model[row * W + col] = color; }
  check(presenter.noteUiWrite(x, y, w, h, false), "noteUiWrite");
}
bool shown_is_model() { return std::memcmp(fb[scanned], model, sizeof(model)) == 0; }

int main() {
  const p4_dsi_camera_presenter::Config config{W, H, p4_dsi_camera_presenter::Transform::Native0Or180, 200, 800, "Test"};
  check(presenter.init(config, &panel, &refresh_sem, fb[0], fb[1]), "init");
  presenter.enableUiFrameSwap();

  step = "frame 1";
  // Frame 1: the panel keeps the old picture until the frame is committed
  // and a refresh took the new framebuffer.
  draw(0, 0, 4, 2, 1);
  draw(0, 2, 4, 2, 1);
  check(draws == 0 && scanned == 0, "nothing shown while drawing");
  check(presenter.commitUi(), "commit 1");
  check(draws == 1 && scanned == 0, "old picture until the refresh");
  panel_refresh();
  check(scanned == 1 && shown_is_model(), "frame 1 shown whole");

  step = "frame 2";
  // Frame 2: LVGL first syncs the area of frame 1 it does not redraw.
  presenter.syncUiArea(0, 0, 4, 4);
  draw(5, 1, 3, 3, 2);
  check(presenter.commitUi(), "commit 2");
  panel_refresh();
  check(scanned == 0 && shown_is_model(), "frame 2 complete after the sync");

  step = "frame 3";
  // Frame 3 starts right after the commit: the first band waits for the
  // refresh instead of writing into the still shown framebuffer.
  presenter.syncUiArea(5, 1, 3, 3);
  draw(2, 4, 3, 2, 3);
  presenter.commitUi();
  const int shown_before = scanned;
  presenter.syncUiArea(2, 4, 3, 2);
  draw(0, 5, 1, 1, 4);
  check(scanned != shown_before, "waited for the refresh");
  presenter.commitUi();
  panel_refresh();
  check(shown_is_model(), "frame 4 complete");

  step = "empty commit";
  // A commit without new bands switches nothing.
  const int draws_before = draws;
  check(presenter.commitUi() && draws == draws_before, "empty commit");

  step = "fill";
  // A fill LVGL does not know about is copied completely afterwards.
  presenter.syncUiArea(0, 5, 1, 1);
  uint16_t* target = presenter.uiFramebuffer();
  for (int i = 0; i < W * H; ++i) { target[i] = 7; model[i] = 7; }
  presenter.noteUiWrite(0, 0, W, H, false);
  presenter.syncAllAfterCommit();
  presenter.commitUi();
  panel_refresh();
  target = presenter.uiFramebuffer();
  check(std::memcmp(target, fb[scanned], sizeof(model)) == 0, "full sync after an unknown fill");

  step = "camera";
  // The camera path takes over (UI frames shown first), the UI then draws
  // into the shown framebuffer as before, and frame swap resumes from a
  // complete copy once the camera ends.
  draw(1, 1, 2, 2, 5);
  alignas(64) static uint16_t camera[W * H];
  for (auto& pixel : camera) pixel = 9;
  const p4_dsi_camera_presenter::PpaRuntime runtime{reinterpret_cast<void*>(1), &ppa_sem, true, false, false, nullptr, nullptr};
  check(presenter.present(0, 0, W, 3, W, camera, sizeof(camera), false, 0, runtime), "camera frame");
  panel_refresh();
  for (int i = 0; i < W * 3; ++i) model[i] = 9;
  check(shown_is_model(), "camera frame over the committed UI frame");
  frame_swap = false;
  check(presenter.uiFramebuffer() == presenter.activeFramebuffer(), "camera: UI draws into the shown framebuffer");
  presenter.end();
  frame_swap = true;
  target = presenter.uiFramebuffer();
  check(target != fb[scanned] && std::memcmp(target, fb[scanned], sizeof(model)) == 0, "camera end: complete copy");

  step = "fallback";
  // Without a panel refresh after a swap the frame swap turns itself off.
  draw(0, 0, 1, 1, 6);
  presenter.commitUi();
  auto_refresh = false;
  frame_swap = false;
  uint16_t* direct = presenter.uiFramebuffer();
  check(direct == presenter.activeFramebuffer() && direct == fb[cur_fb], "fallback draws into the driver's framebuffer");
  check(std::strstr(Serial.last, "UI frame swap disabled") != nullptr, "fallback is logged");
  std::puts("ok");
  return 0;
}
`;

const output = compileAndRun({
  label: 'UI frame swap presenter',
  harness,
  files,
  sources: ['src/devices/common/p4_dsi_camera_presenter.cpp'],
});
if (output !== null) {
  assert.equal(output.trim(), 'ok');
  console.log('UI frame swap: V2/S3 wiring, whole frames, sync areas, camera handover and refresh fallback pass.');
}
