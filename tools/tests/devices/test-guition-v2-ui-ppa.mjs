import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
// Both revisions execute the same regression against their own production driver.
const isV1 = process.argv.includes('--v1');
const revision = isV1 ? 'V1' : 'V2';
const key = 'guition_jc8012p4a1' + (isV1 ? '' : '_v2');
const namespace = 'DeviceGuitionJC8012P4A1' + (isV1 ? '' : 'V2');
const driverPath = `src/devices/${key}/device_${key}.cpp`;
const driver = fs.readFileSync(path.join(root, driverPath), 'utf8');
const definitions = cppFunctionDefinitions(driver);
const getFunction = name => {
  const found = definitions.find(f => f.name === name);
  assert.ok(found, `${name} is missing`);
  return found.source;
};
const draw = getFunction('draw_landscape_area');
assert.match(draw, /g_ui_ppa\.rotate\(/, `${revision} ordinary UI must reach the PPA path`);
for (const entry of ['displayPushPixels', 'displayPushPixelsDMA']) {
  assert.match(getFunction(`${namespace}::${entry}`),
    /draw_landscape_area\(/, `${entry} must use the accelerated path`);
}
assert.match(getFunction('init_display'), /g_ui_ppa\.init\(/);
assert.match(getFunction(`${namespace}::displayTryFullFramePreview`),
  /g_ui_ppa\.runtime\(note_ppa_fault\)/, 'Camera and UI must share the PPA client and cooldown');
assert.match(getFunction(`${namespace}::ppaCooldownActive`),
  /g_ui_ppa\.cooldownActive\(/);
const dispatch = fs.readFileSync(path.join(root, 'src/devices/device.cpp'), 'utf8');
assert.match(dispatch, new RegExp(`defined\\(DEVICE_${key.toUpperCase()}\\)[\\s\\S]{0,100}return DeviceImpl::ppaCooldownActive\\(\\)`));

const cxx = ['clang++', 'g++'].find(name => spawnSync(name, ['--version']).status === 0);
if (!cxx) {
  console.log(`SKIP: Guition ${revision} PPA runtime test requires a host C++ compiler`);
  process.exit(0);
}
const out = path.join(root, `build/tests/guition-${revision.toLowerCase()}-ui-ppa`);
fs.mkdirSync(out, {recursive: true});
const mocks = String.raw`
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
#define CONFIG_IDF_TARGET_ESP32P4 1
#define IRAM_ATTR
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int PPA_OPERATION_SRM = 1;
constexpr int PPA_SRM_COLOR_MODE_RGB565 = 2;
constexpr int PPA_SRM_ROTATION_ANGLE_90 = 90;
constexpr int PPA_SRM_ROTATION_ANGLE_270 = 270;
constexpr int PPA_TRANS_MODE_NON_BLOCKING = 3;
constexpr int ESP_CACHE_MSYNC_FLAG_DIR_C2M = 1;
constexpr int ESP_CACHE_MSYNC_FLAG_TYPE_DATA = 2;
constexpr int pdTRUE = 1, pdFALSE = 0;
using BaseType_t = int;
using SemaphoreHandle_t = void*;
using ppa_client_handle_t = void*;
using esp_lcd_panel_handle_t = void*;
struct ppa_event_data_t {};
struct ppa_client_config_t { int oper_type; };
struct ppa_event_callbacks_t {
  bool (*on_trans_done)(ppa_client_handle_t, ppa_event_data_t*, void*);
};
struct ppa_srm_oper_config_t {
  struct In {const void* buffer; int pic_w, pic_h, block_w, block_h;
    int block_offset_x, block_offset_y, srm_cm;} in;
  struct Out {void* buffer; size_t buffer_size; int pic_w, pic_h;
    int block_offset_x, block_offset_y, srm_cm;} out;
  int rotation_angle; float scale_x, scale_y; bool rgb_swap, byte_swap;
  int mode; void* user_data;
};
struct Logger {template<class... T> void printf(const char*, T...) {}};
inline Logger Serial;
inline uint32_t now_ms = 100;
inline int submitted = 0, registered = 0, unregistered = 0, creates = 0;
inline int submit_error = 0, register_error = 0, unregister_error = 0;
inline int callback_error = 0, cache_error = 0, ready_signal = 0;
inline bool create_fail = false, arbiter_available = true, lease_held = false;
inline bool finish_dma = true, finish_in_grace = false;
inline int waited = 0;
inline ppa_srm_oper_config_t last_operation{};
inline ppa_event_callbacks_t saved_callbacks{};
inline uint32_t millis() { return now_ms; }
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline SemaphoreHandle_t xSemaphoreCreateBinary() {
  ++creates; return create_fail ? nullptr : reinterpret_cast<void*>(10);
}
inline int xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t* woken) {
  assert(sem); ready_signal = 1; *woken = pdTRUE; return pdTRUE;
}
inline int xSemaphoreTake(SemaphoreHandle_t sem, uint32_t timeout) {
  assert(sem);
  if (timeout) waited += timeout;
  if (timeout == 800 && finish_in_grace) ready_signal = 1;
  int result = ready_signal; ready_signal = 0; return result;
}
inline int ppa_register_client(const ppa_client_config_t* cfg, ppa_client_handle_t* out) {
  assert(cfg->oper_type == PPA_OPERATION_SRM); ++registered;
  *out = register_error ? nullptr : reinterpret_cast<void*>(1);
  return register_error;
}
inline int ppa_unregister_client(ppa_client_handle_t handle) {
  assert(handle); ++unregistered; return unregister_error;
}
inline int ppa_client_register_event_callbacks(ppa_client_handle_t,
                                               const ppa_event_callbacks_t* callbacks) {
  saved_callbacks = *callbacks; return callback_error;
}
inline int esp_cache_msync(void*, size_t size, int flags) {
  assert(size && flags == (ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA));
  return cache_error;
}
inline int ppa_do_scale_rotate_mirror(ppa_client_handle_t handle, const ppa_srm_oper_config_t* op) {
  assert(handle && lease_held); ++submitted; last_operation = *op;
  assert(op->mode == PPA_TRANS_MODE_NON_BLOCKING);
  assert(op->scale_x == 1 && op->scale_y == 1 && !op->rgb_swap && !op->byte_swap);
  if (submit_error) return submit_error;
  const auto* src = static_cast<const uint16_t*>(op->in.buffer);
  auto* dst = static_cast<uint16_t*>(op->out.buffer);
  for (int sy = 0; sy < op->in.block_h; ++sy) {
    for (int sx = 0; sx < op->in.block_w; ++sx) {
      const int dx = op->rotation_angle == 90 ? sy : op->in.block_h - 1 - sy;
      const int dy = op->rotation_angle == 90 ? op->in.block_w - 1 - sx : sx;
      const size_t index = (op->out.block_offset_y + dy) * op->out.pic_w + op->out.block_offset_x + dx;
      assert((index + 1) * sizeof(uint16_t) <= op->out.buffer_size);
      dst[index] = src[sy * op->in.pic_w + sx];
    }
  }
  if (finish_dma) saved_callbacks.on_trans_done(handle, nullptr, op->user_data);
  return ESP_OK;
}
`;
fs.writeFileSync(path.join(out, 'mocks.h'), mocks);
for (const name of ['sdkconfig.h', 'Arduino.h', 'esp_cache.h', 'driver/ppa.h',
  'esp_lcd_panel_ops.h', 'freertos/FreeRTOS.h', 'freertos/semphr.h']) {
  const file = path.join(out, name);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, '#pragma once\n#include "mocks.h"\n');
}
const cpp = String.raw`
#include "mocks.h"
#include <algorithm>
#include <vector>
#include "src/devices/common/p4_dsi_ui_ppa.h"
namespace dma2d_arbiter {
bool lock(uint32_t timeout) {
  assert(timeout == 25 && !lease_held);
  lease_held = arbiter_available; return lease_held;
}
void unlock() {assert(lease_held); lease_held = false;}
}
inline std::vector<uint16_t> framebuffer(800 * 1280);
inline bool framebuffer_ready = true, mirror_ok = true;
inline int mirror_writes = 0, cpu_writes = 0, dirty_marks = 0;
struct Timeout {};
namespace p4_dsi_camera_presenter {
uint16_t* Presenter::activeFramebuffer() const {
  return framebuffer_ready ? framebuffer.data() : nullptr;
}
bool Presenter::noteUiWrite(int32_t x, int32_t y, int32_t w, int32_t h, bool ppa) {
  assert(ppa && x >= 0 && y >= 0 && x + w <= 800 && y + h <= 1280);
  ++mirror_writes; return mirror_ok;
}
[[noreturn]] void restartAfterPpaTimeout(const char*, const char*, int32_t,
    int32_t, int32_t, int32_t, int32_t, uint8_t, uint32_t ms) {
  assert(lease_held && ms == 1000); throw Timeout{};
}
}
struct { int width = 800, height = 1280; } display_cfg;
p4_dsi_ui_ppa::Client g_ui_ppa("Guition JC8012P4A1 ${revision}");
p4_dsi_camera_presenter::Presenter g_camera_presenter;
uint8_t g_rotation = 0;
std::vector<uint16_t> rotate_buffer;
uint16_t* g_rotate_buf = nullptr;
bool ensure_rotate_buffer(size_t pixels) {
  rotate_buffer.resize(pixels); g_rotate_buf = rotate_buffer.data(); return true;
}
bool write_physical_to_panel(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  ++cpu_writes;
  for (int row = 0; row < h; ++row)
    std::copy_n(data + row * w, w, framebuffer.data() + (y + row) * 800 + x);
  return true;
}
bool draw_physical(int32_t, int32_t, int32_t, int32_t, const uint16_t*) {
  assert(false); return false;
}
void mark_dirty_rect(int32_t, int32_t, int32_t, int32_t) {++dirty_marks;}
` + draw + String.raw`
void defaults() {
  now_ms = 100; submitted = registered = unregistered = creates = 0;
  submit_error = register_error = unregister_error = callback_error = cache_error = 0;
  ready_signal = waited = 0; create_fail = false; arbiter_available = true;
  lease_held = false; finish_dma = true; finish_in_grace = false;
  framebuffer_ready = mirror_ok = true; mirror_writes = cpu_writes = dirty_marks = 0;
  g_ui_ppa = p4_dsi_ui_ppa::Client("test"); g_ui_ppa.init();
}
std::vector<uint16_t> input(1280 * 800);
void check_draw(int x, int y, int w, int h, bool ppa) {
  std::fill(framebuffer.begin(), framebuffer.end(), 0xffff);
  const int before = submitted, cpu_before = cpu_writes;
  assert(draw_landscape_area(x, y, w, h, input.data()));
  assert(submitted == before + (ppa ? 1 : 0));
  assert(cpu_writes == cpu_before + (ppa ? 0 : 1));
  assert(!lease_held);
  std::vector<uint16_t> expected(800 * 1280, 0xffff);
  for (int row = 0; row < h; ++row) for (int col = 0; col < w; ++col) {
    const int logical_x = x + col, logical_y = y + row;
    const int px = g_rotation == 2 ? logical_y : 799 - logical_y;
    const int py = g_rotation == 2 ? 1279 - logical_x : logical_x;
    expected[py * 800 + px] = input[row * w + col];
  }
  assert(framebuffer == expected);
}
int main() {
  for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint16_t>(i % 65534);
  defaults();
  for (auto rotation : {0, 2}) {
    g_rotation = rotation;
    check_draw(0, 0, 1280, 28, true);
    check_draw(244, 128, 792, 28, true);
    check_draw(260, 210, 752, 8, true);
    check_draw(100, 300, 600, 8, true);
    check_draw(100, 300, 599, 8, false);
    check_draw(260, 210, 752, 3, false);
    check_draw(100, 300, 536, 28, false);
    check_draw(5, 5, 168, 145, false);
  }
  int before = submitted;
  assert(!draw_landscape_area(-1, 0, 1280, 28, input.data()));
  assert(!draw_landscape_area(1, 0, 1280, 28, input.data()));
  assert(!draw_landscape_area(0, 0, 1280, 28, nullptr));
  assert(submitted == before);

  defaults(); arbiter_available = false;
  check_draw(0, 0, 1280, 28, false);
  assert(!lease_held);
  arbiter_available = true;
  check_draw(0, 0, 1280, 28, true);
  framebuffer_ready = false;
  check_draw(0, 0, 1280, 28, false);

  defaults(); g_ui_ppa.pauseFor(1200);
  check_draw(0, 0, 1280, 28, false);
  now_ms += 1200;
  check_draw(0, 0, 1280, 28, true);
  defaults(); submit_error = 259;
  assert(draw_landscape_area(0, 0, 1280, 28, input.data()));
  assert(submitted == 1 && cpu_writes == 1 && g_ui_ppa.cooldownActive());
  check_draw(0, 0, 1280, 28, false);
  now_ms += 1200;
  assert(draw_landscape_area(0, 0, 1280, 28, input.data()));
  assert(registered == 2 && unregistered == 1);
  submit_error = 0; now_ms += 1200;
  check_draw(0, 0, 1280, 28, true);

  defaults(); unregister_error = 259;
  const auto old_handle = g_ui_ppa.runtime(nullptr).handle;
  g_ui_ppa.noteFault(); g_ui_ppa.noteFault();
  assert(g_ui_ppa.runtime(nullptr).handle == old_handle);
  assert(g_ui_ppa.runtime(nullptr).reset_pending && registered == 1);
  unregister_error = 0; now_ms += 1200;
  check_draw(0, 0, 1280, 28, true);
  assert(registered == 2 && creates == 1);

  for (int failure = 0; failure < 3; ++failure) {
    defaults();
    g_ui_ppa = p4_dsi_ui_ppa::Client("initial-failure");
    if (failure == 0) register_error = 257;
    if (failure == 1) callback_error = 258;
    if (failure == 2) create_fail = true;
    g_ui_ppa.init();
    check_draw(0, 0, 1280, 28, false);
    register_error = callback_error = 0; create_fail = false; now_ms += 1200;
    check_draw(0, 0, 1280, 28, true);
  }

  defaults(); finish_dma = false; finish_in_grace = true;
  check_draw(0, 0, 1280, 28, true);
  assert(waited == 1000);
  defaults(); finish_dma = false;
  try {draw_landscape_area(0, 0, 1280, 28, input.data()); assert(false);}
  catch (Timeout&) {}
  assert(lease_held && cpu_writes == 0 && mirror_writes == 0);

  defaults(); mirror_ok = false;
  assert(!draw_landscape_area(0, 0, 1280, 28, input.data()));
  assert(submitted == 1 && cpu_writes == 0 && !lease_held && dirty_marks == 0);
  defaults(); cache_error = 258;
  check_draw(0, 0, 1280, 28, false);
  assert(g_ui_ppa.cooldownActive());
}
`;
const file = path.join(out, 'test.cpp');
fs.writeFileSync(file, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(cxx, ['-std=c++17', '-O1', '-Wall', '-Wextra', '-I', out, '-I', root,
  file, path.join(root, 'src/devices/common/p4_dsi_ui_ppa.cpp'), '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log(`Guition ${revision} UI PPA: real draw path, both rotations, pixel bounds, thin/narrow guards, DMA ownership, cache errors and client recovery: PASS`);
