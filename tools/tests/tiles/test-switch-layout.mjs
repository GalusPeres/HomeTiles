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
  assert.deepEqual([margin, endRadius, handleWidth, handleHeight], [12, 6, 4, 25]);
  assert.equal(low, 2 * 26 - 12);
  assert.equal(high, 156 - 12);

  const values = new Map(pick('value').map(([x, value]) => [x, value]));
  // Off only one radius beyond the 1 % position; 1 % up to it; 100 % at the
  // end; monotonic in between.
  assert.equal(values.get(low - 26 - 1), 0);
  assert.equal(values.get(low - 26), 1);
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
  assert.deepEqual(fills[1], [1, low, 2 * 26], '1 % fills the round start');
  assert.deepEqual(fills[100], [100, high, 156], '100 % fills the bar');
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
  'const margin = Math.floor(height / 5);',
  'const minFill = Math.min(2 * radius, width);',
  "const SWITCH_LAYOUT_NEW_TILE = '3';",
  'SWITCH_I18N'
]) {
  assert.ok(admin.includes(marker), `Web dimmer mirror: ${marker}`);
}
const renderer = readRepoFile('src/types/switch/renderer.cpp');
// The bar draws itself: no LVGL switch/slider widgets, no clip_corner.
for (const forbidden of ['lv_switch_create', 'lv_slider_create', 'set_style_clip_corner']) {
  assert.ok(!renderer.includes(forbidden), `Switch renderer must not use ${forbidden}`);
}
assert.ok(renderer.includes('tile_icon_disc::mark_surface(bar);'));
assert.ok(renderer.includes('lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN_VER);'));

console.log('Switch layout tests passed.');
