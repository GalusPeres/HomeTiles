#include "src/types/energy_flow/renderer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl_private.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/power/power_manager.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_press.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/energy_flow/flow_geometry.h"
#include "src/types/energy_flow/flow_metrics.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"

// The Energy flow tile (decided design: flow_geometry.h). Each node is one
// Home Assistant sensor and opens its normal Sensor popup when tapped; there
// is no popup of the tile itself. Only what flows now is colored, in the
// color of its source, with one dot moving from the source to the target.
//
// Cost: the lines and dots are drawn by one object (DRAW_MAIN), the nodes
// are plain objects above it. A state change only marks the tile; the loop
// redraws it once. The dots move on one shared timer while a flow runs, on
// a shown page, without a popup and while the display is awake: each step
// redraws the dot's small area only, never the tile.
namespace {

using namespace energy_flow;

// The node colors of the decided design (Home Assistant's energy colors).
constexpr uint32_t kSolarColor = 0xFF9800;
constexpr uint32_t kGridColor = 0x488FC2;
constexpr uint32_t kBatteryColor = 0xF06292;
// The header icon (picture 66): the Energy color of the user's panel.
constexpr uint32_t kHeaderIconColor = 0xF29C06;
// Lines without a flow, darker on the light screen than on a white card.
constexpr uint32_t kIdleDark = 0x3C3C3C;
constexpr uint32_t kIdleLightCard = 0xDCDEE2;
constexpr uint32_t kIdleLightScreen = 0xCDD0D5;
// One step of the dots, the pace of the Media tile's playing bars.
constexpr uint32_t kMotionPeriodMs = 40;
// The dots' speed: calm at a few watts, faster towards this power.
constexpr float kFastWatts = 5000.0f;
constexpr float kSlowPxPerSecond = 40.0f;
constexpr float kFastPxPerSecond = 120.0f;
constexpr size_t kMaxFlowTiles = 32;

struct NodeWidgets {
  lv_obj_t* ring = nullptr;
  lv_obj_t* disc = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* value = nullptr;
  uint32_t color = 0xFFFFFFFFu;
  bool shown = false;
};

struct FlowTileState {
  lv_obj_t* card = nullptr;
  lv_obj_t* canvas = nullptr;
  lv_obj_t* charge = nullptr;
  String entities[kRoleCount];
  bool grid_invert = false;
  bool battery_invert = false;
  uint32_t card_color = 0;
  uint32_t idle_color = 0;
  int line_width = 4;
  int dot_radius = 5;
  float speed_scale = 1.0f;
  Layout layout;
  lv_area_t line_box[kLineCount] = {};
  NodeWidgets node[kNodeCount];
  bool line_shown[kLineCount] = {};
  bool line_live[kLineCount] = {};
  bool reverse[kLineCount] = {};
  uint32_t line_color[kLineCount] = {};
  float speed[kLineCount] = {};  // px per ms
  float phase[kLineCount] = {};  // distance along the line
  lv_point_t dot[kLineCount] = {};
  uint32_t last_tick = 0;
  bool dirty = false;
};

FlowTileState* g_tiles[kMaxFlowTiles] = {};
lv_timer_t* g_motion_timer = nullptr;
bool g_any_dirty = false;

int round_px(float value) { return static_cast<int>(lroundf(value)); }

uint32_t node_color(uint8_t node) {
  switch (node) {
    case kNodePv: return kSolarColor;
    case kNodeBattery: return kBatteryColor;
    default: return kGridColor;
  }
}

// A sensor's number from the entity cache (the Bridge's first snapshot
// until a state arrived); unknown and unavailable states give none.
bool cached_number(const String& entity, float& out) {
  if (!entity.length()) return false;
  String payload;
  if (!tile_icon_source::cached_payload(entity, payload)) payload = haBridgeConfig.findSensorInitialValue(entity);
  payload.trim();
  payload.replace(",", ".");
  char* end = nullptr;
  const float parsed = strtof(payload.c_str(), &end);
  if (!payload.length() || !end || end == payload.c_str() || isnan(parsed) || isinf(parsed)) return false;
  out = parsed;
  return true;
}

bool cached_watts(const String& entity, float& out) {
  if (!cached_number(entity, out)) return false;
  float factor = 1.0f;
  unit_factor(haBridgeConfig.findSensorUnit(entity).c_str(), factor);
  out *= factor;
  return true;
}

String power_text(float watts) {
  const char* language = configManager.getConfig().language;
  const float magnitude = fabsf(watts);
  if (magnitude >= 999950.0f) return i18n::format_number(language, magnitude / 1000000.0f, 1) + " MW";
  if (magnitude >= 999.5f) return i18n::format_number(language, magnitude / 1000.0f, 1) + " kW";
  return i18n::format_number(language, magnitude, 0) + " W";
}

void set_text(lv_obj_t* label, const String& text) {
  if (!label) return;
  const char* shown = lv_label_get_text(label);
  if (shown && text == shown) return;
  lv_label_set_text(label, text.c_str());
}

void set_icon(lv_obj_t* label, const String& name, const char* fallback) {
  if (!label) return;
  String glyph = getMdiChar(normalizeMdiIconName(name));
  if (!glyph.length()) glyph = getMdiChar(fallback);
  set_text(label, glyph);
}

// The node's disc and icon for an icon color: the plain tile disc of that
// color on the tile's card ("Ausfuellen" off, user 2026-10-24).
void color_node(FlowTileState& st, NodeWidgets& node, uint32_t rgb) {
  if (!node.disc || node.color == rgb) return;
  node.color = rgb;
  const tone_color::Fill fill = tile_icon_disc::tile_fill(st.card, st.card_color, rgb, true, false, false,
                                                          ui_surface_style::icon_glow_percent());
  lv_obj_set_style_bg_color(node.disc, lv_color_hex(fill.disc), 0);
  lv_obj_set_style_text_color(node.icon, lv_color_hex(tone_color::readable_icon(rgb)), 0);
}

bool shown_on_screen(lv_obj_t* obj) {
  if (!obj || lv_obj_get_screen(obj) != lv_screen_active()) return false;
  for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  }
  return true;
}

