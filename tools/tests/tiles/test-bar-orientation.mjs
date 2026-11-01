// The upright bar of Switch and Fan tiles (user 2026-10-26, mockup
// build/design-mockups/switch-vertical): Web field "bar_orientation", stored
// without a format change (Switch sensor_decimals 4..6, Fan 1), the header
// beside the disc, the bar below it over the rest of the tile; the dimmer
// fills from the bottom with the one-row bar's roundings and handle line,
// the switch is on at the top, a Fan's speeds stack from the bottom. Device
// and Web Admin preview agree.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

// Stored values and the upright geometry (switch_layout.h).
const harness = String.raw`
#include "src/types/switch/layout.h"

#include <cstdio>

using namespace switch_layout;

int main() {
  for (int layout = 0; layout <= 3; ++layout) {
    for (int upright = 0; upright <= 1; ++upright) {
      const uint8_t stored = to_stored(static_cast<Layout>(layout), upright != 0);
      std::printf("stored %d %d %d %d %d\n", layout, upright, stored, static_cast<int>(from_stored(stored)),
                  upright_stored(stored) ? 1 : 0);
    }
  }
  for (int value = 7; value <= 9; ++value) std::printf("unknown %d %d\n", value, static_cast<int>(from_stored(value)));
  std::printf("look %d %d %d\n", sensor_look(Layout::Dimmer, 2.0f) ? 1 : 0, sensor_look(Layout::Dimmer, 2.0f, true) ? 1 : 0,
              sensor_look(Layout::Dimmer, 1.0f, true) ? 1 : 0);
  // level_bar::upright_geometry on the V2 1x1.5 bar: 146 px long, 182 wide,
  // the one-row bar (68 px) as its thickness, radius 27; the handle line as
  // long as the Light popup's for the bar's width.
  const Dimmer up{146, 68, 27, 68};
  std::printf("upright %d %d %d %d %d %d\n", up.min_fill(), up.end_radius(), up.handle_width(), upright_handle_length(182),
              up.fill_width(50), up.value_at(up.handle_x(50)));
  return 0;
}
`;
const output = compileAndRun({label: 'Bar orientation', harness});
if (output !== null) {
  const rows = output.trim().split('\n').map((line) => line.trim().split(' '));
  const pick = (kind) => rows.filter((row) => row[0] === kind).map((row) => row.slice(1).map(Number));
  for (const [layout, upright, stored, back, isUpright] of pick('stored')) {
    // The packed format clamps sensor_decimals to 0..6 (tile_config.cpp clampDecimals).
    assert.ok(stored <= 6, `layout ${layout} upright ${upright}: ${stored} survives the packed format`);
    assert.equal(back, layout, `layout ${layout} upright ${upright} reads back`);
    assert.equal(isUpright, upright && layout !== 0 ? 1 : 0, `layout ${layout}: the icon button has no bar to turn`);
  }
  assert.deepEqual(pick('unknown').map((row) => row[1]), [0, 0, 0], 'unknown values are the icon button');
  assert.deepEqual(pick('look')[0], [1, 0, 0], 'an upright bar keeps the header beside the disc');
  assert.deepEqual(pick('upright')[0], [44, 17, 4, 52, 94, 50], 'upright: the one-row roundings, the popup handle length');
}

// The Web Admin's geometry is the same (switch admin.js switchDimmerGeometry
// with the length as width and the one-row bar as thickness).
const js = vm.createContext({Math, Number});
vm.runInContext(extractDeliveredFunction('switchDimmerGeometry'), js);
const geometry = js.switchDimmerGeometry(146, 68, 27, 68);
assert.deepEqual([geometry.minFill, geometry.endRadius, geometry.handleWidth, geometry.handleHeight, geometry.fillWidth(50)],
  [44, 17, 4, 28, 94], 'the preview draws the device geometry');

// Storage and the web API.
const orientation = readRepoFile('src/tiles/config/bar_orientation.h');
assert.ok(orientation.includes('if (tile.type == TILE_SWITCH) return switch_layout::upright_stored(tile.sensor_decimals);') &&
  orientation.includes('return tile.type == TILE_FAN && tile.sensor_decimals == kFanUpright;') &&
  orientation.includes('inline uint8_t fan_stored(bool upright) { return upright ? kFanUpright : kFanHorizontal; }'));
const switchHandler = readRepoFile('src/types/switch/web_handler.cpp');
assert.ok(switchHandler.includes('const bool upright = server.hasArg("bar_orientation") && server.arg("bar_orientation").toInt() == 1;') &&
  switchHandler.includes('style = switch_layout::to_stored(layout, upright);'), 'Switch: saved with its layout');
assert.ok(readRepoFile('src/types/device/web.cpp').includes(
  'tile.sensor_decimals = bar_orientation::fan_stored(\n      type == TILE_FAN && server.hasArg("bar_orientation") && server.arg("bar_orientation").toInt() == 1);'),
  'Fan: saved in its unused decimals');
const tilesJson = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
assert.ok(tilesJson.includes('out += String(tile.type == TILE_SWITCH ? static_cast<uint8_t>(switch_layout::from_stored(tile.sensor_decimals)) : 0);') &&
  tilesJson.includes('out += bar_orientation::upright(tile) ? "1" : "0";'), 'the tile JSON: layout and orientation apart');

// Device: geometry, drawing and touch.
const levelBar = readRepoFile('src/tiles/runtime/level_bar.h');
assert.ok(levelBar.includes('box.height = bar_orientation::upright(tile) ? tile_h - climate_layout::kOuterInset - top'),
  'the upright bar fills the tile below the header');
