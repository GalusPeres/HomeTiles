// The head bar layout's grid (src/tiles/config/grid_layout.h, user picks
// 2026-10-07): per screen the columns and rows below the head, the cells as
// large as the rest allows, the head's frame like the popups'. The profile's
// own grid stays the former constants. Compiled against the real header with
// a Device stub per panel class.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: head bar grid checks require a C++ compiler');
  process.exit(0);
}

// Screen, profile grid and the expected bar grid (emulator layouts.mjs,
// build/design-mockups/layouts/layouts-data.json): cols x rows, cell w x h,
// top margin (head), side margin.
const panels = [
  {name: 'Guition V2 / 8" / 10.1"', define: '', w: 1280, h: 800, cols: 7, rows: 5, cw: 168, ch: 145, gap: 16, pad: 4,
   bar: {cols: 6, rows: 4, cw: 193, ch: 150, top: 132, pad: 20},
   upright: {cols: 4, rows: 6, half: true, cw: 178, ch: 160}},
  {name: 'Tab5 / Waveshare 7"', define: '', w: 1280, h: 720, cols: 7, rows: 4, cw: 168, ch: 166, gap: 16, pad: 4,
   bar: {cols: 5, rows: 3, cw: 235, ch: 178, top: 132, pad: 20},
   upright: {cols: 3, rows: 6, half: false, cw: 216, ch: 174}},
  {name: 'Guition 7" / Waveshare 7B', define: 'DEVICE_LAYOUT_1024X600', w: 1024, h: 600, cols: 6, rows: 4, cw: 156, ch: 136,
   gap: 16, pad: 4, bar: {cols: 5, rows: 3, cw: 184, ch: 146, top: 108, pad: 20},
   upright: {cols: 3, rows: 6, half: false, cw: 176, ch: 136}},
  {name: 'Waveshare 4B', define: '', w: 720, h: 720, cols: 4, rows: 4, cw: 166, ch: 166, gap: 16, pad: 4,
   bar: {cols: 3, rows: 3, cw: 216, ch: 178, top: 132, pad: 20}},
  {name: 'Guition S3', define: 'DEVICE_LAYOUT_480X480', w: 480, h: 480, cols: 4, rows: 4, cw: 111, ch: 111, gap: 10, pad: 3,
   bar: {cols: 3, rows: 3, cw: 144, ch: 120, top: 87, pad: 13}},
  {name: 'Waveshare 4.3"', define: 'DEVICE_LAYOUT_480X480', w: 800, h: 480, cols: 5, rows: 4, cw: 150, ch: 111, gap: 10, pad: 3,
   bar: {cols: 4, rows: 3, cw: 186, ch: 120, top: 87, pad: 13},
   upright: {cols: 3, rows: 5, half: true, cw: 144, ch: 119}},
];