lv_point_t dot_point(const FlowTileState& st, uint8_t line) {
  const Polyline& path = st.layout.line[line];
  const float length = polyline_length(path);
  const Point p = point_at(path, st.reverse[line] ? length - st.phase[line] : st.phase[line]);
  return {round_px(p.x), round_px(p.y)};
}

void invalidate_dot(const FlowTileState& st, const lv_area_t& canvas, lv_point_t a, lv_point_t b) {
  const int r = st.dot_radius + 1;
  // Neighbouring steps share one small area; a dot that starts over
  // redraws both ends.
  if (abs(a.x - b.x) <= 2 * r && abs(a.y - b.y) <= 2 * r) {
    lv_area_t area = {canvas.x1 + LV_MIN(a.x, b.x) - r, canvas.y1 + LV_MIN(a.y, b.y) - r,
                      canvas.x1 + LV_MAX(a.x, b.x) + r, canvas.y1 + LV_MAX(a.y, b.y) + r};
    lv_obj_invalidate_area(st.canvas, &area);
    return;
  }
  for (const lv_point_t& p : {a, b}) {
    lv_area_t area = {canvas.x1 + p.x - r, canvas.y1 + p.y - r, canvas.x1 + p.x + r, canvas.y1 + p.y + r};
    lv_obj_invalidate_area(st.canvas, &area);
  }
}

bool any_live(const FlowTileState& st) {
  for (bool live : st.line_live) {
    if (live) return true;
  }
  return false;
}

void motion_timer_cb(lv_timer_t* timer) {
  const uint32_t now = lv_tick_get();
  const bool still = popup_shell_active() || powerManager.isInSleep();
  bool running = false;
  for (FlowTileState* st : g_tiles) {
    if (!st || !any_live(*st)) continue;
    running = true;
    uint32_t elapsed = now - st->last_tick;
    st->last_tick = now;
    if (still || !shown_on_screen(st->card)) continue;
    if (elapsed > 4 * kMotionPeriodMs) elapsed = kMotionPeriodMs;
    lv_area_t canvas;
    lv_obj_get_coords(st->canvas, &canvas);
    for (uint8_t line = 0; line < kLineCount; ++line) {
      if (!st->line_live[line]) continue;
      const float length = polyline_length(st->layout.line[line]);
      if (length <= 0.0f) continue;
      st->phase[line] = fmodf(st->phase[line] + st->speed[line] * elapsed, length);
      const lv_point_t next = dot_point(*st, line);
      if (next.x == st->dot[line].x && next.y == st->dot[line].y) continue;
      invalidate_dot(*st, canvas, st->dot[line], next);
      st->dot[line] = next;
    }
  }
  if (!running) lv_timer_pause(timer);
}

