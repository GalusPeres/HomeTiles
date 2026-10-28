// The clock tile cost ~15 ms of a Home page turn (V2 b592, user 2026-10-28:
// "bei der Uhr ... komisch", measured by removing it), most of it LVGL
// visiting its many objects in every draw band. Its analog face was a dial
// with twelve dot or baton children, two full-dial lv_line hands and a center
// dot; now the dial draws them itself (clock_face.cpp). Real LVGL draws the old
// face (rebuilt here as it was) and the new one in 28-row RGB565 bands, also
// after the minute moved the hands and in a partial redraw: every pixel must
// match, and the new face is one object.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const face = read('src/types/clock/clock_face.cpp');
assert.doesNotMatch(face, /lv_line_create|lv_obj_create\(dial\)|plain\(dial\)/, 'the face creates no children');
assert.ok(face.includes('lv_obj_add_event_cb(dial, draw_cb, LV_EVENT_DRAW_MAIN_END, face);'),
  'the dial draws marks, hands and cap where its children were drawn');
assert.ok(face.includes('rounded_corners::attach(dial);'), 'the round dial from kept corners');
assert.ok(face.includes('if (face->dial) lv_obj_invalidate(face->dial);'), 'a new minute redraws the dial');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the clock face pixel run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const out = path.join(root, 'build/tests/clock-face-one-object');
fs.mkdirSync(path.join(out, 'shim'), {recursive: true});
fs.writeFileSync(path.join(out, 'shim/Arduino.h'), `#pragma once
#include <stdint.h>
#include <time.h>
extern int g_hour, g_minute;
inline bool getLocalTime(struct tm* info, uint32_t = 5000) {
  *info = tm{};
  info->tm_hour = g_hour;
  info->tm_min = g_minute;
  return true;
}
`);

