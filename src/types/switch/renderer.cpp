#include "src/ui/shared/ui_surface_style.h"
#include "src/types/switch/renderer.h"
#include "src/types/switch/layout.h"
#include "src/types/climate/layout.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/shared/command_pacer.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/tone_color.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/config/tile_config.h"
#include <Arduino.h>
#include <algorithm>

using switch_layout::Bar;
using switch_layout::Layout;

namespace {

constexpr uint32_t kIconOff = 0xB0B0B0;
// Remote brightness echoes stay out of a dimmer the finger just released,
// like the Light popup (kRemoteBlockMs).
constexpr uint32_t kRemoteBlockMs = 3000;
// Home Assistant starts a slider drag after 10 px (ha-control-slider);
// below it a press is a tap: one command on release.
constexpr int kDragThreshold = tile_layout::scale(10);

struct SwitchEventData {
  String entity_id;
  String title;
  GridType grid_type;
  uint8_t index = 0;
  // Header layouts (Switch, Dimmer, Automatic, half height); null for the
  // full-size icon button.
  SwitchBarView* view = nullptr;
};

SwitchState get_switch_state(GridType grid_type, uint8_t index) {
  SwitchState* states = tile_renderer_get_switch_states(grid_type);
  if (!states || index >= TILES_PER_GRID) return {};
  return states[index];
}

const char* language() { return configManager.getConfig().language; }

// ---------------------------------------------------------------------------
// Commands. Dimmer gestures share one pacer (command_pacer.h, GitHub issue
// #11): slow light buses get Home Assistant's own slider timing.

struct DimmerDrag {
  SwitchEventData* data = nullptr;
  lv_point_t press = {0, 0};
  bool dragging = false;
  bool moved = false;
  uint8_t value = 0;
  uint32_t block_until = 0;
  // A dimmer holds its level; a toggled tile only its on/off (value 0 or 1).
  bool hold_level = true;
};

DimmerDrag g_drag;
command_pacer::Pacer g_pacer;
lv_timer_t* g_live_timer = nullptr;
// Ends the hold of a released dimmer: shows the state Home Assistant last
// reported (a reply skipped during the hold, or no reply at all).
lv_timer_t* g_release_timer = nullptr;
void release_timer_cb(lv_timer_t*);

// Holds the tile's own state against Home Assistant echoes of earlier
// commands for kRemoteBlockMs; release_timer_cb then shows the last
// reported state.
void start_hold() {
  g_drag.dragging = false;
  g_drag.block_until = millis() + kRemoteBlockMs;
  if (g_release_timer) lv_timer_delete(g_release_timer);
  g_release_timer = lv_timer_create(release_timer_cb, kRemoteBlockMs, nullptr);
  if (g_release_timer) lv_timer_set_repeat_count(g_release_timer, 1);
}

// The tile's own value while it holds: a dimmer the finger holds or released
// moments ago (`level` true: its level), or a tile toggled moments ago (on/off
// only).
bool held_value(const SwitchBarView* view, uint8_t& value, bool& level) {
  if (!view || !g_drag.data || g_drag.data->view != view) return false;
  if (!g_drag.dragging && static_cast<int32_t>(g_drag.block_until - millis()) <= 0) return false;
  value = g_drag.value;
  level = g_drag.hold_level;
  return true;
}
lv_timer_t* g_final_timer = nullptr;
String g_final_entity;
uint8_t g_final_value = 0;

void publish_level(const String& entity_id, uint8_t value) {
  if (!entity_id.length()) return;
  if (value == 0) {
    mqttPublishLightCommand(entity_id.c_str(), "off", -1, false, 0, -1);
  } else {
    mqttPublishLightCommand(entity_id.c_str(), "on", value, false, 0, -1);
  }
}

void send_level(const String& entity_id, uint8_t value) {
  publish_level(entity_id, value);
  g_pacer.sent(millis(), value);
}

void cancel_live_timer() {
  if (g_live_timer) {
    lv_timer_delete(g_live_timer);
    g_live_timer = nullptr;
  }
}

void flush_final() {
  if (g_final_timer) {
    lv_timer_delete(g_final_timer);
    g_final_timer = nullptr;
    send_level(g_final_entity, g_final_value);
  }
}

void live_timer_cb(lv_timer_t*) {
  g_live_timer = nullptr;
  if (!g_drag.dragging || !g_drag.data || !g_drag.moved) return;
  send_level(g_drag.data->entity_id, g_drag.value);
}

void final_timer_cb(lv_timer_t*) {
  g_final_timer = nullptr;
  send_level(g_final_entity, g_final_value);
}

void schedule_live() {
  if (!g_drag.data || g_live_timer) return;
  const uint32_t wait = g_pacer.wait(millis());
  if (wait == 0) {
    send_level(g_drag.data->entity_id, g_drag.value);
    return;
  }
  g_live_timer = lv_timer_create(live_timer_cb, wait, nullptr);
  if (g_live_timer) lv_timer_set_repeat_count(g_live_timer, 1);
}

// Release: the final value always goes out, paced, and is skipped when the
// gesture already sent exactly it.
void commit_level(const String& entity_id, uint8_t value) {
  cancel_live_timer();
  const bool repeat = g_pacer.final_redundant(value);
  g_pacer.end_gesture();
  if (repeat) return;
  if (g_final_timer && !g_final_entity.equalsIgnoreCase(entity_id)) flush_final();
  const uint32_t wait = g_pacer.wait(millis());
  if (wait == 0) {
    if (g_final_timer) {
      lv_timer_delete(g_final_timer);
      g_final_timer = nullptr;
    }
    send_level(entity_id, value);
    return;
  }
  g_final_entity = entity_id;
  g_final_value = value;
  if (g_final_timer) return;
  g_final_timer = lv_timer_create(final_timer_cb, wait, nullptr);
  if (g_final_timer) {
    lv_timer_set_repeat_count(g_final_timer, 1);
  } else {
    send_level(entity_id, value);
  }
}

// Echoes of earlier commands must not flip a toggled tile back for a moment
// (off showed the light color): it holds its on/off like a released dimmer.
// A released dimmer level still waiting for its paced send would undo the
// switch (switched off, the light came back on), so it is dropped.
void hold_toggle(SwitchEventData* data, bool on) {
  if (g_final_timer && g_final_entity.equalsIgnoreCase(data->entity_id)) {
    lv_timer_delete(g_final_timer);
    g_final_timer = nullptr;
  }
  cancel_live_timer();
  g_drag = DimmerDrag{};
  g_drag.data = data;
  g_drag.hold_level = false;
  g_drag.value = on ? 1 : 0;
  start_hold();
}

void toggle_switch_tile(SwitchEventData* data) {
  if (!data || !data->entity_id.length()) return;
  const SwitchState current = get_switch_state(data->grid_type, data->index);
  if (!current.available) return;
  // Header layouts show the state, so they switch optimistically like the
  // former LVGL switch; the icon button keeps sending "toggle".
  if (data->view && current.has_state) {
    const bool next_on = !current.is_on;
    hold_toggle(data, next_on);
    update_switch_tile_state(data->grid_type, data->index, next_on ? "on" : "off");
    mqttPublishSwitchCommand(data->entity_id.c_str(), next_on ? "on" : "off");
    return;
  }
  Serial.printf("[Tile] Switch toggle: %s\n", data->entity_id.c_str());
  mqttPublishSwitchCommand(data->entity_id.c_str(), "toggle");
}

// ---------------------------------------------------------------------------
// State line

const lv_font_t* fitting_state_font(const char* text, int width, const lv_font_t* largest) {
  // 12 and 14 px exist only in the 480x480 layout (ui_fonts.h).
  static const lv_font_t* const kSizes[] = {&ui_font_40, &ui_font_32, &ui_font_28, &ui_font_24,
                                            &ui_font_20, &ui_font_16,
#if defined(DEVICE_LAYOUT_480X480)
                                            &ui_font_14, &ui_font_12,
#endif
  };
  const int32_t start = lv_font_get_line_height(largest);
  const lv_font_t* last = kSizes[sizeof(kSizes) / sizeof(kSizes[0]) - 1];
  for (const lv_font_t* font : kSizes) {
    if (lv_font_get_line_height(font) > start) continue;
    last = font;
    if (width <= 0) return font;
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= width) return font;
  }
  return last;
}

