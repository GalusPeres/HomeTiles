#include "src/ui/shared/ui_surface_style.h"
#include "src/types/device/device_tile.h"

#include <algorithm>
#include <cstring>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_bar_icons.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/level_bar.h"
#include "src/tiles/runtime/tile_header.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/device/device_control.h"
#include "src/types/device/device_visual.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/shared/command_pacer.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_pulse.h"

namespace {

using device_detail::Detail;

enum class BarKind : uint8_t { None, Toggle, Buttons, Slots, Segments, Dimmer };

struct View {
  View* next = nullptr;
  TileType type = TILE_EMPTY;
  GridType grid = GridType::TAB0;
  uint8_t index = 0;
  String entity;
  String title;
  // The configured icon; "" shows the state icon.
  String icon_name;
  bool icon_visible = true;
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* state_label = nullptr;
  lv_obj_t* bar = nullptr;
  const char* shown_icon = nullptr;
  const lv_font_t* state_font = nullptr;
  int16_t state_width = 0;
  int16_t state_center = -1;
  bool compact = false;
  int16_t bar_base = 0;
  int16_t bar_width = 0;
  // What the bar draws.
  bool available = false;
  BarKind kind = BarKind::None;
  bool on = false;             // Toggle: thumb on the right
  bool thumb_off_look = false;  // Toggle: the grey off thumb (a Fan that is off)
  const char* thumb_icon = "";
  uint8_t level = 0;  // Dimmer: 1..100; Segments: filled count
  uint8_t count = 0;  // Buttons, Slots, Segments
  uint8_t speed_count = 100;  // Fan: fixed speeds (100: a free percentage)
  const char* button_icons[2] = {"", ""};
  int8_t slots[6] = {};  // Slots: -1 Disarm, else the alarm mode index
  int8_t active = -1;    // Slots: the lit slot
  // The part under the finger, one control step up while pressed (Buttons,
  // Slots: its index; the lock's switch: 0); -1 when none.
  int8_t pressed_part = -1;
  bool bar_pulse = false;
  lv_opa_t pulse_opa = LV_OPA_COVER;
  uint32_t thumb_base = 0;
  uint32_t thumb_off = 0;
};

View* g_views = nullptr;

// ---------------------------------------------------------------------------
// Pulse: Home Assistant's state-control-styles (opacity 1 -> 0 -> 1 in 1 s)
// for a running command or a device that needs attention, in step with the
// popup (ui_pulse.h).

void icon_pulse_exec(void* obj, int32_t) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), ui_pulse::opa_now(), 0);
}

void set_icon_pulse(lv_obj_t* icon, bool on) {
  if (!icon) return;
  // A refresh keeps a running pulse instead of restarting it.
  if (on && lv_anim_get(icon, icon_pulse_exec)) return;
  if (!on && !lv_anim_get(icon, icon_pulse_exec)) return;
  lv_anim_delete(icon, icon_pulse_exec);
  lv_obj_set_style_opa(icon, LV_OPA_COVER, 0);
  if (on) ui_pulse::start(icon, icon_pulse_exec);
}

// The bar's thumb symbol pulses with the icon; only the thumb redraws.
lv_area_t thumb_area(const View* view) {
  lv_area_t area;
  lv_obj_get_coords(view->bar, &area);
  const int32_t half = lv_area_get_width(&area) / 2;
  if (view->on) {
    area.x1 = area.x2 - half + 1;
  } else {
    area.x2 = area.x1 + half - 1;
  }
  return area;
}

void bar_pulse_exec(void* var, int32_t) {
  View* view = static_cast<View*>(var);
  view->pulse_opa = ui_pulse::opa_now();
  if (!view->bar) return;
  lv_area_t area = thumb_area(view);
  lv_obj_invalidate_area(view->bar, &area);
}

void set_bar_pulse(View* view, bool on) {
  if (on == view->bar_pulse) return;
  view->bar_pulse = on;
  lv_anim_delete(view, bar_pulse_exec);
  view->pulse_opa = LV_OPA_COVER;
  if (on && view->bar) ui_pulse::start(view, bar_pulse_exec);
}

