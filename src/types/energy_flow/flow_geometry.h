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
//
// The mockup's bend (a cubic with k 0.552) is a quarter circle; the panel
// draws it as one: a circle arc on whole pixels between straight pieces
// (b532 drew ten short lines on whole pixels: the user saw them wavy).
namespace energy_flow {

struct Point {
  float x;
  float y;
};

// The dots follow a bend through this many points.
constexpr int kBendSegments = 16;
constexpr int kMaxLinePoints = kBendSegments + 4;

// A quarter circle on whole pixels: its centre, its radius and its angles
// in LVGL's degrees (0 at three o'clock, clockwise), start below end.
struct Bend {
  bool present = false;
  int cx = 0;
  int cy = 0;
  int radius = 0;
  int start_angle = 0;
  int end_angle = 0;
};

struct Polyline {
  uint8_t count = 0;
  Point p[kMaxLinePoints] = {};
  // Distance along the line to each point; the last one is its length.
  float along[kMaxLinePoints] = {};
  // A line with a bend: p[bend_first] to p[bend_last] lie on it; the
  // points before and after are straight pieces.
  Bend bend;
  uint8_t bend_first = 0;
  uint8_t bend_last = 0;
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
// right or -1 to the grid on the left, into the lane at `lane_y`: down (or
// up) from the source's disc, a quarter circle, along the lane to the
// target's disc.
inline void build_leg(Polyline& line, const Layout& l, uint8_t from, int dir, uint8_t to, float lane_y,
                      float spacing) {
  const bool down = from == kNodePv;
  const int x = static_cast<int>(lroundf(l.node[kNodePv].x + dir * spacing));
  const int lane = static_cast<int>(lroundf(lane_y));
  const float y0 = l.node[from].y + (down ? 1.0f : -1.0f) * circle_offset(l.disc, x - l.node[from].x);
  const float end_x = l.node[to].x - dir * circle_offset(l.disc, lane - l.node[to].y);
  const int sign = lane >= y0 ? 1 : -1;
  const int radius = static_cast<int>(lroundf(fminf(fabsf(lane - y0), fabsf(end_x - x))));
  Bend& bend = line.bend;
  bend.present = radius > 0;
  bend.cx = x + dir * radius;
  bend.cy = lane - sign * radius;
  bend.radius = radius;
  // Where the bend starts (left or right of its centre) and ends (below or
  // above it), in the order the line runs.
  const float from_angle = dir > 0 ? 180.0f : 0.0f;
  const float to_angle = sign > 0 ? 90.0f : (dir > 0 ? 270.0f : -90.0f);
  const int low = static_cast<int>(fminf(from_angle, to_angle));
  bend.start_angle = low < 0 ? low + 360 : low;
  bend.end_angle = bend.start_angle + 90;

  polyline_add(line, static_cast<float>(x), y0);
  if (fabsf(bend.cy - y0) > 0.5f) polyline_add(line, static_cast<float>(x), static_cast<float>(bend.cy));
  line.bend_first = line.count - 1;
  if (bend.present) {
    for (int i = 1; i <= kBendSegments; ++i) {
      const float angle = (from_angle + (to_angle - from_angle) * i / kBendSegments) * 3.14159265f / 180.0f;
      polyline_add(line, bend.cx + radius * cosf(angle), bend.cy + radius * sinf(angle));
    }
  }
  line.bend_last = line.count - 1;
  if (fabsf(end_x - bend.cx) > 0.5f) polyline_add(line, end_x, static_cast<float>(lane));
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