// Translated states never shorten: the font steps down until the text fits
// (the chosen half-height Sensor value size is the largest).
void set_state_text(SwitchBarView* view, const char* text) {
  if (!view || !view->state_label || !text) return;
  if (strcmp(lv_label_get_text(view->state_label), text) == 0) return;
  if (!view->compact && view->state_font) {
    const lv_font_t* font = fitting_state_font(text, view->state_width, view->state_font);
    if (lv_obj_get_style_text_font(view->state_label, LV_PART_MAIN) != font) {
      lv_obj_set_style_text_font(view->state_label, font, 0);
      if (view->state_center >= 0) {
        lv_obj_set_y(view->state_label, view->state_center - lv_font_get_line_height(font) / 2);
      }
    }
  }
  lv_label_set_text(view->state_label, text);
}

void show_state_text(SwitchBarView* view, const SwitchState& state, bool dimmable, uint8_t level, bool on) {
  if (!view) return;
  const auto& tr = i18n::strings(language());
  if (!state.available) {
    set_state_text(view, i18n::entity_state_label(language(), "unavailable"));
  } else if (state.unknown) {
    set_state_text(view, i18n::entity_state_label(language(), "unknown"));
  } else if (!state.has_state && !state.has_brightness) {
    set_state_text(view, "--");
  } else if (!on) {
    set_state_text(view, tr.light_off);
  } else if (dimmable) {
    // Same text as the Light popup header (update_top_value_label).
    char buf[16];
    snprintf(buf, sizeof(buf), "%u %%", static_cast<unsigned>(level));
    set_state_text(view, buf);
  } else {
    set_state_text(view, tr.light_on);
  }
}

// ---------------------------------------------------------------------------
// Bar drawing: the bar is one object; its fill, handle line, thumb and power
// symbol are drawn here with the colors of the moment (bar = control fill
// of the card, accent = the color the icon shows), so circle strength, tile
// tint and presses need no extra objects or restyling. No clip_corner, no
// opacity layer: every shape lies inside the bar's own outline.

uint32_t accent_color(const SwitchBarView* view) {
  if (view->icon) return lv_color_to_u32(lv_obj_get_style_text_color(view->icon, LV_PART_MAIN)) & 0xFFFFFF;
  return view->fill_rgb;
}