// ---------------------------------------------------------------------------
// Bar drawing: one object, its thumb, buttons, slots, segments, fill and
// symbols drawn in one DRAW_MAIN pass like the Switch bar (no child objects,
// no clip layer). Symbols use the small bar icon font (mdi_bar_icons.h) at
// about 45 % of the one-row bar height.

// The UTF-8 text of a bar symbol, kept for the draw tasks.
const char* bar_glyph(const char* name) {
  struct Glyph {
    const char* name;
    char utf8[8];
  };
  static Glyph cache[12] = {};
  for (Glyph& glyph : cache) {
    if (glyph.name && std::strcmp(glyph.name, name) == 0) return glyph.utf8;
    if (!glyph.name) {
      const String text = getMdiChar(name);
      glyph.name = name;
      strlcpy(glyph.utf8, text.c_str(), sizeof(glyph.utf8));
      return glyph.utf8;
    }
  }
  return "";
}

void draw_glyph(lv_layer_t* layer, const lv_area_t& area, const char* name, lv_color_t color, lv_opa_t opa,
                int16_t base) {
  if (!name || !*name) return;
  const lv_font_t* font = mdi_bar_icons::for_bar(base > 0 ? base : lv_area_get_height(&area));
  const char* text = bar_glyph(name);
  if (!*text) return;
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.font = font;
  dsc.color = color;
  dsc.opa = opa;
  dsc.text = text;
  dsc.align = LV_TEXT_ALIGN_CENTER;
  const int32_t line = lv_font_get_line_height(font);
  const int32_t top = area.y1 + (lv_area_get_height(&area) - line) / 2;
  const lv_area_t box = {area.x1, top, area.x2, top + line - 1};
  lv_draw_label(layer, &dsc, &box);
}

uint32_t accent_rgb(const View* view) {
  if (view->icon) return tile_icon_disc::icon_color(view->icon);
  return device_visual::kGrey;
}

uint32_t button_rgb(uint32_t base) { return tone_color::lifted(base, base, false, 0.04f); }
// A pressed button: one more control step, like a pressed PIN key.
uint32_t pressed_button_rgb(uint32_t base) { return tone_color::lifted(base, base, false, 0.08f); }

