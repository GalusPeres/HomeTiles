// Power flow: what a change redraws. A line that switches on or off redraws
// only where it runs, its dot included (b539 log: the whole card, 42-49 ms
// frames on the V2, each time the grid crossed its idle threshold); each
// line's stroke is one picture made once with the layout and kept through
// color changes (test-power-flow-stroke.mjs checks its pixels). Compiles
// the renderer's real invalidate_line with the real layout on the host.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const renderer = read('src/types/power_flow/renderer.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(renderer).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const withoutIncludes = text => text.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Power flow redraw areas need a native C++ compiler'); process.exit(0); }

const cpp = String.raw`
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <math.h>
#include <stdint.h>
#include <vector>
${withoutIncludes(read('src/types/power_flow/flow_model.h'))}
${withoutIncludes(read('src/types/power_flow/flow_geometry.h'))}
using namespace power_flow;
struct lv_area_t {int32_t x1, y1, x2, y2;};
struct lv_obj_t {lv_area_t coords;};
#define LV_COORD_MAX 1000000
#define LV_COORD_MIN (-1000000)
#define LV_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LV_MAX(a, b) ((a) > (b) ? (a) : (b))
void lv_obj_get_coords(const lv_obj_t* o, lv_area_t* a) {*a = o->coords;}
void lv_area_increase(lv_area_t* a, int32_t w, int32_t h) {a->x1 -= w; a->x2 += w; a->y1 -= h; a->y2 += h;}
void lv_area_move(lv_area_t* a, int32_t x, int32_t y) {a->x1 += x; a->x2 += x; a->y1 += y; a->y2 += y;}
std::vector<lv_area_t> marked;
void lv_obj_invalidate_area(lv_obj_t*, const lv_area_t* a) {marked.push_back(*a);}
int round_px(float value) {return static_cast<int>(lroundf(value));}
struct FlowTileState {Layout layout; lv_obj_t* canvas = nullptr; int line_width = 4; int dot_radius = 5;};
${fn('invalidate_line')}
static bool marked_at(int x, int y) {
  for (const lv_area_t& a : marked) if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) return true;
  return false;
}
int main() {
  lv_obj_t canvas{{858, 296, 858 + 403, 296 + 319}};
  FlowTileState st;
  st.canvas = &canvas;
  st.layout = layout(404.0f, 320.0f, Metrics{});  // the V2 2x2 tile
  long all = 0;
  for (uint8_t line = 0; line < kLineCount; ++line) {
    const Polyline& path = st.layout.line[line];
    marked.clear();
    invalidate_line(st, line);
    assert(!marked.empty() && marked.size() <= 6);
    // Every pixel the stroke or its dot can cover, all along the line.
    const float reach = st.dot_radius + 0.5f;
    const float length = polyline_length(path);
    for (float d = 0.0f; d <= length; d += 0.5f) {
      const Point p = point_at(path, d);
      for (int dy = -st.dot_radius; dy <= st.dot_radius; ++dy)
        for (int dx = -st.dot_radius; dx <= st.dot_radius; ++dx) {
          if (dx * dx + dy * dy > reach * reach) continue;
          assert(marked_at(canvas.coords.x1 + round_px(p.x) + dx, canvas.coords.y1 + round_px(p.y) + dy));
        }
    }
    long size = 0;
    for (const lv_area_t& a : marked) size += long(a.x2 - a.x1 + 1) * (a.y2 - a.y1 + 1);
    all += size;
    // Never the whole card again: a line redraws a part of it.
    assert(size < 404L * 320L / 2);
  }
  std::printf("ok %ld\n", all);
}
`;
const out = path.join(root, 'build/tests/power-flow-redraw-areas');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let r = spawnSync(compiler, ['-std=c++17', source, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
// Wiring: a switched line redraws itself, its picture is made once with
// the layout and drawn in the line's color now, and goes with the tile.
assert.match(renderer, /if \(changed && st\.line_shown\[line\]\) invalidate_line\(st, line\);\s*\}/);
assert.doesNotMatch(renderer, /lv_obj_invalidate\(st\.canvas\)/, 'no whole-card redraw on a value change');
assert.match(renderer, /st->line_color\[line\] = st->idle_color;\s*\}\s*render_strokes\(\*st\);/);
assert.match(renderer, /if \(st->stroke_pixels\[line\]\) \{\s*image_dsc\.src = &st->stroke\[line\];\s*image_dsc\.recolor = lv_color_hex\(st->line_color\[line\]\);\s*lv_draw_image\(layer, &image_dsc, &box\);/);
assert.match(renderer, /lv_image_cache_drop\(&st->stroke\[line\]\);\s*lv_free\(st->stroke_pixels\[line\]\);/);
assert.doesNotMatch(renderer, /lv_draw_arc\(/, 'no LVGL arc beside LVGL lines (kinks where they met)');
console.log('Power flow redraw areas: a switched line redraws only itself with its dot, its picture is made once');
