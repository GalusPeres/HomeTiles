// Hochkant (user 2026-10-08, layouts step 2): the upright layout on a panel
// that is landscape in its profile. Every P4 panel with a landscape profile
// here is upright by itself; its driver turns the landscape UI into it. One
// general way for all of them (user 2026-10-08, tested on the Guition V2):
// the upright UI is copied straight into the panel (180: turned into the
// mirrored place, p4_dsi_camera_presenter drawUpright), and DisplayManager
// turns the drivers' landscape touch point once for all. The tile grid gets
// the upright grid's rows and half row, the screensaver three rows at the
// bottom.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const display = read('src/core/display/display_manager.cpp');
const v2 = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
const eight = read('src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.cpp');

// --- Native: the shared copy, and the central touch turn against the V2's
// and the 8-inch's own landscape mappings (their raw axes differ).
const compiler = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: native upright copy check needs a C++ compiler');
} else {
  const turn = display.slice(display.indexOf('    if (grid_layout::turned()) {'),
    display.indexOf('    data->state = LV_INDEV_STATE_PRESSED;'));
  const landscape = (source, from, to) => source.slice(source.indexOf(from), source.indexOf(to));
  const v2Touch = landscape(v2, '  if (g_rotation & 0x02) {\n    mapped_x = logical_w - 1', '  if (mapped_x < 0) mapped_x = 0;');
  const eightTouch = landscape(eight, '  if (g_rotation & 0x02) {\n    mapped_x = static_cast<int32_t>(display_cfg.height)',
    '  const int32_t logical_w = static_cast<int32_t>(display_cfg.height);');
  const code = `#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
${read('src/devices/common/p4_dsi_cpu_rotate.h').replace('#pragma once', '')}
constexpr int32_t W = 800, H = 1280;  // the panel, upright
namespace grid_layout { inline bool turned() { return true; } inline int screen_w() { return W; } }
struct { int width = W, height = H; } display_cfg;
// The landscape UI point (x, y) in the framebuffer (draw_landscape_area).
void landscape_to_fb(int x, int y, bool flip, int& fx, int& fy) {
  if (flip) { fx = y; fy = (H - 1) - x; } else { fx = (W - 1) - y; fy = x; }
}
void v2_touch(uint8_t g_rotation, int32_t px0, int32_t py0, int32_t& mapped_x, int32_t& mapped_y) {
  const int32_t logical_w = H, logical_h = W;
  const int32_t px[1] = {px0}, py[1] = {py0};
  const int selected = 0;
${v2Touch}}
void eight_touch(uint8_t g_rotation, int32_t px0, int32_t py0, int32_t& mapped_x, int32_t& mapped_y) {
  const int32_t px[1] = {px0}, py[1] = {py0};
  const int selected = 0;
${eightTouch}}
void central(int16_t& mapped_x, int16_t& mapped_y) {
${turn}}
int main() {
  for (int fx : {0, 17, 400, 799}) for (int fy : {0, 33, 640, 1279}) {
    for (uint8_t rotation : {0, 2}) {
      // The upright UI point a finger on panel pixel (fx, fy) means.
      const int ux = rotation ? W - 1 - fx : fx, uy = rotation ? H - 1 - fy : fy;
      // V2: the GSL3680's raw axes (px = panel Y, py = W - 1 - panel X).
      int32_t lx, ly; v2_touch(rotation, fy, W - 1 - fx, lx, ly);
      int16_t x = static_cast<int16_t>(lx), y = static_cast<int16_t>(ly); central(x, y);
      assert(x == ux && y == uy);
      // 8-inch: the GT911 reports the panel's own coordinates.
      eight_touch(rotation, fx, fy, lx, ly);
      x = static_cast<int16_t>(lx); y = static_cast<int16_t>(ly); central(x, y);
      assert(x == ux && y == uy);
    }
  }
  // The copy: straight, 180 into the mirrored place, and bytes swapped.
  std::vector<uint16_t> fb(W * H, 0), src(5 * 3);
  for (int i = 0; i < 15; ++i) src[i] = static_cast<uint16_t>(0x0100 * (i + 1) + i);
  p4_dsi_cpu_rotate::copy_into(fb.data(), W, W, H, 10, 20, 5, 3, src.data(), false);
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 5; ++c) assert(fb[(20 + r) * W + 10 + c] == src[r * 5 + c]);
  std::fill(fb.begin(), fb.end(), 0);
  p4_dsi_cpu_rotate::copy_into(fb.data(), W, W, H, 10, 20, 5, 3, src.data(), true);
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 5; ++c) {
    assert(fb[(H - 1 - (20 + r)) * W + (W - 1 - (10 + c))] == src[r * 5 + c]);
  }
  std::fill(fb.begin(), fb.end(), 0);
  p4_dsi_cpu_rotate::copy_into(fb.data(), W, W, H, 10, 20, 5, 3, src.data(), false, true);
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 5; ++c) {
    const uint16_t v = src[r * 5 + c];
    assert(fb[(20 + r) * W + 10 + c] == static_cast<uint16_t>((v << 8) | (v >> 8)));
  }
  std::puts("ok");
}
`;
  const out = path.resolve('build/tests/upright-layout');
  fs.mkdirSync(out, {recursive: true});
  const cpp = path.join(out, 'upright.cpp');
  const bin = path.join(out, process.platform === 'win32' ? 'upright.exe' : 'upright');
  fs.writeFileSync(cpp, code);
  const compiled = spawnSync(compiler, ['-std=c++17', cpp, '-o', bin], {encoding: 'utf8'});
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
  const run = spawnSync(bin, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stdout + run.stderr);
  assert.equal(run.stdout.trim(), 'ok');
}