void bar_draw_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!view || !view->bar || !layer || !view->available || view->kind == BarKind::None) return;
  lv_area_t area;
  lv_obj_get_coords(view->bar, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  if (width < 4 || height < 4) return;
  const int32_t radius = level_bar::radius_of(view->bar, height);
  const lv_color_t card = lv_obj_get_style_bg_color(view->card, LV_PART_MAIN);
  const uint32_t base = lv_color_to_u32(lv_obj_get_style_bg_color(view->bar, LV_PART_MAIN)) & 0xFFFFFF;
  const lv_color_t accent = lv_color_hex(accent_rgb(view));
  const int32_t gap = climate_layout::kOuterInset;

  switch (view->kind) {
    case BarKind::Toggle: {
      // Flush half-width thumb like the Light popup switch.
      const int32_t half = width / 2;
      const lv_area_t thumb = view->on ? lv_area_t{area.x2 - half + 1, area.y1, area.x2, area.y2}
                                       : lv_area_t{area.x1, area.y1, area.x1 + half - 1, area.y2};
      lv_color_t thumb_color = accent;
      lv_color_t symbol = card;
      // A pressed lock switch: the rail one control step up.
      if (view->pressed_part == 0) level_bar::draw_rect(layer, area, lv_color_hex(button_rgb(base)), radius);
      if (view->thumb_off_look) {
        if (base != view->thumb_base || !view->thumb_off) {
          view->thumb_base = base;
          view->thumb_off = tone_color::switch_thumb_off(base);
        }
        thumb_color = lv_color_hex(view->thumb_off);
        symbol = lv_color_hex(tone_color::kOffIcon);
      }
      level_bar::draw_rect(layer, thumb, thumb_color, radius);
      draw_glyph(layer, thumb, view->thumb_icon, symbol, view->bar_pulse ? view->pulse_opa : LV_OPA_COVER,
                 view->bar_base);
      return;
    }
    case BarKind::Buttons: {
      // Home Assistant's ha-control-button-group: buttons side by side, one
      // control step up.
      const int32_t n = std::max<int32_t>(1, view->count);
      const int32_t step = (width - gap * (n - 1)) / n;
      for (int32_t i = 0; i < n; ++i) {
        const lv_area_t button = {area.x1 + i * (step + gap), area.y1, area.x1 + i * (step + gap) + step - 1,
                                  area.y2};
        level_bar::draw_rect(layer, button,
                             lv_color_hex(i == view->pressed_part ? pressed_button_rgb(base) : button_rgb(base)),
                             radius);
        draw_glyph(layer, button, view->button_icons[i < 2 ? i : 1], lv_color_white(), LV_OPA_COVER,
                   view->bar_base);
      }
      return;
    }
    case BarKind::Slots: {
      // Home Assistant's alarm mode bar: the current mode lit, the others as
      // symbols on the bar.
      const int32_t n = std::max<int32_t>(1, view->count);
      for (int32_t i = 0; i < n; ++i) {
        const lv_area_t slot = {area.x1 + i * width / n, area.y1, area.x1 + (i + 1) * width / n - 1, area.y2};
        const bool lit = i == view->active;
        if (lit) {
          level_bar::draw_rect(layer, slot, accent, radius);
        } else if (i == view->pressed_part) {
          level_bar::draw_rect(layer, slot, lv_color_hex(button_rgb(base)), radius);
        }
        const int8_t mode = view->slots[i];
        const char* icon = mode < 0 ? "shield-off" : device_control::alarm_mode(static_cast<size_t>(mode)).icon;
        draw_glyph(layer, slot, icon, lit ? card : lv_color_white(), LV_OPA_COVER, view->bar_base);
      }
      return;
    }
    case BarKind::Segments: {
      const int32_t n = std::max<int32_t>(1, view->count);
      const int32_t step = (width - gap * (n - 1)) / n;
      for (int32_t i = 0; i < n; ++i) {
        const lv_area_t segment = {area.x1 + i * (step + gap), area.y1, area.x1 + i * (step + gap) + step - 1,
                                   area.y2};
        level_bar::draw_rect(layer, segment, i < view->level ? accent : lv_color_hex(button_rgb(base)), radius);
      }
      return;
    }
    case BarKind::Dimmer:
      level_bar::draw_fill(layer, view->bar, view->level, view->bar_base, accent, card);
      return;
    case BarKind::None:
      return;
  }
}

// ---------------------------------------------------------------------------
// Fan dimmer and held values: the Switch dimmer's logic (level_bar.h for the
// touch mapping; command_pacer.h for Home Assistant's slider timing): the
// press jumps to the finger, pressing follows, live commands at most every
// pacer interval, the final value paced on release, and the tile holds its
// own value for kRemoteBlockMs against echoes of earlier commands.

constexpr uint32_t kRemoteBlockMs = 3000;
constexpr int kDragThreshold = tile_layout::scale(10);

struct Hold {
  View* view = nullptr;
  lv_point_t press = {0, 0};
  bool dragging = false;
  bool moved = false;
  // Fan percentage (0 = off) the tile shows while held.
  uint8_t value = 0;
  uint32_t until = 0;
};
Hold g_hold;
command_pacer::Pacer g_pacer;
lv_timer_t* g_live_timer = nullptr;
lv_timer_t* g_final_timer = nullptr;
lv_timer_t* g_release_timer = nullptr;
String g_final_entity;
uint8_t g_final_value = 0;

void show(View* view);
void show_drag_level(View* view, uint8_t value);

bool held_value(const View* view, uint8_t& value) {
  if (!g_hold.view || g_hold.view != view) return false;
  if (!g_hold.dragging && static_cast<int32_t>(g_hold.until - millis()) <= 0) return false;
  value = g_hold.value;
  return true;
}

void release_timer_cb(lv_timer_t*) {
  g_release_timer = nullptr;
  View* view = g_hold.view;
  if (!view || g_hold.dragging) return;
  g_hold.until = millis();
  show(view);
}

