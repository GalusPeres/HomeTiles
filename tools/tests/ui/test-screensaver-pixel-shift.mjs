// Screensaver pixel shift: the clock and tiles orbit by a few pixels so their
// edges do not stand on the same LCD pixels for hours (image retention). The
// orbit must cover every offset from -radius to +radius on both axes, move
// one small step at a time, stay within half a grid gap, and the screensaver
// must apply it to the tiles and the clock together.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

const harness = String.raw`
#include <cstdio>
#include <cstdlib>
#include "src/ui/screensaver/screensaver_pixel_shift.h"
using namespace screensaver_pixel_shift;
static int fail(const char* what, int a, int b) { std::printf("FAIL %s (%d, %d)\n", what, a, b); return 1; }
int main() {
  for (int gap = 0; gap <= 40; ++gap) {
    const int16_t r = radius_for_gap(gap);
    if (r > kMaxRadius || r * 2 > gap) return fail("radius beyond half a gap", gap, r);
    const uint32_t n = steps_per_orbit(r);
    if (r == 0) { if (offset_at(5, r).dx || offset_at(5, r).dy) return fail("radius 0 moves", gap, 0); continue; }
    bool seen_x[2 * kMaxRadius + 1] = {}, seen_y[2 * kMaxRadius + 1] = {};
    Offset prev = offset_at(n - 1, r);
    for (uint32_t i = 0; i < n; ++i) {
      const Offset o = offset_at(i, r);
      if (std::abs(o.dx) > r || std::abs(o.dy) > r) return fail("offset out of range", o.dx, o.dy);
      const int step = std::abs(o.dx - prev.dx) + std::abs(o.dy - prev.dy);
      if (step != unit_for(r)) return fail("step size", static_cast<int>(i), step);
      seen_x[o.dx + kMaxRadius] = true; seen_y[o.dy + kMaxRadius] = true;
      const Offset w = offset_at(i + n, r);
      if (w.dx != o.dx || w.dy != o.dy) return fail("orbit does not wrap", static_cast<int>(i), r);
      prev = o;
    }
    for (int v = -r; v <= r; v += unit_for(r)) {
      if (!seen_x[v + kMaxRadius] || !seen_y[v + kMaxRadius]) return fail("offset never visited", v, r);
    }
  }
  std::printf("orbit %u radius %d\n", steps_per_orbit(radius_for_gap(12)), radius_for_gap(12));
  return 0;
}
`;
const out = compileAndRun({label: 'Screensaver pixel shift', harness});
if (out !== null) assert.match(out, /^orbit 16 radius 4$/m);

// The screensaver moves tiles and clock together, at a new picture, on a
// fallback timer, and keeps the offset when the tile grid is created.
const src = fs.readFileSync(path.join(root, 'src/ui/screensaver/image_screensaver.cpp'), 'utf8');
assert.match(src, /lv_obj_set_pos\(st->slot_grid, g_shift\.dx, g_shift\.dy\)/,
  'the tile grid takes the shift');
assert.match(src, /lv_obj_get_width\(st->clock_box\) \/ 2 \+ g_shift\.dx/,
  'the clock takes the shift');
const present = src.slice(src.indexOf('void present_picture('), src.indexOf('bool apply_wallpaper('));
assert.ok(present.indexOf('advance_pixel_shift(st)') > 0 &&
  present.indexOf('advance_pixel_shift(st)') < present.indexOf('present_composited_screensaver_frame(st)'),
  'a new picture shifts before its frame is composited');
const timer = src.slice(src.indexOf('void global_screensaver_timer_cb('), src.indexOf('void on_global_screensaver_clicked('));
assert.match(timer, /pixel_shift_due\(st, screensaver_pixel_shift::kMaxIntervalMs\)/,
  'the timer shifts without a new picture');
// A shifted grid must not clip its card shadows a few pixels inside the
// screen, and the timer's step must reach the S3 as one atomic frame.
assert.match(src, /lv_obj_add_flag\(st->slot_grid, LV_OBJ_FLAG_OVERFLOW_VISIBLE\)/,
  'shadows reach past the shifted grid');
assert.match(timer, /displayBeginAtomicFrame\("screensaver-shift"\)/,
  'the S3 shows the timer step as one frame');
console.log('OK: screensaver pixel shift');