// lv_area_intersect is private in LVGL 9.6.
bool intersect(const lv_area_t& a, const lv_area_t& b, lv_area_t& out) {
  out.x1 = std::max(a.x1, b.x1);
  out.y1 = std::max(a.y1, b.y1);
  out.x2 = std::min(a.x2, b.x2);
  out.y2 = std::min(a.y2, b.y2);
  return out.x1 <= out.x2 && out.y1 <= out.y2;
}

void draw_rect(lv_layer_t* layer, const lv_area_t& area, lv_color_t color, int32_t radius) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = radius;
  lv_draw_rect(layer, &dsc, &area);
}

// The power symbol (on) or a ring (off), centered in `area`.
// `max_side`: the symbol keeps the one-row bar's size on a taller bar.
void draw_power_symbol(lv_layer_t* layer, const lv_area_t& area, lv_color_t color, bool on, int32_t max_side) {
  int32_t side = std::min(lv_area_get_width(&area), lv_area_get_height(&area));
  if (max_side > 0) side = std::min(side, max_side);
  const int32_t radius = side * 21 / 100;
  const int32_t width = std::max<int32_t>(2, side / 16);
  if (radius < 3) return;
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.base.layer = layer;
  arc.color = color;
  arc.width = width;
  arc.rounded = 1;
  arc.center.x = area.x1 + lv_area_get_width(&area) / 2;
  arc.center.y = area.y1 + lv_area_get_height(&area) / 2;
  arc.radius = static_cast<uint16_t>(radius);
  // LVGL angles run clockwise from 3 o'clock; the gap faces up.
  arc.start_angle = on ? 300 : 0;
  arc.end_angle = on ? 240 : 360;
  lv_draw_arc(layer, &arc);
  if (!on) return;
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.base.layer = layer;
  line.color = color;
  line.width = width;
  line.round_start = 1;
  line.round_end = 1;
  line.p1.x = arc.center.x;
  line.p1.y = arc.center.y - radius - width / 2;
  line.p2.x = arc.center.x;
  line.p2.y = arc.center.y - radius / 5;
  lv_draw_line(layer, &line);
}

void bar_draw_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
  SwitchBarView* view = static_cast<SwitchBarView*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!view || !view->bar || !layer || !view->available) return;
  lv_area_t area;
  lv_obj_get_coords(view->bar, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  if (width < 4 || height < 4) return;
  const int32_t radius =
      std::min<int32_t>(lv_obj_get_style_radius(view->bar, LV_PART_MAIN), height / 2);
  const lv_color_t card = lv_obj_get_style_bg_color(view->card, LV_PART_MAIN);
  const lv_color_t accent = lv_color_hex(accent_color(view));

  if (view->bar_kind == static_cast<uint8_t>(Bar::Dimmer)) {
    if (view->level == 0) return;
    switch_layout::Dimmer geometry{width, height, radius, view->bar_base};
    const int32_t fill = geometry.fill_width(view->level);
    const int32_t end = geometry.end_radius_for(fill);
    // The fill stays inside the bar's shape without a clip layer: up to where
    // its end rounding starts it is the bar's own rounded rect, clipped to
    // that range; the end is a rect with the end rounding, clipped to the
    // rest. Both pieces meet at a full-height column, so there is no seam
    // (switch_layout::Dimmer keeps the end inside the bar's round end).
    const lv_area_t clip_ori = layer->_clip_area;
    const lv_area_t start_range = {area.x1, area.y1, area.x1 + fill - end - 1, area.y2};
    lv_area_t clip;
    if (intersect(clip_ori, start_range, clip)) {
      layer->_clip_area = clip;
      draw_rect(layer, area, accent, radius);
    }
    const lv_area_t end_range = {area.x1 + fill - end, area.y1, area.x1 + fill - 1, area.y2};
    if (intersect(clip_ori, end_range, clip)) {
      layer->_clip_area = clip;
      const lv_area_t end_piece = {area.x1 + fill - 2 * end, area.y1, area.x1 + fill - 1, area.y2};
      draw_rect(layer, end_piece, accent, end);
    }
    layer->_clip_area = clip_ori;
    const int32_t handle_w = geometry.handle_width();
    const int32_t handle_h = geometry.handle_height();
    const int32_t handle_x = area.x1 + geometry.handle_x(view->level) - handle_w / 2;
    const int32_t handle_y = area.y1 + (height - handle_h) / 2;
    const lv_area_t handle = {handle_x, handle_y, handle_x + handle_w - 1, handle_y + handle_h - 1};
    draw_rect(layer, handle, card, LV_RADIUS_CIRCLE);
    return;
  }

  if (view->bar_kind != static_cast<uint8_t>(Bar::Toggle)) return;
  // Flush half-width thumb like the Light popup switch, the bar's radius.
  const int32_t half = width / 2;
  const lv_area_t thumb = view->on ? lv_area_t{area.x2 - half + 1, area.y1, area.x2, area.y2}
                                   : lv_area_t{area.x1, area.y1, area.x1 + half - 1, area.y2};
  lv_color_t thumb_color = accent;
  lv_color_t symbol_color = card;
  if (!view->on) {
    // One OKLCH step above the bar, computed once per bar color.
    const uint32_t base = lv_color_to_u32(lv_obj_get_style_bg_color(view->bar, LV_PART_MAIN)) & 0xFFFFFF;
    if (base != view->thumb_base || !view->thumb_off) {
      view->thumb_base = base;
      view->thumb_off = tone_color::lifted(base, base, false, 0.06f);
    }
    thumb_color = lv_color_hex(view->thumb_off);
    symbol_color = lv_color_white();
  }
  draw_rect(layer, thumb, thumb_color, radius);
  draw_power_symbol(layer, thumb, symbol_color, view->on, view->bar_base);
}