void start_hold(View* view, uint8_t value) {
  g_hold.view = view;
  g_hold.value = value;
  g_hold.dragging = false;
  g_hold.until = millis() + kRemoteBlockMs;
  if (g_release_timer) lv_timer_delete(g_release_timer);
  g_release_timer = lv_timer_create(release_timer_cb, kRemoteBlockMs, nullptr);
  if (g_release_timer) lv_timer_set_repeat_count(g_release_timer, 1);
}

void send_level(const String& entity, uint8_t value) {
  // Percentage 0 turns the fan off (Home Assistant's set_percentage).
  device_control::fan_percentage(entity, value);
  g_pacer.sent(millis(), value);
}

void cancel_live_timer() {
  if (!g_live_timer) return;
  lv_timer_delete(g_live_timer);
  g_live_timer = nullptr;
}

void live_timer_cb(lv_timer_t*) {
  g_live_timer = nullptr;
  if (!g_hold.dragging || !g_hold.view || !g_hold.moved) return;
  send_level(g_hold.view->entity, g_hold.value);
}

void final_timer_cb(lv_timer_t*) {
  g_final_timer = nullptr;
  send_level(g_final_entity, g_final_value);
}

void schedule_live() {
  if (!g_hold.view || g_live_timer) return;
  const uint32_t wait = g_pacer.wait(millis());
  if (wait == 0) {
    send_level(g_hold.view->entity, g_hold.value);
    return;
  }
  g_live_timer = lv_timer_create(live_timer_cb, wait, nullptr);
  if (g_live_timer) lv_timer_set_repeat_count(g_live_timer, 1);
}

void commit_level(const String& entity, uint8_t value) {
  cancel_live_timer();
  const bool repeat = g_pacer.final_redundant(value);
  g_pacer.end_gesture();
  if (repeat) return;
  if (g_final_timer && !g_final_entity.equalsIgnoreCase(entity)) {
    lv_timer_delete(g_final_timer);
    g_final_timer = nullptr;
    send_level(g_final_entity, g_final_value);
  }
  const uint32_t wait = g_pacer.wait(millis());
  if (wait == 0) {
    if (g_final_timer) {
      lv_timer_delete(g_final_timer);
      g_final_timer = nullptr;
    }
    send_level(entity, value);
    return;
  }
  g_final_entity = entity;
  g_final_value = value;
  if (g_final_timer) return;
  g_final_timer = lv_timer_create(final_timer_cb, wait, nullptr);
  if (g_final_timer) {
    lv_timer_set_repeat_count(g_final_timer, 1);
  } else {
    send_level(entity, value);
  }
}

void dimmer_event(View* view, lv_event_code_t code) {
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t point = g_hold.press;
  if (indev) lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    cancel_live_timer();
    g_hold.view = view;
    g_hold.press = point;
    g_hold.dragging = true;
    g_hold.moved = false;
    g_pacer.begin_gesture();
  }
  if (g_hold.view != view || !g_hold.dragging) return;
  if (!g_hold.moved) {
    const int dx = point.x - g_hold.press.x;
    const int dy = point.y - g_hold.press.y;
    g_hold.moved = dx * dx + dy * dy >= kDragThreshold * kDragThreshold;
  }
  const uint8_t value = level_bar::value_at(view->bar, view->bar_base, point);
  const bool changed = value != g_hold.value || code == LV_EVENT_PRESSED;
  g_hold.value = value;
  if (changed) show_drag_level(view, value);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    start_hold(view, value);
    commit_level(view->entity, value);
    return;
  }
  if (changed && g_hold.moved) schedule_live();
}

// ---------------------------------------------------------------------------
// State

DevicePopupTarget popup_target(const View* view) {
  DevicePopupTarget target;
  target.type = view->type;
  target.entity = view->entity;
  target.title = view->title;
  target.icon_name = view->icon_name;
  target.icon_visible = view->icon_visible;
  target.source = view->card;
  return target;
}

