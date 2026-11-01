// Power flow: each line is one smoothed stroke and each dot sits on quarter
// pixels (user 2026-10-27, S3 photo: the bends had kinks where LVGL's
// straight lines met its arc, and a slow dot stood for about seven frames
// and then jumped a pixel). Compiles flow_geometry.h and the renderer's real
// dot_point and dot_area on the host: every pixel of a line's picture has
// the coverage of its distance from the exact path (straight pieces and a
// true quarter circle, no joint), on the V2 and S3 line widths; a dot's
// picture is a disc centred on its quarter pixel; the slowest dot moves on
// screen in every frame.
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
if (!compiler) { console.log('SKIP: Power flow stroke needs a native C++ compiler'); process.exit(0); }

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
struct lv_point_t {int32_t x, y;};
int round_px(float value) {return static_cast<int>(lroundf(value));}
struct FlowTileState {Layout layout; bool reverse[kLineCount] = {}; float phase[kLineCount] = {}; int dot_radius = 5;};
${fn('dot_point')}
${fn('dot_area')}

// The exact path, computed here on its own: the straight pieces and the
// bend as a circle, by angle.
static float exact_distance(const Polyline& line, float x, float y) {
  auto piece = [](Point a, Point b, float x, float y) {
    const float dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    const float t = l > 0 ? std::fmax(0.0f, std::fmin(1.0f, ((x - a.x) * dx + (y - a.y) * dy) / l)) : 0.0f;
    return std::hypot(x - a.x - t * dx, y - a.y - t * dy);
  };
  float best = 1e9f;
  const int first = line.bend.present ? line.bend_first : line.count - 1;
  for (int k = 0; k < first; ++k) best = std::fmin(best, piece(line.p[k], line.p[k + 1], x, y));
  if (!line.bend.present) return best;
  for (int k = line.bend_last; k + 1 < line.count; ++k) best = std::fmin(best, piece(line.p[k], line.p[k + 1], x, y));
  const Bend& b = line.bend;
  float angle = std::atan2(y - b.cy, x - b.cx) * 180.0f / 3.14159265f;  // clockwise from three o'clock, y down
  while (angle < b.start_angle) angle += 360.0f;
  while (angle > b.start_angle + 360.0f) angle -= 360.0f;
  if (angle <= b.end_angle) best = std::fmin(best, std::fabs(std::hypot(x - b.cx, y - b.cy) - b.radius));
  for (int end : {b.start_angle, b.end_angle}) {
    const float a = end * 3.14159265f / 180.0f;
    best = std::fmin(best, std::hypot(x - (b.cx + b.radius * std::cos(a)), y - (b.cy + b.radius * std::sin(a))));
  }
  return best;
}

static long check_lines(const Layout& l, int width) {
  long pixels = 0;
  const float half = width / 2.0f;
  for (uint8_t i = 0; i < kLineCount; ++i) {
    const Polyline& line = l.line[i];
    // The renderer's box: the points a line width and a pixel around.
    const int margin = width + 1;
    int x1 = 1 << 20, y1 = 1 << 20, x2 = -(1 << 20), y2 = -(1 << 20);
    for (int k = 0; k < line.count; ++k) {
      x1 = std::min(x1, round_px(line.p[k].x) - margin); y1 = std::min(y1, round_px(line.p[k].y) - margin);
      x2 = std::max(x2, round_px(line.p[k].x) + margin); y2 = std::max(y2, round_px(line.p[k].y) + margin);
    }
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    std::vector<uint8_t> mask(w * h, 77);
    render_stroke(mask.data(), w, h, x1, y1, line, static_cast<float>(width));
    for (int j = 0; j < h; ++j) for (int k = 0; k < w; ++k) {
      const float d = exact_distance(line, x1 + k + 0.5f, y1 + j + 0.5f);
      const float c = half + 0.5f - d;
      const int want = c <= 0 ? 0 : c >= 1 ? 255 : int(c * 255.0f + 0.5f);
      const int got = mask[j * w + k];
      if (std::abs(got - want) > 2) {
        std::printf("line %d width %d pixel %d,%d: %d, the exact path gives %d (d %.3f)\n", i, width, x1 + k, y1 + j, got, want, d);
        assert(false);
      }
      // The picture holds the whole stroke: nothing at its border.
      if (j == 0 || k == 0 || j == h - 1 || k == w - 1) assert(got == 0);
      pixels += got == 255;
    }
    // Along the whole middle line, joints included, the stroke is solid.
    const float length = polyline_length(line);
    for (float s = 0.0f; s <= length; s += 0.25f) {
      const Point p = point_at(line, s);
      const int k = int(std::floor(p.x)) - x1, j = int(std::floor(p.y)) - y1;
      assert(mask[j * w + k] >= (width >= 4 ? 255 : 170));
    }
  }
  return pixels;
}

