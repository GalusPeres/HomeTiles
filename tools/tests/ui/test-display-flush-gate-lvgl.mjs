// A full frame on the V2 waited 22-24 ms for the PPA after its 29 bands (b600
// FrameProbe "flush"). With display_flush_gate a band's copy runs on in the
// background while LVGL walks the next band, and LVGL's draw dispatch (and the
// kept-corner drawer, which draws marked tasks itself) waits for the copy
// before writing the first pixel. Real LVGL draws cards, frame lines, discs
// and text in 28-row bands with a copy that reads the band only when it
// completes: the screen must equal the synchronous one, the dispatch must
// have held tasks back, and without the gate the same copy shows the next
// band's pixels (the test catches the fault it guards against).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

// The flush: settle first, background copy only before the frame's last band.
const display = read('src/core/display/display_manager.cpp');
const flush = fn(display, 'DisplayManager::flush_cb');
assert.ok(flush.indexOf('display_flush_gate::settle();') < flush.indexOf('BoardHAL::displayPushPixelsDMA('),
  'a copy still under way completes before anything else reaches the panel');
assert.ok(flush.includes('if (!is_last_flush && display_flush_gate::begin(') &&
  /display_flush_gate::begin\([\s\S]*?lv_display_flush_ready\(lv_disp\);\s*return;/.test(flush),
  'a band before the last is handed back to LVGL at once');
assert.ok(display.includes('lv_display_set_flush_cb(disp, flush_cb);\n  // A board that copies bands') &&
  display.includes('  display_flush_gate::install_dispatch();'), 'the gate goes in front of the draw unit at init');

// The kept-corner drawer waits too, before it draws a marked task itself.
const corners = read('src/ui/shared/rounded_corners.cpp');
const cornerDispatch = fn(corners, 'corner_dispatch');
assert.ok(cornerDispatch.indexOf('if (!display_flush_gate::ready()) {') < cornerDispatch.indexOf('lv_draw_get_available_task('));

// The shared PPA client: rotate() is begin + finish, the DMA2D lease is held
// until the rotation completed.
const ppa = read('src/devices/common/p4_dsi_ui_ppa.cpp');
assert.ok(/Result Client::rotate\([^)]*\)\s*\{\s*const Result started = rotateBegin\([^;]*\);\s*if \(started != Result::Drawn\) return started;\s*return rotateFinish\(presenter\);/.test(ppa),
  'every board keeps the synchronous rotation');
const begin = fn(ppa, 'Client::rotateBegin');
assert.ok(begin.includes('if (pending_.active) rotateFinish(presenter);') && begin.includes('dma2d_arbiter::lock(25)') &&
  !begin.includes('dma2d_arbiter::unlock();\n  pending_') && begin.includes('pending_.active = true;'));
const finish = fn(ppa, 'Client::rotateFinish');
assert.ok(finish.indexOf('presenter.noteUiWrite(') < finish.indexOf('if (lease_held) dma2d_arbiter::unlock();') &&
  finish.includes('restartAfterPpaTimeout('), 'the lease ends after the cache sync; a hung engine restarts as before');

// Only the V2 registers a background copy.
const boards = fs.readdirSync(path.join(root, 'src/devices'), {recursive: true})
  .filter(f => /\.(cpp|h)$/.test(f))
  .filter(f => read(path.join('src/devices', f)).includes('display_flush_gate::set_backend('));
assert.deepEqual(boards.map(f => f.replace(/\\/g, '/')), ['guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp'],
  'one board, the others stay synchronous');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the flush gate pixel run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/core/display/display_flush_gate.h"
#include "src/ui/shared/rounded_corners.h"

extern "C" { extern const lv_font_t ui_font_20; }

static constexpr int W = 760, H = 420, kBand = 28;
static std::vector<uint16_t> g_screen(W * H);
static std::vector<uint16_t> g_band(W * kBand);

// The fake engine: reads the band only when it completes, a few polls and at
// least 300 us later (the gate's wait is timed).
static lv_area_t g_area;
static const uint16_t* g_data = nullptr;
static int g_polls = 0, g_started = 0, g_busy = 0;
static uint32_t g_begin_us = 0;
static void copy(const lv_area_t& a, const uint16_t* src) {
  const int w = a.x2 - a.x1 + 1;
  for (int y = a.y1; y <= a.y2; ++y) memcpy(&g_screen[y * W + a.x1], src + (y - a.y1) * w, w * 2);
}
static bool fake_begin(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  g_area = {x, y, x + w - 1, y + h - 1};
  g_data = data;
  g_polls = 0;
  g_begin_us = display_flush_gate::clock_us();
  ++g_started;
  return true;
}
static bool fake_done() {
  if (++g_polls >= 4 && display_flush_gate::clock_us() - g_begin_us >= 300) return true;
  ++g_busy;
  return false;
}
static void fake_finish() { copy(g_area, g_data); }

// As display_manager.cpp flush_cb.
static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  display_flush_gate::settle();
  const int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  if (!lv_display_flush_is_last(d) &&
      display_flush_gate::begin(a->x1, a->y1, w, h, reinterpret_cast<const uint16_t*>(px))) {
    lv_display_flush_ready(d);
    return;
  }
  copy(*a, reinterpret_cast<const uint16_t*>(px));
  lv_display_flush_ready(d);
}