// The alarm modes the bar shows: Disarm first, then the supported modes in
// reverse (Home Assistant's tile: Disarm left .. Home right). When not all
// fit (1x1), Disarm and the first modes stay (Home, Away, ...).
void alarm_slots(View* view, const Detail& d) {
  const int icon_w = FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, tile_icon_disc::kMdiReferenceGlyph, 0)
                                    : view->bar_base;
  const int fit = std::max(2, std::min(6, view->bar_width / std::max(1, icon_w)));
  int8_t modes[device_control::kAlarmModeCount];
  int count = 0;
  for (size_t i = 0; i < device_control::kAlarmModeCount && count < fit - 1; ++i) {
    if (d.features & device_control::alarm_mode(i).feature) modes[count++] = static_cast<int8_t>(i);
  }
  view->slots[0] = -1;
  for (int i = 0; i < count; ++i) view->slots[1 + i] = modes[count - 1 - i];
  view->count = static_cast<uint8_t>(1 + count);
}

void bar_model(View* view, const Detail& d, const device_visual::Visual& v) {
  view->available = d.valid && d.available;
  view->thumb_off_look = false;
  bool pulse = false;
  if (view->type == TILE_LOCK) {
    if (device_visual::lock_buttons(d)) {
      view->kind = BarKind::Buttons;
      view->count = 2;
      view->button_icons[0] = "lock-open-variant";
      view->button_icons[1] = "lock";
    } else {
      // The thumb shows only what the lock reports; a sent command waits
      // with the icon pulsing (show()) and moves nothing, so a code the lock
      // itself rejected no longer looks as if it had worked (user 02.10.).
      view->kind = BarKind::Toggle;
      view->on = device_visual::lock_on(d);
      view->thumb_icon = v.icon;
      pulse = device_visual::lock_moving(d);
    }
  } else if (view->type == TILE_ALARM) {
    if (device_visual::alarm_disarm_only(d)) {
      view->kind = BarKind::Buttons;
      view->count = 1;
      view->button_icons[0] = "shield-off";
    } else {
      view->kind = BarKind::Slots;
      alarm_slots(view, d);
      // Only the reported mode lights; a sent one waits with the icon
      // pulsing (show()), so a rejected code looks like nothing happened.
      const char* shown = d.state;
      view->active = -1;
      for (uint8_t i = 0; i < view->count; ++i) {
        const int8_t mode = view->slots[i];
        const char* state = mode < 0 ? "disarmed" : device_control::alarm_mode(static_cast<size_t>(mode)).state;
        if (std::strcmp(state, shown) == 0) view->active = static_cast<int8_t>(i);
      }
    }
  } else {
    uint8_t held = 0;
    const bool holding = held_value(view, held);
    const bool on = holding ? held > 0 : device_visual::fan_on(d);
    const int percentage = holding ? held : (on ? (d.has_percentage ? d.percentage : 100) : 0);
    view->speed_count = static_cast<uint8_t>(device_detail::fan_speed_count(d));
    if (!device_visual::fan_has_speed(d)) {
      view->kind = BarKind::Toggle;
      view->on = on;
      view->thumb_off_look = !on;
      view->thumb_icon = on ? "fan" : "fan-off";
    } else if (device_visual::fan_segmented(d)) {
      view->kind = BarKind::Segments;
      view->count = static_cast<uint8_t>(device_detail::fan_speed_count(d));
      view->level = static_cast<uint8_t>(device_detail::fan_speed_of(d, percentage));
    } else {
      view->kind = BarKind::Dimmer;
      view->level = static_cast<uint8_t>(on ? std::max(1, percentage) : 0);
    }
  }
  if (view->bar) {
    set_bar_pulse(view, pulse && view->available);
    lv_obj_invalidate(view->bar);
  }
}

void show(View* view) {
  if (!view) return;
  const Detail d = device_control::detail(view->entity);
  device_visual::Visual v = device_visual::visual(view->type, d);
  const char* target = device_control::pending_target(view->entity);
  // A held Fan value shows its level at once.
  uint8_t held = 0;
  if (view->type == TILE_FAN && d.valid && d.available && held_value(view, held)) {
    if (held == 0) {
      v.label = i18n::strings(device_visual::language()).light_off;
      v.icon = "fan-off";
      v.color = device_visual::kGrey;
    } else {
      v.label = device_visual::fan_level_text(device_detail::fan_speed_count(d), held);
      v.icon = "fan";
      v.color = device_visual::kCyan;
    }
  }
  if (view->icon) {
    if (!view->icon_name.length() && (!view->shown_icon || std::strcmp(view->shown_icon, v.icon) != 0)) {
      view->shown_icon = v.icon;
      lv_label_set_text(view->icon, getMdiChar(v.icon).c_str());
    }
    tile_icon_disc::set_icon_color(view->icon, lv_color_hex(v.color));
    set_icon_pulse(view->icon, v.waiting || (*target && std::strcmp(target, d.state) != 0));
  }
  tile_header::set_state(view->state_label, v.label.c_str(), view->compact ? nullptr : view->state_font,
                         view->state_width, view->state_center);
  if (!view->compact) bar_model(view, d, v);
}

