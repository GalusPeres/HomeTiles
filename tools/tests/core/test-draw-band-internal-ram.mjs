// The S3 got slower since v0.9.5 (user 2026-10-28: "die alte Version war
// ueberall schneller"). Its SRAM draw band gets the internal RAM left above
// the 150 KB reserve at display start, and ~17 KB of new static tables had
// moved into internal RAM (.dram0.bss b480 74144 -> b605 87112 bytes, boot
// heap 208 -> 191 KB): v0.9.5 drew in 50 rows, b605 in 31, so a full frame
// took 16 bands instead of 10 and LVGL walked every widget again for each.
// The large tables now come from LVGL's heap (PSRAM on the panels), made on
// first use, and the frame probe exists in test builds only.
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
assert.ok(probe.includes('#include "src/core/firmware/firmware_version.h"') &&
  probe.indexOf('#if defined(HOMETILES_BETA_BUILD)') < probe.indexOf('namespace {') &&
  probe.includes('#else\n\nvoid watch(lv_obj_t*, uint8_t, uint8_t) {}\n\n#endif\n\n}  // namespace tile_frame_probe'),
  'a release build carries no probe');

// Kept corner sets (b594, 24 x 124 bytes): made in LVGL's heap with the first set.
const corners = read('src/ui/shared/rounded_corners.cpp');
assert.ok(!corners.includes('CornerSet g_sets[') && corners.includes('CornerSet* g_sets = nullptr;'));
assert.ok(corners.includes('if (!g_sets) g_sets = static_cast<CornerSet*>(lv_malloc_zeroed(sizeof(CornerSet) * kMaxSets));'));
assert.ok(corners.includes('if (!g_sets || at < first || at >= first + sizeof(CornerSet) * kMaxSets || (at - first) % sizeof(CornerSet)) {'),
  'a marked task is recognised by the heap block, not by the size of a pointer');

// Energy flow draw probes (96 x 36 bytes).
const flow = read('src/types/energy_flow/renderer.cpp');
assert.ok(!flow.includes('DrawProbe g_probes[') && flow.includes('DrawProbe* g_probes = nullptr;'));
assert.ok(flow.includes('if (!g_probes) g_probes = static_cast<DrawProbe*>(lv_malloc_zeroed(sizeof(DrawProbe) * kMaxProbes));\n  if (!g_probes) return;'),
  'made with the first probe');

console.log('Large tables stay out of internal RAM, the S3 draw band keeps its rows');
