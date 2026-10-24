// Energy flow: what a change redraws. A line that switches on or off redraws
// only where it runs, its dot included (b539 log: the whole card, 42-49 ms
// frames on the V2, each time the grid crossed its idle threshold); a bend's
// picture holds its whole stroke with the round ends (b540 log: LVGL built
// a bend's circle masks in every frame, ~0.25 ms). Compiles the renderer's
// real invalidate_line and bend_box with the real layout on the host.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const renderer = read('src/types/energy_flow/renderer.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(renderer).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const withoutIncludes = text => text.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Energy flow redraw areas need a native C++ compiler'); process.exit(0); }

const cpp = String.raw`
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <math.h>
#include <stdint.h>
#include <vector>
${withoutIncludes(read('src/types/energy_flow/flow_model.h'))}
${withoutIncludes(read('src/types/energy_flow/flow_geometry.h'))}
using namespace energy_flow;
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
${fn('bend_box')}
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
    if (!path.bend.present) continue;
    // The bend's picture box holds its stroke and both round ends.
    const lv_area_t box = bend_box(st, path);
    const float outer = path.bend.radius + st.line_width / 2;
    const float inner = outer - st.line_width;
    for (int a = path.bend.start_angle * 4; a <= path.bend.end_angle * 4; ++a) {
      const float angle = a / 4.0f * 3.14159265f / 180.0f;
      for (float r = inner; r <= outer; r += 0.5f) {
        const float x = path.bend.cx + r * cosf(angle), y = path.bend.cy + r * sinf(angle);
        assert(x >= box.x1 && x <= box.x2 && y >= box.y1 && y <= box.y2);
      }
    }
    for (int end : {path.bend.start_angle, path.bend.end_angle}) {
      const float angle = end * 3.14159265f / 180.0f;
      const float mid = outer - st.line_width / 2.0f;
      const float ex = path.bend.cx + mid * cosf(angle), ey = path.bend.cy + mid * sinf(angle);
      for (int k = 0; k < 64; ++k) {
        const float t = k * 2.0f * 3.14159265f / 64.0f;
        const float x = ex + st.line_width / 2.0f * cosf(t), y = ey + st.line_width / 2.0f * sinf(t);
        assert(x >= box.x1 && x <= box.x2 && y >= box.y1 && y <= box.y2);
      }
    }
    // A quarter of the circle, not more.
    assert(box.x2 - box.x1 <= outer + 2 * (st.line_width + 2) + 1 && box.y2 - box.y1 <= outer + 2 * (st.line_width + 2) + 1);
  }
  std::printf("ok %ld\n", all);
}
`;
const out = path.join(root, 'build/tests/energy-flow-redraw-areas');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let r = spawnSync(compiler, ['-std=c++17', source, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
// Wiring: a switched line redraws itself, the bends draw their pictures once
// per color and fall back to the arc, the pictures go with the tile.
assert.match(renderer, /if \(changed && st\.line_shown\[line\]\) invalidate_line\(st, line\);\s*if \(st\.line_shown\[line\]\) render_bend\(st, line, color\);/);
assert.doesNotMatch(renderer, /lv_obj_invalidate\(st\.canvas\)/, 'no whole-card redraw on a value change');
assert.match(renderer, /if \(picture && st->bend_color\[line\] == st->line_color\[line\]\) \{[\s\S]*?lv_draw_image\(layer, &image_dsc, &area\);\s*\} else \{[\s\S]*?lv_draw_arc\(layer, &arc_dsc\);/);
assert.match(renderer, /for \(lv_draw_buf_t\*& picture : st->bend_picture\) \{\s*if \(!picture\) continue;\s*lv_image_cache_drop\(picture\);\s*lv_draw_buf_destroy\(picture\);/);
console.log('Energy flow redraw areas: a switched line redraws only itself with its dot, a bend picture holds its stroke');