// A dimmer drag step: the fill and the state line follow the finger and only
// the changed columns redraw (level_bar::invalidate_change). Crossing between
// off and on shows the whole tile once (icon, circle, tint).
void show_drag_level(View* view, uint8_t value) {
  if (view->kind != BarKind::Dimmer || (value > 0) != (view->level > 0)) {
    show(view);
    return;
  }
  const uint8_t old_level = view->level;
  view->level = value;
  level_bar::invalidate_change(view->bar, view->bar_base, old_level, value);
  tile_header::set_state(view->state_label, device_visual::fan_level_text(view->speed_count, value).c_str(),
                         view->compact ? nullptr : view->state_font, view->state_width, view->state_center);
}

// ---------------------------------------------------------------------------
// Touch

int32_t bar_x(lv_obj_t* bar, int32_t& width) {
  lv_area_t area;
  lv_obj_get_coords(bar, &area);
  width = lv_area_get_width(&area);
  lv_point_t point = {0, 0};
  if (lv_indev_t* indev = lv_indev_get_act()) lv_indev_get_point(indev, &point);
  return std::max<int32_t>(0, std::min<int32_t>(width - 1, point.x - area.x1));
}

// The part a press on the bar lands on, -1 when the press does nothing there
// (the lit alarm mode, a mode or the lock while a sent command waits) or the
// bar shows the touch itself (the Fan's segments and switch move at once).
int8_t pressed_part_at(const View* view) {
  if (view->kind != BarKind::Buttons && view->kind != BarKind::Slots &&
      !(view->kind == BarKind::Toggle && view->type == TILE_LOCK)) {
    return -1;
  }
  const char* target = device_control::pending_target(view->entity);
  const bool waiting = *target && std::strcmp(target, device_control::detail(view->entity).state) != 0;
  if (view->kind == BarKind::Toggle) return waiting ? -1 : 0;
  int32_t width = 1;
  const int32_t x = bar_x(view->bar, width);
  const int n = std::max<int>(1, view->count);
  const int part = std::min(n - 1, static_cast<int>(x * n / std::max<int32_t>(1, width)));
  if (view->kind == BarKind::Slots && (part == view->active || (waiting && view->slots[part] >= 0))) return -1;
  return static_cast<int8_t>(part);
}

