#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// The Sensor tile's looks below its header, decided with the user in the
// gauge lab (build/design-mockups/gauge-media/lab.html, 2026-10-27/28;
// build/analysis/2026-10-28-analyse.md). Pure geometry: the box under the
// header, the shapes of every look as anti-aliased coverage, and the arc fit.
// The tile renderer (look_view.h) turns the shapes into A8 masks, computed
// again only when the value, the size or the colors of a rule change, and
// draws them recolored, so a redraw costs a few image blits. Every size is
// the lab's in 1280x800 pixels, scaled by the caller (tile_layout::scale).
namespace sensor_look {

// Tile::sensor_display_mode. 0 to 2 were there before (the value alone, the
// gauge with its own arc settings, the graph); the looks are appended, so a
// stored tile keeps its meaning.
enum Mode : uint8_t {
  kNone = 0,
  kGauge = 1,
  kGraph = 2,
  kLevelSide = 3,      // "Füllstand seitwärts": the Switch bar's fill
  kLevelUp = 4,        // "Füllstand nach oben"
  kStripes = 5,        // "Striche": standing stripes, all box corners round
  kStripesUp = 6,      // "Striche nach oben": lying stripes, box flat on top
  kArc = 7,            // lab A: arc in the box, value in the head
  kArcValue = 8,       // lab B: arc, value in the arc, title alone in the head
  kTickArc = 9,        // lab AS/S: arc of stripes, value in the head
  kTickArcValue = 10,  // lab BS/SM: arc of stripes, value in the arc
  kRing = 11,          // lab G: closed ring, value in it
  kTickRing = 12,      // lab GS: closed ring of stripes, value in it
  kRingHead = 13,      // closed ring, value in the head (user 2026-10-28)
  kTickRingHead = 14,  // closed ring of stripes, value in the head
};
inline constexpr uint8_t kLastMode = kTickRingHead;

inline bool is_look(uint8_t mode) { return mode >= kLevelSide && mode <= kLastMode; }
// The value sits in the arc or ring; the head shows the title alone,
// right-aligned (lab: "nur Titel oben rechtsbündig").
inline bool value_inside(uint8_t mode) {
  return mode == kArcValue || mode == kTickArcValue || mode == kRing || mode == kTickRing;
}
inline bool is_level(uint8_t mode) { return mode == kLevelSide || mode == kLevelUp; }
inline bool is_stripes(uint8_t mode) { return mode == kStripes || mode == kStripesUp; }
inline bool is_arc(uint8_t mode) { return mode == kArc || mode == kArcValue; }
inline bool is_tick_arc(uint8_t mode) { return mode == kTickArc || mode == kTickArcValue; }
inline bool is_ring(uint8_t mode) {
  return mode == kRing || mode == kTickRing || mode == kRingHead || mode == kTickRingHead;
}
// Looks whose places can take a rule's color each (stripes, arcs, rings);
// a level takes the current value's color as a whole.
inline bool colors_places(uint8_t mode) { return is_look(mode) && !is_level(mode); }

constexpr float kPi = 3.14159265358979f;

// The lab's sizes at 1280x800, scaled by the caller.
struct Metrics {
  float stroke = 12;  // arc and ring width
  float band = 16;    // the band of an arc of stripes
  float stripe = 6;   // stripe width (bars and arcs)
  float gap = 5;      // smallest gap between bar stripes
  float pitch = 11;   // stripe spacing along an arc
  float end = 17;     // level fill end rounding: a quarter of the one-row bar
};

// The box under the header (the graph's box): its size and corner radius
// (the tile radius minus the inset, concentric to the card).
struct Box {
  float w = 0;
  float h = 0;
  float radius = 0;
};

inline Box make_box(float w, float h, float control_radius) {
  Box box;
  box.w = w;
  box.h = h;
  box.radius = std::max(0.0f, std::min(control_radius, std::min(w, h) * 0.5f));
  return box;
}

// ---------------------------------------------------------------------------
// The arc fit (lab gaugeFit/gaugeAuto): an arc symmetric around the top, its
// centre line `sw / 2` inside the box and its corner curves, as large as it
// fits and centred between the highest and lowest place it fits at.

struct Arc {
  float cx = 0;
  float cy = 0;
  float r = 0;
  float half = 0;  // half the opening, radians; the arc runs from -half to half
  float sw = 0;
  bool ok = false;
};

// The rows a point at `x` may take inside the box shrunk by `m` (its corner
// curves too), false outside it.
inline bool column(const Box& box, float m, float x, float& top, float& bottom) {
  if (x < m - 1e-4f || x > box.w - m + 1e-4f) return false;
  top = m;
  bottom = box.h - m;
  const float dx = std::max(std::max(box.radius - x, x - (box.w - box.radius)), 0.0f);
  if (dx > 0) {
    const float inner = box.radius - m;
    if (inner <= 0 || dx > inner + 1e-4f) return false;
    const float s = std::sqrt(std::max(0.0f, inner * inner - dx * dx));
    top = std::max(top, box.radius - s);
    bottom = std::min(bottom, box.h - box.radius + s);
  }
  return top <= bottom + 1e-4f;
}

inline constexpr int kFitSamples = 72;

// The centre heights an arc of radius `r` fits at (every sample point of
// its centre line inside), as one interval: the box is convex, so each
// point allows an interval of heights.
inline bool fit_heights(const Box& box, float r, float sw, const float* sines, const float* cosines, float& lo,
                        float& hi) {
  const float cx = box.w * 0.5f;
  lo = -1e9f;
  hi = 1e9f;
  for (int k = 0; k <= kFitSamples; ++k) {
    float top = 0, bottom = 0;
    if (!column(box, sw * 0.5f, cx + r * sines[k], top, bottom)) return false;
    lo = std::max(lo, top + r * cosines[k]);
    hi = std::min(hi, bottom + r * cosines[k]);
    if (lo > hi) return false;
  }
  return true;
}

// The largest arc of `angle_deg` that fits (lab gaugeFit with a fixed width).
inline Arc fit_arc(const Box& box, float angle_deg, float sw) {
  Arc arc;
  arc.sw = sw;
  arc.cx = box.w * 0.5f;
  arc.half = angle_deg * 0.5f * kPi / 180.0f;
  float sines[kFitSamples + 1], cosines[kFitSamples + 1];
  for (int k = 0; k <= kFitSamples; ++k) {
    const float a = -arc.half + 2.0f * arc.half * k / kFitSamples;
    sines[k] = std::sin(a);
    cosines[k] = std::cos(a);
  }
  float lo_r = 5.0f, hi_r = 2.0f * std::max(box.w, box.h);
  float lo = 0, hi = 0;
  if (!fit_heights(box, lo_r, sw, sines, cosines, lo, hi)) return arc;
  for (int i = 0; i < 24; ++i) {
    const float mid = (lo_r + hi_r) * 0.5f;
    if (fit_heights(box, mid, sw, sines, cosines, lo, hi)) lo_r = mid;
    else hi_r = mid;
  }
  fit_heights(box, lo_r, sw, sines, cosines, lo, hi);
  arc.r = lo_r;
  arc.cy = (lo + hi) * 0.5f;
  arc.ok = true;
  return arc;
}

// The opening between 40 and 300 degrees whose arc covers the box best (its
// bounding box over the box's), lab gaugeAuto.
inline Arc auto_arc(const Box& box, float sw) {
  Arc best;
  float best_score = -1;
  for (int angle = 40; angle <= 300; angle += 10) {
    const Arc arc = fit_arc(box, static_cast<float>(angle), sw);
    if (!arc.ok) continue;
    const float w = 2 * arc.r * (arc.half >= kPi / 2 ? 1.0f : std::sin(arc.half)) + arc.sw;
    const float h = arc.r * (1 - std::cos(std::min(arc.half, kPi))) + arc.sw;
    const float score = (w / box.w) * (h / box.h);
    if (score > best_score) {
      best_score = score;
      best = arc;
    }
  }
  return best;
}

// Where a value inside an arc puts its top (lab: the middle of the arc's
// inside, between the inner edge at its top and the line between its ends).
inline float arc_value_top(const Arc& arc) {
  return (arc.cy - arc.r + arc.sw * 0.5f + arc.cy - arc.r * std::cos(arc.half)) * 0.5f;
}

// The closed ring in the box (lab G/GS): as large as the box's shorter side,
// centred, `sw` wide.
inline Arc ring(const Box& box, float sw) {
  Arc arc;
  arc.cx = box.w * 0.5f;
  arc.cy = box.h * 0.5f;
  arc.sw = sw;
  arc.r = (std::min(box.w, box.h) - sw) * 0.5f;
  arc.half = kPi;
  arc.ok = arc.r > sw * 0.5f;
  return arc;
}

// ---------------------------------------------------------------------------
// Shapes. Coordinates are picture pixels (0,0 the box's top left corner,
// pixel centres at +0.5); angles run clockwise from the top.

enum class Shape : uint8_t {
  RoundRect,  // a rounded rect, optionally within the box shape (levels)
  Polyline,   // a closed outline grown by `grow` (bar stripes)
  ArcRound,   // an arc band with round ends
  ArcSlice,   // a piece of an arc band, cut straight at its ends (rules)
  Ring,       // a closed ring band
  Disc,       // a round end on its own (rules)
  Capsule,    // a segment with round ends (arc stripes)
};

inline constexpr int kMaxPoints = 18;

struct Element {
  Shape shape = Shape::Disc;
  // Draw order: 0 under (the track, the unlit places), 1 the lit places and
  // a rule arc's pieces, 2 the round ends on top of a rule arc.
  uint8_t layer = 0;
  bool lit = false;
  // The place on the gauge (0..1) whose rule color it takes.
  float place = 0;
  // Bounding box, inclusive pixels.
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
  // RoundRect: x0 y0 x1 y1 of the rect and its corner radii (tl tr br bl);
  // with `clip`, also inside the box. Arcs: cx cy r half-width a0 a1.
  // Capsule/Disc: ax ay bx by radius.
  float p[8] = {};
  bool clip = false;
  // Polyline: the outline and how far it grows.
  int points = 0;
  float px[kMaxPoints] = {};
  float py[kMaxPoints] = {};
  float grow = 0;
};

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// The signed distance to a rounded rect (negative inside).
inline float round_rect_distance(float x, float y, float x0, float y0, float x1, float y1, float tl, float tr,
                                 float br, float bl) {
  const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
  const float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
  const float dx = x - cx, dy = y - cy;
  const float r = dx > 0 ? (dy > 0 ? br : tr) : (dy > 0 ? bl : tl);
  const float qx = std::fabs(dx) - hx + r, qy = std::fabs(dy) - hy + r;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::min(std::max(qx, qy), 0.0f) + std::sqrt(ox * ox + oy * oy) - r;
}

inline float segment_distance(float x, float y, float ax, float ay, float bx, float by) {
  const float dx = bx - ax, dy = by - ay;
  const float length = dx * dx + dy * dy;
  const float t = length > 0 ? clamp01(((x - ax) * dx + (y - ay) * dy) / length) : 0.0f;
  const float ex = x - ax - t * dx, ey = y - ay - t * dy;
  return std::sqrt(ex * ex + ey * ey);
}

// The angle of a point around a centre, clockwise from the top, 0..2pi.
inline float angle_of(float dx, float dy) {
  float a = std::atan2(dx, -dy);
  return a < 0 ? a + 2 * kPi : a;
}

// Whether angle `a` (0..2pi) lies in [from, to) (from may be negative).
inline bool angle_in(float a, float from, float to) {
  float t = a - from;
  while (t < 0) t += 2 * kPi;
  while (t >= 2 * kPi) t -= 2 * kPi;
  return t < to - from;
}

// The element's coverage (0..1) of the pixel centred at x, y.
inline float coverage(const Element& e, const Box& box, float x, float y) {
  switch (e.shape) {
    case Shape::RoundRect: {
      float d = round_rect_distance(x, y, e.p[0], e.p[1], e.p[2], e.p[3], e.p[4], e.p[5], e.p[6], e.p[7]);
      if (e.clip) {
        const float r = box.radius;
        d = std::max(d, round_rect_distance(x, y, 0, 0, box.w, box.h, r, r, r, r));
      }
      return clamp01(0.5f - d);
    }
    case Shape::Polyline: {
      float d = 1e9f;
      for (int i = 0; i < e.points; ++i) {
        const int j = i + 1 == e.points ? 0 : i + 1;
        d = std::min(d, segment_distance(x, y, e.px[i], e.py[i], e.px[j], e.py[j]));
      }
      return clamp01(e.grow + 0.5f - d);
    }
    case Shape::Capsule:
    case Shape::Disc:
      return clamp01(e.p[4] + 0.5f - segment_distance(x, y, e.p[0], e.p[1], e.p[2], e.p[3]));
    case Shape::Ring: {
      const float dx = x - e.p[0], dy = y - e.p[1];
      return clamp01(0.5f - (std::fabs(std::sqrt(dx * dx + dy * dy) - e.p[2]) - e.p[3]));
    }
    case Shape::ArcRound:
    case Shape::ArcSlice: {
      const float dx = x - e.p[0], dy = y - e.p[1];
      const float d = std::sqrt(dx * dx + dy * dy);
      // Far off the band neither the band nor its round ends reach.
      if (std::fabs(d - e.p[2]) > e.p[3] + 1) return 0;
      const float a = angle_of(dx, dy);
      if (angle_in(a, e.p[4], e.p[5])) return clamp01(0.5f - (std::fabs(d - e.p[2]) - e.p[3]));
      if (e.shape == Shape::ArcSlice) return 0;
      float best = 1e9f;
      for (const float end : {e.p[4], e.p[5]}) {
        const float ex = e.p[0] + e.p[2] * std::sin(end), ey = e.p[1] - e.p[2] * std::cos(end);
        best = std::min(best, std::sqrt((x - ex) * (x - ex) + (y - ey) * (y - ey)));
      }
      return clamp01(0.5f - (best - e.p[3]));
    }
  }
  return 0;
}

inline void bound(Element& e, float x0, float y0, float x1, float y1, const Box& box) {
  e.x0 = std::max(0, static_cast<int>(std::floor(x0)) - 1);
  e.y0 = std::max(0, static_cast<int>(std::floor(y0)) - 1);
  e.x1 = std::min(static_cast<int>(std::ceil(box.w)) - 1, static_cast<int>(std::ceil(x1)) + 1);
  e.y1 = std::min(static_cast<int>(std::ceil(box.h)) - 1, static_cast<int>(std::ceil(y1)) + 1);
}

inline void arc_bounds(Element& e, const Box& box) {
  const float reach = e.p[2] + e.p[3] + 1;
  bound(e, e.p[0] - reach, e.p[1] - reach, e.p[0] + reach, e.p[1] + reach, box);
}

// The arc's point at angle `a`.
inline void arc_point(const Arc& arc, float r, float a, float& x, float& y) {
  x = arc.cx + r * std::sin(a);
  y = arc.cy - r * std::cos(a);
}

// ---------------------------------------------------------------------------
// The looks as elements. `fraction` is the value on the gauge (0..1);
// `places` asks for a rule's colors per place (arcs then come in pieces).
// `emit(const Element&)` gets every element in drawing order.

struct Spec {
  uint8_t mode = kNone;
  Box box;
  Metrics metrics;
  float fraction = 0;
  bool places = false;
  // The arc a look fits in (auto_arc or ring), computed once per size.
  Arc arc;
};

// The arc a look uses in its box.
inline Arc look_arc(uint8_t mode, const Box& box, const Metrics& m) {
  if (is_arc(mode)) return auto_arc(box, m.stroke);
  if (is_tick_arc(mode)) return auto_arc(box, m.band);
  if (mode == kRing || mode == kRingHead) return ring(box, m.stroke);
  if (mode == kTickRing || mode == kTickRingHead) return ring(box, m.band);
  return {};
}

// Stripes in the box (lab stripeRow, decided 2026-10-27): `stripe` wide, at
// least `gap` apart, the first and last on the box's edges. Both ends of a
// stripe are round (half its width) and sit on the box; in the corners the
// ends follow the corner curve, rounded by a third of the width where the
// cut drops more than the stripe is wide. Upright stripes stand across the
// box (all four corners round); lying ones (`up`) stack from the bottom in a
// box that is straight on top.
template <typename Emit>
inline void emit_stripes(const Spec& spec, bool up, Emit&& emit) {
  const Box& box = spec.box;
  const float sw = spec.metrics.stripe, rc = sw * 0.5f;
  const float U = up ? box.h : box.w, V = up ? box.w : box.h, R = box.radius;
  int n = static_cast<int>(std::floor((U + spec.metrics.gap) / (sw + spec.metrics.gap)));
  if (n < 1) return;
  const float gap = n > 1 ? (U - n * sw) / (n - 1) : 0;
  const int lit = static_cast<int>(std::lround(n * spec.fraction));
  // The box edge across from u for an outline shrunk by r: the far end
  // (dir 1) or the near end (dir -1).
  auto edge = [&](float u, float r, int dir) {
    const float off = std::max(std::max(0.0f, R - u), up ? 0.0f : u - (U - R));
    const float ri = R - r;
    if (off <= 0) return dir > 0 ? V - r : r;
    const float d = std::sqrt(std::max(0.0f, ri * ri - off * off));
    return dir > 0 ? V - R + d : R - d;
  };
  auto map = [&](float u, float v, float& x, float& y) {
    if (up) {
      x = v;
      y = U - u;
    } else {
      x = u;
      y = v;
    }
  };
  for (int i = 0; i < n; ++i) {
    const float su = i * (sw + gap);
    Element e;
    e.shape = Shape::Polyline;
    e.lit = i < lit;
    e.layer = e.lit ? 1 : 0;
    e.place = (i + 0.5f) / n;
    const float drop = std::fabs(edge(su, 0, 1) - edge(su + sw, 0, 1));
    const float r = drop > sw ? sw / 3 : rc;
    e.grow = r;
    const float a = su + r, b = su + sw - r;
    const bool straight = su >= R && (up || su + sw <= U - R);
    if (straight) {
      // Away from the corners a stripe is a plain capsule.
      e.points = 2;
      map(a, r, e.px[0], e.py[0]);
      map(a, V - r, e.px[1], e.py[1]);
    } else {
      e.points = kMaxPoints;
      for (int k = 0; k <= 8; ++k) {
        const float u = a + (b - a) * k / 8;
        map(u, edge(u, r, -1), e.px[k], e.py[k]);
      }
      for (int k = 0; k <= 8; ++k) {
        const float u = b - (b - a) * k / 8;
        map(u, edge(u, r, 1), e.px[9 + k], e.py[9 + k]);
      }
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < e.points; ++k) {
      x0 = std::min(x0, e.px[k]);
      y0 = std::min(y0, e.py[k]);
      x1 = std::max(x1, e.px[k]);
      y1 = std::max(y1, e.py[k]);
    }
    bound(e, x0 - r, y0 - r, x1 + r, y1 + r, box);
    emit(e);
  }
}

// The Switch tile's dimmer fill as a level (lab levelFill): the track is the
// whole box, the fill exactly as long as the value (no Switch minimum piece),
// its moving end rounded by `end`, both inside the box's shape.
template <typename Emit>
inline void emit_level(const Spec& spec, bool up, Emit&& emit) {
  const Box& box = spec.box;
  const float r = box.radius;
  Element track;
  track.shape = Shape::RoundRect;
  track.layer = 0;
  track.place = 0.5f;
  const float track_p[8] = {0, 0, box.w, box.h, r, r, r, r};
  std::copy(track_p, track_p + 8, track.p);
  bound(track, 0, 0, box.w, box.h, box);
  emit(track);
  if (spec.fraction <= 0) return;
  Element fill;
  fill.shape = Shape::RoundRect;
  fill.layer = 1;
  fill.lit = true;
  fill.place = spec.fraction;
  fill.clip = true;
  if (up) {
    const float length = box.h * spec.fraction;
    const float end = std::min(spec.metrics.end, std::min(length, box.w * 0.5f));
    const float p[8] = {0, box.h - length, box.w, box.h, end, end, 0, 0};
    std::copy(p, p + 8, fill.p);
    bound(fill, 0, box.h - length, box.w, box.h, box);
  } else {
    const float length = box.w * spec.fraction;
    const float end = std::min(spec.metrics.end, std::min(length, box.h * 0.5f));
    const float p[8] = {0, 0, length, box.h, 0, end, end, 0};
    std::copy(p, p + 8, fill.p);
    bound(fill, 0, 0, length, box.h, box);
  }
  emit(fill);
}

inline Element arc_element(Shape shape, const Arc& arc, float a0, float a1, uint8_t layer, bool lit, float place,
                           const Box& box) {
  Element e;
  e.shape = shape;
  e.layer = layer;
  e.lit = lit;
  e.place = place;
  const float p[8] = {arc.cx, arc.cy, arc.r, arc.sw * 0.5f, a0, a1, 0, 0};
  std::copy(p, p + 8, e.p);
  arc_bounds(e, box);
  return e;
}

inline Element disc_element(const Arc& arc, float a, uint8_t layer, bool lit, float place, const Box& box) {
  Element e;
  e.shape = Shape::Disc;
  e.layer = layer;
  e.lit = lit;
  e.place = place;
  float x = 0, y = 0;
  arc_point(arc, arc.r, a, x, y);
  const float p[8] = {x, y, x, y, arc.sw * 0.5f, 0, 0, 0};
  std::copy(p, p + 8, e.p);
  bound(e, x - arc.sw * 0.5f, y - arc.sw * 0.5f, x + arc.sw * 0.5f, y + arc.sw * 0.5f, box);
  return e;
}

// An arc or ring (lab arcBody): the track in the circle's tone, the fill in
// the icon's color up to the value with round ends. With a rule's colors
// every place shows its color: pieces cut straight, round ends at the
// start, the end and the value (lab: small slices plus caps).
inline constexpr int kRulePieces = 48;

template <typename Emit>
inline void emit_arc(const Spec& spec, bool closed, Emit&& emit) {
  const Arc& arc = spec.arc;
  if (!arc.ok) return;
  const float a0 = closed ? 0.0f : -arc.half, a1 = closed ? 2 * kPi : arc.half;
  const float f = spec.fraction;
  const float af = a0 + (a1 - a0) * f;
  if (!spec.places) {
    if (closed) {
      Element track;
      track.shape = Shape::Ring;
      track.layer = 0;
      track.place = 0.5f;
      const float p[8] = {arc.cx, arc.cy, arc.r, arc.sw * 0.5f, 0, 0, 0, 0};
      std::copy(p, p + 8, track.p);
      arc_bounds(track, spec.box);
      emit(track);
    } else {
      emit(arc_element(Shape::ArcRound, arc, a0, a1, 0, false, 0.5f, spec.box));
    }
    if (f > 0) emit(arc_element(Shape::ArcRound, arc, a0, std::max(af, a0 + 1e-4f), 1, true, f, spec.box));
    return;
  }
  // The places in their own colors: the ends under the pieces, the lit
  // ends on top.
  if (!closed) {
    emit(disc_element(arc, a0, 0, f > 0, 0.0f, spec.box));
    emit(disc_element(arc, a1, 0, f >= 1, 1.0f, spec.box));
  }
  for (int k = 0; k < kRulePieces; ++k) {
    const float p0 = static_cast<float>(k) / kRulePieces, p1 = static_cast<float>(k + 1) / kRulePieces;
    const float centre = (p0 + p1) * 0.5f;
    emit(arc_element(Shape::ArcSlice, arc, a0 + (a1 - a0) * p0, a0 + (a1 - a0) * p1, 1, centre < f, centre,
                     spec.box));
  }
  if (f > 0) emit(disc_element(arc, a0, 2, true, 0.0f, spec.box));
  if (f > 0 && f < 1) emit(disc_element(arc, af, 2, true, f, spec.box));
}

// Round stripes standing across an arc or ring (lab tickArc and GS): 6 px
// wide, `pitch` apart along it, lit up to the value.
template <typename Emit>
inline void emit_ticks(const Spec& spec, bool closed, Emit&& emit) {
  const Arc& arc = spec.arc;
  if (!arc.ok) return;
  const float band = spec.metrics.band, half_w = spec.metrics.stripe * 0.5f;
  const float r0 = arc.r - band * 0.5f + half_w, r1 = arc.r + band * 0.5f - half_w;
  int n = 0;
  if (closed) n = static_cast<int>(std::floor(2 * kPi * arc.r / spec.metrics.pitch));
  else n = std::max(2, static_cast<int>(std::floor(arc.r * 2 * arc.half / spec.metrics.pitch)) + 1);
  if (n < 2) return;
  const int lit = static_cast<int>(std::lround(n * spec.fraction));
  for (int i = 0; i < n; ++i) {
    const float a = closed ? 2 * kPi * i / n : -arc.half + 2 * arc.half * i / (n - 1);
    Element e;
    e.shape = Shape::Capsule;
    e.lit = i < lit;
    e.layer = e.lit ? 1 : 0;
    e.place = (i + 0.5f) / n;
    float ax = 0, ay = 0, bx = 0, by = 0;
    arc_point(arc, r0, a, ax, ay);
    arc_point(arc, r1, a, bx, by);
    const float p[8] = {ax, ay, bx, by, half_w, 0, 0, 0};
    std::copy(p, p + 8, e.p);
    bound(e, std::min(ax, bx) - half_w, std::min(ay, by) - half_w, std::max(ax, bx) + half_w,
          std::max(ay, by) + half_w, spec.box);
    emit(e);
  }
}

template <typename Emit>
inline void emit_look(const Spec& spec, Emit&& emit) {
  switch (spec.mode) {
    case kLevelSide: emit_level(spec, false, emit); break;
    case kLevelUp: emit_level(spec, true, emit); break;
    case kStripes: emit_stripes(spec, false, emit); break;
    case kStripesUp: emit_stripes(spec, true, emit); break;
    case kArc:
    case kArcValue: emit_arc(spec, false, emit); break;
    case kRing:
    case kRingHead: emit_arc(spec, true, emit); break;
    case kTickArc:
    case kTickArcValue: emit_ticks(spec, false, emit); break;
    case kTickRing:
    case kTickRingHead: emit_ticks(spec, true, emit); break;
    default: break;
  }
}

// Where the value's top goes for a look with the value inside (picture
// rows, the value `size` px high): rings and arcs of stripes that nearly
// close (270 degrees and more) centre it (lab G, GS, SM), other arcs put its
// top at the middle of their inside (lab B, BS, SM).
inline float value_top(uint8_t mode, const Arc& arc, float size) {
  if (is_ring(mode)) return arc.cy - size * 0.6f;
  if (mode == kTickArcValue && 2 * arc.half >= 1.5f * kPi) return arc.cy - size * 0.6f;
  return arc_value_top(arc);
}

}  // namespace sensor_look