// --- Every panel upright by itself under a landscape profile: the same
// landscape turn (the central touch turn relies on it) and the shared hook.
for (const [file, ns] of [
  ['guition_jc8012p4a1/device_guition_jc8012p4a1.cpp', 'DeviceGuitionJC8012P4A1'],
  ['guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp', 'DeviceGuitionJC8012P4A1V2'],
  ['waveshare_touch_lcd_4_3/device_waveshare_touch_lcd_4_3.cpp', 'DeviceWaveshareTouchLCD4_3'],
  ['waveshare_touch_lcd_7/device_waveshare_touch_lcd_7.cpp', 'DeviceWaveshareTouchLCD7'],
  ['waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.cpp', 'DeviceWaveshareTouchLCD8'],
  ['waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.cpp', 'DeviceWaveshareTouchLCD10']]) {
  const source = read('src/devices/' + file);
  assert.match(source, /\} else \{\n    dst_x = logical_h - y - h;\n    dst_y = x;/, `${file}: landscape (x, y) at panel (w - 1 - y, x)`);
  assert.match(source, /if \(g_camera_presenter\.upright\(\)\) \{\n    g_camera_presenter\.drawUpright\(x, y, w, h, data, g_rotation\);\n    return;\n  \}\n  draw_landscape_area\(x, y, w, h, data\);/,
    `${file}: the shared upright draw`);
  assert.ok(source.includes(`void ${ns}::displaySetUpright(bool upright) {\n  // The UI and the camera popup's frames without the quarter turn.\n  g_camera_presenter.setUpright(upright);\n}`),
    `${file}: the presenter turns the camera frames too`);
}
// The Tab5 (M5GFX): the same turn in its PPA path, its own copy with the
// bytes swapped; M5GFX keeps its landscape rotation, so its touch too.
const tab5 = read('src/devices/m5stacks_tab5/device_m5stacks_tab5.cpp');
assert.match(tab5, /\} else \{\n    dst_x = kLogicalHeight - y - h;\n    dst_y = x;/);
assert.match(tab5, /p4_dsi_cpu_rotate::copy_into\(g_panel_fb, kPanelWidth, kPanelWidth, kPanelHeight, x, y, w, h, data, flipped, true\);/);
assert.match(tab5, /oper\.rotation_angle = g_upright \? \(\(g_rotation & 0x02\) \? PPA_SRM_ROTATION_ANGLE_180/);
assert.ok(!/M5\.Display\.setRotation\([^)]*g_upright/.test(tab5), 'M5GFX keeps its landscape rotation');
// The JC1060 and the Waveshare 7B are landscape panels (no turn today) and
// the JC4880's profile is upright: no upright layout there yet.
const upright = /constexpr bool upright_ready\(\) \{\n#if ([\s\S]*?)\n  return true;/.exec(read('src/tiles/config/grid_layout.h'))[1];
for (const name of ['JC8012P4A1)', 'JC8012P4A1_V2)', 'LCD_4_3)', 'LCD_7)', 'LCD_8)', 'LCD_10_1)', 'TAB5)']) {
  assert.ok(upright.includes(name), name);
}
for (const name of ['JC1060', 'LCD_7B', 'JC4880']) assert.ok(!upright.includes(name), name + ' stays without');
assert.ok(read('src/devices/device.cpp').includes('void displaySetUpright(bool upright) {\n#if ' + upright + '\n  DeviceImpl::displaySetUpright(upright);'));
const presenter = read('src/devices/common/p4_dsi_camera_presenter.cpp');
assert.match(presenter, /config_\.transform = upright \? Transform::Native0Or180 : landscape_transform_;/);
assert.match(presenter, /p4_dsi_cpu_rotate::copy_into\(framebuffer, config_\.panel_width/);

// --- The switch and the boot.
const grid = read('src/tiles/config/grid_layout.h');
assert.match(grid, /return available\(layout\) && \(\(!needs_rotation\(layout\) && !layout_grid\(layout\)\.half_row\) \|\| upright_ready\(\)\);/);
assert.match(grid, /constexpr uint8_t kSpaceRows = larger\(larger\(Device::kGridRows, whole_rows\(layout_grid\(Layout::kBar\)\)\),/);
// Before the splash: LVGL's screen turns and the driver draws upright. After
// tileConfig.load(): a layout without places falls back to the classic one
// there, and the screen must not turn then.
const ino = read('HomeTiles.ino');
assert.ok(ino.indexOf('displayManager.applyShownScreen();') > ino.indexOf('  tileConfig.load();') &&
  ino.indexOf('  tileConfig.load();') > ino.indexOf('grid_layout::apply(configManager.getConfig().layout);') &&
  ino.indexOf('displayManager.applyShownScreen();') < ino.indexOf('BootSplash::show();'));
assert.match(display, /BoardHAL::displaySetUpright\(grid_layout::turned\(\)\);\n  lv_display_set_resolution\(disp, w, h\);/);

// --- The tile grid: the upright grid's rows and its half row.
const config = read('src/tiles/config/tile_config.h');
assert.match(config, /if \(tile\.layout_hidden \|\| tile\.col >= tilePlaceCols\(\) \|\| tile\.row >= tilePlaceRows\(\)\) return false;/);
assert.match(read('src/tiles/config/tile_config.cpp'),
  /grid_layout::inside\(place\.col, place\.row, place\.span_w, place\.span_h\) &&\n\s*tile_geometry::supported_size\(/,
  'a place in the upright grid below the profile\'s rows is shown');
const tab = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.match(tab, /row_dsc\[GRID_SHOWN_ROWS\] = tile_geometry::extent\(GRID_SHOWN_ROWS, 0\.5f, GRID_CELL_H, GAP\);/);
assert.ok(!/bool occupied\[GRID_ROWS\]\[GRID_COLS\]/.test(tab), 'the occupancy covers the upright rows');
assert.match(read('src/web/server/render/web_admin_scripts.cpp'),
  /String\(GRID_SHOWN_ROWS\) \+ \(grid_layout::shown\(\)\.half_row \? "\.5" : ""\)/);
assert.match(read('src/web/assets/admin.css'), /grid-template-rows:repeat\(var\(--grid-rows\), var\(--preview-cell-h\)\) var\(--grid-half-track,\);/);
const handler = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(handler, /setPlaceGrid\(classic_places \|\| \(screensaver_grid && !screensaver_layout\)\);/);
assert.match(handler, /const float first_row = screensaver_layout \? screensaver_places::first_row\(grid_layout::active\(\)\)/);

// --- The own camera turns with the panel: upright, its image gets a
// clockwise quarter turn (V2 hardware 2026-10-08: three quarters stood upside
// down). The 8-inch's quarter-turn mounting then adds up to 180 degrees:
// sensor flips, no Bridge turn.
const camera = read('src/video/local_camera/local_camera.cpp');
assert.match(camera, /uint8_t displayQuarterTurns\(\) \{ return grid_layout::turned\(\) \? 1u : 0u; \}/);
const contract = read('src/video/local_camera/local_camera_contract.h');
const turnsOf = (quarter, rotated180, user) => ((quarter ? 1 : 0) + (rotated180 ? 2 : 0) + user) & 3;
assert.match(contract, /\(\(quarter_turn \? 1u : 0u\) \+ \(rotated_180 \? 2u : 0u\) \+ user_turns\) & 3u;/);
assert.equal(turnsOf(true, false, 0 + 1), 2, '8-inch upright: sensor flips, no Bridge turn');
assert.equal(turnsOf(true, false, 0), 1, '8-inch landscape: the Bridge turns 90 as before');
assert.equal(turnsOf(false, false, 0 + 1), 1, 'V2 upright: the Bridge turns 90 clockwise');
assert.equal(turnsOf(false, false, 0), 0, 'V2 landscape unchanged');

// --- Settings and the screensaver take the screen of this boot.
assert.match(read('src/ui/tabs/settings/settings_screen.cpp'),
  /return w > h \? Layout::Split : h > w \? Layout::Portrait : Layout::Tabs;/);
const saver = read('src/ui/screensaver/image_screensaver.cpp');
assert.ok(!saver.includes('Device::kScreenWidth'), 'the wallpaper is fitted to the upright screen');
assert.match(read('src/ui/screensaver/screensaver_places.cpp'), /const float count = layout == Layout::kPortrait \? 3 : 2;/);

console.log('Hochkant: upright copy, touch, grid rows, half row, Settings and screensaver');