void bar_event_cb(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view || !view->bar) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (!view->available) {
    if (g_hold.view == view) g_hold.dragging = false;
    return;
  }
  if (view->kind == BarKind::Dimmer) {
    if (code != LV_EVENT_CLICKED) dimmer_event(view, code);
    return;
  }
  // The part under the finger shows the press like a PIN key (user 02.10.:
  // the bars showed no press); only that part redraws.
  if (code != LV_EVENT_CLICKED) {
    const int8_t part = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ? pressed_part_at(view) : -1;
    if (part != view->pressed_part) {
      view->pressed_part = part;
      lv_obj_invalidate(view->bar);
    }
    return;
  }
  int32_t width = 1;
  const int32_t x = bar_x(view->bar, width);
  const Detail d = device_control::detail(view->entity);
  if (view->type == TILE_FAN) {
    if (view->kind == BarKind::Toggle) {
      const bool on = device_visual::fan_on(d);
      const uint32_t needed = on ? device_detail::kFanTurnOff : device_detail::kFanTurnOn;
      if (!(d.features & needed)) return;
      start_hold(view, on ? 0 : 100);
      device_control::fan_action(view->entity, on ? "turn_off" : "turn_on");
    } else if (view->kind == BarKind::Segments) {
      const int count = std::max<int>(1, view->count);
      const int speed = std::min<int>(count, x * count / std::max<int32_t>(1, width) + 1);
      const uint8_t percentage = static_cast<uint8_t>(device_detail::fan_percentage_of(d, speed));
      start_hold(view, percentage);
      device_control::fan_percentage(view->entity, percentage);
    }
    show(view);
    return;
  }
  const char* action = nullptr;
  if (view->type == TILE_LOCK) {
    if (view->kind == BarKind::Buttons) {
      action = x < width / 2 ? "unlock" : "lock";
    } else {
      // While a sent command waits the switch does nothing, like the
      // popup's; then it switches from the side the lock reports.
      const char* target = device_control::pending_target(view->entity);
      if (*target && std::strcmp(target, d.state) != 0) return;
      action = view->on ? "unlock" : "lock";
    }
  } else if (view->type == TILE_ALARM) {
    if (view->kind == BarKind::Buttons) {
      action = "disarm";
    } else {
      const int n = std::max<int>(1, view->count);
      const int slot = std::min(n - 1, static_cast<int>(x * n / std::max<int32_t>(1, width)));
      // Home Assistant ignores a tap on the current mode; while a sent
      // command waits only Disarm goes, like in the popup.
      if (slot == view->active) return;
      const int8_t mode = view->slots[slot];
      const char* target = device_control::pending_target(view->entity);
      if (mode >= 0 && *target && std::strcmp(target, d.state) != 0) return;
      action = mode < 0 ? "disarm" : device_control::alarm_mode(static_cast<size_t>(mode)).action;
    }
  }
  if (action) device_request(popup_target(view), action, true);
}

void unregister(View* view) {
  for (View** link = &g_views; *link; link = &(*link)->next) {
    if (*link == view) {
      *link = view->next;
      return;
    }
  }
}

// The card's other gesture (the one that does not open the popup) switches,
// like the Switch tile (user 02.10.): a Fan on and off, a Lock locked and
// unlocked (asking for the code when Home Assistant needs one). A Lock
// without a toggle position (unknown, jammed) opens the popup instead. The
// Alarm panel has no obvious other action, so it has none.
void on_card_toggle(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (!view || !view->entity.length() || (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED)) return;
  const Detail d = device_control::detail(view->entity);
  if (!d.valid || !d.available) return;
  if (view->type == TILE_FAN) {
    uint8_t held = 0;
    const bool on = held_value(view, held) ? held > 0 : device_visual::fan_on(d);
    if (!(d.features & (on ? device_detail::kFanTurnOff : device_detail::kFanTurnOn))) return;
    start_hold(view, on ? 0 : (d.has_percentage && d.percentage ? d.percentage : 100));
    device_control::fan_action(view->entity, on ? "turn_off" : "turn_on");
    show(view);
    return;
  }
  if (view->type != TILE_LOCK) return;
  if (device_visual::lock_buttons(d)) {
    finish_press_before_popup(e);
    show_device_popup(popup_target(view));
    return;
  }
  // While a sent command waits the gesture does nothing, like the popup's
  // switch; then it switches from the side the lock reports.
  const char* target = device_control::pending_target(view->entity);
  if (*target && std::strcmp(target, d.state) != 0) return;
  device_request(popup_target(view), device_visual::lock_on(d) ? "unlock" : "lock", true);
}

void on_card_event(lv_event_t* e) {
  View* view = static_cast<View*>(lv_event_get_user_data(e));
  if (!view) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DELETE) {
    if (g_hold.view == view) {
      cancel_live_timer();
      if (g_release_timer) {
        lv_timer_delete(g_release_timer);
        g_release_timer = nullptr;
      }
      g_hold = Hold{};
    }
    lv_anim_delete(view, bar_pulse_exec);
    if (view->icon) lv_anim_delete(view->icon, icon_pulse_exec);
    unregister(view);
    delete view;
    return;
  }
  if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
  if (!view->entity.length()) return;
  DevicePopupTarget target = popup_target(view);
  finish_press_before_popup(e);
  show_device_popup(target);
}

}  // namespace