const cpp = String.raw`
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
#include <Arduino.h>
#include "src/types/clock/clock_face.h"

int g_hour = 10, g_minute = 10;

// The face as it was before b594: a dial with child objects.
namespace old_face {
constexpr float kPi = 3.14159265f;
struct Face {
  lv_obj_t* dial = nullptr; lv_obj_t* hour = nullptr; lv_obj_t* minute = nullptr;
  lv_point_precise_t hour_points[2] = {}; lv_point_precise_t minute_points[2] = {};
  lv_point_precise_t marks[12][2] = {};
  lv_coord_t size = 0; float hour_share = 0.45f; float minute_share = 0.66f; int shown = -1;
};
lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}
lv_point_precise_t polar(float radius, float degrees, float share) {
  const float a = degrees * kPi / 180.0f;
  lv_point_precise_t p;
  p.x = static_cast<lv_value_precise_t>(radius + share * radius * sinf(a));
  p.y = static_cast<lv_value_precise_t>(radius - share * radius * cosf(a));
  return p;
}
lv_obj_t* line(lv_obj_t* dial, lv_coord_t size, const lv_point_precise_t* points, lv_coord_t width, uint32_t color) {
  lv_obj_t* obj = lv_line_create(dial);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(obj, 0, 0);
  lv_obj_set_size(obj, size, size);
  lv_obj_set_style_line_width(obj, width, 0);
  lv_obj_set_style_line_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_line_rounded(obj, true, 0);
  lv_line_set_points(obj, points, 2);
  return obj;
}
lv_obj_t* dot(lv_obj_t* dial, lv_point_precise_t center, lv_coord_t diameter, uint32_t color) {
  lv_obj_t* obj = plain(dial);
  lv_obj_set_size(obj, diameter, diameter);
  lv_obj_set_pos(obj, static_cast<lv_coord_t>(center.x) - diameter / 2, static_cast<lv_coord_t>(center.y) - diameter / 2);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}
void update(Face* face) {
  struct tm now;
  if (!getLocalTime(&now, 0)) return;
  const int minute_of_day = now.tm_hour * 60 + now.tm_min;
  if (minute_of_day == face->shown) return;
  face->shown = minute_of_day;
  const float radius = face->size / 2.0f;
  const float hour_deg = ((now.tm_hour % 12) + now.tm_min / 60.0f) * 30.0f;
  const float minute_deg = now.tm_min * 6.0f;
  face->hour_points[0] = polar(radius, 0, 0);
  face->hour_points[1] = polar(radius, hour_deg, face->hour_share);
  face->minute_points[0] = polar(radius, 0, 0);
  face->minute_points[1] = polar(radius, minute_deg, face->minute_share);
  lv_line_set_points(face->hour, face->hour_points, 2);
  lv_line_set_points(face->minute, face->minute_points, 2);
}
Face* create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t size, const clock_face::Colors& colors,
             clock_face::Marks marks, bool small) {
  Face* face = new Face{};
  lv_obj_t* dial = plain(parent);
  face->dial = dial; face->size = size;
  lv_obj_set_pos(dial, x, y);
  lv_obj_set_size(dial, size, size);
  lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dial, lv_color_hex(colors.dial), 0);
  lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
  lv_obj_add_flag(dial, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  const float radius = size / 2.0f;
  for (int i = 0; i < 12; ++i) {
    const bool quarter = i % 3 == 0;
    if (marks == clock_face::Marks::kBatons) {
      face->marks[i][0] = polar(radius, i * 30.0f, small ? 0.62f : 0.68f);
      face->marks[i][1] = polar(radius, i * 30.0f, 0.86f);
      line(dial, size, face->marks[i], LV_MAX(1, size / 50), colors.major);
    } else if (quarter || !small) {
      const lv_coord_t diameter = LV_MAX(3, static_cast<lv_coord_t>(size * (quarter ? 0.048f : 0.028f)));
      dot(dial, polar(radius, i * 30.0f, 0.8f), diameter, quarter ? colors.major : colors.minor);
    }
  }
  const bool batons = marks == clock_face::Marks::kBatons;
  face->hour_share = batons ? 0.5f : 0.45f;
  face->minute_share = batons ? 0.78f : 0.66f;
  const lv_coord_t hour_width = LV_MAX(2, static_cast<lv_coord_t>(size * (batons ? 0.022f : 0.05f)));
  const lv_coord_t minute_width = LV_MAX(2, static_cast<lv_coord_t>(size * (batons ? 0.016f : 0.032f)));
  face->hour_points[0] = face->hour_points[1] = polar(radius, 0, 0);
  face->minute_points[0] = face->minute_points[1] = polar(radius, 0, 0);
  face->hour = line(dial, size, face->hour_points, hour_width, colors.hour);
  face->minute = line(dial, size, face->minute_points, minute_width, colors.minute);
  const lv_coord_t center = LV_MAX(4, static_cast<lv_coord_t>(size * (batons ? 0.036f : 0.09f)));
  dot(dial, polar(radius, 0, 0), center, colors.center);
  update(face);
  return face;
}
}  // namespace old_face

static constexpr int W = 480, H = 260, kBand = 28;
static std::vector<uint16_t> g_screen(W * H);
static std::vector<uint16_t> g_band(W * kBand);

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = a->x2 - a->x1 + 1;
  const uint16_t* src = reinterpret_cast<const uint16_t*>(px);
  for (int y = a->y1; y <= a->y2; ++y) memcpy(&g_screen[y * W + a->x1], src + (y - a->y1) * w, w * 2);
  lv_display_flush_ready(d);
}

struct Spec { int x, y, size; clock_face::Marks marks; bool small; };
static const Spec kFaces[] = {
    {13, 7, 200, clock_face::Marks::kDots, false},
    {230, 9, 101, clock_face::Marks::kDots, true},
    {345, 15, 120, clock_face::Marks::kBatons, false},
    {240, 140, 63, clock_face::Marks::kBatons, true},
    {350, 150, 96, clock_face::Marks::kDots, false},
};
static const clock_face::Colors kAnalog{0x333333, 0xFFFFFF, 0x888888, 0xEEEEEE, 0xDDDDDD, 0xCCCCCC};
static const clock_face::Colors kWall{0x5E6C82, 0xC9A45C, 0xC9A45C, 0xC9A45C, 0xC9A45C, 0xC9A45C};

static std::vector<old_face::Face*> g_old;
static unsigned g_new_children = 0;

static void build(bool fresh) {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  g_old.clear();
  g_new_children = 0;
  for (const Spec& s : kFaces) {
    const clock_face::Colors& colors = s.marks == clock_face::Marks::kBatons ? kWall : kAnalog;
    if (fresh) {
      lv_obj_t* dial = clock_face::create(screen, s.x, s.y, s.size, colors, s.marks, s.small);
      g_new_children += lv_obj_get_child_count(dial);
    } else {
      g_old.push_back(old_face::create(screen, s.x, s.y, s.size, colors, s.marks, s.small));
    }
  }
  lv_obj_t* old = lv_screen_active();
  lv_screen_load(screen);
  if (old) lv_obj_delete(old);
}

static void render(std::vector<uint16_t>& full, std::vector<uint16_t>& part) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  full = g_screen;
  const lv_area_t cut = {60, 40, 300, 90};
  for (int y = cut.y1; y <= cut.y2; ++y)
    for (int x = cut.x1; x <= cut.x2; ++x) g_screen[y * W + x] = 0xF81F;
  lv_obj_invalidate_area(lv_screen_active(), &cut);
  lv_refr_now(nullptr);
  part = g_screen;
}

static unsigned compare(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b, unsigned* first) {
  unsigned n = 0;
  for (int i = 0; i < W * H; ++i) {
    if (a[i] == b[i]) continue;
    if (!n) *first = i;
    ++n;
  }
  return n;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_band.data(), nullptr, g_band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  unsigned diffs = 0, first = 0, children = 0;
  const int times[][2] = {{10, 10}, {3, 47}, {8, 0}, {11, 59}};
  for (const auto& t : times) {
    g_hour = t[0];
    g_minute = t[1];
    std::vector<uint16_t> old_full, old_part, new_full, new_part;
    build(false);
    render(old_full, old_part);
    // The next minute moves the old hands.
    g_minute = (t[1] + 17) % 60;
    for (old_face::Face* f : g_old) old_face::update(f);
    std::vector<uint16_t> old_moved, old_moved_part;
    render(old_moved, old_moved_part);
    g_minute = t[1];
    build(true);
    children += g_new_children;
    render(new_full, new_part);
    g_minute = (t[1] + 17) % 60;
    lv_tick_inc(1100);
    lv_timer_handler();
    std::vector<uint16_t> new_moved, new_moved_part;
    render(new_moved, new_moved_part);
    diffs += compare(old_full, new_full, &first) + compare(old_part, new_part, &first) +
             compare(old_moved, new_moved, &first) + compare(old_moved_part, new_moved_part, &first);
    for (old_face::Face* f : g_old) delete f;
  }
  printf("{\"diffs\":%u,\"first\":[%u,%u],\"children\":%u}\n", diffs, first % W, first / W, children);
  return 0;
}
`;

const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, ['-I', path.join(out, 'shim'), ...host.flags, '-std=c++17', '-O1', '-w', source,
  path.join(root, 'src/types/clock/clock_face.cpp'), path.join(root, 'src/ui/shared/rounded_corners.cpp'), path.join(root, 'src/ui/shared/kept_shadow.cpp'), host.archive,
  '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.equal(run.diffs, 0, `the one-object face draws every pixel like the old one ${JSON.stringify(run)}`);
assert.equal(run.children, 0, 'the new face has no child objects');
console.log(`PASS: the clock face is one object with the old pixels ${JSON.stringify(run)}`);
