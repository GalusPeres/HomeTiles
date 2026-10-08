// Hochkant (user 2026-10-08, layouts step 2): the upright layout on a panel
// that is landscape in its profile. The Guition V2 panel is upright by itself
// (800 x 1280): the landscape UI is turned into it, the upright UI is copied
// straight (180: turned into the mirrored place) and the touch follows the
// same framebuffer mapping. The tile grid gets the upright grid's rows and
// half row (4 x 6.5), the screensaver three rows at the bottom.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// --- Native: the copy and the touch mapping against the landscape turn.
const compiler = ['clang++', 'g++'].find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: native upright copy check needs a C++ compiler');
} else {
  const driver = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
  const touch = driver.slice(driver.indexOf('  if (g_upright) {\n    const int32_t fb_x'),
    driver.indexOf('  if (mapped_x < 0) mapped_x = 0;'));
  const code = `#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
${read('src/devices/common/p4_dsi_cpu_rotate.h').replace('#pragma once', '')}
constexpr int32_t W = 800, H = 1280;  // the panel, upright
// The landscape UI point (x, y) in the framebuffer (draw_landscape_area).
void landscape_to_fb(int x, int y, bool flip, int& fx, int& fy) {
  if (flip) { fx = y; fy = (H - 1) - x; } else { fx = (W - 1) - y; fy = x; }
}
void map(bool g_upright, uint8_t g_rotation, int32_t px0, int32_t py0, int32_t& mapped_x, int32_t& mapped_y) {
  const int32_t logical_w = g_upright ? W : H;
  const int32_t logical_h = g_upright ? H : W;
  const int32_t px[1] = {px0}, py[1] = {py0};
  const int selected = 0;
${touch}}
int main() {
  // The touch's raw axes from the landscape mapping at rotation 0.
  for (int x : {0, 17, 640, 1279}) for (int y : {0, 33, 400, 799}) {
    int fx, fy; landscape_to_fb(x, y, false, fx, fy);
    const int32_t px = x, py = y;  // landscape rotation 0 maps px, py through
    // Upright: the touch lands on the framebuffer pixel, 180 on its mirror.
    int32_t ux, uy; map(true, 0, px, py, ux, uy); assert(ux == fx && uy == fy);
    map(true, 2, px, py, ux, uy); assert(ux == W - 1 - fx && uy == H - 1 - fy);
    // Landscape flipped keeps its own mapping (same raw axes).
    int lfx, lfy; landscape_to_fb(x, y, true, lfx, lfy);
    int32_t lx, ly; map(false, 2, px, py, lx, ly);
    int cfx, cfy; landscape_to_fb(lx, ly, true, cfx, cfy); assert(cfx == fx && cfy == fy);
  }
  // The copy: straight, and 180 into the mirrored place.
  std::vector<uint16_t> fb(W * H, 0), src(5 * 3);
  for (int i = 0; i < 15; ++i) src[i] = static_cast<uint16_t>(i + 1);
  p4_dsi_cpu_rotate::copy_into(fb.data(), W, W, H, 10, 20, 5, 3, src.data(), false);
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 5; ++c) assert(fb[(20 + r) * W + 10 + c] == src[r * 5 + c]);
  std::fill(fb.begin(), fb.end(), 0);
  p4_dsi_cpu_rotate::copy_into(fb.data(), W, W, H, 10, 20, 5, 3, src.data(), true);
  for (int r = 0; r < 3; ++r) for (int c = 0; c < 5; ++c) {
    // UI pixel (10 + c, 20 + r) shows at (W-1-(10+c), H-1-(20+r)).
    assert(fb[(H - 1 - (20 + r)) * W + (W - 1 - (10 + c))] == src[r * 5 + c]);
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

// --- The switch: only a panel that draws upright starts a turned layout.
const grid = read('src/tiles/config/grid_layout.h');
assert.match(grid, /constexpr bool upright_ready\(\) \{\n#if defined\(DEVICE_GUITION_JC8012P4A1_V2\)\n  return true;/);
assert.match(grid, /return available\(layout\) && \(\(!needs_rotation\(layout\) && !layout_grid\(layout\)\.half_row\) \|\| upright_ready\(\)\);/);
assert.match(grid, /constexpr uint8_t kSpaceRows = larger\(larger\(Device::kGridRows, whole_rows\(layout_grid\(Layout::kBar\)\)\),/);
assert.match(read('src/devices/device.cpp'), /void displaySetUpright\(bool upright\) \{\n#if defined\(DEVICE_GUITION_JC8012P4A1_V2\)/);
// Before the splash: LVGL's screen turns and the driver draws upright. After
// tileConfig.load(): a layout without places falls back to the classic one
// there, and the screen must not turn then.
const ino = read('HomeTiles.ino');
assert.ok(ino.indexOf('displayManager.applyShownScreen();') > ino.indexOf('  tileConfig.load();') &&
  ino.indexOf('  tileConfig.load();') > ino.indexOf('grid_layout::apply(configManager.getConfig().layout);') &&
  ino.indexOf('displayManager.applyShownScreen();') < ino.indexOf('BootSplash::show();'));
const display = read('src/core/display/display_manager.cpp');
assert.match(display, /BoardHAL::displaySetUpright\(grid_layout::turned\(\)\);\n  lv_display_set_resolution\(disp, w, h\);/);
const driver = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
assert.match(driver, /g_camera_presenter\.setTransform\(upright \? p4_dsi_camera_presenter::Transform::Native0Or180/,
  'the camera popup frames go straight into the upright screen');

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

// --- Settings and the screensaver take the screen of this boot.
assert.match(read('src/ui/tabs/settings/settings_screen.cpp'),
  /return w > h \? Layout::Split : h > w \? Layout::Portrait : Layout::Tabs;/);
const saver = read('src/ui/screensaver/image_screensaver.cpp');
assert.ok(!saver.includes('Device::kScreenWidth'), 'the wallpaper is fitted to the upright screen');
assert.match(read('src/ui/screensaver/screensaver_places.cpp'), /const float count = layout == Layout::kPortrait \? 3 : 2;/);

console.log('Hochkant: upright copy, touch, grid rows, half row, Settings and screensaver');
