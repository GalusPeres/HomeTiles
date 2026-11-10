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
  // The panels turned the other way round (JC1060, Waveshare 7B, JC4880):
  // their draw_turned_area geometry, taken from the driver.
  const jc1060 = read('src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.cpp');
  const turnedDst = /  const int32_t dst_x = \(turn & 0x02\) \? y : panel_w - y - h;\n  const int32_t dst_y = \(turn & 0x02\) \? panel_h - x - w : x;\n/.exec(jc1060)[0];
  const code = `#include <cassert>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
${read('src/devices/common/p4_dsi_cpu_rotate.h').replace('#pragma once', '')}
constexpr int32_t W = 800, H = 1280;  // the panel, upright
int g_screen_w = W;
namespace grid_layout { inline bool turned() { return true; } inline int screen_w() { return g_screen_w; } }
// draw_turned_area's place for a UI area on a panel_w x panel_h panel.
void turned_dst(int32_t panel_w, int32_t panel_h, uint8_t turn, int32_t x, int32_t y, int32_t w, int32_t h,
                int32_t& out_x, int32_t& out_y) {
${turnedDst}  out_x = dst_x; out_y = dst_y;
}
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
  // The panels turned the other way round: a 1024 x 600 landscape panel
  // shown upright (JC1060, Waveshare 7B) and the 480 x 800 JC4880 shown in
  // landscape. Every UI pixel lands where a finger on it maps back to it.
  for (const auto& panel : {std::pair<int32_t, int32_t>{1024, 600}, std::pair<int32_t, int32_t>{480, 800}}) {
    const int32_t pw = panel.first, ph = panel.second;
    g_screen_w = ph;  // the turned UI is as wide as the panel is high
    for (uint8_t rotation : {0, 2}) {
      const uint8_t turn = rotation ^ 0x02;
      const int32_t ax = 7, ay = 11, aw = 5, ah = 3;  // a UI area
      std::vector<uint16_t> fb(pw * ph, 0), src(aw * ah);
      for (int i = 0; i < aw * ah; ++i) src[i] = static_cast<uint16_t>(i + 1);
      int32_t dx = 0, dy = 0;
      turned_dst(pw, ph, turn, ax, ay, aw, ah, dx, dy);
      p4_dsi_cpu_rotate::rotate_into(fb.data(), pw, dx, dy, aw, ah, src.data(), (turn & 0x02) != 0);
      int found = 0;
      for (int32_t fy = 0; fy < ph; ++fy) for (int32_t fx = 0; fx < pw; ++fx) {
        const uint16_t v = fb[fy * pw + fx];
        if (!v) continue;
        ++found;
        // The drivers' touch: the panel's point, with 180 the mirrored one.
        int16_t x = static_cast<int16_t>(rotation ? pw - 1 - fx : fx);
        int16_t y = static_cast<int16_t>(rotation ? ph - 1 - fy : fy);
        central(x, y);
        const int i = v - 1;
        assert(x == ax + i % aw && y == ay + i / aw);
      }
      assert(found == aw * ah);
    }
  }
  g_screen_w = W;
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
// The JC1060 and the Waveshare 7B are landscape panels and the JC4880 is
// upright (user 2026-10-11): their drivers turn a turned layout a real
// quarter, the same turn the other way round (draw_turned_area: the shared
// PPA and CPU turn with the rotation's 180 bit flipped), checked natively
// above against the shared touch turn.
const upright = /constexpr bool upright_ready\(\) \{\n#if ([\s\S]*?)\n  return true;/.exec(read('src/tiles/config/grid_layout.h'))[1];
for (const name of ['JC8012P4A1)', 'JC8012P4A1_V2)', 'LCD_4_3)', 'LCD_7)', 'LCD_8)', 'LCD_10_1)', 'TAB5)',
  'JC1060P470C)', 'JC1060P470C_V2)', 'JC1060P470_V3)', 'LCD_7B)', 'JC4880P443_PORTRAIT)']) {
  assert.ok(upright.includes(name), name);
}
const reversed = /constexpr bool turn_reversed\(\) \{\n#if ([\s\S]*?)\n  return true;/.exec(read('src/tiles/config/grid_layout.h'))[1];
for (const name of ['JC1060P470C)', 'JC1060P470C_V2)', 'JC1060P470_V3)', 'LCD_7B)', 'JC4880P443_PORTRAIT)']) assert.ok(reversed.includes(name), name);
assert.ok(!reversed.includes('JC8012') && !reversed.includes('TAB5'), 'the panels upright by themselves turn as before');
const displaySetUpright = /void displaySetUpright\(bool upright\) \{([\s\S]*?)\n\}/
  .exec(read('src/devices/device.cpp'))[1];
assert.ok(displaySetUpright.includes('defined(DEVICE_GUITION_JC1060P470C_FAMILY)') &&
  displaySetUpright.includes('DeviceImpl::displaySetUpright(upright);'));
const turnedDraw = body => body.slice(body.indexOf('bool draw_turned_area('), body.indexOf('}  // namespace\n\nbool Device'));
const turnedRef = turnedDraw(read('src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.cpp'));
assert.ok(turnedRef.includes('g_turn_ppa.rotate(g_camera_presenter, panel_w, panel_h, x, y, w, h, data, turn);') &&
  turnedRef.includes('p4_dsi_cpu_rotate::rotate_into(fb, panel_w, dst_x, dst_y, w, h, data, (turn & 0x02) != 0);'),
  'the shared PPA and CPU turn');
for (const [file, ns, own] of [
  ['guition_jc1060p470c/device_guition_jc1060p470c.cpp', 'DeviceGuitionJC1060P470C', 'draw_landscape_area'],
  ['guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.cpp', 'DeviceGuitionJC1060P470CV2', 'draw_landscape_area'],
  ['waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.cpp', 'DeviceWaveshareTouchLCD7B', 'draw_landscape_area'],
  ['guition_jc4880p443_portrait/device_guition_jc4880p443_portrait.cpp', 'DeviceGuitionJC4880P443Portrait', 'draw_portrait_area']]) {
  const source = read('src/devices/' + file);
  assert.equal(turnedDraw(source), turnedRef, `${file}: the same turned draw`);
  assert.match(source, /uint8_t turn_rotation\(\) \{ return g_rotation \^ 0x02; \}/, `${file}: the other way round`);
  assert.equal((source.match(new RegExp(`  if \\(g_turned\\) \\{\\n    draw_turned_area\\(x, y, w, h, data\\);\\n    return;\\n  \\}\\n  ${own}\\(x, y, w, h, data\\);`, 'g')) || []).length, 2,
    `${file}: both pushes draw turned`);
  assert.ok(source.includes('byte_swap, frame_rotation(),') && source.includes('fullFrameInfo(frame_rotation(), '),
    `${file}: the camera frames turn with the UI`);
  assert.ok(source.includes(`void ${ns}::displaySetUpright(bool upright) {\n  // The turned layout: the UI and the camera popup's frames a quarter turned.\n  g_turned = upright;`) &&
    source.includes('  g_camera_presenter.setUpright(upright);\n}'), `${file}: the shared hook`);
  // Their touch reports the panel's point (with 180 the mirrored one), as the
  // native check above assumes.
  assert.match(source, /  if \(g_rotation & 0x02\) \{\n    mapped_x = static_cast<int32_t>\(display_cfg\.width\) - 1 - static_cast<int32_t>\(px\[selected\]\);\n    mapped_y = static_cast<int32_t>\(display_cfg\.height\) - 1 - static_cast<int32_t>\(py\[selected\]\);\n  \} else \{\n    mapped_x = static_cast<int32_t>\(px\[selected\]\);\n    mapped_y = static_cast<int32_t>\(py\[selected\]\);/,
    `${file}: touch on the panel's own axes`);
}
const presenter = read('src/devices/common/p4_dsi_camera_presenter.cpp');
assert.match(presenter, /config_\.transform = upright \? turned\(own_transform_\) : own_transform_;/);
assert.match(presenter, /return own == Transform::Portrait90Or270 \? Transform::Native0Or180 : Transform::Portrait90Or270;/);
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