// ---------------------------------------------------------------------------
// Dimmer touch: the Light popup's brightness logic sideways
// (on_brightness_track_event): the press jumps to the finger, pressing
// follows, release commits. The bar never toggles or opens the popup.

uint8_t level_at(const SwitchBarView* view, const lv_point_t& point) {
  lv_area_t area;
  lv_obj_get_coords(view->bar, &area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t radius =
      std::min<int32_t>(lv_obj_get_style_radius(view->bar, LV_PART_MAIN), height / 2);
  switch_layout::Dimmer geometry{lv_area_get_width(&area), height, radius, view->bar_base};
  return geometry.value_at(point.x - area.x1);
}

// Redraws only the columns a level change touches, like the Light popup's
// invalidate_brightness_change: between the old and new fill end, plus the
// end rounding and the handle line before it. The card corners, the tile
// border and the bar's round start stay untouched while dragging.
void invalidate_level_change(const SwitchBarView* view, uint8_t old_level, uint8_t new_level) {
  lv_area_t area;
  lv_obj_get_coords(view->bar, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t radius =
      std::min<int32_t>(lv_obj_get_style_radius(view->bar, LV_PART_MAIN), height / 2);
  const switch_layout::Dimmer geometry{width, height, radius, view->bar_base};
  const int32_t old_fill = geometry.fill_width(old_level);
  const int32_t new_fill = geometry.fill_width(new_level);
  const int32_t reach = std::max<int32_t>(radius, geometry.handle_margin() + geometry.handle_width()) + 1;
  const int32_t from = std::max<int32_t>(0, std::min(old_fill, new_fill) - reach);
  const int32_t to = std::min<int32_t>(width, std::max(old_fill, new_fill) + 1);
  lv_area_t dirty = {area.x1 + from, area.y1, area.x1 + to - 1, area.y2};
  lv_obj_invalidate_area(view->bar, &dirty);
}

void show_local_level(SwitchEventData* data, uint8_t value) {
  SwitchBarView* view = data->view;
  if (!view) return;
  const SwitchState current = get_switch_state(data->grid_type, data->index);
  const bool was_on = current.has_state ? current.is_on : (current.has_brightness && current.brightness_pct > 0);
  if ((value > 0) != was_on) {
    // Off to on (or back) switches the tile at once, like the toggle: icon,
    // circle, tile tint and fill color follow before Home Assistant replies.
    // Once per crossing, not per drag step; the held level stays local.
    char payload[48];
    snprintf(payload, sizeof(payload), "{\"state\":\"%s\",\"brightness_pct\":%u}",
             value ? "on" : "off", static_cast<unsigned>(value));
    update_switch_tile_state(data->grid_type, data->index, payload);
    return;
  }
  if (view->level != value || view->on != (value > 0)) {
    const uint8_t old_level = view->level;
    view->level = value;
    view->on = value > 0;
    invalidate_level_change(view, old_level, value);
  }
  SwitchState shown = current;
  shown.has_state = true;
  shown.unknown = false;
  show_state_text(view, shown, true, value, value > 0);
}

void dimmer_event(SwitchEventData* data, lv_event_code_t code) {
  SwitchBarView* view = data->view;
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t point = g_drag.press;
  if (indev) lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    cancel_live_timer();
    g_drag.data = data;
    g_drag.press = point;
    g_drag.dragging = true;
    g_drag.moved = false;
    g_drag.hold_level = true;
    g_pacer.begin_gesture();
  }
  if (g_drag.data != data || !g_drag.dragging) return;
  if (!g_drag.moved) {
    const int dx = point.x - g_drag.press.x;
    const int dy = point.y - g_drag.press.y;
    g_drag.moved = dx * dx + dy * dy >= kDragThreshold * kDragThreshold;
  }
  const uint8_t value = level_at(view, point);
  const bool changed = value != g_drag.value || code == LV_EVENT_PRESSED;
  g_drag.value = value;
  if (changed) show_local_level(data, value);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    start_hold();
    // Keep the tile state, other duplicates and an open popup in step, like
    // the popup's sync_bound_tile_from_popup.
    char payload[48];
    snprintf(payload, sizeof(payload), "{\"state\":\"%s\",\"brightness_pct\":%u}",
             value ? "on" : "off", static_cast<unsigned>(value));
    commit_level(data->entity_id, value);
    update_switch_tile_state(data->grid_type, data->index, payload);
    return;
  }
  if (changed && g_drag.moved) schedule_live();
}

void bar_event_cb(lv_event_t* e) {
  SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
  if (!data || !data->view || !data->entity_id.length()) return;
  SwitchBarView* view = data->view;
  const lv_event_code_t code = lv_event_get_code(e);
  if (!view->available) {
    if (g_drag.data == data) g_drag.dragging = false;
    return;
  }
  if (view->bar_kind == static_cast<uint8_t>(Bar::Toggle)) {
    if (code == LV_EVENT_CLICKED) toggle_switch_tile(data);
    return;
  }
  if (view->bar_kind != static_cast<uint8_t>(Bar::Dimmer)) return;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED ||
      code == LV_EVENT_PRESS_LOST) {
    dimmer_event(data, code);
  }
}

