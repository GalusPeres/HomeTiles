#pragma once

#include <math.h>
#include <stdint.h>

#include "src/types/power_flow/flow_model.h"

// Where the Power flow tile draws (no LVGL; host tests:
// test-power-flow-geometry.mjs). The decided design is the mockup
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
// draws it as one: a true circle on whole pixels between straight pieces
// (b532 drew ten short lines on whole pixels: the user saw them wavy), the
// whole line as one smoothed stroke (render_stroke).
namespace power_flow {

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

// `top`: the room the header takes (header_top); the flow then fills the
// tile below it, pressed together downwards (user 2026-10-26).
inline Layout layout(float width, float height, const Metrics& m, float top = 0.0f) {
  Layout l;
  l.width = width;
  l.height = height;
  l.ring = m.ring;
  l.disc = m.disc;
  const float ox = width / 2.0f, oy = (top + height) / 2.0f;
  l.node[kNodePv] = {ox, top + m.inset_y + m.ring};
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

// The header's room: its node sits in the tile's corner, so solar's ring
// starts right under the header's ring.
inline float header_top(const Metrics& m) { return 2.0f * m.ring - m.inset_y; }

// How much of a pixel whose centre lies `d` from a stroke's middle line the
// stroke covers (0..255), for a stroke reaching `half` px to each side.
inline uint8_t stroke_coverage(float d, float half) {
  const float c = half + 0.5f - d;
  return c <= 0.0f ? 0 : c >= 1.0f ? 255 : static_cast<uint8_t>(c * 255.0f + 0.5f);
}

// The distance from (x, y) to the piece a..b.
inline float segment_distance(Point a, Point b, float x, float y) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float length = dx * dx + dy * dy;
  float t = length > 0.0f ? ((x - a.x) * dx + (y - a.y) * dy) / length : 0.0f;
  t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  return hypotf(x - a.x - t * dx, y - a.y - t * dy);
}

// The distance from (x, y) to a bend, the true quarter circle: from its
// radius inside its quarter, else from its nearer end.
inline float bend_distance(const Bend& bend, float x, float y) {
  const float vx = x - bend.cx, vy = y - bend.cy;
  const int quarter = bend.start_angle % 360;  // 0 right-down, 90 left-down, 180 left-up, 270 right-up
  const bool left = quarter == 90 || quarter == 180;
  const bool up = quarter >= 180;
  if ((left ? vx <= 0.0f : vx >= 0.0f) && (up ? vy <= 0.0f : vy >= 0.0f)) {
    return fabsf(hypotf(vx, vy) - bend.radius);
  }
  const float r = static_cast<float>(bend.radius);
  return fminf(hypotf(vx - (left ? -r : r), vy), hypotf(vx, vy - (up ? -r : r)));
}

// A line's whole stroke, `width` px wide, as one picture of coverages (A8):
// `mask` is `mask_w` x `mask_h` pixels with its top left corner at (x0, y0)
// in the line's coordinates, pixel (i, j) centred at (x0 + i + 0.5,
// y0 + j + 0.5). Each straight piece and the bend add their stroke, the
// larger coverage wins, so where they meet the nearer one counts and the
// stroke runs on without a joint (b533-b567 drew LVGL lines and an LVGL arc
// side by side, each on its own pixel grid with round caps; on the S3's
// 2 px lines the user saw kinks where they met, 2026-10-27).
inline void render_stroke(uint8_t* mask, int mask_w, int mask_h, int x0, int y0, const Polyline& line, float width) {
  for (int i = 0; i < mask_w * mask_h; ++i) mask[i] = 0;
  if (line.count < 2) return;
  const float half = width * 0.5f;
  const float reach = half + 1.0f;
  // Adds one piece inside the box (line coordinates) by its distance.
  auto add = [&](float bx1, float by1, float bx2, float by2, auto&& distance) {
    const int i1 = static_cast<int>(floorf(bx1 - reach - x0)), i2 = static_cast<int>(ceilf(bx2 + reach - x0));
    const int j1 = static_cast<int>(floorf(by1 - reach - y0)), j2 = static_cast<int>(ceilf(by2 + reach - y0));
    for (int j = j1 < 0 ? 0 : j1; j <= j2 && j < mask_h; ++j) {
      for (int i = i1 < 0 ? 0 : i1; i <= i2 && i < mask_w; ++i) {
        const uint8_t c = stroke_coverage(distance(x0 + i + 0.5f, y0 + j + 0.5f), half);
        uint8_t& pixel = mask[j * mask_w + i];
        if (c > pixel) pixel = c;
      }
    }
  };
  auto add_pieces = [&](uint8_t first, uint8_t last) {
    for (uint8_t k = first; k < last; ++k) {
      const Point a = line.p[k], b = line.p[k + 1];
      add(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y),
          [&](float x, float y) { return segment_distance(a, b, x, y); });
    }
  };
  if (!line.bend.present) {
    add_pieces(0, line.count - 1);
    return;
  }
  add_pieces(0, line.bend_first);
  const Bend& bend = line.bend;
  const int quarter = bend.start_angle % 360;
  const bool left = quarter == 90 || quarter == 180;
  const bool up = quarter >= 180;
  const float r = static_cast<float>(bend.radius);
  add(left ? bend.cx - r : bend.cx, up ? bend.cy - r : bend.cy, left ? bend.cx : bend.cx + r, up ? bend.cy : bend.cy + r,
      [&](float x, float y) { return bend_distance(bend, x, y); });
  add_pieces(line.bend_last, line.count - 1);
}

// The dots sit on quarter pixels and are drawn smoothed there (b567 put
// them on whole pixels: at the slowest speed a dot stood for about seven
// frames and then jumped a pixel, user 2026-10-27).
constexpr int kDotSteps = 4;

// A dot's picture is this many pixels wide and high: its top left corner
// lies the radius left of and above the whole pixel under its position.
inline int dot_picture_size(int radius) { return 2 * radius + 1; }

// The picture of a dot of that radius lying `fx`, `fy` quarter pixels past
// its whole pixel (A8 coverages).
inline void render_dot(uint8_t* mask, int radius, int fx, int fy) {
  const int size = dot_picture_size(radius);
  const float cx = radius + static_cast<float>(fx) / kDotSteps;
  const float cy = radius + static_cast<float>(fy) / kDotSteps;
  for (int j = 0; j < size; ++j) {
    for (int i = 0; i < size; ++i) {
      mask[j * size + i] = stroke_coverage(hypotf(i + 0.5f - cx, j + 0.5f - cy), static_cast<float>(radius));
    }
  }
}

// The whole pixel under a position in quarter pixels (rounded down).
inline int dot_pixel(int quarter) { return quarter >= 0 ? quarter / kDotSteps : -((kDotSteps - 1 - quarter) / kDotSteps); }

}  // namespace power_flow