// --- The own camera turns with the panel: upright, its image turns a
// quarter back, clockwise on the V2 (hardware 2026-10-08: three quarters
// stood upside down), counter-clockwise on the 8-inch (hardware 2026-10-08:
// one quarter, 180 with its mounting, stood upside down) - their pictures are
// mirrored differently. The user's mirror never turns the picture (V2, user
// 2026-10-11: reversing the turn for it stood the upright picture upside down).
const camera = read('src/video/local_camera/local_camera.cpp');
assert.match(camera, /if \(!grid_layout::turned\(\)\) return 0u;/);
assert.match(camera, /const bool counter_clockwise =\s*local_camera_board::Sensor::kUprightTurnCounterClockwise != grid_layout::turn_reversed\(\);\s*return counter_clockwise \? 3u : 1u;/);
{
  const start = camera.indexOf('uint8_t displayQuarterTurns() {');
  assert.doesNotMatch(camera.slice(start, camera.indexOf('\n}\n', start)), /g_mirror/, 'the user mirror never turns the picture');
}
// Why: desiredOrientation() mirrors on the other sensor axis for a quarter
// turn, so sensor flips plus the Bridge's turn always give the user's mirror
// of the turned picture. Coordinates (x, y), R = a clockwise quarter turn.
{
  const contractSource = read('src/video/local_camera/local_camera_contract.h');
  assert.match(contractSource, /orientation\.mirror = rotated_180 != user_mirror;\s*orientation\.flip = rotated_180;\s*if \(quarter_turn\) \{\s*const bool mirror = orientation\.mirror;\s*orientation\.mirror = orientation\.flip;\s*orientation\.flip = mirror;/);
  // `|| 0` keeps -0 apart from deepEqual's view of 0.
  const mul = (a, b) => [[a[0][0] * b[0][0] + a[0][1] * b[1][0], a[0][0] * b[0][1] + a[0][1] * b[1][1]],
                         [a[1][0] * b[0][0] + a[1][1] * b[1][0], a[1][0] * b[0][1] + a[1][1] * b[1][1]]].map(row => row.map(v => v || 0));
  const I = [[1, 0], [0, 1]], R = [[0, 1], [-1, 0]], Hm = [[-1, 0], [0, 1]], Vf = [[1, 0], [0, -1]];
  const turn = n => Array.from({length: n & 3}).reduce(m => mul(R, m), I);
  for (const turns of [0, 1, 2, 3]) {
    for (const userMirror of [false, true]) {
      const rotated180 = (turns & 2) !== 0, quarter = (turns & 1) !== 0;
      let mirror = rotated180 !== userMirror, flip = rotated180;
      if (quarter) [mirror, flip] = [flip, mirror];
      const delivered = mul(turn(quarter ? 1 : 0), mul(mirror ? Hm : I, flip ? Vf : I));
      assert.deepEqual(delivered, mul(userMirror ? Hm : I, turn(turns)), `${turns} turns, mirror ${userMirror}`);
    }
  }
}
assert.match(camera, /#else\n  return grid_layout::turn_reversed\(\) \? 3u : 1u;/, 'a panel turned the other way round turns its camera back');
assert.match(read('src/video/local_camera/sensors/ov5647/ov5647_sensor.h'), /static constexpr bool kUprightTurnCounterClockwise = true;/);
assert.match(read('src/video/local_camera/sensors/ov02c10/ov02c10_sensor.h'), /static constexpr bool kUprightTurnCounterClockwise = false;/);
assert.match(read('src/video/local_camera/sensors/sc202cs/sc202cs_sensor.h'), /static constexpr bool kUprightTurnCounterClockwise = false;/);
const contract = read('src/video/local_camera/local_camera_contract.h');
const turnsOf = (quarter, rotated180, user) => ((quarter ? 1 : 0) + (rotated180 ? 2 : 0) + user) & 3;
assert.match(contract, /\(\(quarter_turn \? 1u : 0u\) \+ \(rotated_180 \? 2u : 0u\) \+ user_turns\) & 3u;/);
assert.equal(turnsOf(true, false, 0 + 3), 0, '8-inch upright: no flips, no Bridge turn');
assert.equal(turnsOf(true, false, 0), 1, '8-inch landscape: the Bridge turns 90 as before');
assert.equal(turnsOf(false, false, 0 + 1), 1, 'V2 upright: the Bridge turns 90 clockwise');
assert.equal(turnsOf(false, false, 0), 0, 'V2 landscape unchanged');
// One rotation for every layout (user 2026-10-11: set once, right in every
// orientation); an upright layout adds its quarter turn itself.
assert.doesNotMatch(camera, /kPrefsRotationUprightKey|g_rotation_upright/);
assert.match(camera, /uint8_t userTurns\(\) \{ return g_rotation\.load\(\); \}/);
assert.match(camera, /prefs\.putUChar\(kPrefsRotationKey, quarter_turns\)/);
assert.match(camera, /uint8_t rotation\(\) \{ return userTurns\(\); \}/);
assert.doesNotMatch(camera, /g_rotation\.load\(\) \+ displayQuarterTurns\(\)/);
// The Web Admin shows the one rotation under one name in every layout.
assert.match(read('src/web/server/render/web_admin_html.cpp'), /appendHtmlEscaped\(html, tr\.local_camera_rotation\);/);
const i18n = read('src/core/i18n/i18n.cpp');
for (const upright of ['Drehung (hochkant)', 'Rotation (portrait)', 'Obrót (pionowo)']) {
  assert.ok(!i18n.includes(`"${upright}"`), upright);
}
// The sensor window follows the Bridge's turn (user 2026-10-08, drawn):
// the upright strip while it turns, the whole image otherwise.
const strip = (quarter, user, upright) => (turnsOf(quarter, false, user + (upright ? 1 : 0)) & 1) === 1;
assert.equal(strip(true, 0, false), true, '8-inch landscape: the 720x960 strip, the Bridge turns');
assert.equal(strip(true, 0, true), false, '8-inch upright: the whole 1280x960, no turn');
assert.equal(strip(false, 0, false), false, 'V2 landscape: the whole 1280x720');
assert.equal(strip(false, 0, true), true, 'V2 upright: the 544x720 strip, the Bridge turns');
assert.equal(strip(true, 1, false), false, 'a camera rotation of 90 degrees swaps strip and whole');
assert.equal(strip(false, 1, false), true, 'the same on a landscape sensor');

// --- Settings and the screensaver take the screen of this boot.
assert.match(read('src/ui/tabs/settings/settings_screen.cpp'),
  /return w > h \? Layout::Split : h > w \? Layout::Portrait : Layout::Tabs;/);
// The portrait Settings categories: one row, nine tenths of a grid row high
// (user 2026-10-12: a little taller than three quarters looks nicer).
assert.match(read('src/ui/tabs/settings/settings_screen.cpp'),
  /const int tile_h = static_cast<int>\(lroundf\(0\.9f \* \(settings_style::kGrid\.cell_h \+ GRID_GAP\) - GRID_GAP\)\);/);
const saver = read('src/ui/screensaver/image_screensaver.cpp');
assert.ok(!saver.includes('Device::kScreenWidth'), 'the wallpaper is fitted to the upright screen');
assert.match(read('src/ui/screensaver/screensaver_places.cpp'), /const float count = layout == Layout::kPortrait \? 3 : 2;/);

console.log('Hochkant: upright copy, touch, grid rows, half row, Settings and screensaver');
