import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

// Small UI regions on the P4 DSI panels are rotated by the CPU. They are now
// written straight into the framebuffer (p4_dsi_cpu_rotate.h) instead of
// through a PSRAM rotate buffer; the pixels must land exactly where the former
// buffer loops and the row copy put them, for both rotations.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const drivers = [
  'src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp',
  'src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.cpp',
  // The same driver code (optimisation hand-over 2026-10-08), compile only.
  'src/devices/guition_jc8012p4a1/device_guition_jc8012p4a1.cpp',
  'src/devices/waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.cpp',
  'src/devices/waveshare_touch_lcd_4_3/device_waveshare_touch_lcd_4_3.cpp',
  'src/devices/waveshare_touch_lcd_7/device_waveshare_touch_lcd_7.cpp',
];
for (const driverPath of drivers) {
  const driver = fs.readFileSync(path.join(root, driverPath), 'utf8');
  const draw = cppFunctionDefinitions(driver).find(f => f.name === 'draw_landscape_area');
  assert.ok(draw, `${driverPath}: draw_landscape_area is missing`);
  const direct = draw.source.indexOf('p4_dsi_cpu_rotate::rotate_into(');
  const buffered = draw.source.indexOf('ensure_rotate_buffer(');
  assert.ok(direct >= 0, `${driverPath}: small regions must rotate into the framebuffer`);
  assert.ok(buffered > direct,
            `${driverPath}: the rotate buffer stays only as fallback without a framebuffer`);
  const directPath = draw.source.slice(direct, buffered);
  for (const step of ['flush_framebuffer_rect(fb, dst_x, dst_y, dst_w, dst_h)',
    'mark_dirty_rect(dst_x, dst_y, dst_w, dst_h)',
    'g_camera_presenter.noteUiWrite(dst_x, dst_y, dst_w, dst_h, false)']) {
    assert.ok(directPath.includes(step), `${driverPath}: direct path must keep ${step}`);
  }
}

// The Tab5 (M5GFX, RGB565_SWAPPED UI): landscape areas below the PPA's width
// are turned by the CPU into the panel framebuffer at the PPA's places, bytes
// swapped, instead of through M5GFX; every PPA write drops its cached lines.
{
  const tab5 = fs.readFileSync(path.join(root, 'src/devices/m5stacks_tab5/device_m5stacks_tab5.cpp'), 'utf8');
  const push = cppFunctionDefinitions(tab5).find(f => f.name === 'push_pixels_with_ppa_fallback');
  assert.ok(push, 'Tab5: push_pixels_with_ppa_fallback is missing');
  const cpu = push.source.indexOf('p4_dsi_cpu_rotate::rotate_into(g_panel_fb, kPanelWidth, dst_x, dst_y, w, h, data, flipped, true);');
  assert.ok(cpu > push.source.indexOf('ppa_rotate_to_panel('), 'Tab5: the PPA first, then the CPU');
  assert.ok(cpu < push.source.indexOf('M5.Display.pushImage'), 'Tab5: M5GFX only without the framebuffer');
  assert.match(push.source, /const int32_t dst_x = flipped \? y : kLogicalHeight - y - h;\s*const int32_t dst_y = flipped \? kLogicalWidth - x - w : x;/);
  const ppa = cppFunctionDefinitions(tab5).find(f => f.name === 'ppa_rotate_to_panel').source;
  assert.match(ppa, /\} else if \(g_rotation & 0x02\) \{\s*dst_x = y;\s*dst_y = kLogicalWidth - x - w;\s*\} else \{\s*dst_x = kLogicalHeight - y - h;\s*dst_y = x;/,
    'Tab5: the CPU places equal the PPA\'s');
  assert.equal((ppa.match(/invalidate_panel_rect\(dst_x, dst_y, dst_w, dst_h\);\s*return true;/g) || []).length, 2,
    'Tab5: both PPA success paths drop the cached lines');
}

