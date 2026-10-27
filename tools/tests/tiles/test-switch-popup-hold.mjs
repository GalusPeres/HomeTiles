// The Switch tile behind the Light popup keeps the popup's level while the
// popup changes it (user video 2026-10-27: after each release the tile ran
// through Home Assistant's echoes of the commands in between, a light's
// transition steps included, 85 -> 71 -> 30 %, while the popup itself held
// its value). Compiles the Switch renderer's real hold (held_value,
// start_hold, switch_tile_hold_from_popup, bar_event_data) on the host: an
// echo during the popup's gesture and up to kRemoteBlockMs after its release
// does not reach the tile, the end of the hold shows the reported state, and
// the popup calls it on every brightness step and on every commit.
import assert from 'node:assert/strict';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';

const renderer = readRepoFile('src/types/switch/renderer.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(renderer).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const dimmerDrag = renderer.slice(renderer.indexOf('struct DimmerDrag {'), renderer.indexOf('DimmerDrag g_drag;'));
assert.ok(dimmerDrag.length > 0, 'DimmerDrag');

const harness = String.raw`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
uint32_t g_now = 1000;
uint32_t millis() { return g_now; }
enum class GridType : uint8_t { TAB0 = 0 };
constexpr int TILES_PER_GRID = 4;
constexpr uint32_t kRemoteBlockMs = 3000;
struct lv_point_t { int32_t x, y; };
struct lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t*);
struct lv_event_dsc_t { lv_event_cb_t cb; void* user_data; };
struct lv_obj_t { std::vector<lv_event_dsc_t> events; };
uint32_t lv_obj_get_event_count(lv_obj_t* obj) { return static_cast<uint32_t>(obj->events.size()); }
lv_event_dsc_t* lv_obj_get_event_dsc(lv_obj_t* obj, uint32_t i) { return &obj->events[i]; }
lv_event_cb_t lv_event_dsc_get_cb(lv_event_dsc_t* dsc) { return dsc->cb; }
void* lv_event_dsc_get_user_data(lv_event_dsc_t* dsc) { return dsc->user_data; }
struct lv_timer_t { uint32_t due; };
typedef void (*lv_timer_cb_t)(lv_timer_t*);
lv_timer_t g_timer_slot;
bool g_timer_alive = false;
lv_timer_t* lv_timer_create(lv_timer_cb_t, uint32_t period, void*) { g_timer_alive = true; g_timer_slot.due = g_now + period; return &g_timer_slot; }
void lv_timer_set_repeat_count(lv_timer_t*, int32_t) {}
void lv_timer_delete(lv_timer_t*) { g_timer_alive = false; }
struct SwitchBarView { lv_obj_t* bar = nullptr; };
struct String { const char* s = ""; };
struct SwitchEventData { String entity_id; String title; GridType grid_type; uint8_t index = 0; SwitchBarView* view = nullptr; };
struct SwitchTileWidgets { SwitchBarView* view = nullptr; };
SwitchTileWidgets g_widgets[TILES_PER_GRID];
SwitchTileWidgets* tile_renderer_get_switch_widgets(GridType) { return g_widgets; }
${dimmerDrag}
DimmerDrag g_drag;
lv_timer_t* g_release_timer = nullptr;
int g_reported_shown = 0;
// The end of a hold: the reported state is shown (release_timer_cb).
void release_timer_cb(lv_timer_t*) { g_release_timer = nullptr; if (!g_drag.data || g_drag.dragging) return; g_drag.block_until = millis(); ++g_reported_shown; }
void cancel_live_timer() {}
void bar_event_cb(lv_event_t*) {}
void bar_draw_cb(lv_event_t*) {}
${fn('start_hold')}
${fn('held_value')}
${fn('bar_event_data')}
${fn('switch_tile_hold_from_popup')}
// What the tile shows for a reported level (show_view_state's hold).
int shown(const SwitchBarView* view, int reported) {
  uint8_t value = 0; bool level = false;
  return held_value(view, value, level) && level ? value : reported;
}
void tick(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    g_now += 10;
    if (g_timer_alive && g_release_timer && g_now >= g_timer_slot.due) { g_timer_alive = false; release_timer_cb(g_release_timer); }
  }
}
int main() {
  lv_obj_t bar; SwitchBarView view; view.bar = &bar;
  SwitchEventData data; data.index = 2; data.view = &view;
  bar.events.push_back({bar_draw_cb, &view});
  bar.events.push_back({bar_event_cb, &data});
  g_widgets[2].view = &view;
  // The popup drags 74 -> 27 %; an echo of an earlier live command (33 %)
  // arrives meanwhile: the tile shows the popup's level.
  for (int level = 74; level >= 27; --level) { switch_tile_hold_from_popup(GridType::TAB0, 2, level, true, false); tick(20); }
  assert(shown(&view, 33) == 27);
  // Released: echoes of the transition (71 %) stay out for kRemoteBlockMs.
  switch_tile_hold_from_popup(GridType::TAB0, 2, 30, true, true);
  tick(500);
  assert(shown(&view, 71) == 30);
  tick(2400);
  assert(shown(&view, 45) == 30 && g_reported_shown == 0);
  // Then the reported state again, shown once.
  tick(200);
  assert(g_reported_shown == 1 && shown(&view, 30) == 30 && shown(&view, 55) == 55);
  // A new gesture right after a release is not ended by the old timer.
  switch_tile_hold_from_popup(GridType::TAB0, 2, 60, true, true);
  tick(2900);
  switch_tile_hold_from_popup(GridType::TAB0, 2, 65, true, false);
  tick(500);
  assert(shown(&view, 12) == 65 && g_reported_shown == 1);
  // An icon button (no bar) and an index beyond the grid are left alone.
  g_widgets[1].view = nullptr;
  switch_tile_hold_from_popup(GridType::TAB0, 1, 10, true, true);
  switch_tile_hold_from_popup(GridType::TAB0, 9, 10, true, true);
  assert(g_drag.data == &data);
  std::puts("ok");
}
`;
const out = compileAndRun({label: 'Switch popup hold', harness});
if (out !== null) assert.equal(out.trim(), 'ok');

// The popup holds its tile on every brightness step and on every commit
// (release, tap, power button, color), before it shows its value there.
const popup = readRepoFile('src/ui/popups/light/light_popup.cpp');
assert.match(popup, /cancel_pending_live_publish\(ctx\);\s*hold_bound_tile\(ctx, true\);\s*sync_bound_tile_from_popup\(ctx\);/);
assert.match(popup, /\} else \{\s*hold_bound_tile\(ctx, false\);\s*maybe_live_publish_brightness\(ctx\);\s*\}/);
assert.match(popup, /switch_tile_hold_from_popup\(static_cast<GridType>\(ctx->tile_grid\), ctx->tile_index, value, level, released\);/);
console.log('Switch popup hold: the tile behind the Light popup keeps the popup\'s level against echoes');