int main() {
  // The V2 2x2 tile (4 px lines) and the S3's (2 px lines, smaller metrics).
  const Layout v2 = layout(404.0f, 320.0f, Metrics{});
  Metrics s3m; s3m.ring = 25; s3m.disc = 21; s3m.inset_x = 16; s3m.inset_y = 13; s3m.spacing = 12;
  const Layout s3 = layout(232.0f, 232.0f, s3m);
  long solid = check_lines(v2, 4) + check_lines(v2, 2) + check_lines(s3, 2) + check_lines(s3, 4);
  for (const Layout* l : {&v2, &s3}) assert(l->line[kLinePvHome].bend.present && l->line[kLineBatteryGrid].bend.present);

  // A dot's picture: a disc of its radius centred on its quarter pixel.
  for (int radius : {3, 5}) {
    const int size = dot_picture_size(radius);
    std::vector<uint8_t> mask(size * size);
    for (int fy = 0; fy < kDotSteps; ++fy) for (int fx = 0; fx < kDotSteps; ++fx) {
      render_dot(mask.data(), radius, fx, fy);
      double sum = 0, sx = 0, sy = 0;
      for (int j = 0; j < size; ++j) for (int i = 0; i < size; ++i) {
        const double c = mask[j * size + i] / 255.0;
        sum += c; sx += c * (i + 0.5); sy += c * (j + 0.5);
      }
      const double cx = radius + double(fx) / kDotSteps, cy = radius + double(fy) / kDotSteps;
      assert(std::fabs(sx / sum - cx) < 0.03 && std::fabs(sy / sum - cy) < 0.03);
      assert(std::fabs(sum - 3.14159265 * radius * radius) < 0.04 * 3.14159265 * radius * radius);
    }
  }
  assert(dot_pixel(0) == 0 && dot_pixel(3) == 0 && dot_pixel(4) == 1 && dot_pixel(-1) == -1 && dot_pixel(-4) == -1 &&
         dot_pixel(-5) == -2);

  // The slowest dot along the V2's grid -> home line: its picture changes
  // in every frame (b567: whole pixels at 0.15 px a frame stood ~7 frames),
  // and its area holds the dot drawn there.
  FlowTileState st;
  st.layout = v2;
  const lv_area_t canvas = {100, 50, 503, 369};
  lv_point_t last = dot_point(st, kLineGridHome);
  int still = 0;
  for (int frame = 0; frame < 300; ++frame) {
    st.phase[kLineGridHome] += dot_speed(20.0f, 5000.0f);
    const lv_point_t q = dot_point(st, kLineGridHome);
    if (q.x == last.x && q.y == last.y) ++still;
    last = q;
    const lv_area_t a = dot_area(st, canvas, q);
    const float x = canvas.x1 + q.x / float(kDotSteps), y = canvas.y1 + q.y / float(kDotSteps);
    assert(a.x2 - a.x1 + 1 == dot_picture_size(st.dot_radius) && a.y2 - a.y1 + 1 == dot_picture_size(st.dot_radius));
    assert(a.x1 <= x - st.dot_radius + 0.5f && a.x2 + 1 >= x + st.dot_radius && a.y1 <= y - st.dot_radius + 0.5f &&
           a.y2 + 1 >= y + st.dot_radius);
  }
  assert(dot_speed(20.0f, 5000.0f) == kSlowestSpeed);
  if (still) std::printf("the slowest dot stood still in %d frames\n", still);
  assert(still == 0);
  std::printf("ok %ld\n", solid);
}
`;
const out = path.join(root, 'build/tests/power-flow-stroke');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let r = spawnSync(compiler, ['-std=c++17', '-O1', source, '-o', binary], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
r = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(r.status, 0, r.stdout + r.stderr);
// The renderer draws the dots from these pictures where it has them.
assert.match(renderer, /image_dsc\.src = &g_dot_image\[fy \* kDotSteps \+ fx\];/);
assert.match(renderer, /render_dot\(pixels, radius, fx, fy\);/);
console.log('Power flow stroke: one smoothed stroke per line with a true circle bend, dots on quarter pixels');
