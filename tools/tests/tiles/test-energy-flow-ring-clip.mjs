// Energy flow: a dot's redraw leaves out what a node ring covers completely
// (b539 log: each dot step near a node drew its ring, disc and icon again,
// ~0.45 ms of every frame on the V2). Compiles the renderer's real
// clip_to_rings on the host: only pixels inside the ring's box without its
// rounded corners are ever dropped, a dot near a corner or beside a hidden
// ring keeps its whole area.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const renderer = fs.readFileSync(path.join(root, 'src/types/energy_flow/renderer.cpp'), 'utf8');
const fn = cppFunctionDefinitions(renderer).find(f => f.name === 'clip_to_rings');
assert.ok(fn, 'clip_to_rings');
assert.match(renderer, /if \(!clip_to_rings\(st, area\)\) return;\s*lv_obj_invalidate_area\(st\.canvas, &area\);/,
             'every dot area goes through the ring clip before it is invalidated');
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(c => spawnSync(c, ['--version']).status === 0);
if (!compiler) { console.log('SKIP: Energy flow ring clip needs a native C++ compiler'); process.exit(0); }

const cpp = String.raw`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
struct lv_area_t {int32_t x1, y1, x2, y2;};
struct lv_obj_t {lv_area_t coords; int32_t radius; bool hidden;};
enum {LV_OBJ_FLAG_HIDDEN = 1, LV_PART_MAIN = 0};
#define LV_MIN(a, b) ((a) < (b) ? (a) : (b))
bool lv_obj_has_flag(const lv_obj_t* o, int) {return o->hidden;}
void lv_obj_get_coords(const lv_obj_t* o, lv_area_t* a) {*a = o->coords;}
int32_t lv_obj_get_style_radius(const lv_obj_t* o, int) {return o->radius;}
int32_t lv_area_get_width(const lv_area_t* a) {return a->x2 - a->x1 + 1;}
int32_t lv_area_get_height(const lv_area_t* a) {return a->y2 - a->y1 + 1;}
bool lv_area_is_on(const lv_area_t* a, const lv_area_t* b) {
  return !(a->x1 > b->x2 || b->x1 > a->x2 || a->y1 > b->y2 || b->y1 > a->y2);
}
struct NodeWidgets {lv_obj_t* ring = nullptr; bool shown = false;};
struct FlowTileState {NodeWidgets node[4];};
${fn.source}
static bool in(const lv_area_t& a, int x, int y) {return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;}
// Whether the opaque rounded ring covers the pixel: its box without the
// corner squares its radius rounds (conservative, as the clip must be).
static bool covered(const lv_obj_t& ring, int x, int y) {
  const lv_area_t& b = ring.coords;
  if (!in(b, x, y)) return false;
  const int r = LV_MIN(ring.radius, LV_MIN(lv_area_get_width(&b), lv_area_get_height(&b)) / 2);
  const bool corner_x = x < b.x1 + r || x > b.x2 - r;
  const bool corner_y = y < b.y1 + r || y > b.y2 - r;
  return !(corner_x && corner_y);
}
int main() {
  lv_obj_t ring{{100, 100, 171, 171}, 22, false};
  FlowTileState st;
  st.node[0] = {&ring, true};
  // A dot leaving the bottom side: only the part below the ring is left.
  lv_area_t a{130, 165, 141, 176};
  assert(clip_to_rings(st, a) && a.x1 == 130 && a.x2 == 141 && a.y1 == 172 && a.y2 == 176);
  // Wholly under the ring: nothing to redraw.
  a = {130, 150, 141, 161};
  assert(!clip_to_rings(st, a));
  // Leaving the left side.
  a = {95, 130, 106, 141};
  assert(clip_to_rings(st, a) && a.x1 == 95 && a.x2 == 99 && a.y1 == 130 && a.y2 == 141);
  // Beside a rounded corner: the whole area stays.
  a = {160, 165, 171, 176};
  assert(clip_to_rings(st, a) && a.x1 == 160 && a.y1 == 165 && a.x2 == 171 && a.y2 == 176);
  // A hidden ring, or one not shown, covers nothing.
  ring.hidden = true;
  a = {130, 150, 141, 161};
  assert(clip_to_rings(st, a) && a.y1 == 150 && a.y2 == 161);
  ring.hidden = false;
  st.node[0].shown = false;
  assert(clip_to_rings(st, a) && a.y1 == 150);
  st.node[0].shown = true;
  // A full circle covers no straight side: nothing dropped near its edge.
  ring.radius = 0x7FFF;
  a = {130, 165, 141, 176};
  assert(clip_to_rings(st, a) && a.y1 == 165 && a.y2 == 176);
  // Every area around the ring: the result lies in the area, and every
  // pixel it drops is one the ring covers.
  srand(7);
  for (int radius : {0, 10, 22, 36, 0x7FFF}) {
    ring.radius = radius;
    for (int i = 0; i < 20000; ++i) {
      const int w = 1 + rand() % 16, h = 1 + rand() % 16;
      const lv_area_t before{80 + rand() % 110, 80 + rand() % 110, 0, 0};
      lv_area_t area{before.x1, before.y1, before.x1 + w - 1, before.y1 + h - 1};
      const lv_area_t original = area;
      const bool left = clip_to_rings(st, area);
      for (int y = original.y1; y <= original.y2; ++y)
        for (int x = original.x1; x <= original.x2; ++x)
          if (!left || !in(area, x, y)) assert(covered(ring, x, y));
      if (left) assert(area.x1 >= original.x1 && area.x2 <= original.x2 && area.y1 >= original.y1 &&
                       area.y2 <= original.y2 && area.x1 <= area.x2 && area.y1 <= area.y2);
    }
  }
  std::puts("ok");
}
`;
const out = path.join(root, 'build/tests/energy-flow-ring-clip');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let r = spawnSync(compiler, ['-std=c++17', source, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
console.log('Energy flow ring clip: only pixels a ring covers are left out of a dot redraw');
