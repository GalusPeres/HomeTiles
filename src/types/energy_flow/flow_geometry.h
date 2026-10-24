#pragma once

#include <math.h>
#include <stdint.h>

#include "src/types/energy_flow/flow_model.h"

// Where the Energy flow tile draws (no LVGL; host tests:
// test-energy-flow-geometry.mjs). The decided design is the mockup
// build/design-mockups/ideen-1023/energie-stand.html (picture 65, user
// 2026-10-24) on the V2's 2x2 tile, 404 x 320: solar at (202, 54), grid at
// (58, 160), home at (346, 160), battery at (202, 266), every node a disc
// of radius 28 in a ring of radius 34. The lines start and end on the disc
// circle (under the ring), parallel lines 18 apart: grid -> home and solar
// -> battery straight, the others leave solar or the battery 18 beside the
// middle and turn in one wide quarter bend into a lane 18 above or below
// the middle. Other tile sizes keep the insets and spacings; a bend that
// would run into its target node gets a smaller radius after a straight
// piece, so the V2 2x2 tile stays exactly the mockup.
namespace energy_flow {

struct Point {
  float x;
  float y;
};

// A bend is drawn as this many straight pieces.
constexpr int kBendSegments = 10;
constexpr int kMaxLinePoints = kBendSegments + 4;

struct Polyline {
  uint8_t count = 0;
  Point p[kMaxLinePoints] = {};
  // Distance along the line to each point; the last one is its length.
  float along[kMaxLinePoints] = {};
};

struct Layout {
  float width = 0.0f;
  float height = 0.0f;
  float ring = 0.0f;
  float disc = 0.0f;
  Point node[kNodeCount] = {};
  Polyline line[kLineCount];
};

// The design's distances, already scaled to the device layout.
struct Metrics {
  float ring = 34.0f;     // node ring radius (the tile corner radius)
  float disc = 28.0f;     // node disc radius
  float inset_x = 24.0f;  // ring to the tile's left/right edge
  float inset_y = 20.0f;  // ring to the tile's top/bottom edge
  float spacing = 18.0f;  // between parallel lines and lanes
};

inline void polyline_add(Polyline& line, float x, float y) {
  if (line.count >= kMaxLinePoints) return;
  const uint8_t i = line.count++;
  line.p[i] = {x, y};
  line.along[i] = i == 0 ? 0.0f : line.along[i - 1] + hypotf(x - line.p[i - 1].x, y - line.p[i - 1].y);
}

inline float polyline_length(const Polyline& line) { return line.count ? line.along[line.count - 1] : 0.0f; }

// The point `distance` along the line (clamped to its ends).
inline Point point_at(const Polyline& line, float distance) {
  if (!line.count) return {0.0f, 0.0f};
  if (distance <= 0.0f) return line.p[0];
  for (uint8_t i = 1; i < line.count; ++i) {
    if (distance > line.along[i]) continue;
    const float piece = line.along[i] - line.along[i - 1];
    const float t = piece > 0.0f ? (distance - line.along[i - 1]) / piece : 0.0f;
    return {line.p[i - 1].x + (line.p[i].x - line.p[i - 1].x) * t,
            line.p[i - 1].y + (line.p[i].y - line.p[i - 1].y) * t};
  }
  return line.p[line.count - 1];
}

inline float circle_offset(float radius, float d) {
  const float q = radius * radius - d * d;
  return q > 0.0f ? sqrtf(q) : 0.0f;
}

// A leg from solar (`down`) or the battery, `dir` +1 to the home on the
// right or -1 to the grid on the left, into the lane at `lane_y`.
inline void build_leg(Polyline& line, const Layout& l, uint8_t from, int dir, uint8_t to, float lane_y,
                      float spacing) {
  const bool down = from == kNodePv;
  const float x = l.node[kNodePv].x + dir * spacing;
  const float y0 = l.node[from].y + (down ? 1.0f : -1.0f) * circle_offset(l.disc, spacing);
  const float end_x = l.node[to].x - dir * circle_offset(l.disc, lane_y - l.node[to].y);
  const float vertical = fabsf(lane_y - y0);
  const float room = fabsf(end_x - x);
  const float radius = fminf(vertical, room);
  const float sign = lane_y >= y0 ? 1.0f : -1.0f;
  const float bend_y = lane_y - sign * radius;
  constexpr float k = 0.552f;  // a cubic close to a quarter circle

  polyline_add(line, x, y0);
  if (fabsf(bend_y - y0) > 0.5f) polyline_add(line, x, bend_y);
  const Point c0 = {x, bend_y}, c1 = {x, bend_y + sign * k * radius},
              c2 = {x + dir * (1.0f - k) * radius, lane_y}, c3 = {x + dir * radius, lane_y};
  for (int i = 1; i <= kBendSegments; ++i) {
    const float t = static_cast<float>(i) / kBendSegments, u = 1.0f - t;
    const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    polyline_add(line, a * c0.x + b * c1.x + c * c2.x + d * c3.x, a * c0.y + b * c1.y + c * c2.y + d * c3.y);
  }
  if (fabsf(end_x - c3.x) > 0.5f) polyline_add(line, end_x, lane_y);
}

inline Layout layout(float width, float height, const Metrics& m) {
  Layout l;
  l.width = width;
  l.height = height;
  l.ring = m.ring;
  l.disc = m.disc;
  const float ox = width / 2.0f, oy = height / 2.0f;
  l.node[kNodePv] = {ox, m.inset_y + m.ring};
  l.node[kNodeGrid] = {m.inset_x + m.ring, oy};
  l.node[kNodeHome] = {width - m.inset_x - m.ring, oy};
  l.node[kNodeBattery] = {ox, height - m.inset_y - m.ring};
  // A spacing wider than the disc would leave the lines beside it.
  const float spacing = fminf(m.spacing, m.disc * 0.8f);

  Polyline& grid_home = l.line[kLineGridHome];
  polyline_add(grid_home, l.node[kNodeGrid].x + m.disc, oy);
  polyline_add(grid_home, l.node[kNodeHome].x - m.disc, oy);
  Polyline& pv_battery = l.line[kLinePvBattery];
  polyline_add(pv_battery, ox, l.node[kNodePv].y + m.disc);
  polyline_add(pv_battery, ox, l.node[kNodeBattery].y - m.disc);
  build_leg(l.line[kLinePvHome], l, kNodePv, 1, kNodeHome, oy - spacing, spacing);
  build_leg(l.line[kLinePvGrid], l, kNodePv, -1, kNodeGrid, oy - spacing, spacing);
  build_leg(l.line[kLineBatteryHome], l, kNodeBattery, 1, kNodeHome, oy + spacing, spacing);
  build_leg(l.line[kLineBatteryGrid], l, kNodeBattery, -1, kNodeGrid, oy + spacing, spacing);
  return l;
}

}  // namespace energy_flow
