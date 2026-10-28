#include "src/types/clock/clock_face.h"

#include <Arduino.h>
#include <lvgl_private.h>
#include <math.h>
#include <new>
#include <time.h>

#include "src/ui/shared/rounded_corners.h"

namespace clock_face {
namespace {

constexpr float kPi = 3.14159265f;

// The face is one object, the dial. It draws its marks, hands and center cap
// itself, in the order and with the descriptors LVGL used for them as
// children (twelve dots or batons, two full-dial lv_line objects and a dot):
// fifteen objects visited in every draw band cost more than drawing them
// (V2 b592/b593: the clock tile's ~130 element pieces per page turn, ~15 ms).
struct Face {
  lv_obj_t* dial = nullptr;
  Colors colors;
  Marks marks = Marks::kDots;
  lv_point_precise_t hour_points[2] = {};
  lv_point_precise_t minute_points[2] = {};
  lv_point_precise_t batons[12][2] = {};
  lv_area_t dots[12] = {};  // in dial coordinates; x1 > x2: no dot
  bool dot_major[12] = {};
  lv_area_t cap = {};
  lv_coord_t baton_width = 1;
  lv_coord_t hour_width = 2;
  lv_coord_t minute_width = 2;
  lv_coord_t size = 0;
  // Hand lengths as shares of the radius (the mockup's: analog 0.45 / 0.66,
  // wall clock 0.5 / 0.78).
  float hour_share = 0.45f;
  float minute_share = 0.66f;
  int shown = -1;
  lv_timer_t* timer = nullptr;
};

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  if (!obj) return nullptr;
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

// A point `share` of the radius from the center at `degrees` clockwise from 12.
lv_point_precise_t polar(float radius, float degrees, float share) {
  const float a = degrees * kPi / 180.0f;
  lv_point_precise_t p;
  p.x = static_cast<lv_value_precise_t>(radius + share * radius * sinf(a));
  p.y = static_cast<lv_value_precise_t>(radius - share * radius * cosf(a));
  return p;
}

// The dot a plain round child of that diameter centered on `center` covered.
lv_area_t dot_area(lv_point_precise_t center, lv_coord_t diameter) {
  const lv_coord_t x = static_cast<lv_coord_t>(center.x) - diameter / 2;
  const lv_coord_t y = static_cast<lv_coord_t>(center.y) - diameter / 2;
  return {x, y, x + diameter - 1, y + diameter - 1};
}

// What lv_obj_init_draw_*_dsc gives a main part without styles of its own:
// the layer's opacity and recolor (lv_obj_draw.c get_layer_opa,
// normal_apply_layer_recolor).
lv_opa_t layer_opa(const lv_layer_t* layer) {
  return layer->opa < LV_OPA_MAX ? LV_OPA_MIX2(LV_OPA_COVER, layer->opa) : LV_OPA_COVER;
}

lv_color_t layer_color(const lv_layer_t* layer, uint32_t hex) {
  const lv_color32_t recolor = layer->recolor;
  return lv_color_mix(lv_color_make(recolor.red, recolor.green, recolor.blue), lv_color_hex(hex), recolor.alpha);
}

// A plain dot child: radius circle, background color, full background opacity.
void draw_dot(lv_layer_t* layer, const lv_area_t& dial, const lv_area_t& dot, uint32_t color) {
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.base.layer = layer;
  rect.radius = LV_RADIUS_CIRCLE;
  rect.bg_opa = layer_opa(layer);
  if (rect.bg_opa <= LV_OPA_MIN) return;
  rect.bg_color = layer_color(layer, color);
  const lv_area_t at = {dial.x1 + dot.x1, dial.y1 + dot.y1, dial.x1 + dot.x2, dial.y1 + dot.y2};
  lv_draw_rect(layer, &rect, &at);
}

// A full-dial lv_line child with rounded ends (lv_line.c LV_EVENT_DRAW_MAIN).
void draw_line(lv_layer_t* layer, const lv_area_t& dial, const lv_point_precise_t* points, lv_coord_t width,
               uint32_t color) {
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.base.layer = layer;
  line.opa = layer_opa(layer);
  if (line.opa <= LV_OPA_MIN) return;
  line.width = width;
  line.color = layer_color(layer, color);
  line.round_start = 1;
  line.round_end = 1;
  line.p1.x = points[0].x + dial.x1;
  line.p1.y = points[0].y + dial.y1;
  line.p2.x = points[1].x + dial.x1;
  line.p2.y = points[1].y + dial.y1;
  lv_draw_line(layer, &line);
}

// After the dial's own background, where its children were drawn.
void draw_cb(lv_event_t* e) {
  Face* face = static_cast<Face*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!face || !layer) return;
  lv_area_t dial;
  lv_obj_get_coords(face->dial, &dial);
  for (int i = 0; i < 12; ++i) {
    if (face->marks == Marks::kBatons) {
      draw_line(layer, dial, face->batons[i], face->baton_width, face->colors.major);
    } else if (face->dots[i].x1 <= face->dots[i].x2) {
      draw_dot(layer, dial, face->dots[i], face->dot_major[i] ? face->colors.major : face->colors.minor);
    }
  }
  draw_line(layer, dial, face->hour_points, face->hour_width, face->colors.hour);
  draw_line(layer, dial, face->minute_points, face->minute_width, face->colors.minute);
  draw_dot(layer, dial, face->cap, face->colors.center);
}

// The hands for the time now, only when the minute changed.
void update(Face* face) {
  struct tm now;
  if (!face || !getLocalTime(&now, 0)) return;
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
  if (face->dial) lv_obj_invalidate(face->dial);
}

void timer_cb(lv_timer_t* timer) { update(static_cast<Face*>(lv_timer_get_user_data(timer))); }

}  // namespace