const buildRoot = path.join(repoRoot, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'head-bar-grid-'));
try {
  for (const panel of panels) {
    const stub = path.join(tempRoot, panel.name.replace(/[^a-z0-9]+/gi, '_'));
    fs.mkdirSync(path.join(stub, 'src', 'devices'), {recursive: true});
    fs.writeFileSync(path.join(stub, 'src', 'devices', 'device.h'), `#pragma once
#include <stdint.h>
namespace Device {
inline constexpr uint16_t kScreenWidth = ${panel.w};
inline constexpr uint16_t kScreenHeight = ${panel.h};
inline constexpr uint8_t kGridCols = ${panel.cols};
inline constexpr uint8_t kGridRows = ${panel.rows};
inline constexpr uint16_t kGridGap = ${panel.gap};
inline constexpr uint16_t kGridPad = ${panel.pad};
inline constexpr uint16_t kGridCellW = ${panel.cw};
inline constexpr uint16_t kGridCellH = ${panel.ch};
}
`);
    fs.writeFileSync(path.join(stub, 'src', 'devices', 'device_select.h'), '#pragma once\n');
    const b = panel.bar;
    const extraX = panel.w - (b.cols * b.cw + (b.cols - 1) * panel.gap + 2 * b.pad);
    const extraY = panel.h - (b.rows * b.ch + (b.rows - 1) * panel.gap + b.top + b.pad);
    const pExtraX = panel.w - (panel.cols * panel.cw + (panel.cols - 1) * panel.gap + 2 * panel.pad);
    const pExtraY = panel.h - (panel.rows * panel.ch + (panel.rows - 1) * panel.gap + 2 * panel.pad);
    const source = `
#include "src/tiles/config/grid_layout.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do { if (!(c)) { std::printf("failed: %s\\n", #c); std::exit(1); } } while (0)
int main() {
  // Without the bar: the profile's grid, margins split like before.
  constexpr grid_layout::Shown p = grid_layout::profile_grid();
  static_assert(p.cols == ${panel.cols} && p.rows == ${panel.rows} && p.cell_w == ${panel.cw} && p.cell_h == ${panel.ch}, "profile");
  static_assert(p.pad_left == ${panel.pad + Math.trunc(pExtraX / 2)} && p.pad_top == ${panel.pad + Math.trunc(pExtraY / 2)}, "profile margins");
  static_assert(!p.head_bar, "profile has no head");
  CHECK(!grid_layout::head_bar() && grid_layout::shown().cols == ${panel.cols});
  // With the bar: the picked grid below the head (card margin, gap, the X's
  // box, gap), the leftover pixels split between opposite margins.
  constexpr grid_layout::Shown b = grid_layout::layout_grid(grid_layout::Layout::kBar);
  static_assert(b.cols == ${b.cols} && b.rows == ${b.rows}, "bar grid");
  static_assert(b.cell_w == ${b.cw} && b.cell_h == ${b.ch}, "bar cells");
  static_assert(b.pad_left == ${b.pad + Math.trunc(extraX / 2)} && b.pad_right == ${b.pad + extraX - Math.trunc(extraX / 2)}, "bar sides");
  static_assert(b.pad_top == ${b.top + Math.trunc(extraY / 2)} && b.pad_bottom == ${b.pad + extraY - Math.trunc(extraY / 2)}, "bar top");
  static_assert(b.cols <= p.cols && b.rows <= p.rows, "stored positions keep the profile's space");
  static_assert(grid_layout::switchable(grid_layout::Layout::kBar), "the landscape bar needs no turn here");
  ${panel.upright ? `// Upright (prepared; the turned screen comes later): the screen's
  // own picks, a half row where the user chose one.
  constexpr grid_layout::Shown u = grid_layout::layout_grid(grid_layout::Layout::kPortrait);
  static_assert(u.portrait && u.screen_w == ${panel.h} && u.screen_h == ${panel.w}, "upright screen");
  static_assert(u.cols == ${panel.upright.cols} && u.rows == ${panel.upright.rows} && u.half_row == ${panel.upright.half}, "upright grid");
  static_assert(u.cell_w == ${panel.upright.cw} && u.cell_h == ${panel.upright.ch}, "upright cells");
  static_assert(grid_layout::available(grid_layout::Layout::kPortrait) &&
                grid_layout::needs_rotation(grid_layout::Layout::kPortrait) &&
                !grid_layout::switchable(grid_layout::Layout::kPortrait), "upright waits for the turn");
  CHECK(grid_layout::apply(2) == grid_layout::Layout::kClassic && !grid_layout::head_bar());`
  : `static_assert(!grid_layout::available(grid_layout::Layout::kPortrait), "a square panel has no upright layout");`}
  CHECK(grid_layout::apply(1) == grid_layout::Layout::kBar);
  CHECK(grid_layout::head_bar() && grid_layout::shown().cell_h == ${b.ch});
  // A tile shows only wholly inside the shown grid; the stored grid is larger.
  CHECK(grid_layout::inside(${b.cols - 1}, ${b.rows - 1}, 1, 1));
  CHECK(!grid_layout::inside(${b.cols - 1}, 0, 2, 1));
  CHECK(!grid_layout::inside(0, ${b.rows - 0.5}, 1, 1) && grid_layout::inside(0, ${b.rows - 0.5}, 1, 0.5f));
  CHECK(grid_layout::apply(0) == grid_layout::Layout::kClassic);
  CHECK(!grid_layout::head_bar() && grid_layout::shown().cell_w == ${panel.cw});
  CHECK(grid_layout::apply(7) == grid_layout::Layout::kClassic);
  return 0;
}
`;
    const file = path.join(stub, 'test.cpp');
    fs.writeFileSync(file, source);
    const exe = path.join(stub, process.platform === 'win32' ? 'test.exe' : 'test');
    const defines = panel.define ? [`-D${panel.define}`] : [];
    const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Werror', ...defines, `-I${stub}`, `-I${repoRoot}`,
      file, '-o', exe], {encoding: 'utf8'});
    assert.equal(build.status, 0, `${panel.name}:\n${build.stderr}`);
    const run = spawnSync(exe, [], {encoding: 'utf8'});
    assert.equal(run.status, 0, `${panel.name}: ${run.stdout}${run.stderr}`);
  }
  console.log(`Head bar grids (landscape and upright) match the picks on ${panels.length} panels; the profile grid is unchanged`);
} finally {
  fs.rmSync(tempRoot, {recursive: true, force: true});
}
