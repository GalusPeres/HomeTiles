#include "src/types/sensor/look_view.h"

#include <Arduino.h>
#include <lvgl_private.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/types/climate/layout.h"
#include "src/types/sensor/sensor_look.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"

namespace sensor_look_view {
namespace {

// More places than this in their own colors share the last group's color
// (a smooth rule over a very long row of stripes).
constexpr int kMaxGroups = 64;

// One color's shapes: an A8 mask over their bounds (picture pixels).
struct Group {
  uint8_t layer = 0;
  bool lit = false;
  // A rule's bar color for this group's places; 0 without places.
  uint32_t rgb = 0;
  bool rule = false;
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
  uint8_t* mask = nullptr;
  lv_image_dsc_t image = {};
  // The rule color shown: lit in the readable icon color, unlit in the
  // control fill the tile gives that icon color.
  uint32_t shown = 0;
};

struct View {
  lv_obj_t* frame = nullptr;
  lv_obj_t* icon = nullptr;
  sensor_look::Spec spec;
  float gauge_min = 0;
  float gauge_max = 100;
  float fraction = 0;
  bool has_bar = false;
  tile_icon_colors::Bar bar;
  Group* groups = nullptr;
  int group_count = 0;
  uint32_t key = 0;
  bool built = false;
  uint32_t drawn_icon = 0;
  // The object's top in the card's content box.
  int32_t top = 0;
};

// A group's identity and bounds while the masks are planned.
struct Found {
  uint8_t layer;
  bool lit;
  uint32_t rgb;
  int x0, y0, x1, y1;
};

// The shown amount in steps fine enough for every look (a level's fill end
// moves by under half a pixel per step on the widest tile).
uint32_t quantized(float fraction) { return static_cast<uint32_t>(std::lround(fraction * 4096.0f)); }

void release(View& view) {
  for (int i = 0; i < view.group_count; ++i) {
    lv_image_cache_drop(&view.groups[i].image);
    lv_free(view.groups[i].mask);
  }
  lv_free(view.groups);
  view.groups = nullptr;
  view.group_count = 0;
}

uint32_t place_rgb(const View& view, float place) {
  const double value = view.gauge_min + (view.gauge_max - view.gauge_min) * place;
  return tile_icon_colors::bar_color(view.bar, value) & 0xFFFFFF;
}

bool places(const View& view) { return view.has_bar && sensor_look::colors_places(view.spec.mode); }

// What a rule's unlit colors depend on: the theme, the resting card, a
// see-through card, the circle strength and a filled light card.
uint32_t color_context(lv_obj_t* card) {
  uint32_t hash = 2166136261u;
  auto mix = [&hash](uint32_t value) { hash = (hash ^ value) * 16777619u; };
  mix(ui_theme::light() ? 1u : 0u);
  mix(tile_icon_disc::card_state_color(card, false));
  mix(tile_icon_disc::see_through(card) ? 1u : 0u);
  mix(ui_surface_style::icon_glow_percent());
  mix(tile_surface::find(card));
  return hash;
}

uint32_t view_key(const View& view, lv_obj_t* card) {
  uint32_t hash = 2166136261u;
  auto mix = [&hash](uint32_t value) { hash = (hash ^ value) * 16777619u; };
  mix(quantized(view.fraction));
  mix(static_cast<uint32_t>(view.spec.box.w));
  mix(static_cast<uint32_t>(view.spec.box.h));
  if (places(view)) mix(color_context(card));
  return hash;
}

// The color a rule gives a place: lit as the icon would show it, unlit as
// the controls of a tile with that icon color.
uint32_t rule_color(lv_obj_t* card, lv_obj_t* icon, uint32_t rgb, bool lit) {
  if (lit) return tone_color::readable_icon(rgb);
  lv_obj_t* disc = icon ? tile_icon_disc::disc_of(icon) : nullptr;
  const bool tinted = disc && tile_icon_disc::glow_of(disc) && tile_icon_disc::icon_color_tints(rgb);
  return tile_icon_disc::tile_fill(card, tile_icon_disc::card_state_color(card, false), rgb, tinted, false,
                                   tile_icon_disc::see_through(card), ui_surface_style::icon_glow_percent())
      .control_color;
}

// The masks for the current value: the groups and their bounds first, then
// every shape's coverage into its group's mask.
void build(View& view, lv_obj_t* card) {
  release(view);
  sensor_look::Spec spec = view.spec;
  spec.fraction = view.fraction;
  spec.places = places(view);
  // A level's track is the object's own background.
  const bool own_track = sensor_look::is_level(spec.mode);
  // The plan lives in the LVGL heap: draw callbacks run deep in the stack.
  auto* found = static_cast<Found*>(lv_malloc(sizeof(Found) * kMaxGroups));
  if (!found) return;
  int count = 0;
  auto group_of = [&](const sensor_look::Element& e) -> int {
    const uint32_t rgb = spec.places ? place_rgb(view, e.place) : 0;
    for (int i = 0; i < count; ++i) {
      if (found[i].layer == e.layer && found[i].lit == e.lit && found[i].rgb == rgb) return i;
    }
    if (count == kMaxGroups) return count - 1;
    found[count] = {e.layer, e.lit, rgb, e.x0, e.y0, e.x1, e.y1};
    return count++;
  };
  sensor_look::emit_look(spec, [&](const sensor_look::Element& e) {
    if ((own_track && e.layer == 0) || e.x1 < e.x0 || e.y1 < e.y0) return;
    Found& group = found[group_of(e)];
    group.x0 = std::min(group.x0, e.x0);
    group.y0 = std::min(group.y0, e.y0);
    group.x1 = std::max(group.x1, e.x1);
    group.y1 = std::max(group.y1, e.y1);
  });
  view.built = true;
  // Drawn in layer order: the track and unlit places under the lit ones.
  std::stable_sort(found, found + count, [](const Found& a, const Found& b) { return a.layer < b.layer; });
  view.groups = count ? static_cast<Group*>(lv_malloc(sizeof(Group) * count)) : nullptr;
  if (!view.groups) {
    lv_free(found);
    return;
  }
  for (int i = 0; i < count; ++i) {
    Group& group = view.groups[i];
    group = Group();
    group.layer = found[i].layer;
    group.lit = found[i].lit;
    group.rgb = found[i].rgb;
    group.rule = spec.places;
    group.x0 = found[i].x0;
    group.y0 = found[i].y0;
    group.x1 = found[i].x1;
    group.y1 = found[i].y1;
    const int w = group.x1 - group.x0 + 1, h = group.y1 - group.y0 + 1;
    group.mask = static_cast<uint8_t*>(lv_malloc(static_cast<size_t>(w) * h));
    if (!group.mask) {
      view.group_count = i;
      release(view);
      lv_free(found);
      Serial.println("[SensorLook] WARNING: no memory for the look's masks");
      return;
    }
    std::memset(group.mask, 0, static_cast<size_t>(w) * h);
    group.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    group.image.header.cf = LV_COLOR_FORMAT_A8;
    group.image.header.w = w;
    group.image.header.h = h;
    group.image.header.stride = w;
    group.image.data = group.mask;
    group.image.data_size = static_cast<size_t>(w) * h;
    if (group.rule) group.shown = rule_color(card, view.icon, group.rgb, group.lit);
    view.group_count = i + 1;
  }
  lv_free(found);
  const sensor_look::Box& box = spec.box;
  sensor_look::emit_look(spec, [&](const sensor_look::Element& e) {
    if ((own_track && e.layer == 0) || e.x1 < e.x0 || e.y1 < e.y0) return;
    const uint32_t rgb = spec.places ? place_rgb(view, e.place) : 0;
    Group* group = nullptr;
    for (int i = 0; i < view.group_count && !group; ++i) {
      Group& candidate = view.groups[i];
      if (candidate.layer == e.layer && candidate.lit == e.lit && candidate.rgb == rgb) group = &candidate;
    }
    // Beyond kMaxGroups: the last group of the element's layer.
    for (int i = view.group_count - 1; i >= 0 && !group; --i) {
      if (view.groups[i].layer == e.layer) group = &view.groups[i];
    }
    if (!group) return;
    const int w = group->x1 - group->x0 + 1;
    for (int y = std::max(e.y0, group->y0); y <= std::min(e.y1, group->y1); ++y) {
      uint8_t* row = group->mask + (y - group->y0) * w - group->x0;
      for (int x = std::max(e.x0, group->x0); x <= std::min(e.x1, group->x1); ++x) {
        const float c = sensor_look::coverage(e, box, x + 0.5f, y + 0.5f);
        if (c <= 0) continue;
        const uint8_t a = static_cast<uint8_t>(c * 255.0f + 0.5f);
        if (a > row[x]) row[x] = a;
      }
    }
  });
}

void draw_cb(lv_event_t* e) {
  auto* view = static_cast<View*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!view || !layer) return;
  lv_obj_t* card = lv_obj_get_parent(view->frame);
  // The look draws with its tile's surface (tile_surface, tile_icon_disc.h).
  const ui_theme::SurfaceScope surface = tile_surface::scope(view->frame);
  const uint32_t key = view_key(*view, card);
  if (!view->built || key != view->key) {
    build(*view, card);
    view->key = key;
  }
  if (!view->group_count) return;
  lv_area_t coords;
  lv_obj_get_coords(view->frame, &coords);
  const lv_color_t icon =
      view->icon ? lv_obj_get_style_text_color(view->icon, LV_PART_MAIN) : lv_color_hex(ui_theme::text());
  view->drawn_icon = lv_color_to_u32(icon) & 0xFFFFFF;
  const lv_color_t control = lv_obj_get_style_bg_color(view->frame, LV_PART_MAIN);
  const lv_opa_t control_opa =
      tone_color::control_opa(ui_surface_style::icon_glow_percent(), tile_icon_disc::see_through(card));
  lv_draw_image_dsc_t image;
  lv_draw_image_dsc_init(&image);
  image.base.layer = layer;
  image.recolor_opa = LV_OPA_COVER;
  for (int i = 0; i < view->group_count; ++i) {
    const Group& group = view->groups[i];
    image.src = &group.image;
    image.recolor = group.rule ? lv_color_hex(group.shown) : group.lit ? icon : control;
    image.opa = group.lit ? LV_OPA_COVER : control_opa;
    const lv_area_t at = {coords.x1 + group.x0, coords.y1 + group.y0, coords.x1 + group.x1, coords.y1 + group.y1};
    lv_draw_image(layer, &image, &at);
  }
}

void delete_cb(lv_event_t* e) {
  auto* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view) return;
  release(*view);
  delete view;
}