lv_obj_t* create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t size, const Colors& colors, Marks marks,
                 bool small) {
  if (!parent || size < 8) return nullptr;
  Face* face = new (std::nothrow) Face{};
  if (!face) return nullptr;
  lv_obj_t* dial = plain(parent);
  if (!dial) {
    delete face;
    return nullptr;
  }
  face->dial = dial;
  face->size = size;
  face->colors = colors;
  face->marks = marks;
  lv_obj_set_pos(dial, x, y);
  lv_obj_set_size(dial, size, size);
  lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dial, lv_color_hex(colors.dial), 0);
  lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
  lv_obj_add_flag(dial, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  const float radius = size / 2.0f;
  face->baton_width = LV_MAX(1, size / 50);
  for (int i = 0; i < 12; ++i) {
    const bool quarter = i % 3 == 0;
    face->dots[i] = {0, 0, -1, -1};
    if (marks == Marks::kBatons) {
      face->batons[i][0] = polar(radius, i * 30.0f, small ? 0.62f : 0.68f);
      face->batons[i][1] = polar(radius, i * 30.0f, 0.86f);
    } else if (quarter || !small) {
      const lv_coord_t diameter = LV_MAX(3, static_cast<lv_coord_t>(size * (quarter ? 0.048f : 0.028f)));
      face->dots[i] = dot_area(polar(radius, i * 30.0f, 0.8f), diameter);
      face->dot_major[i] = quarter;
    }
  }
  // The hands from the center; placed by update().
  const bool batons = marks == Marks::kBatons;
  face->hour_share = batons ? 0.5f : 0.45f;
  face->minute_share = batons ? 0.78f : 0.66f;
  face->hour_width = LV_MAX(2, static_cast<lv_coord_t>(size * (batons ? 0.022f : 0.05f)));
  face->minute_width = LV_MAX(2, static_cast<lv_coord_t>(size * (batons ? 0.016f : 0.032f)));
  face->hour_points[0] = face->hour_points[1] = polar(radius, 0, 0);
  face->minute_points[0] = face->minute_points[1] = polar(radius, 0, 0);
  const lv_coord_t center = LV_MAX(4, static_cast<lv_coord_t>(size * (batons ? 0.036f : 0.09f)));
  face->cap = dot_area(polar(radius, 0, 0), center);
  lv_obj_add_event_cb(dial, draw_cb, LV_EVENT_DRAW_MAIN_END, face);
  // The round dial's fill from kept corners (rounded_corners.h).
  rounded_corners::attach(dial);
  update(face);
  face->timer = lv_timer_create(timer_cb, 1000, face);

  lv_obj_add_event_cb(
      dial,
      [](lv_event_t* e) {
        Face* face = static_cast<Face*>(lv_event_get_user_data(e));
        if (!face) return;
        if (face->timer) lv_timer_delete(face->timer);
        delete face;
      },
      LV_EVENT_DELETE, face);
  return dial;
}

}  // namespace clock_face