assert.ok(levelBar.includes('return switch_layout::Dimmer{height, thickness, radius, thickness};') &&
  levelBar.includes('const int32_t line_len = switch_layout::upright_handle_length(lv_area_get_width(&area));') &&
  levelBar.includes('lv_color_t card, bool upright = false) {') &&
  levelBar.includes('return upright_geometry(width, height, radius_of(bar, width), base).value_at(area.y2 - point.y);') &&
  levelBar.includes('lv_area_t dirty = {area.x1, area.y2 - to + 1, area.x2, area.y2 - from};'),
  'level bar: upright fill, touch and redraw');
const switchRenderer = readRepoFile('src/types/switch/renderer.cpp');
assert.ok(switchRenderer.includes('view->upright = bar_orientation::upright(tile);') &&
  switchRenderer.includes('thumb = view->on ? lv_area_t{area.x1, area.y1, area.x2, area.y1 + half - 1}') &&
  switchRenderer.includes('return level_bar::value_at(view->bar, view->bar_base, point, view->upright);') &&
  switchRenderer.includes('level_bar::invalidate_change(view->bar, view->bar_base, old_level, new_level, view->upright);'),
  'Switch tile: upright switch and dimmer');
const deviceTile = readRepoFile('src/types/device/device_tile.cpp');
assert.ok(deviceTile.includes('view->upright = bar_orientation::upright(tile);') &&
  deviceTile.includes('const bool tall = !view->compact && tile.span_h > 1.0f && !view->upright;') &&
  deviceTile.includes('view->upright ? lv_area_t{area.x1, area.y2 - i * (step + gap) - step + 1, area.x2, area.y2 - i * (step + gap)}') &&
  deviceTile.includes('const int32_t along = view->upright ? bar_up(view->bar, length) : x;') &&
  deviceTile.includes('level_bar::draw_fill(layer, view->bar, view->level, view->bar_base, accent, card, view->upright);'),
  'Fan tile: upright switch, speeds and percentage');

// Web Admin: the row under Layout (Switch) and in the Fan's fields, every language.
assert.ok(readRepoFile('src/types/switch/web_html.cpp').includes(
  'append_switch_choice(html, tab_id, "switch_orientation", tr.bar_orientation, orientations, 2);'));
assert.ok(readRepoFile('src/types/device/web.cpp').includes(
  'append_switch_choice(html, tab_id, "fan_orientation", tr.bar_orientation, orientations, 2);'));
const header = readRepoFile('src/core/i18n/i18n.h');
for (const field of ['bar_orientation', 'bar_horizontal', 'bar_upright']) assert.ok(header.includes(`const char* ${field};`), field);
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
for (const word of ['"Ausrichtung",', '"Senkrecht",', '"Orientation",', '"Vertical",', '"Verticale",', '"Pionowa",', '"Svisle",',
  '"Orientación",', '"Zvislo",']) {
  assert.ok(i18n.includes(word), `translation ${word}`);
}
const switchAdmin = readRepoFile('src/types/switch/admin.js');
assert.ok(switchAdmin.includes("bar.style.setProperty('--switch-handle-h', (upright ? Math.floor(width * 46 / 160) : geometry.handleHeight) + 'px');") &&
  switchAdmin.includes("'camera_tap', 'switch_orientation', 'fan_orientation', 'sensor_layout',") &&
  switchAdmin.includes("const hide = switchLayoutValue(style.value) === 0;") &&
  switchAdmin.includes("tileElem.classList.toggle('switch-upright', bar && !!upright);") &&
  switchAdmin.includes("if (orientationEl) orientationEl.value = Number(data.bar_orientation) === 1 ? '1' : '0';") &&
  switchAdmin.includes("formData.append('bar_orientation', document.getElementById(prefix + '_switch_orientation')?.value === '1' ? '1' : '0');") &&
  switchAdmin.includes("if (orientationEl) orientationEl.value = '0';"),
  'Switch editor: load, save, reset, hidden for the icon button, the preview class');
const deviceAdmin = readRepoFile('src/types/device/admin.js');
assert.ok(deviceAdmin.includes("tileElem.classList.toggle('switch-upright', !halfHeight && kind === 'fan' && !!upright);") &&
  deviceAdmin.includes("if (orientation) formData.append('bar_orientation', orientation.value === '1' ? '1' : '0');"),
  'Fan editor: save and preview');
assert.ok(readRepoFile('src/web/admin/tiles/editor.js').includes("for (const id of ['_switch_orientation', '_fan_orientation']) {"),
  'the choice updates preview, draft and autosave');
assert.ok(readRepoFile('src/web/admin/tiles/snapshots.js').includes("'switch_style', 'bar_orientation',"), 'drafts keep it');
const importExport = readRepoFile('src/web/admin/tiles/import-export.js');
assert.ok(importExport.includes("fd.append('bar_orientation', Number(tile.bar_orientation) === 1 ? '1' : '0');") &&
  importExport.includes("if (safeType === 26) fd.append('bar_orientation', Number(tile.bar_orientation) === 1 ? '1' : '0');"),
  'import keeps it (older exports: horizontal)');
const css = readRepoFile('src/web/assets/admin.css');
assert.ok(css.includes('.tile.switch-upright .tile-switch .tile-switch-fill {') &&
  css.includes('.tile.switch-upright .tile-switch.is-on .tile-switch-knob { top:0; bottom:50%; left:0; }') &&
  css.includes('.tile.switch-upright .tile-switch .tile-device-parts { flex-direction:column-reverse; }'),
  'preview: fill from the bottom, switch on at the top, speeds from the bottom');
console.log('bar orientation: stored without a format change, upright geometry on device and preview, editor and languages');