// ---------------------------------------------------------------------------
// Popup

LightPopupInit build_light_popup_init(const SwitchEventData* data) {
  LightPopupInit init;
  if (!data) return init;
  init.entity_id = data->entity_id;
  init.title = data->title;
  init.is_light = is_light_entity_id(data->entity_id);

  // Get icon from tile config (fallback to HA icon when empty)
  const Tile* tile_ptr = tile_renderer_get_tile_config(data->grid_type, data->index);
  if (tile_ptr) {
    const Tile& tile = *tile_ptr;
    init.keep_icon_white = false;
    init.has_tile_ref = true;
    init.tile_grid = static_cast<uint8_t>(data->grid_type);
    init.tile_index = data->index;
    bool icon_disabled = isMdiIconDisabled(tile.icon_name);
    init.icon_name = normalizeMdiIconName(tile.icon_name);
    if (!icon_disabled && !init.icon_name.length() && data->entity_id.length()) {
      init.icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(data->entity_id));
    }
  }

  const SwitchState state = get_switch_state(data->grid_type, data->index);
  init.available = state.available;
  init.has_state = state.has_state;
  init.has_color = state.has_color;
  init.has_brightness = state.has_brightness;
  init.has_color_temp = state.has_color_temp;
  init.has_hs = state.has_hs;
  init.hs_h = state.hs_h;
  init.hs_s = state.hs_s;
  init.color_temp_kelvin = state.color_temp_kelvin;
  init.min_color_temp_kelvin = state.min_color_temp_kelvin;
  init.max_color_temp_kelvin = state.max_color_temp_kelvin;
  if (state.has_state) {
    init.is_on = state.is_on;
  } else if (state.has_brightness) {
    init.is_on = state.brightness_pct > 0;
  } else {
    init.is_on = true;
  }

  if (init.is_light) {
    init.supports_color = state.supports_color;
    init.supports_brightness = state.supports_brightness || state.supports_color;
    init.supports_temperature = state.supports_temperature;
  } else {
    init.supports_color = false;
    init.supports_brightness = false;
    init.supports_temperature = false;
  }
  if (state.has_color) {
    init.color = state.color;
  }
  if (state.has_brightness) {
    init.brightness_pct = state.brightness_pct;
  } else if (state.has_state && !state.is_on) {
    init.brightness_pct = 0;
  } else {
    init.brightness_pct = 100;
  }
  return init;
}

// ---------------------------------------------------------------------------
// Layout

lv_obj_t* create_icon(lv_obj_t* card, const Tile& tile) {
  String icon_name = tile.icon_name;
  const bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (!icon_name.length() || FONT_MDI_ICONS == nullptr) return nullptr;
  const String glyph = getMdiChar(icon_name);
  if (!glyph.length()) return nullptr;
  lv_obj_t* icon = lv_label_create(card);
  if (!icon) return nullptr;
  set_label_style(icon, lv_color_white(), FONT_MDI_ICONS);
  lv_label_set_text(icon, glyph.c_str());
  return icon;
}

// The control bar below the Sensor header: the Climate target pill's box
// (climate_layout.h: kOuterInset to the edges, kControlRadius) one Climate
// gap below the corner disc, as high as in a one-row tile and anchored to the
// card's bottom on taller tiles.
// LVGL marks a touched object pressed before its own callbacks run. The bar
// is a resting surface that shows the pressed step only with its card
// (tile_icon_source follow_card_press); its own touch left the card
// unpressed, so the track lit up under the finger. Its pressed color is its
// resting color until the release restores the card's pressed step.
void bar_press_cb(lv_event_t* e) {
  lv_obj_t* bar = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_style_value_t rest;
  if (lv_obj_get_local_style_prop(bar, LV_STYLE_BG_COLOR, &rest, LV_PART_MAIN | LV_STATE_DEFAULT) !=
      LV_STYLE_RES_FOUND) {
    return;
  }
  const uint32_t rgb = lv_color_to_u32(rest.color) & 0xFFFFFF;
  tile_icon_disc::set_fill_colors(bar, rgb, rgb);
}

void bar_release_cb(lv_event_t* e) {
  lv_obj_t* bar = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  tile_icon_source::refresh_controls(lv_obj_get_parent(bar));
}

// The bar's box in the card: the Climate target pill's box, one Climate gap
// below the corner disc as high as in a one-row tile, anchored at the card's
// bottom; taller tiles add a third of their extra height
// (switch_layout::bar_growth).
struct BarBox {
  int width = 0;
  int height = 0;
  int top = 0;
  int base = 0;  // the one-row height
};

BarBox bar_box(const Tile& tile) {
  const int tile_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
  const int tile_h = tile_geometry::extent(tile.row, std::max(1.0f, tile.span_h), GRID_CELL_H, GRID_GAP);
  const int icon_width =
      FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
  const int top = std::max<int>(
      tile_icon_disc::inset() + tile_icon_disc::header_diameter(icon_width) + climate_layout::kGap,
      climate_layout::kContentTop);
  BarBox box;
  box.width = tile_w - climate_layout::kOuterInset * 2;
  box.base = GRID_CELL_H - climate_layout::kOuterInset - top;
  box.height = box.base + switch_layout::bar_growth(tile_h, GRID_CELL_H);
  box.top = tile_h - climate_layout::kOuterInset - box.height;
  return box;
}

