// The Sensor graph's line (area_graph::trace_line) measured every segment
// near a column for every row the column's segments spanned: a jumpy line
// (the house load) took 77 ms for the popup picture on the S3, a temperature
// 38 ms (b611 log, user 2026-10-29: the graph still came after the popup).
// Each segment now only reaches the rows its part near the column runs
// through. The old line, kept here as it was, and the new one must plot the
// same pixels with the same coverage for smooth, jumpy, steep, flat, gapped
// and crowded lines in the popup's and the tile's sizes and line widths.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const header = readRepoFile('src/ui/shared/area_graph.h').replace(/\r\n?/g, '\n');
const start = header.indexOf('template <typename Plot>\ninline void trace_line(');
const end = header.indexOf('\n}\n', start) + 3;
assert.ok(start >= 0 && end > start, 'trace_line in area_graph.h');
const traceLine = header.slice(start, end);
assert.ok(traceLine.includes('if (y >= near_top[n] && y <= near_bottom[n]) measure(near_index[n]);'),
  'a row measures only the segments that reach it');

const harness = String.raw`
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace area_graph {
struct Point {
  int16_t x;
  int16_t y;
};
inline constexpr int16_t kGap = INT16_MIN;
${traceLine}

// The line before b612, as it was.
template <typename Plot>
inline void trace_line_before(const Point* points, int32_t count, int32_t width, int32_t height, float width_px,
                              Plot&& plot) {
  const float half = width_px * 0.5f;
  const int32_t reach = static_cast<int32_t>(std::ceil(half + 1.0f));
  int32_t first = 1;
  for (int32_t x = 0; x < width; ++x) {
    while (first < count && std::max(points[first - 1].x, points[first].x) < x - reach) ++first;
    int32_t top = INT32_MAX, bottom = INT32_MIN;
    int32_t last = first;
    for (int32_t i = first; i < count && std::min(points[i - 1].x, points[i].x) <= x + reach; ++i) {
      last = i + 1;
      if (points[i - 1].y == kGap || points[i].y == kGap) continue;
      top = std::min<int32_t>(top, std::min(points[i - 1].y, points[i].y) - reach);
      bottom = std::max<int32_t>(bottom, std::max(points[i - 1].y, points[i].y) + reach);
    }
    if (top > bottom) continue;
    top = std::max<int32_t>(0, top);
    bottom = std::min<int32_t>(height - 1, bottom);
    for (int32_t y = top; y <= bottom; ++y) {
      float nearest = 1e9f;
      for (int32_t i = first; i < last; ++i) {
        const Point a = points[i - 1], b = points[i];
        if (a.y == kGap || b.y == kGap) continue;
        const float dx = static_cast<float>(b.x - a.x), dy = static_cast<float>(b.y - a.y);
        const float px = static_cast<float>(x - a.x), py = static_cast<float>(y - a.y);
        const float length = dx * dx + dy * dy;
        const float t = length > 0 ? std::min(1.0f, std::max(0.0f, (px * dx + py * dy) / length)) : 0.0f;
        const float ex = px - t * dx, ey = py - t * dy;
        nearest = std::min(nearest, ex * ex + ey * ey);
      }
      const float coverage = half + 0.5f - std::sqrt(nearest);
      if (coverage <= 0.0f) continue;
      plot(x, y, coverage >= 1.0f ? 255u : static_cast<uint32_t>(coverage * 255.0f + 0.5f));
    }
  }
}
}  // namespace area_graph

using area_graph::Point;

static uint32_t g_seed = 12345;
static int32_t rnd(int32_t n) {
  g_seed = g_seed * 1103515245u + 12345u;
  return static_cast<int32_t>((g_seed >> 8) % static_cast<uint32_t>(n));
}

// Points as lv_chart places them: evenly over the width, x rising.
static std::vector<Point> series(int kind, int32_t count, int32_t width, int32_t height) {
  std::vector<Point> out;
  int32_t y = height / 2;
  for (int32_t i = 0; i < count; ++i) {
    const int32_t x = count > 1 ? i * (width - 1) / (count - 1) : 0;
    switch (kind) {
      case 0: y = static_cast<int32_t>(height / 2 + std::sin(i * 0.05f) * height * 0.35f); break;  // smooth
      case 1: y = std::max<int32_t>(0, std::min<int32_t>(height - 1, y + rnd(height) - height / 2)); break;  // jumpy
      case 2: y = (i & 1) ? 2 : height - 3; break;  // steep, top to bottom every point
      case 3: y = height / 3; break;  // flat
      default: y = rnd(height); break;  // random
    }
    Point p{static_cast<int16_t>(x), static_cast<int16_t>(y)};
    if (kind == 4 && rnd(9) == 0) p.y = area_graph::kGap;  // gaps
    out.push_back(p);
  }
  return out;
}

int main() {
  struct Case { int32_t width, height, count; float line; };
  // Popup 24H/6H/7D on the 480 and larger panels, the tile graph, a crowded
  // line (more segments near a column than the near list holds).
  const Case cases[] = {{380, 215, 288, 4.0f}, {380, 215, 72, 4.0f}, {380, 215, 168, 4.0f}, {600, 330, 288, 6.0f},
                        {140, 60, 288, 3.0f}, {140, 60, 288, 2.0f}, {100, 80, 2000, 4.0f}, {380, 215, 2, 8.0f}};
  long compared = 0, pixels = 0, diffs = 0;
  for (const Case& c : cases) {
    for (int kind = 0; kind < 5; ++kind) {
      for (int round = 0; round < 6; ++round) {
        const std::vector<Point> points = series(kind, c.count, c.width, c.height);
        std::vector<uint32_t> before, after;
        area_graph::trace_line_before(points.data(), static_cast<int32_t>(points.size()), c.width, c.height, c.line,
                                      [&](int32_t x, int32_t y, uint32_t cov) { before.push_back((x << 20) | (y << 8) | cov); });
        area_graph::trace_line(points.data(), static_cast<int32_t>(points.size()), c.width, c.height, c.line,
                               [&](int32_t x, int32_t y, uint32_t cov) { after.push_back((x << 20) | (y << 8) | cov); });
        ++compared;
        pixels += static_cast<long>(before.size());
        if (before != after) ++diffs;
      }
    }
  }
  std::printf("{\"compared\":%ld,\"pixels\":%ld,\"diffs\":%ld}\n", compared, pixels, diffs);
  return 0;
}
`;

const out = compileAndRun({label: 'area graph line cull', harness});
if (out !== null) {
  const run = JSON.parse(out.trim().split('\n').at(-1));
  assert.ok(run.compared >= 200 && run.pixels > 100000, `enough lines compared ${JSON.stringify(run)}`);
  assert.equal(run.diffs, 0, `every line plots the same pixels as before ${JSON.stringify(run)}`);
  console.log(`PASS: the culled line plots every pixel as before ${JSON.stringify(run)}`);
}