const cxx = ['clang++', 'g++'].find(name => spawnSync(name, ['--version']).status === 0);
if (!cxx) {
  console.log('SKIP: P4 CPU rotation runtime test requires a host C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/p4-cpu-rotate');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/devices/common/p4_dsi_cpu_rotate.h"

// The former driver path: rotate into a buffer, then copy its rows.
static void reference(std::vector<uint16_t>& fb, int stride, int dst_x, int dst_y,
                      int w, int h, const uint16_t* data, bool bit2) {
  std::vector<uint16_t> buf(static_cast<size_t>(w) * h);
  if (bit2) {
    for (int sy = 0; sy < h; ++sy)
      for (int sx = 0; sx < w; ++sx) buf[static_cast<size_t>(w - 1 - sx) * h + sy] = data[sy * w + sx];
  } else {
    for (int sy = 0; sy < h; ++sy)
      for (int sx = 0; sx < w; ++sx) buf[static_cast<size_t>(sx) * h + (h - 1 - sy)] = data[sy * w + sx];
  }
  const int dst_w = h, dst_h = w;
  for (int row = 0; row < dst_h; ++row)
    std::memcpy(&fb[static_cast<size_t>(dst_y + row) * stride + dst_x], &buf[static_cast<size_t>(row) * dst_w],
                dst_w * sizeof(uint16_t));
}

int main() {
  const int panel_w = 800, panel_h = 1280;  // portrait framebuffer
  unsigned seed = 12345;
  auto next = [&]() { seed = seed * 1103515245u + 12345u; return seed >> 8; };
  const int sizes[][2] = {{1, 1}, {1, 40}, {37, 1}, {64, 48}, {200, 120}, {599, 72}, {13, 799}};
  for (const auto& size : sizes) {
    for (int bit2 = 0; bit2 < 2; ++bit2) {
      const int w = size[0], h = size[1];
      std::vector<uint16_t> data(static_cast<size_t>(w) * h);
      for (auto& px : data) px = static_cast<uint16_t>(next());
      const int dst_x = static_cast<int>(next() % (panel_w - h + 1));
      const int dst_y = static_cast<int>(next() % (panel_h - w + 1));
      std::vector<uint16_t> expected(static_cast<size_t>(panel_w) * panel_h, 0xA5A5);
      std::vector<uint16_t> actual(expected);
      reference(expected, panel_w, dst_x, dst_y, w, h, data.data(), bit2 != 0);
      p4_dsi_cpu_rotate::rotate_into(actual.data(), panel_w, dst_x, dst_y, w, h, data.data(), bit2 != 0);
      if (expected != actual) {
        std::printf("mismatch w=%d h=%d bit2=%d\n", w, h, bit2);
        return 1;
      }
      // Swapped bytes (Tab5): the same places, every pixel byte-swapped.
      std::vector<uint16_t> swapped_expected(static_cast<size_t>(panel_w) * panel_h, 0xA5A5);
      std::vector<uint16_t> swapped_data(data);
      for (auto& px : swapped_data) px = static_cast<uint16_t>((px << 8) | (px >> 8));
      reference(swapped_expected, panel_w, dst_x, dst_y, w, h, swapped_data.data(), bit2 != 0);
      std::vector<uint16_t> swapped_actual(static_cast<size_t>(panel_w) * panel_h, 0xA5A5);
      p4_dsi_cpu_rotate::rotate_into(swapped_actual.data(), panel_w, dst_x, dst_y, w, h, data.data(), bit2 != 0, true);
      if (swapped_expected != swapped_actual) {
        std::printf("swap mismatch w=%d h=%d bit2=%d\n", w, h, bit2);
        return 1;
      }
    }
  }
  return 0;
}
`;
const file = path.join(out, 'rotate.cpp');
fs.writeFileSync(file, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'rotate.exe' : 'rotate');
let result = spawnSync(cxx, ['-std=c++17', '-O1', '-Wall', '-Wextra', '-I', root, file, '-o', binary],
                       {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('P4 CPU rotation: direct framebuffer path equals the former rotate buffer, both rotations: PASS');