lv_obj_t* create_bar(lv_obj_t* card, const Tile& tile) {
  const BarBox box = bar_box(tile);
  const int bar_h = box.height;
  const int bar_w = box.width;
  if (bar_h < 8 || bar_w < 8) return nullptr;

  lv_obj_t* bar = lv_obj_create(card);
  if (!bar) return nullptr;
  // The control fill of the card (tile_icon_source::refresh_controls), like
  // the Climate target pill. Its shared style sets color and opacity; a local
  // bg_opa would outrank it and hide the bar's track.
  tile_icon_disc::mark_surface(bar);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_shadow_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  ui_surface_style::apply_radius(bar, climate_layout::kControlRadius, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  // The bar owns its touches: dragging must not scroll the grid or press
  // the card.
  lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  // Touch area up to the card edges (left, right, bottom) and the same
  // distance up towards the disc (approved touch zones, 2026-10-01): a touch
  // just beside the bar still reaches the bar, never the card's toggle or
  // popup.
  lv_obj_set_ext_click_area(bar, climate_layout::kOuterInset);
  lv_obj_add_event_cb(bar, bar_press_cb,
                      static_cast<lv_event_code_t>(LV_EVENT_PRESSED | LV_EVENT_PREPROCESS), nullptr);
  lv_obj_add_event_cb(bar, bar_release_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(bar, bar_release_cb, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_set_size(bar, bar_w, bar_h);
  // Positions are inside the card's content box (Sensor paddings).
  lv_obj_set_pos(bar, climate_layout::kOuterInset - tile_layout::scale_480(20),
                 box.top - tile_layout::scale_480(24));
  return bar;
}

}  // namespace

namespace {

void show_view_state(SwitchBarView* view, const Tile& tile, const SwitchState& state) {
  if (!view) return;
  const bool dimmable = is_light_entity_id(tile.sensor_entity) && state.supports_brightness;
  const Bar kind = switch_layout::bar_for(static_cast<Layout>(view->layout), view->compact, dimmable);
  bool on = state.has_state ? state.is_on : (state.has_brightness && state.brightness_pct > 0);
  // A dimmable light switched on before Home Assistant reports its
  // brightness (a toggle) shows its last level instead of 100 % that jumps
  // back; never seen on, it shows On without a level until the reply.
  const uint8_t unreported_level = dimmable ? view->last_on_level : 100;
  uint8_t level = 0;
  if (state.available && on) {
    level = state.has_brightness ? std::max<uint8_t>(1, state.brightness_pct) : unreported_level;
  }
  if (!state.available) on = false;

  // A dimmer the finger holds, or released moments ago, keeps its own value
  // until Home Assistant reports it (Light popup kRemoteBlockMs); a tile
  // toggled moments ago keeps its on/off.
  uint8_t held = 0;
  bool level_held = false;
  if (state.available && held_value(view, held, level_held)) {
    if (level_held) {
      if (level != held) {
        level = held;
        on = level > 0;
      }
    } else if (on != (held > 0)) {
      on = held > 0;
      level = on ? unreported_level : 0;
    }
  }
  if (dimmable && on && level > 0) view->last_on_level = level;

  const bool redraw = view->bar_kind != static_cast<uint8_t>(kind) || view->level != level ||
                      view->on != on || view->available != state.available;
  view->bar_kind = static_cast<uint8_t>(kind);
  view->level = level;
  view->on = on;
  view->available = state.available;
  if (redraw && view->bar) lv_obj_invalidate(view->bar);
  show_state_text(view, state, dimmable && level > 0, level, on);
}

void release_timer_cb(lv_timer_t*) {
  g_release_timer = nullptr;
  SwitchEventData* data = g_drag.data;
  if (!data || g_drag.dragging) return;
  g_drag.block_until = millis();
  SwitchTileWidgets* widgets = tile_renderer_get_switch_widgets(data->grid_type);
  const Tile* tile = tile_renderer_get_tile_config(data->grid_type, data->index);
  if (!widgets || !tile || data->index >= TILES_PER_GRID || widgets[data->index].view != data->view) return;
  // Icon, circle and tile tint too: they kept the held on/off.
  const SwitchState state = get_switch_state(data->grid_type, data->index);
  switch_tile_show_state(widgets[data->index], *tile, state, switch_state_icon_color(state));
}

}  // namespace

bool switch_tile_card_color(GridType grid_type, uint8_t index, uint32_t& rgb) {
  SwitchTileWidgets* widgets = tile_renderer_get_switch_widgets(grid_type);
  if (!widgets || index >= TILES_PER_GRID) return false;
  lv_obj_t* card = widgets[index].view ? widgets[index].view->card : nullptr;
  if (!card && widgets[index].icon_label) card = lv_obj_get_parent(widgets[index].icon_label);
  if (!card) return false;
  rgb = lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF;
  return true;
}

bool switch_tile_held_on(const SwitchTileWidgets& widgets, bool& on) {
  uint8_t value = 0;
  bool level = false;
  if (!held_value(widgets.view, value, level)) return false;
  on = value > 0;
  return true;
}

void switch_tile_show_state(SwitchTileWidgets& widgets, const Tile& tile, const SwitchState& state,
                            uint32_t icon_rgb) {
  SwitchBarView* view = widgets.view;
  const lv_color_t icon_color = lv_color_hex(state.available ? icon_rgb : kIconOff);
  if (widgets.icon_label) {
    tile_icon_disc::set_icon_color(widgets.icon_label, icon_color);
  } else if (widgets.title_label && !view) {
    lv_obj_set_style_text_color(widgets.title_label, icon_color, 0);
  }
  if (!view) return;
  // The bar draws its fill and thumb in the icon's color: a new color needs
  // a redraw even when the level stays.
  if (view->bar && view->fill_rgb != icon_rgb) lv_obj_invalidate(view->bar);
  view->fill_rgb = icon_rgb;
  show_view_state(view, tile, state);
}

lv_obj_t* render_switch_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
  const Layout layout = switch_layout::from_stored(tile.sensor_decimals);
  const bool compact = tile_geometry::compact_switch(tile.type, tile.span_w, tile.span_h);
  const bool header = compact || switch_layout::horizontal(layout);

  lv_obj_t* container = lv_button_create(parent);
  ui_surface_style::apply_radius(container, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(container, 0, 0);

  // Use the configured color, else the global default tile color.
  uint32_t tile_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(container, lv_color_hex(tile_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(container, lv_color_hex(tile_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(container, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Pressed state: 10% brighter.
  uint32_t pressed_color = brighten_rgb_color(tile_color, 0x10);
  lv_obj_set_style_bg_color(container, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(container, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(container, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(container, 0, 0);
  if (header) {
    // The Sensor card paddings, so header and bar match the Sensor tile.
    lv_obj_set_style_pad_hor(container, tile_layout::scale_480(20), 0);
    lv_obj_set_style_pad_ver(container, tile_layout::scale_480(24), 0);
  }
  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(container);

  place_tile_card(container, col, row, tile);

  lv_obj_t* icon_lbl = create_icon(container, tile);
  lv_obj_t* title_lbl = nullptr;
  SwitchBarView* view = nullptr;
  const bool has_title = tile.title.length() > 0;

  if (!header) {
    // Icon button: icon and title centered on two lines, or the icon alone.
    if (icon_lbl) {
      if (has_title) {
        lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, tile_layout::scale_480(-20));
      } else {
        lv_obj_center(icon_lbl);
      }
    }
    if (has_title) {
      title_lbl = lv_label_create(container);
      if (title_lbl) {
        set_label_style(title_lbl, lv_color_white(), tile_layout::header_title_font());
        hometiles_title::tile(title_lbl, tile.title.c_str(), false);
        if (icon_lbl) {
          lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0, tile_layout::scale_480(35));
        } else {
          lv_obj_center(title_lbl);
        }
      }
    }
    // After the title exists, so the disc can lift a corner header.
    if (icon_lbl) tile_icon_disc::add_round(container, icon_lbl);
  } else {
    view = new SwitchBarView();
    view->card = container;
    view->icon = icon_lbl;
    view->layout = static_cast<uint8_t>(layout);
    view->compact = compact;
    // Before any state: the likely bar, an empty dimmer for lights.
    const bool light = is_light_entity_id(tile.sensor_entity);
    view->bar_kind = static_cast<uint8_t>(switch_layout::bar_for(layout, compact, light));

    // One row high: the Sensor corner disc; title and state left-aligned
    // beside it like the half-height tiles (compact_sensor_layout::apply_content: two insets
    // from the disc, the block centered on it, no gap between the lines),
    // the state at the half-height Sensor value size (title size by default,
    // 24 or 28 when chosen). Explicit positions, so the corner disc
    // (add_round) does not move them.
    if (icon_lbl) {
      lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
    }
    const int card_w = tile_geometry::extent(tile.col, std::max(1.0f, tile.span_w), GRID_CELL_W, GRID_GAP);
    const int inset = tile_icon_disc::inset();
    const int icon_width =
        FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0) : 0;
    const int disc = tile_icon_disc::header_diameter(icon_width);
    const int text_x = inset + disc + 2 * inset;
    const int text_w = std::max(1, card_w - text_x - 2 * inset);
    const int title_h = lv_font_get_line_height(compact_sensor_layout::title_font());
    const bool tall = switch_layout::sensor_look(layout, tile.span_h);
    const lv_font_t* state_font = tall ? tile_layout::value_font_for_choice(tile.sensor_value_font, FONT_VALUE)
                                       : compact_sensor_layout::value_font(tile.sensor_value_font);
    view->state_font = state_font;
    const int state_h = lv_font_get_line_height(state_font);
    const int block = (has_title ? title_h : 0) + state_h;
    const int text_y = inset + disc / 2 - block / 2;
    // Label positions are inside the card's content box (Sensor paddings).
    const int pad_x = tile_layout::scale_480(20);
    const int pad_y = tile_layout::scale_480(24);
    if (tall) {
      // From 1.5 rows: the Sensor tile's title, top right (add_round moves
      // it with the corner disc), and its value size, the state centered
      // between the disc and the bar.
      if (has_title) {
        title_lbl = lv_label_create(container);
        if (title_lbl) {
          set_label_style(title_lbl, lv_color_white(), tile_layout::header_title_font());
          lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
          lv_obj_set_width(title_lbl, LV_PCT(70));
          lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_RIGHT, 0);
          hometiles_title::tile(title_lbl, tile.title.c_str(), true);
          lv_obj_align(title_lbl, LV_ALIGN_TOP_RIGHT, tile_layout::scale_480(4), tile_layout::scale_480(4));
        }
      }
      const int content_w = std::max(1, card_w - 2 * pad_x);
      view->state_center = static_cast<int16_t>((inset + disc + bar_box(tile).top) / 2 - pad_y);
      view->state_label = lv_label_create(container);
      if (view->state_label) {
        set_label_style(view->state_label, lv_color_white(), state_font);
        lv_label_set_long_mode(view->state_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(view->state_label, content_w);
        lv_obj_set_style_text_align(view->state_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(view->state_label, "--");
        lv_obj_set_pos(view->state_label, 0, view->state_center - state_h / 2);
        lv_obj_clear_flag(view->state_label, LV_OBJ_FLAG_CLICKABLE);
      }
      view->state_width = static_cast<int16_t>(content_w);
    } else if (has_title) {
      title_lbl = lv_label_create(container);
      if (title_lbl) {
        set_label_style(title_lbl, lv_color_white(), compact_sensor_layout::title_font());
        lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title_lbl, text_w);
        hometiles_title::tile(title_lbl, tile.title.c_str(), true);
        // One title line: the state line takes the second.
        if (auto* title_state = hometiles_title::state_for(title_lbl)) title_state->single_line = true;
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(title_lbl, text_x - pad_x, text_y - pad_y);
      }
    }
    if (!tall) {
      view->state_label = lv_label_create(container);
      if (view->state_label) {
        set_label_style(view->state_label, lv_color_white(), state_font);
        lv_label_set_long_mode(view->state_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(view->state_label, text_w);
        lv_obj_set_style_text_align(view->state_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(view->state_label, "--");
        lv_obj_set_pos(view->state_label, text_x - pad_x, text_y + (has_title ? title_h : 0) - pad_y);
        lv_obj_clear_flag(view->state_label, LV_OBJ_FLAG_CLICKABLE);
      }
      view->state_width = static_cast<int16_t>(text_w);
    }

    if (compact) {
      compact_sensor_layout::apply(container, icon_lbl, title_lbl, view->state_label, tile);
    } else {
      if (icon_lbl) tile_icon_disc::add_round(container, icon_lbl);
      view->bar = create_bar(container, tile);
      view->bar_base = static_cast<int16_t>(bar_box(tile).base);
    }
  }

  SwitchTileWidgets* target = tile_renderer_get_switch_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].icon_label = icon_lbl;
    target[index].title_label = title_lbl;
    target[index].view = view;
  }

  if (view && view->bar) tile_icon_source::refresh_controls(container);

  if (tile.sensor_entity.length()) {
    String initial = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    if (initial.length()) {
      update_switch_tile_state(grid_type, index, initial.c_str());
    }
  }

  SwitchEventData* event_data = new SwitchEventData{tile.sensor_entity, tile.title, grid_type, index, view};
  if (view && view->bar) {
    lv_obj_add_event_cb(view->bar, bar_draw_cb, LV_EVENT_DRAW_MAIN, view);
    // Only these codes: the bar's own DELETE comes after the card freed the
    // event data.
    for (const lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                       LV_EVENT_PRESS_LOST, LV_EVENT_CLICKED}) {
      lv_obj_add_event_cb(view->bar, bar_event_cb, code, event_data);
    }
  }

  if (tile.sensor_entity.length()) {
    const bool allow_popup = grid_type != GridType::SCREENSAVER;
    const bool popup_on_short =
        allow_popup &&
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS;
    const lv_event_code_t popup_event =
        popup_on_short ? LV_EVENT_SHORT_CLICKED : LV_EVENT_LONG_PRESSED;
    const lv_event_code_t toggle_event =
        popup_on_short ? LV_EVENT_LONG_PRESSED : LV_EVENT_SHORT_CLICKED;

    lv_obj_add_event_cb(
        container,
        [](lv_event_t* e) {
          lv_event_code_t code = lv_event_get_code(e);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
          toggle_switch_tile(data);
        },
        toggle_event,
        event_data);

    if (allow_popup) {
      lv_obj_add_event_cb(
          container,
          [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
            SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
            if (!data) return;
            LightPopupInit init = build_light_popup_init(data);
            // For now the popup keeps the global tile color and does not follow
            // the tile: following a light color dragged in the popup restyled it
            // on every step (tile_icon_source::forget_popup_source). Icon and
            // circle still match the tile.
            init.bg_color = tileDefaultBgColor();
            tile_icon_source::forget_popup_source(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
            finish_press_before_popup(e);
            show_light_popup(init);
          },
          popup_event,
          event_data);
    }
  }

  lv_obj_add_event_cb(
      container,
      [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
        SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
        if (!data) return;
        if (g_drag.data == data) {
          cancel_live_timer();
          if (g_release_timer) {
            lv_timer_delete(g_release_timer);
            g_release_timer = nullptr;
          }
          g_drag = DimmerDrag{};
        }
        delete data->view;
        delete data;
      },
      LV_EVENT_DELETE,
      event_data);

  return container;
}