void wake_motion(FlowTileState& st) {
  st.last_tick = lv_tick_get();
  if (!g_motion_timer) {
    g_motion_timer = lv_timer_create(motion_timer_cb, kMotionPeriodMs, nullptr);
    return;
  }
  lv_timer_resume(g_motion_timer);
}

void canvas_draw_cb(lv_event_t* e) {
  FlowTileState* st = static_cast<FlowTileState*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!st || !layer) return;
  lv_area_t canvas;
  lv_obj_get_coords(st->canvas, &canvas);
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.width = st->line_width;
  line_dsc.round_start = 1;
  line_dsc.round_end = 1;
  lv_point_precise_t points[kMaxLinePoints];
  // The idle lines first, the flows over them, then their dots.
  for (int pass = 0; pass < 2; ++pass) {
    for (uint8_t line = 0; line < kLineCount; ++line) {
      if (!st->line_shown[line] || st->line_live[line] != (pass == 1)) continue;
      lv_area_t box = st->line_box[line];
      lv_area_move(&box, canvas.x1, canvas.y1);
      if (!lv_area_is_on(&box, &layer->_clip_area)) continue;
      const Polyline& path = st->layout.line[line];
      for (uint8_t i = 0; i < path.count; ++i) {
        points[i].x = canvas.x1 + round_px(path.p[i].x);
        points[i].y = canvas.y1 + round_px(path.p[i].y);
      }
      line_dsc.color = lv_color_hex(st->line_color[line]);
      line_dsc.points = points;
      line_dsc.point_cnt = path.count;
      lv_draw_line(layer, &line_dsc);
    }
  }
  lv_draw_rect_dsc_t dot_dsc;
  lv_draw_rect_dsc_init(&dot_dsc);
  dot_dsc.radius = LV_RADIUS_CIRCLE;
  dot_dsc.bg_opa = LV_OPA_COVER;
  for (uint8_t line = 0; line < kLineCount; ++line) {
    if (!st->line_live[line]) continue;
    const lv_point_t p = st->dot[line];
    const lv_area_t area = {canvas.x1 + p.x - st->dot_radius, canvas.y1 + p.y - st->dot_radius,
                            canvas.x1 + p.x + st->dot_radius - 1, canvas.y1 + p.y + st->dot_radius - 1};
    if (!lv_area_is_on(&area, &layer->_clip_area)) continue;
    dot_dsc.bg_color = lv_color_hex(st->line_color[line]);
    lv_draw_rect(layer, &dot_dsc, &area);
  }
}

