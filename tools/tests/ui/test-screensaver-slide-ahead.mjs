// The screensaver decodes the next slide ahead on a worker (user log b635:
// reading, decoding and fitting each slide inside the slide change stood the
// loop about 550 ms and froze the Media tile's marquee and bars). Compiles
// the real worker and its LVGL-side handling (image_screensaver.cpp) on the
// host with one-slot queues: the slide change takes the decoded slide, a
// changed picture setting decodes again, a closed screensaver frees what the
// worker still finishes, and a lost job never holds the slideshow back.
import assert from 'node:assert/strict';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const screensaver = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const fn = (name) => {
  const found = cppFunctionDefinitions(screensaver).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const slice = (from, to) => {
  const start = screensaver.indexOf(from);
  const end = screensaver.indexOf(to, start);
  assert.ok(start >= 0 && end > start, from);
  return screensaver.slice(start, end + to.length);
};

// ---- Wiring -------------------------------------------------------------------
assert.match(fn('get_or_decode_cached'), /lv_image_dsc_t\* dsc = picture \? nullptr : ahead_take\(wallpaper, w, h\);\n  if \(!dsc\) \{/,
  'the slide change takes a slide decoded ahead before decoding it itself');
assert.match(fn('apply_wallpaper'), /present_picture\(st, dsc, wallpaper\.file_name, cache_hit\);[\s\S]*const int next = next_enabled_wallpaper\(index\);\n  if \(next >= 0 && next != index\) ahead_request\(/,
  'each shown slide asks for the next one');
assert.match(fn('global_screensaver_timer_cb'), /ahead_decoding\(config\.wallpapers\[static_cast<size_t>\(next\)\]\)\) \{\s*st->next_wallpaper_ms = now \+ 200U;\s*return;/,
  'a slide still being decoded comes a little later instead of a second decode');
assert.ok(fn('hide_image_screensaver').includes('ahead_release();'), 'closing frees the slide decoded ahead');
// No LVGL on the worker: the decode chain only allocates with malloc and
// waits for the 2D DMA arbiter.
for (const name of ['ahead_worker_task', 'decode_wallpaper_to_size', 'decode_jpeg_to_size', 'read_wallpaper_file',
  'hw_decode_jpeg', 'sw_decode_jpeg', 's3_decode_jpeg_direct_cover', 'finish_cover_in_place', 'make_cover_dsc',
  'free_unshown_dsc']) {
  assert.doesNotMatch(fn(name), /\blv_[a-z_]+\(/, `${name} runs on the worker and calls no LVGL function`);
}

// ---- Real code, one-slot queues ------------------------------------------------
const harness = String.raw`
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  unsigned length() const { return static_cast<unsigned>(size()); }
};
struct lv_image_dsc_t { const uint8_t* data = nullptr; };
struct ScreensaverWallpaperConfig { String file_name; uint16_t focus_x = 500, focus_y = 500, zoom = 1000; };
uint32_t now_ms = 1000;
uint32_t millis() { return now_ms; }
uint16_t radius = 16;
uint16_t image_radius() { return radius; }
namespace grid_layout { int screen_w() { return 1280; } int screen_h() { return 800; } }
// FreeRTOS: queues of one item; the worker's blocking wait ends the step.
struct Queue { size_t size; bool full = false; std::vector<uint8_t> item; };
using QueueHandle_t = Queue*;
using TaskHandle_t = void*;
using BaseType_t = int;
constexpr int pdTRUE = 1, pdPASS = 1, MALLOC_CAP_SPIRAM = 0;
constexpr uint32_t portMAX_DELAY = 0xFFFFFFFF;
QueueHandle_t xQueueCreate(int, size_t size) { auto* q = new Queue{size, false, {}}; q->item.resize(size); return q; }
int xQueueSend(QueueHandle_t q, const void* item, uint32_t) {
  if (q->full) return 0;
  memcpy(q->item.data(), item, q->size);
  q->full = true;
  return pdTRUE;
}
struct WorkerIdle {};
int xQueueReceive(QueueHandle_t q, void* out, uint32_t wait) {
  if (!q->full) { if (wait == portMAX_DELAY) throw WorkerIdle{}; return 0; }
  memcpy(out, q->item.data(), q->size);
  q->full = false;
  return pdTRUE;
}
int tasks = 0;
int xTaskCreateWithCaps(void (*)(void*), const char*, int, void*, int, TaskHandle_t* handle, int) {
  ++tasks;
  *handle = &tasks;
  return pdPASS;
}
// The decode: a picture named after the file, counted.
int decodes = 0, live = 0;
std::set<void*> pictures;
void tracked_free(void* p) { if (pictures.erase(p)) --live; std::free(p); }
lv_image_dsc_t* decode_wallpaper_to_size(const String& name, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t) {
  ++decodes;
  if (name == "broken.jpg") return nullptr;
  auto* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  dsc->data = static_cast<uint8_t*>(malloc(16));
  pictures.insert(dsc);
  ++live;
  return dsc;
}
#define free(p) tracked_free((void*)(p))
${slice('struct AheadJob {', 'constexpr uint32_t kAheadMaxWaitMs = 5000;')}
${fn('free_unshown_dsc')}
${fn('ahead_worker_task')}
${fn('ensure_ahead_worker')}
${fn('ahead_job_for')}
${fn('ahead_collect')}
${fn('ahead_request')}
${fn('ahead_take')}
${fn('ahead_decoding')}
${fn('ahead_release')}
#undef free
// One worker step: it decodes what waits and answers.
void worker_step() { try { ahead_worker_task(nullptr); } catch (const WorkerIdle&) {} }
void drop(lv_image_dsc_t* dsc) { if (dsc) { tracked_free((void*)dsc->data); tracked_free(dsc); } }

int main() {
  ScreensaverWallpaperConfig a{"a.jpg"}, b{"b.jpg"};
  // Slide a shows and asks for b; until the worker answers, b is decoding.
  ahead_request(b);
  std::printf("asked %d %d\n", ahead_decoding(b) ? 1 : 0, ahead_take(b, 1280, 800) ? 1 : 0);
  worker_step();
  // The slide change takes b: no second decode.
  lv_image_dsc_t* shown = ahead_take(b, 1280, 800);
  std::printf("taken %d %d %d\n", shown ? 1 : 0, decodes, ahead_decoding(b) ? 1 : 0);
  drop(shown);
  // Asked again for the same slide while a result waits: no new job.
  ahead_request(a);
  worker_step();
  ahead_request(a);
  worker_step();
  std::printf("once %d\n", decodes);
  // A changed corner radius or focus: the waiting slide is not taken.
  radius = 20;
  std::printf("changed %d\n", ahead_take(a, 1280, 800) ? 1 : 0);
  radius = 16;
  ScreensaverWallpaperConfig moved = a;
  moved.focus_x = 300;
  std::printf("moved %d\n", ahead_take(moved, 1280, 800) ? 1 : 0);
  // Closing frees the waiting slide; one the worker finishes later is freed
  // by the worker, and the next opening asks again.
  ahead_release();
  std::printf("released %d\n", live);
  ahead_request(b);
  ahead_release();
  worker_step();
  ahead_request(a);
  std::printf("late %d %d\n", live, ahead_decoding(a) ? 1 : 0);
  worker_step();
  shown = ahead_take(a, 1280, 800);
  std::printf("again %d\n", shown ? 1 : 0);
  drop(shown);
  // A job that never comes back holds the slideshow back 5 s at most and
  // then another can be asked.
  ahead_request(b);
  now_ms += 4000;
  const int held = ahead_decoding(b) ? 1 : 0;
  now_ms += 1500;
  std::printf("stuck %d %d\n", held, ahead_decoding(b) ? 1 : 0);
  // A broken file: an empty answer, the slide change decodes it itself.
  worker_step();
  ScreensaverWallpaperConfig broken{"broken.jpg"};
  ahead_request(broken);
  worker_step();
  std::printf("broken %d %d tasks %d\n", ahead_take(broken, 1280, 800) ? 1 : 0, ahead_decoding(broken) ? 1 : 0, tasks);
  ahead_release();
  std::printf("leak %d\n", live);
  return 0;
}
`;
const output = compileAndRun({label: 'Screensaver slide decoded ahead', harness});
if (output !== null) {
  const rows = Object.fromEntries(output.trim().split('\n').map((line) => {
    const [key, ...rest] = line.trim().split(' ');
    return [key, rest];
  }));
  assert.deepEqual(rows.asked, ['1', '0'], 'asked: decoding, not there yet');
  assert.deepEqual(rows.taken, ['1', '1', '0'], 'the slide change takes it, decoded once');
  assert.deepEqual(rows.once, ['2'], 'asked twice for the same slide: one decode');
  assert.deepEqual(rows.changed, ['0'], 'another corner radius: decoded again');
  assert.deepEqual(rows.moved, ['0'], 'another focus: decoded again');
  assert.deepEqual(rows.released, ['0'], 'closing frees the waiting slide');
  assert.deepEqual(rows.late, ['0', '1'], 'a slide finished after closing is freed by the worker; the next opening asks again');
  assert.deepEqual(rows.again, ['1'], 'and gets it');
  assert.deepEqual(rows.stuck, ['1', '0'], 'a lost job holds the slide back 5 s at most');
  assert.deepEqual(rows.broken, ['0', '0', 'tasks', '1'], 'a broken file answers empty; one worker task');
  assert.deepEqual(rows.leak, ['0'], 'no picture left behind');
}
console.log('Screensaver slide ahead: decoded once on the worker, taken by the slide change, freed on close, never stuck');