View* view_of(lv_obj_t* look) {
  if (!look) return nullptr;
  for (uint32_t i = 0; i < lv_obj_get_event_count(look); ++i) {
    lv_event_dsc_t* descriptor = lv_obj_get_event_dsc(look, i);
    if (lv_event_dsc_get_cb(descriptor) == draw_cb) return static_cast<View*>(lv_event_dsc_get_user_data(descriptor));
  }
  return nullptr;
}

}  // namespace

lv_obj_t* create(lv_obj_t* card, const Tile& tile, lv_obj_t* icon, const lv_area_t& box, float gauge_min,
                 float gauge_max) {
  if (!card || !sensor_look::is_look(tile.sensor_display_mode)) return nullptr;
  const int32_t width = lv_area_get_width(&box), height = lv_area_get_height(&box);
  if (width < 8 || height < 8) return nullptr;
  lv_obj_t* frame = lv_obj_create(card);
  if (!frame) return nullptr;
  const uint8_t mode = tile.sensor_display_mode;
  // A resting surface (tile_icon_source::refresh_controls gives it the
  // control fill like the Switch bar): a level shows it as its track; the
  // other looks keep it transparent and take its color for their track.
  tile_icon_disc::mark_surface(frame);
  if (!sensor_look::is_level(mode)) lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(frame, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(frame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(frame, 0, LV_PART_MAIN);
  ui_surface_style::apply_radius(frame, climate_layout::kControlRadius, 0);
  lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(frame, width, height);
  // Positions inside the card's content box (its paddings).
  const int32_t top = box.y1 - lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  lv_obj_set_pos(frame, box.x1 - lv_obj_get_style_pad_left(card, LV_PART_MAIN), top);

  auto* view = new View();
  view->frame = frame;
  view->top = top;
  view->icon = icon;
  view->gauge_min = gauge_min;
  view->gauge_max = gauge_max > gauge_min ? gauge_max : gauge_min + 1;
  view->spec.mode = mode;
  view->spec.box = sensor_look::make_box(static_cast<float>(width), static_cast<float>(height),
                                         static_cast<float>(lv_obj_get_style_radius(frame, LV_PART_MAIN)));
  sensor_look::Metrics& m = view->spec.metrics;
  m.stroke = tile_layout::scale(12);
  m.band = tile_layout::scale(16);
  m.stripe = tile_layout::scale(6);
  m.gap = tile_layout::scale(5);
  m.pitch = tile_layout::scale(11);
  // A quarter of the one-row bar's height (level_bar::box base: the box
  // from its top to the inset above a one-row tile's bottom), like the
  // Switch dimmer's end.
  m.end = std::max(1.0f, std::round((GRID_CELL_H - climate_layout::kOuterInset - box.y1) / 4.0f));
  view->spec.arc = sensor_look::look_arc(mode, view->spec.box, m);
  view->has_bar = tile_icon_colors::own_bar(tile.icon_colors.c_str(), view->bar);
  lv_obj_add_event_cb(frame, draw_cb, LV_EVENT_DRAW_MAIN, view);
  lv_obj_add_event_cb(frame, delete_cb, LV_EVENT_DELETE, view);
  return frame;
}

int32_t value_top(lv_obj_t* look, int32_t size, int32_t standard) {
  View* view = view_of(look);
  if (!view) return 0;
  return view->top + static_cast<int32_t>(std::lround(sensor_look::value_top(
                         view->spec.mode, view->spec.arc, static_cast<float>(size), static_cast<float>(standard))));
}

void set_value(lv_obj_t* look, float value) {
  View* view = view_of(look);
  if (!view) return;
  const float fraction =
      std::isfinite(value) ? sensor_look::clamp01((value - view->gauge_min) / (view->gauge_max - view->gauge_min)) : 0;
  if (quantized(fraction) == quantized(view->fraction)) return;
  view->fraction = fraction;
  lv_obj_invalidate(look);
}

void sync_color(lv_obj_t* look) {
  View* view = view_of(look);
  if (!view || !view->icon) return;
  if ((lv_color_to_u32(lv_obj_get_style_text_color(view->icon, LV_PART_MAIN)) & 0xFFFFFF) != view->drawn_icon) {
    lv_obj_invalidate(look);
  }
}

}  // namespace sensor_look_view