// Reads every node from the entity cache and shows it.
void refresh(FlowTileState& st) {
  st.dirty = false;
  const ui_theme::SurfaceScope surface = tile_surface::scope(st.card);
  Inputs in;
  in.has_pv = cached_watts(st.entities[kRolePv], in.pv);
  in.has_grid = cached_watts(st.entities[kRoleGrid], in.grid);
  in.has_battery = cached_watts(st.entities[kRoleBattery], in.battery);
  in.has_home = cached_watts(st.entities[kRoleHome], in.home);
  if (st.grid_invert) in.grid = -in.grid;
  if (st.battery_invert) in.battery = -in.battery;
  const Flows flows = compute(in);

  set_text(st.node[kNodePv].value, in.has_pv ? power_text(in.pv) : String("--"));
  set_text(st.node[kNodeGrid].value, in.has_grid ? power_text(in.grid) : String("--"));
  set_text(st.node[kNodeBattery].value, in.has_battery ? power_text(in.battery) : String("--"));
  const bool home_known = st.entities[kRoleHome].length() ? in.has_home : flows.home_known;
  set_text(st.node[kNodeHome].value, home_known ? power_text(flows.home) : String("--"));
  float charge = 0.0f;
  if (st.charge) {
    set_text(st.charge, cached_number(st.entities[kRoleBatteryCharge], charge)
                            ? i18n::format_number(configManager.getConfig().language, charge, 0) + " %"
                            : String("--"));
  }
  // The battery shows its charge in the icon Home Assistant gives it.
  const String& battery_icon_entity =
      st.entities[kRoleBatteryCharge].length() ? st.entities[kRoleBatteryCharge] : st.entities[kRoleBattery];
  set_icon(st.node[kNodeBattery].icon, haBridgeConfig.findEntityIcon(battery_icon_entity), "battery");
  // Home in the color of the source that delivers most.
  color_node(st, st.node[kNodeHome],
             node_color(flows.home_source == kNodeCount ? kNodeGrid : flows.home_source));

  bool redraw = false;
  for (uint8_t line = 0; line < kLineCount; ++line) {
    const bool live = st.line_shown[line] && flows.watts[line] > 0.0f;
    const uint32_t color = live ? node_color(line_source(static_cast<Line>(line), flows)) : st.idle_color;
    const bool reverse = line == kLineBatteryGrid && flows.grid_to_battery;
    if (live != st.line_live[line] || color != st.line_color[line] || reverse != st.reverse[line]) {
      redraw = true;
      if (live && (!st.line_live[line] || reverse != st.reverse[line])) st.phase[line] = 0.0f;
    }
    st.line_live[line] = live;
    st.line_color[line] = color;
    st.reverse[line] = reverse;
    const float share = fminf(flows.watts[line] / kFastWatts, 1.0f);
    st.speed[line] = (kSlowPxPerSecond + (kFastPxPerSecond - kSlowPxPerSecond) * sqrtf(share)) * st.speed_scale / 1000.0f;
    if (live) st.dot[line] = dot_point(st, line);
  }
  if (redraw) lv_obj_invalidate(st.canvas);
  if (any_live(st)) wake_motion(st);
}

void open_node_popup(lv_event_t* e, FlowTileState& st, uint8_t role, uint8_t node) {
  const String& entity = st.entities[role];
  if (!entity.length()) return;
  SensorPopupInit init;
  init.entity_id = entity;
  init.title = haBridgeConfig.findSensorName(entity);
  if (!init.title.length()) init.title = entity;
  String icon = normalizeMdiIconName(haBridgeConfig.findEntityIcon(entity));
  if (!icon.length()) icon = node == kNodePv ? "solar-power-variant" : node == kNodeGrid ? "transmission-tower"
                             : node == kNodeHome ? "home" : "battery";
  init.icon_name = icon;
  init.unit = haBridgeConfig.findSensorUnit(entity);
  init.bg_color = st.card_color;
  init.value = haBridgeConfig.findSensorInitialValue(entity);
  String kind = haBridgeConfig.findSensorStateKind(entity);
  kind.trim();
  kind.toLowerCase();
  init.state_history_mode = kind == "state" ? true
                            : kind == "number" ? false
                                               : sensor_popup_should_use_state_history(init.value, init.unit);
  // The header icon in the node's color.
  init.forced_icon = true;
  init.forced_icon_color = st.node[node].color & 0xFFFFFF;
  finish_press_before_popup(e);
  show_sensor_popup(init);
}

// The node a ring or the charge label belongs to, as user data:
// role * 8 + node.
void node_click_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  FlowTileState* st = static_cast<FlowTileState*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  if (!st || !target) return;
  const uintptr_t tag = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
  open_node_popup(e, *st, static_cast<uint8_t>(tag / 8), static_cast<uint8_t>(tag % 8));
}

void card_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  FlowTileState* st = static_cast<FlowTileState*>(lv_event_get_user_data(e));
  if (!st) return;
  for (FlowTileState*& slot : g_tiles) {
    if (slot == st) slot = nullptr;
  }
  st->~FlowTileState();
  free(st);
}