void device_tiles_refresh(const String& entity) {
  const bool all = entity == "*";
  for (View* view = g_views; view; view = view->next) {
    if (all || view->entity == entity) show(view);
  }
}

bool device_tile_alive(lv_obj_t* card) {
  if (!card) return false;
  for (View* view = g_views; view; view = view->next) {
    if (view->card == card) return true;
  }
  return false;
}

lv_obj_t* render_device_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                             GridType grid_type) {
  lv_obj_t* card = lv_button_create(parent);
  const uint32_t color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  const uint32_t pressed_color = brighten_rgb_color(color, 0x10);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  // The Sensor card paddings, so header and bar match the Sensor tile.
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  View* view = new View();
  view->type = static_cast<TileType>(tile.type);
  view->grid = grid_type;
  view->index = index;
  view->entity = tile.sensor_entity;
  view->card = card;
  view->title = tile.title.length() ? tile.title : haBridgeConfig.findSensorName(tile.sensor_entity);
  if (!view->title.length()) view->title = tile.sensor_entity;
  view->icon_visible = !isMdiIconDisabled(tile.icon_name);
  view->icon_name = view->icon_visible ? normalizeMdiIconName(tile.icon_name) : String();
  view->compact = tile_geometry::compact_device_control(tile.type, tile.span_w, tile.span_h);

  if (view->icon_visible && FONT_MDI_ICONS) {
    view->icon = lv_label_create(card);
    set_label_style(view->icon, lv_color_hex(device_visual::kGrey), FONT_MDI_ICONS);
    const String glyph = getMdiChar(view->icon_name.length() ? view->icon_name : String(
        view->type == TILE_LOCK ? "lock" : view->type == TILE_ALARM ? "shield" : "fan-off"));
    lv_label_set_text(view->icon, glyph.c_str());
    lv_obj_align(view->icon, LV_ALIGN_TOP_LEFT, tile_layout::scale_480(-8), tile_layout::scale_480(-8));
  }

  const level_bar::BarBox box = level_bar::box(tile);
  const bool tall = !view->compact && tile.span_h > 1.0f;
  const tile_header::Header header = tile_header::create(card, tile, tall, box.top);
  view->state_label = header.state;
  view->state_font = header.state_font;
  view->state_width = header.state_width;
  view->state_center = header.state_center;
  if (view->compact) {
    compact_sensor_layout::apply(card, view->icon, header.title, header.state, tile);
  } else {
    if (view->icon) tile_icon_disc::add_round(card, view->icon);
    view->bar = level_bar::create(card, tile);
    view->bar_base = static_cast<int16_t>(box.base);
    view->bar_width = static_cast<int16_t>(box.width);
    if (view->bar) {
      lv_obj_add_event_cb(view->bar, bar_draw_cb, LV_EVENT_DRAW_MAIN, view);
      if (view->entity.length() && grid_type != GridType::SCREENSAVER) {
        // Only these codes: the bar's own DELETE comes after the card freed
        // the view.
        for (const lv_event_code_t code :
             {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST, LV_EVENT_CLICKED}) {
          lv_obj_add_event_cb(view->bar, bar_event_cb, code, view);
        }
      } else {
        lv_obj_remove_flag(view->bar, LV_OBJ_FLAG_CLICKABLE);
      }
      tile_icon_source::refresh_controls(card);
    }
  }

  view->next = g_views;
  g_views = view;
  show(view);

  if (view->entity.length() && grid_type != GridType::SCREENSAVER) {
    const lv_event_code_t popup_event =
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS ? LV_EVENT_SHORT_CLICKED : LV_EVENT_LONG_PRESSED;
    lv_obj_add_event_cb(card, on_card_event, popup_event, view);
    if (view->type != TILE_ALARM) {
      lv_obj_add_event_cb(card, on_card_toggle,
                          popup_event == LV_EVENT_SHORT_CLICKED ? LV_EVENT_LONG_PRESSED : LV_EVENT_SHORT_CLICKED, view);
    }
  }
  lv_obj_add_event_cb(card, on_card_event, LV_EVENT_DELETE, view);
  return card;
}
