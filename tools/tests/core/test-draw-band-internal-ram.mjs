// The S3 got slower since v0.9.5 (user 2026-10-28: "die alte Version war
// ueberall schneller"). Its SRAM draw band gets the internal RAM left above
// the 150 KB reserve at display start, and ~17 KB of new static tables had
// moved into internal RAM (.dram0.bss b480 74144 -> b605 87112 bytes, boot
// heap 208 -> 191 KB): v0.9.5 drew in 50 rows, b605 in 31, so a full frame
// took 16 bands instead of 10 and LVGL walked every widget again for each.
// The large tables now come from LVGL's heap or PSRAM (b606, b607), and the
// frame and text fit probes exist only when a measurement round turns them
// on (b607: the user judges test builds by feel against the releases).
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// The band takes what is left above the reserve: every internal byte a
// static table takes is a byte less for the band.
const display = read('src/core/display/display_manager.cpp');
assert.ok(display.includes('static constexpr size_t kInternalDrawReserveBytes = 150 * 1024;') &&
  display.includes('(free_internal > internal_reserve_bytes) ? (free_internal - internal_reserve_bytes) : 0;'),
  'the SRAM band is what internal RAM leaves above the reserve');

// Frame probe (b578-b603, ~6.7 KB): tables in LVGL's heap, test builds only.
const probe = read('src/core/diagnostics/tile_frame_probe.cpp');
for (const table of ['FontTime g_fonts[', 'TileTime g_tiles[', 'ElementTime g_elements[', 'TaskTime g_tasks[', 'RestTime g_rest[']) {
  assert.ok(!probe.includes(table), `${table} is no static array`);
}
for (const alloc of ['g_fonts = static_cast<FontTime*>(lv_malloc_zeroed(sizeof(FontTime) * kFonts));',
  'g_tiles = static_cast<TileTime*>(lv_malloc_zeroed(sizeof(TileTime) * TILES_PER_GRID));',
  'g_elements = static_cast<ElementTime*>(lv_malloc_zeroed(sizeof(ElementTime) * kElements));',
  'g_tasks = static_cast<TaskTime(*)[kTaskTypes]>(lv_malloc_zeroed(sizeof(TaskTime) * kTaskKinds * kTaskTypes));',
  'g_rest = static_cast<RestTime*>(lv_malloc_zeroed(sizeof(RestTime) * kRestObjects));']) {
  assert.ok(probe.includes(alloc), alloc);
}
const watch = probe.slice(probe.indexOf('void watch(lv_obj_t* card, uint8_t index, uint8_t type) {'));
assert.ok(watch.indexOf('if (!g_tiles && !alloc_tables()) return;') >= 0 &&
  watch.indexOf('if (!g_tiles && !alloc_tables()) return;') < watch.indexOf('lv_display_add_event_cb(g_display, display_cb, code, nullptr);'),
  'the tables exist before any callback can use them');
assert.ok(probe.indexOf('#if defined(HOMETILES_FRAME_PROBE)') >= 0 &&
  probe.indexOf('#if defined(HOMETILES_FRAME_PROBE)') < probe.indexOf('namespace {') &&
  probe.includes('#else\n\nvoid watch(lv_obj_t*, uint8_t, uint8_t) {}\n\n#endif\n\n}  // namespace tile_frame_probe'),
  'a build without the switch carries no probe');
assert.ok(read('src/core/diagnostics/tile_frame_probe.h').includes('\n// #define HOMETILES_FRAME_PROBE 1\n'),
  'the switch is off in test builds');
const textFit = read('src/core/diagnostics/text_fit_probe.h');
assert.ok(textFit.includes('\n// #define HOMETILES_TEXT_FIT_PROBE 1\n') && !textFit.includes('#if defined(HOMETILES_TEST_BETA)'),
  'the text fit probe is off in test builds too');

// Kept corner sets (b594, 24 x 124 bytes): made in LVGL's heap with the first set.
const corners = read('src/ui/shared/rounded_corners.cpp');
assert.ok(!corners.includes('CornerSet g_sets[') && corners.includes('CornerSet* g_sets = nullptr;'));
assert.ok(corners.includes('if (!g_sets) g_sets = static_cast<CornerSet*>(lv_malloc_zeroed(sizeof(CornerSet) * kMaxSets));'));
assert.ok(corners.includes('if (!g_sets || at < first || at >= first + sizeof(CornerSet) * kMaxSets || (at - first) % sizeof(CornerSet)) {'),
  'a marked task is recognised by the heap block, not by the size of a pointer');

// Power flow draw probes (96 x 36 bytes).
const flow = read('src/types/power_flow/renderer.cpp');
assert.ok(!flow.includes('DrawProbe g_probes[') && flow.includes('DrawProbe* g_probes = nullptr;'));
assert.ok(flow.includes('if (!g_probes) g_probes = static_cast<DrawProbe*>(lv_malloc_zeroed(sizeof(DrawProbe) * kMaxProbes));\n  if (!g_probes) return;'),
  'made with the first probe');

// b607: static arrays placed in PSRAM while static objects are built
// (psram_static.h): the tile update queues, the WiFi lists and the bridge
// pictures; the color table and the popup timing line in the heap.
const helper = read('src/core/memory/psram_static.h');
assert.ok(helper.includes('heap_caps_malloc(sizeof(T) * N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);') &&
  helper.includes('for (size_t i = 0; i < N; ++i) new (&items[i]) T();'), 'PSRAM first, every item constructed');
for (const [file, table] of [
  ['src/tiles/runtime/tile_renderer.cpp', 'static SensorUpdate (&g_update_queue)[QUEUE_SIZE] = psram_static_array<SensorUpdate, QUEUE_SIZE>();'],
  ['src/tiles/runtime/tile_renderer.cpp', 'psram_static_array<SwitchUpdate, SWITCH_QUEUE_SIZE>();'],
  ['src/tiles/runtime/tile_renderer.cpp', 'static MediaUpdate (&g_media_queue)[MEDIA_QUEUE_SIZE] = psram_static_array<MediaUpdate, MEDIA_QUEUE_SIZE>();'],
  ['src/ui/tabs/settings/tab_settings.cpp', 'static WifiScanEntry (&wifi_scan_results)[24] = psram_static_array<WifiScanEntry, 24>();'],
  ['src/ui/tabs/settings/tab_settings.cpp', 'static WifiNetwork (&g_wifi_list)[25] = psram_static_array<WifiNetwork, 25>();'],
  ['src/ui/tabs/settings/setup_screen.cpp', 'settings_model::WifiNetwork (&g_networks)[24] = psram_static_array<settings_model::WifiNetwork, 24>();'],
  ['src/network/bridge/bridge_images.cpp', 'Picture (&g_pictures)[kPictures] = psram_static_array<Picture, kPictures>();']]) {
  assert.ok(read(file).includes(table), `${file}: ${table}`);
}
assert.ok(!read('src/ui/shared/tone_color.h').includes('static float table[256];') &&
  read('src/ui/shared/tone_color.h').includes('float* made = static_cast<float*>(malloc(256 * sizeof(float)));'));
assert.ok(!read('src/core/diagnostics/popup_timing.h').includes('static char line[1024];') &&
  read('src/core/diagnostics/popup_timing.h').includes('static char* const line = static_cast<char*>(malloc(kLineBytes));'));

console.log('Large tables stay out of internal RAM, the S3 draw band keeps its rows');