FlowTileState* new_state() {
  void* memory = heap_caps_malloc(sizeof(FlowTileState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) memory = malloc(sizeof(FlowTileState));
  return memory ? new (memory) FlowTileState() : nullptr;
}

bool register_state(FlowTileState* st) {
  for (FlowTileState*& slot : g_tiles) {
    if (slot) continue;
    slot = st;
    return true;
  }
  static uint32_t last_log_ms = 0;
  if (last_log_ms == 0 || millis() - last_log_ms > 60000) {
    last_log_ms = millis();
    Serial.printf("[EnergyFlow] More than %u tiles; this one does not follow states\n",
                  static_cast<unsigned>(kMaxFlowTiles));
  }
  return false;
}

// A node: the plain tile disc with its icon inside a ring in the card's
// color (the corner of a tile, user 2026-10-24). The ring is what a tap hits.
void create_node(FlowTileState& st, uint8_t node, uint8_t role, int pad_left, int pad_top, bool frame_lines) {
  NodeWidgets& w = st.node[node];
  const Point centre = st.layout.node[node];
  const int ring = round_px(2.0f * st.layout.ring);
  const int disc = round_px(2.0f * st.layout.disc);
  w.ring = lv_obj_create(st.card);
  lv_obj_remove_style_all(w.ring);
  lv_obj_remove_flag(w.ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(w.ring, ring, ring);
  lv_obj_set_pos(w.ring, round_px(centre.x) - ring / 2 - pad_left, round_px(centre.y) - ring / 2 - pad_top);
  lv_obj_set_style_radius(w.ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(w.ring, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(w.ring, lv_color_hex(st.card_color), 0);
  lv_obj_set_style_bg_color(w.ring, lv_color_hex(tone_color::pressed_card(st.card_color)), LV_STATE_PRESSED);
  tile_press::apply(w.ring);
  // Without a card the rings take the frame line when it is on (user
  // 2026-10-24); on a card only the card has it.
  if (frame_lines) ui_surface_style::apply_global_tile_border(w.ring);
  if (st.entities[role].length()) {
    lv_obj_add_flag(w.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(w.ring, reinterpret_cast<void*>(static_cast<uintptr_t>(role * 8 + node)));
    lv_obj_add_event_cb(w.ring, node_click_cb, LV_EVENT_SHORT_CLICKED, &st);
  } else {
    lv_obj_remove_flag(w.ring, LV_OBJ_FLAG_CLICKABLE);
  }

  w.disc = lv_obj_create(w.ring);
  lv_obj_remove_style_all(w.disc);
  lv_obj_remove_flag(w.disc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(w.disc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(w.disc, disc, disc);
  lv_obj_center(w.disc);
  lv_obj_set_style_radius(w.disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(w.disc, LV_OPA_COVER, 0);

  w.icon = lv_label_create(w.ring);
  set_label_style(w.icon, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
  lv_obj_center(w.icon);
  w.shown = true;
}

lv_obj_t* create_value(FlowTileState& st, const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(st.card);
  set_label_style(label, lv_color_hex(ui_theme::text()), font);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_label_set_text(label, "--");
  return label;
}

// A value box: `x` its left edge, `width` its width, centred on `centre_y`.
void place_value(lv_obj_t* label, int x, int width, float centre_y, lv_text_align_t align, int pad_left,
                 int pad_top) {
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  const int height = lv_font_get_line_height(font);
  lv_obj_set_width(label, width > 1 ? width : 1);
  lv_obj_set_style_text_align(label, align, 0);
  lv_obj_set_pos(label, x - pad_left, round_px(centre_y) - height / 2 - pad_top);
}

}  // namespace

lv_obj_t* render_energy_flow_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                                  GridType grid_type) {
  (void)index;
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for energy flow tile");
    return nullptr;
  }
  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;
  const uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  // The card has no action of its own; its nodes do.
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  FlowTileState* st = new_state();
  if (!st) {
    Serial.println("[EnergyFlow] ERROR: no memory for the tile state");
    return card;
  }
  st->card = card;
  for (uint8_t role = 0; role < kRoleCount; ++role) st->entities[role] = flowEntityAt(tile.flow_entities, role);
  st->grid_invert = energyFlowOption(tile, kFlowGridInvertBit);
  st->battery_invert = energyFlowOption(tile, kFlowBatteryInvertBit);
  const bool on_card = !energyFlowOption(tile, kFlowNoCardBit);
  st->card_color = card_color;
  st->idle_color = !ui_theme::light() ? kIdleDark : on_card ? kIdleLightCard : kIdleLightScreen;
  st->line_width = line_width();
  st->dot_radius = dot_radius();
  st->speed_scale = tile_layout::scale(1000) / 1000.0f;
  lv_obj_add_event_cb(card, card_delete_cb, LV_EVENT_DELETE, st);

  // The tile's size as the grid places it (whole and half cells alike).
  const float width = tile_geometry::extent(tile.col, tile.span_w, GRID_CELL_W, GRID_GAP);
  const float height = tile_geometry::extent(tile.row, tile.span_h, GRID_CELL_H, GRID_GAP);
  st->layout = layout(width, height, device_metrics());
  const int pad_left = lv_obj_get_style_pad_left(card, LV_PART_MAIN);
  const int pad_top = lv_obj_get_style_pad_top(card, LV_PART_MAIN);

  const bool has_pv = st->entities[kRolePv].length() > 0;
  const bool has_grid = st->entities[kRoleGrid].length() > 0;
  const bool has_battery = st->entities[kRoleBattery].length() || st->entities[kRoleBatteryCharge].length();
  // Lines to a node without an entity are left out (like Home Assistant).
  st->line_shown[kLineGridHome] = has_grid;
  st->line_shown[kLinePvBattery] = has_pv && has_battery;
  st->line_shown[kLinePvHome] = has_pv;
  st->line_shown[kLinePvGrid] = has_pv && has_grid;
  st->line_shown[kLineBatteryHome] = has_battery;
  st->line_shown[kLineBatteryGrid] = has_battery && has_grid;
  for (uint8_t line = 0; line < kLineCount; ++line) {
    const Polyline& path = st->layout.line[line];
    lv_area_t& box = st->line_box[line];
    box = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
    for (uint8_t i = 0; i < path.count; ++i) {
      box.x1 = LV_MIN(box.x1, round_px(path.p[i].x) - st->line_width);
      box.y1 = LV_MIN(box.y1, round_px(path.p[i].y) - st->line_width);
      box.x2 = LV_MAX(box.x2, round_px(path.p[i].x) + st->line_width);
      box.y2 = LV_MAX(box.y2, round_px(path.p[i].y) + st->line_width);
    }
    st->line_color[line] = st->idle_color;
  }

  st->canvas = lv_obj_create(card);
  lv_obj_remove_style_all(st->canvas);
  lv_obj_remove_flag(st->canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(st->canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(st->canvas, round_px(width), round_px(height));
  lv_obj_set_pos(st->canvas, -pad_left, -pad_top);
  lv_obj_add_event_cb(st->canvas, canvas_draw_cb, LV_EVENT_DRAW_MAIN, st);

  const bool frame_lines = !on_card && grid_type != GridType::SCREENSAVER;
  static const uint8_t kNodeRole[kNodeCount] = {kRolePv, kRoleGrid, kRoleHome, kRoleBattery};
  const bool node_shown[kNodeCount] = {has_pv, has_grid, true, has_battery};
  const lv_font_t* font = value_font();
  const Layout& l = st->layout;
  const int gap = round_px(l.ring) + value_gap();
  const int edge = value_edge();
  for (uint8_t node = 0; node < kNodeCount; ++node) {
    if (!node_shown[node]) continue;
    create_node(*st, node, kNodeRole[node], pad_left, pad_top, frame_lines);
    st->node[node].value = create_value(*st, font);
  }
  // Values (mockup): solar right of its node, grid and home under theirs,
  // the battery's charge left and its power right of it.
  const float line_h = lv_font_get_line_height(font);
  const float below = gap + value_below() - (line_h - font->base_line) + line_h / 2.0f;
  if (NodeWidgets& n = st->node[kNodePv]; n.value) {
    const int x = round_px(l.node[kNodePv].x) + gap;
    place_value(n.value, x, round_px(width) - edge - x, l.node[kNodePv].y, LV_TEXT_ALIGN_LEFT, pad_left, pad_top);
  }
  if (NodeWidgets& n = st->node[kNodeGrid]; n.value) {
    const int half = round_px(l.node[kNodeGrid].x) - edge;
    place_value(n.value, round_px(l.node[kNodeGrid].x) - half, 2 * half, l.node[kNodeGrid].y + below,
                LV_TEXT_ALIGN_CENTER, pad_left, pad_top);
  }
  if (NodeWidgets& n = st->node[kNodeHome]; n.value) {
    const int half = round_px(width - l.node[kNodeHome].x) - edge;
    place_value(n.value, round_px(l.node[kNodeHome].x) - half, 2 * half, l.node[kNodeHome].y + below,
                LV_TEXT_ALIGN_CENTER, pad_left, pad_top);
  }
  if (NodeWidgets& n = st->node[kNodeBattery]; n.value) {
    const int x = round_px(l.node[kNodeBattery].x) + gap;
    place_value(n.value, x, round_px(width) - edge - x, l.node[kNodeBattery].y, LV_TEXT_ALIGN_LEFT, pad_left,
                pad_top);
    const int charge_right = round_px(l.node[kNodeBattery].x) - gap;
    st->charge = create_value(*st, font);
    place_value(st->charge, edge, charge_right - edge, l.node[kNodeBattery].y, LV_TEXT_ALIGN_RIGHT, pad_left,
                pad_top);
    if (st->entities[kRoleBatteryCharge].length()) {
      // A tap on the charge opens the charge sensor (user 2026-10-24).
      lv_obj_add_flag(st->charge, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_ext_click_area(st->charge, tile_layout::scale(10));
      lv_obj_set_user_data(st->charge,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(kRoleBatteryCharge * 8 + kNodeBattery)));
      lv_obj_add_event_cb(st->charge, node_click_cb, LV_EVENT_SHORT_CLICKED, st);
    }
  }

  set_icon(st->node[kNodePv].icon, "", "solar-power-variant");
  set_icon(st->node[kNodeGrid].icon, "", "transmission-tower");
  set_icon(st->node[kNodeHome].icon, "", "home");
  color_node(*st, st->node[kNodePv], kSolarColor);
  color_node(*st, st->node[kNodeGrid], kGridColor);
  color_node(*st, st->node[kNodeBattery], kBatteryColor);

  // Header (off by default): the tile's icon in a node's disc in the top
  // left corner and the title alone top right (picture 66).
  if (energyFlowOption(tile, kFlowHeaderBit)) {
    NodeWidgets header;
    const float corner = l.ring;
    const int ring = round_px(2.0f * l.ring), disc = round_px(2.0f * l.disc);
    header.ring = lv_obj_create(card);
    lv_obj_remove_style_all(header.ring);
    lv_obj_remove_flag(header.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header.ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(header.ring, ring, ring);
    lv_obj_set_pos(header.ring, round_px(corner) - ring / 2 - pad_left, round_px(corner) - ring / 2 - pad_top);
    lv_obj_set_style_radius(header.ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(header.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header.ring, lv_color_hex(card_color), 0);
    if (frame_lines) ui_surface_style::apply_global_tile_border(header.ring);
    header.disc = lv_obj_create(header.ring);
    lv_obj_remove_style_all(header.disc);
    lv_obj_remove_flag(header.disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(header.disc, disc, disc);
    lv_obj_center(header.disc);
    lv_obj_set_style_radius(header.disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(header.disc, LV_OPA_COVER, 0);
    header.icon = lv_label_create(header.ring);
    set_label_style(header.icon, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
    lv_obj_center(header.icon);
    set_icon(header.icon, isMdiIconDisabled(tile.icon_name) ? String() : tile.icon_name, "lightning-bolt");
    color_node(*st, header, kHeaderIconColor);
    if (tile.title.length()) {
      lv_obj_t* title = lv_label_create(card);
      set_label_style(title, lv_color_hex(ui_theme::text()), font);
      lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
      const int right = round_px(width) - title_right();
      const int left = round_px(l.ring * 2.0f) + value_gap();
      lv_obj_set_width(title, right - left > 1 ? right - left : 1);
      lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_RIGHT, 0);
      hometiles_title::tile(title, tile.title.c_str(), true);
      const int top = title_baseline() - (lv_font_get_line_height(font) - font->base_line);
      lv_obj_set_pos(title, left - pad_left, top - pad_top);
    }
  }

  register_state(st);
  refresh(*st);
  return card;
}

bool energy_flow_entity_changed(const char* entity_id) {
  if (!entity_id || !*entity_id) return false;
  bool used = false;
  for (FlowTileState* st : g_tiles) {
    if (!st) continue;
    for (const String& entity : st->entities) {
      if (!entity.length() || !entity.equalsIgnoreCase(entity_id)) continue;
      st->dirty = true;
      used = true;
      break;
    }
  }
  if (used) g_any_dirty = true;
  return used;
}

void process_energy_flow_updates() {
  if (!g_any_dirty) return;
  g_any_dirty = false;
  for (FlowTileState* st : g_tiles) {
    if (st && st->dirty) refresh(*st);
  }
}