static void build(bool kept) {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B0D10), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  for (int i = 0; i < 9; ++i) {
    lv_obj_t* card = lv_obj_create(screen);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 13 + (i % 3) * 248, 7 + (i / 3) * 138);
    lv_obj_set_size(card, 236, 126);
    lv_obj_set_style_radius(card, 31, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A + i * 0x0A0B0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(card, 1, 0);
    lv_obj_set_style_outline_pad(card, -1, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(0x2E2E2E), 0);
    lv_obj_set_style_outline_opa(card, LV_OPA_COVER, 0);
    lv_obj_t* disc = lv_obj_create(card);
    lv_obj_remove_style_all(disc);
    lv_obj_set_pos(disc, 12, 13);
    lv_obj_set_size(disc, 64, 64);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0xF0E0D0), 0);
    lv_obj_set_style_bg_opa(disc, 60, 0);
    lv_obj_t* label = lv_label_create(card);
    lv_obj_set_style_text_font(label, &ui_font_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F2F2), 0);
    lv_label_set_text_fmt(label, "Wohnzimmer %d\n%d.%d W", i, 100 + i * 37, i);
    lv_obj_set_pos(label, 84, 30 + (i % 2) * 9);
    if (kept) {
      rounded_corners::attach(card);
      rounded_corners::attach(disc);
    }
  }
  lv_obj_t* old = lv_screen_active();
  lv_screen_load(screen);
  if (old) lv_obj_delete(old);
}

static std::vector<uint16_t> render() {
  std::fill(g_screen.begin(), g_screen.end(), 0xF81F);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  return g_screen;
}

static unsigned diff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
  unsigned n = 0;
  for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
  return n;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_band.data(), nullptr, g_band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);

  // Synchronous: no backend.
  build(false);
  const std::vector<uint16_t> sync_plain = render();

  // The fault the gate prevents: background copies, nothing holds LVGL back.
  display_flush_gate::set_backend({fake_begin, fake_done, fake_finish});
  display_flush_gate::g_dispatch_gated = true;
  const std::vector<uint16_t> ungated = render();
  const unsigned ungated_diffs = diff(sync_plain, ungated);
  const int ungated_started = g_started;

  // The gate in front of the draw unit, kept corners outside it.
  display_flush_gate::g_dispatch_gated = false;
  display_flush_gate::install_dispatch();
  g_started = g_busy = 0;
  display_flush_gate::g_wait_us = 0;
  const uint32_t render_start_us = display_flush_gate::clock_us();
  const std::vector<uint16_t> gated_plain = render();
  const uint32_t render_us = display_flush_gate::clock_us() - render_start_us;
  const int plain_started = g_started, plain_busy = g_busy;
  const uint32_t wait_us = display_flush_gate::g_wait_us;

  display_flush_gate::set_backend({nullptr, nullptr, nullptr});
  build(true);
  const std::vector<uint16_t> sync_kept = render();
  display_flush_gate::set_backend({fake_begin, fake_done, fake_finish});
  g_started = g_busy = 0;
  const std::vector<uint16_t> gated_kept = render();

  printf("{\"gated\":%d,\"plainDiffs\":%u,\"plainStarted\":%d,\"plainBusy\":%d,\"keptDiffs\":%u,"
         "\"keptStarted\":%d,\"keptBusy\":%d,\"ungatedDiffs\":%u,\"ungatedStarted\":%d,\"pending\":%d,"
         "\"cornersDrawn\":%u,\"waitUs\":%u,\"renderUs\":%u,\"waitOpen\":%u}\n",
         display_flush_gate::g_dispatch_gated ? 1 : 0, diff(sync_plain, gated_plain), plain_started, plain_busy,
         diff(sync_kept, gated_kept), g_started, g_busy, ungated_diffs, ungated_started,
         display_flush_gate::g_pending ? 1 : 0, rounded_corners::stats().drawn, static_cast<unsigned>(wait_us),
         static_cast<unsigned>(render_us), static_cast<unsigned>(display_flush_gate::g_wait_since_us));
  return 0;
}
`;

const out = path.join(root, 'build/tests/display-flush-gate');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-w', source,
  path.join(root, 'src/core/display/display_flush_gate.cpp'), path.join(root, 'src/ui/shared/rounded_corners.cpp'),
  host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
const bands = Math.ceil(420 / 28);
assert.equal(run.gated, 1, 'the gate wraps the single software draw unit');
assert.ok(run.ungatedStarted >= bands - 1 && run.ungatedDiffs > 0,
  `without the gate the background copy shows wrong pixels ${JSON.stringify(run)}`);
assert.equal(run.plainStarted, bands - 1, 'every band but the last copied in the background');
assert.ok(run.plainBusy > 0, `LVGL wanted to draw while a copy ran and was held back ${JSON.stringify(run)}`);
assert.equal(run.plainDiffs, 0, `every pixel as with synchronous copies ${JSON.stringify(run)}`);
assert.equal(run.keptStarted, bands - 1);
assert.ok(run.keptBusy > 0 && run.cornersDrawn > 0, `kept corners drawn behind the gate ${JSON.stringify(run)}`);
assert.equal(run.keptDiffs, 0, `kept corners: every pixel as with synchronous copies ${JSON.stringify(run)}`);
assert.equal(run.pending, 0, 'a frame ends with every copy done');
// b603: the dispatch's wait for the copies is timed for the FrameProbe.
assert.ok(run.waitUs >= 300 && run.waitUs <= run.renderUs, `the wait is timed within the frame ${JSON.stringify(run)}`);
assert.equal(run.waitOpen, 0, 'no wait left open after the frame');
console.log(`PASS: band copies run in the background without a wrong pixel ${JSON.stringify(run)}`);
