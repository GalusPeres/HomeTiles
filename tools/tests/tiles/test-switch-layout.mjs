// Host test for src/types/switch/layout.h: the stored Switch layouts, which
// bar a tile shows, and the dimmer geometry (the Light popup's brightness
// logic sideways). Also pins the half-height policy, the save path and the
// Web Admin mirror of the dimmer geometry.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const harness = String.raw`
#include "src/types/switch/layout.h"

#include <cstdio>

using namespace switch_layout;

int main() {
  // Stored values; unknown ones fall back to the icon button.
  for (int value = 0; value <= 5; ++value) {
    std::printf("layout %d %d\n", value, static_cast<int>(from_stored(static_cast<uint8_t>(value))));
  }
  const Layout layouts[] = {Layout::IconButton, Layout::Switch, Layout::Dimmer, Layout::Automatic};
  for (Layout layout : layouts) {
    for (int half = 0; half <= 1; ++half) {
      for (int dimmable = 0; dimmable <= 1; ++dimmable) {
        std::printf("bar %d %d %d %d\n", static_cast<int>(layout), half, dimmable,
                    static_cast<int>(bar_for(layout, half, dimmable)));
      }
    }
  }
  // Guition V2 1x1: 156 x 61 bar, radius 26.
  const Dimmer v2{156, 61, 26};
  std::printf("geometry %d %d %d %d %d %d\n", v2.handle_margin(), v2.end_radius(), v2.handle_width(),
              v2.handle_height(), v2.handle_low(), v2.handle_high());
  for (int x = -40; x <= 170; x += 1) {
    std::printf("value %d %d\n", x, v2.value_at(x));
  }
  for (int value = 0; value <= 100; ++value) {
    std::printf("fill %d %d %d\n", value, v2.handle_x(static_cast<uint8_t>(value)),
                v2.fill_width(static_cast<uint8_t>(value)));
  }
  for (int fill = v2.min_fill(); fill <= 156; ++fill) {
    std::printf("end %d %d\n", fill, v2.end_radius_for(fill));
  }
  return 0;
}
`;

const output = compileAndRun({label: 'Switch layout', harness});
if (output !== null) {
  const rows = output.trim().split('\n').map(line => line.trim().split(' '));
  const pick = kind => rows.filter(row => row[0] === kind).map(row => row.slice(1).map(Number));

  assert.deepEqual(pick('layout'), [[0, 0], [1, 1], [2, 2], [3, 3], [4, 0], [5, 0]]);

  // Bar: None 0, Toggle 1, Dimmer 2.
  const bars = new Map(pick('bar').map(([layout, half, dimmable, bar]) => [`${layout}${half}${dimmable}`, bar]));
  for (const key of bars.keys()) {
    const [layout, half] = key;
    if (half === '1' || layout === '0') assert.equal(bars.get(key), 0, `no bar ${key}`);
  }
  assert.equal(bars.get('100'), 1);
  assert.equal(bars.get('101'), 1, 'Switch stays a switch for dimmable lights');
  assert.equal(bars.get('200'), 1, 'Dimmer falls back to the switch');
  assert.equal(bars.get('201'), 2);
  assert.equal(bars.get('300'), 1);
  assert.equal(bars.get('301'), 2);

  const [[margin, endRadius, handleWidth, handleHeight, low, high]] = pick('geometry');
  // Smallest piece = bar radius + end rounding (tangential), handle in its
  // middle (approved mockup switch-tiles-left).
  assert.deepEqual([endRadius, handleWidth, handleHeight], [15, 4, 25]);
  assert.equal(margin, Math.floor((26 + 15) / 2));
  assert.equal(low, 26 + 15 - margin);
  assert.equal(high, 156 - margin);

  const values = new Map(pick('value').map(([x, value]) => [x, value]));
  // Off only beyond the bar's start (like the popup slider's end); 1 % up to
  // the smallest piece's handle; 100 % at the end; monotonic in between.
  assert.equal(values.get(-1), 0);
  assert.equal(values.get(0), 1);
  assert.equal(values.get(low), 1);
  assert.equal(values.get(high), 100);
  assert.equal(values.get(170), 100);
  let previous = 0;
  for (let x = -40; x <= 170; x += 1) {
    assert.ok(values.get(x) >= previous, `value at ${x}`);
    previous = values.get(x);
  }

  const fills = pick('fill');
  assert.deepEqual(fills[0], [0, low, 0], 'off: no fill');
  assert.deepEqual(fills[1], [1, low, 26 + 15], '1 %: bar radius + end rounding');
  assert.deepEqual(fills[100], [100, high, 156], '100 % fills the bar');
  // The handle sits exactly in the middle of the smallest piece.
  assert.ok(Math.abs(fills[1][1] - (26 + 15) / 2) <= 0.5, 'handle centered at 1 %');
  // The end rounding is fixed in the middle and becomes the bar radius at
  // the full end, growing monotonically, never shrinking.
  const ends = pick('end');
  const endAt = new Map(ends.map(([fill, end]) => [fill, end]));
  assert.equal(endAt.get(26 + 15), 15);
  assert.equal(endAt.get(100), 15);
  assert.equal(endAt.get(156), 26);
  let lastEnd = 0;
  for (const [fill, end] of ends) {
    assert.ok(end >= lastEnd && end >= 15 && end <= 26, `end rounding at ${fill}`);
    lastEnd = end;
  }
  for (let value = 1; value <= 100; ++value) {
    // The handle round-trips through value_at.
    assert.equal(values.get(fills[value][1]), value, `round trip ${value}`);
  }
}

// Policy, save path and Web mirror.
const geometry = readRepoFile('src/tiles/config/tile_geometry.h');
assert.match(geometry, /icon_title\(type\) \|\| type == TILE_SWITCH/);
assert.match(geometry, /inline bool compact_switch\(int type, float w, float h\)/);
const handler = readRepoFile('src/types/switch/web_handler.cpp');
assert.match(handler, /raw >= 0 && raw <= switch_layout::kLayoutMax/);
const layoutJs = readRepoFile('src/web/admin/tiles/layout.js');
assert.match(layoutJs, /\[2, 4, 5, 7, 8, 9, 18\]\.includes\(Number\(type\)\)/);
const admin = readRepoFile('src/types/switch/admin.js');
for (const marker of [
  'const endRadius = Math.floor(height / 4);',
  'const minFill = Math.min(radius + endRadius, width);',
  'const margin = Math.floor(minFill / 2);',
  'endRadiusFor(fill) {',
  "const SWITCH_LAYOUT_NEW_TILE = '3';",
  'SWITCH_I18N'
]) {
  assert.ok(admin.includes(marker), `Web dimmer mirror: ${marker}`);
}
// Value size like the half-height Sensor: editor field, save and load.
const html = readRepoFile('src/types/switch/web_html.cpp');
assert.ok(html.includes('append_switch_choice(html, tab_id, "switch_value_font", tr.sensor_value_size, sizes, 3);'));
assert.ok(admin.includes("formData.append('sensor_value_font', switchValueFont("));
assert.ok(admin.includes('fontEl.value = switchValueFont(data.sensor_value_font);'));
assert.match(handler, /tile\.sensor_value_font =\s*font >= 1 && font <= SENSOR_VALUE_FONT_MAX/);
const renderer = readRepoFile('src/types/switch/renderer.cpp');
// The bar draws itself: no LVGL switch/slider widgets, no clip_corner.
for (const forbidden of ['lv_switch_create', 'lv_slider_create', 'set_style_clip_corner']) {
  assert.ok(!renderer.includes(forbidden), `Switch renderer must not use ${forbidden}`);
}
assert.ok(renderer.includes('tile_icon_disc::mark_surface(bar);'));
// Approved touch zones: the bar takes touches up to the card edges.
assert.ok(renderer.includes('lv_obj_set_ext_click_area(bar, climate_layout::kOuterInset);'));
// The fill stays inside the bar's shape through narrowed clip areas, no layer.
assert.ok(renderer.includes('layer->_clip_area = clip_ori;'));
// Title and state left-aligned beside the disc like the half-height tiles.
assert.ok(renderer.includes('const int text_x = inset + disc + 2 * inset;'));
assert.ok(renderer.includes('compact_sensor_layout::value_font(tile.sensor_value_font)'));
// b143 regression: a local bg_opa on the bar outranked the shared control
// fill style (ui_surface_style::apply_control_fill), so the track never showed.
const createBar = renderer.slice(renderer.indexOf('lv_obj_t* create_bar('),
                                 renderer.indexOf('void switch_tile_show_state('));
assert.ok(createBar.length > 0, 'create_bar must exist');
assert.ok(!/lv_obj_set_style_bg_opa\(bar,/.test(createBar),
          'The bar track opacity must come from the control fill style');
assert.ok(renderer.includes('lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN_VER);'));

// b144 regressions on the V2: dragging from off showed a grey 1 % piece
// until Home Assistant replied (the fill takes the icon color), and a later
// icon color change did not redraw the bar.
const localLevel = renderer.slice(renderer.indexOf('void show_local_level('),
                                  renderer.indexOf('void dimmer_event('));
assert.ok(localLevel.includes('if ((value > 0) != was_on) {') &&
          localLevel.includes('update_switch_tile_state(data->grid_type, data->index, payload);'),
          'Crossing off/on while dragging switches the tile at once');
assert.ok(renderer.includes('if (view->bar && view->fill_rgb != icon_rgb) lv_obj_invalidate(view->bar);'),
          'A new icon color redraws the bar');
// Smoothness: a drag step redraws only the changed columns, like the popup.
assert.ok(localLevel.includes('invalidate_level_change(view, old_level, value);') &&
          !localLevel.includes('lv_obj_invalidate(view->bar)'),
          'Drag steps must not redraw the whole bar');
assert.ok(renderer.includes('lv_obj_invalidate_area(view->bar, &dirty);'));

console.log('Switch layout tests passed.');
