// A Sensor look with rule colors froze the panel: tile 23 (an arc look,
// 194x236) took 620 ms for its first frame and 655-719 ms per value change
// on the Guition V2 (b583 log). Its arc comes in 48 pieces, and each piece
// scanned the whole circle for its mask: 1.2 million coverage calls per
// build. A piece now scans its own bounds. Runs the real sensor_look.h on
// the host: no piece loses a pixel (outside its bounds its coverage is 0
// everywhere the circle scan looked), and the calls drop by far.
import assert from 'node:assert/strict';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const harness = `#include <cstdio>
#include "src/types/sensor/sensor_look.h"
using namespace sensor_look;

int main() {
  const float sizes[][2] = {{170, 150}, {120, 100}, {300, 200}, {178, 170}, {404, 236}};
  const float fractions[] = {0.0f, 0.37f, 0.5f, 1.0f};
  const uint8_t modes[] = {kArc, kArcValue, kRing, kRingHead};
  long calls = 0, circle_calls = 0, slices = 0, lost = 0;
  for (const auto& size : sizes) {
    for (const uint8_t mode : modes) {
      for (const float fraction : fractions) {
        Spec spec;
        spec.mode = mode;
        spec.box = make_box(size[0], size[1], 14);
        spec.arc = look_arc(mode, spec.box, spec.metrics);
        spec.fraction = fraction;
        spec.places = true;
        emit_look(spec, [&](const Element& e) {
          if (e.shape != Shape::ArcSlice) return;
          ++slices;
          Element circle = e;
          arc_bounds(circle, spec.box);
          for (int y = circle.y0; y <= circle.y1; ++y) {
            for (int x = circle.x0; x <= circle.x1; ++x) {
              ++circle_calls;
              const bool inside = x >= e.x0 && x <= e.x1 && y >= e.y0 && y <= e.y1;
              if (inside) ++calls;
              else if (coverage(e, spec.box, x + 0.5f, y + 0.5f) > 0) ++lost;
            }
          }
        });
      }
    }
  }
  std::printf("slices=%ld calls=%ld circle=%ld lost=%ld\\n", slices, calls, circle_calls, lost);
  return 0;
}
`;

const out = compileAndRun({label: 'sensor look slice bounds', harness});
if (out !== null) {
  const m = out.match(/slices=(\d+) calls=(\d+) circle=(\d+) lost=(\d+)/);
  assert.ok(m, out);
  const [, slices, calls, circle, lost] = m.map(Number);
  assert.ok(slices > 0, 'the rule arcs come in pieces');
  assert.equal(lost, 0, 'no piece loses a covered pixel outside its bounds');
  assert.ok(calls * 8 < circle, `a piece scans far less than the circle (${calls} of ${circle})`);
}
console.log('Sensor look: rule arc pieces scan their own bounds, every pixel kept');
