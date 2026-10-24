#include "src/types/energy_flow/renderer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl_private.h>
// lvgl.h no longer exports lv_image_cache_drop() (tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>
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
#include "src/ui/screensaver/image_screensaver.h"
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
// redraws it once. The dots move with the display's own refresh while a
// flow runs, on a shown page, without a popup and while the display is
// awake: each step redraws the dot's small area only, never the tile.
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
// One step of the dots per display frame (60 fps on the 17 ms refresh,
// user 2026-10-24): the display's refresh moves them (LV_EVENT_REFR_START),
// so every frame shows one step and no timer beats against the refresh
// (b533's 40 ms timer showed its steps for 34 or 51 ms). Whole pixels per
// step, the same every frame: 1 px, 2 px from this power.
constexpr float kStep2Watts = 1500.0f;
// The dots' cost in the log: one line per this many ms while they move.
constexpr uint32_t kMotionLogMs = 10000;
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
  Layout layout;
  lv_area_t line_box[kLineCount] = {};
  NodeWidgets node[kNodeCount];
  bool line_shown[kLineCount] = {};
  bool line_live[kLineCount] = {};
  bool reverse[kLineCount] = {};
  uint32_t line_color[kLineCount] = {};
  uint8_t step[kLineCount] = {};  // px per motion step
  float phase[kLineCount] = {};  // distance along the line
  lv_point_t dot[kLineCount] = {};
  // The bends as pictures, drawn once per color (render_bend): LVGL builds
  // a bend's circle masks again in every frame a dot passes it (b540 log:
  // ~0.25 ms per bend drawn). Areas relative to the canvas.
  lv_draw_buf_t* bend_picture[kLineCount] = {};
  uint32_t bend_color[kLineCount] = {};
  lv_area_t bend_area[kLineCount] = {};
  bool dirty = false;
};

FlowTileState* g_tiles[kMaxFlowTiles] = {};
lv_display_t* g_motion_display = nullptr;

// What the dots cost (one log line per kMotionLogMs while they move): the
// frames that moved one, their dirty areas and render time, the steps' own
// time and the longest gap between two frames (the loop elsewhere).
struct MotionStats {
  uint32_t window_ms = 0;
  uint32_t frames = 0;
  uint32_t step_us = 0;
  uint32_t step_max_us = 0;
  uint32_t render_us = 0;
  uint32_t render_max_us = 0;
  uint32_t areas = 0;
  uint32_t areas_max = 0;
  uint32_t gap_max_us = 0;
  uint32_t dots_max = 0;
  // The tiles' line drawing (canvas DRAW_MAIN) inside those frames.
  uint32_t lines_us = 0;
  uint32_t lines_max_us = 0;
  uint32_t frame_lines_us = 0;
  // Within it: straight pieces, bends and dots (count and time).
  uint32_t piece_n[3] = {};
  uint32_t piece_us[3] = {};
  // LVGL's search for the topmost covering object of each area (from the
  // screen's cover check to the first object drawn).
  uint32_t search_us = 0;
  uint32_t search_start_us = 0;
  // Where a frame's time goes: LVGL's layout pass before rendering, the
  // flushes to the panel (count and time) and the dirty pixels, so the log
  // shows whether the cost follows the number of areas or their size.
  uint32_t layout_us = 0;
  uint32_t flush_us = 0;
  uint32_t flushes = 0;
  uint32_t pixels = 0;
  // Drawing the areas (between the flushes) and the end of the refresh
  // after the last flush.
  uint32_t draw_us = 0;
  uint32_t tail_us = 0;
  uint32_t segment_start_us = 0;
  // The frame under way.
  uint32_t frame_start_us = 0;
  uint32_t frame_areas = 0;
  uint32_t flush_start_us = 0;
  uint32_t last_ready_us = 0;
};
MotionStats g_motion_stats;
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

// The battery's power keeps the sign Home Assistant gives it (user
// 2026-10-24); the other values show their amount.
String signed_power_text(float watts) {
  return watts <= -0.5f ? String("-") + power_text(watts) : power_text(watts);
}

// While the battery charges, its node shows Home Assistant's charging icon
// for its level (the frontend's batteryIcon: the level rounded to tens,
// below 10 % the outline; user 2026-10-24).
String charging_icon(bool has_level, float level) {
  if (!has_level) return "battery-charging";
  if (level < 10.0f) return "battery-charging-outline";
  const long tens = LV_MIN(10L, lroundf(level / 10.0f)) * 10L;
  return String("battery-charging-") + String(tens);
}

// The grid's icon shows its direction past the idle threshold (user
// 2026-10-24): importing, exporting, or the plain tower.
const char* grid_icon(const Inputs& in) {
  if (in.has_grid && in.grid >= kIdleWatts) return "transmission-tower-import";
  if (in.has_grid && -in.grid >= kIdleWatts) return "transmission-tower-export";
  return "transmission-tower";
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

// A node ring lies opaque over the ends of its lines; the dots run out from
// under it. Where it covers a dot's area completely (its box without the
// corners its radius rounds), nothing changes on screen, so that part is
// not redrawn. Redrawn, every dot step near a node drew its ring, disc and
// icon again (b539: ~0.45 ms of each frame on the V2). False when nothing
// is left.
bool clip_to_rings(const FlowTileState& st, lv_area_t& area) {
  for (const NodeWidgets& node : st.node) {
    if (!node.shown || !node.ring || lv_obj_has_flag(node.ring, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t box;
    lv_obj_get_coords(node.ring, &box);
    if (!lv_area_is_on(&area, &box)) continue;
    const int32_t corner = LV_MIN(lv_obj_get_style_radius(node.ring, LV_PART_MAIN),
                                  LV_MIN(lv_area_get_width(&box), lv_area_get_height(&box)) / 2);
    if (area.x1 >= box.x1 + corner && area.x2 <= box.x2 - corner) {
      // Across the ring's top or bottom side.
      if (area.y1 >= box.y1 && area.y2 <= box.y2) return false;
      if (area.y1 < box.y1 && area.y2 <= box.y2) area.y2 = box.y1 - 1;
      else if (area.y2 > box.y2 && area.y1 >= box.y1) area.y1 = box.y2 + 1;
    } else if (area.y1 >= box.y1 + corner && area.y2 <= box.y2 - corner) {
      // Across its left or right side.
      if (area.x1 >= box.x1 && area.x2 <= box.x2) return false;
      if (area.x1 < box.x1 && area.x2 <= box.x2) area.x2 = box.x1 - 1;
      else if (area.x2 > box.x2 && area.x1 >= box.x1) area.x1 = box.x2 + 1;
    }
  }
  return true;
}

// Redraws a dot's old and new place; false when the rings cover both.
bool invalidate_dot(const FlowTileState& st, const lv_area_t& canvas, lv_point_t a, lv_point_t b) {
  const int r = st.dot_radius + 1;
  bool any = false;
  auto invalidate = [&](lv_area_t area) {
    if (!clip_to_rings(st, area)) return;
    lv_obj_invalidate_area(st.canvas, &area);
    any = true;
  };
  // Neighbouring steps share one small area; a dot that starts over
  // redraws both ends.
  if (abs(a.x - b.x) <= 2 * r && abs(a.y - b.y) <= 2 * r) {
    invalidate({canvas.x1 + LV_MIN(a.x, b.x) - r, canvas.y1 + LV_MIN(a.y, b.y) - r,
                canvas.x1 + LV_MAX(a.x, b.x) + r, canvas.y1 + LV_MAX(a.y, b.y) + r});
    return any;
  }
  for (const lv_point_t& p : {a, b}) {
    invalidate({canvas.x1 + p.x - r, canvas.y1 + p.y - r, canvas.x1 + p.x + r, canvas.y1 + p.y + r});
  }
  return any;
}

bool any_live(const FlowTileState& st) {
  for (bool live : st.line_live) {
    if (live) return true;
  }
  return false;
}

// One step of every shown dot; the number of dots that moved. `waiting`:
// a dot is shown that did not change its pixel this frame.
uint32_t step_dots(bool& waiting) {
  waiting = false;
  // Nothing moves where nobody sees it: behind a popup, asleep, or under the
  // screensaver (b539 log: there the dots ran on at ~47 frames a second and
  // redrew the screensaver above them in every area).
  if (popup_shell_active() || powerManager.isInSleep() || image_screensaver_covers_ui()) return 0;
  uint32_t moved = 0;
  for (FlowTileState* st : g_tiles) {
    if (!st || !any_live(*st) || !shown_on_screen(st->card)) continue;
    lv_area_t canvas;
    lv_obj_get_coords(st->canvas, &canvas);
    for (uint8_t line = 0; line < kLineCount; ++line) {
      if (!st->line_live[line]) continue;
      const float length = polyline_length(st->layout.line[line]);
      if (length <= 0.0f) continue;
      st->phase[line] = fmodf(st->phase[line] + st->step[line], length);
      const lv_point_t next = dot_point(*st, line);
      if (next.x == st->dot[line].x && next.y == st->dot[line].y) {
        waiting = true;
        continue;
      }
      const bool redrawn = invalidate_dot(*st, canvas, st->dot[line], next);
      st->dot[line] = next;
      // Under a ring: the next frame still comes.
      if (!redrawn) {
        waiting = true;
        continue;
      }
      ++moved;
    }
  }
  return moved;
}

// Which objects the dot frames draw (one log line per kMotionLogMs, with
// the dots' line): the card and its children, every tile of the grid, the
// grid, the siblings LVGL draws after each level on the way up to the screen,
// and the top and system layers with their children. Each probe keeps its
// object's place, size and child count from when it was added, so the log
// never reads an object that is gone. Tags: k card child, c card, t tile,
// g grid, a level above (with its number), s sibling of a level, L layer,
// l layer child.
struct DrawProbe {
  lv_obj_t* obj = nullptr;
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  uint32_t children = 0;
  char tag = '?';
  uint8_t level = 0;
  uint32_t start_us = 0;
  uint32_t sum_us = 0;
};
constexpr size_t kMaxProbes = 96;
DrawProbe g_probes[kMaxProbes];
size_t g_probe_count = 0;

void probe_cb(lv_event_t* e) {
  DrawProbe* probe = static_cast<DrawProbe*>(lv_event_get_user_data(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DELETE) {
    probe->obj = nullptr;
    return;
  }
  MotionStats& m = g_motion_stats;
  if (!m.frame_areas) return;
  if (code == LV_EVENT_DRAW_MAIN_BEGIN) {
    probe->start_us = micros();
    if (m.search_start_us) {
      m.search_us += probe->start_us - m.search_start_us;
      m.search_start_us = 0;
    }
  } else if (code == LV_EVENT_DRAW_POST_BEGIN) {
    // A level above the top object only gets its post-draw events.
    if (!probe->start_us) probe->start_us = micros();
  } else if (code == LV_EVENT_DRAW_POST_END && probe->start_us) {
    probe->sum_us += micros() - probe->start_us;
    probe->start_us = 0;
  }
}

void add_probe(lv_obj_t* obj, char tag, uint8_t level) {
  if (!obj || g_probe_count >= kMaxProbes) return;
  for (size_t i = 0; i < g_probe_count; ++i) {
    if (g_probes[i].obj == obj) return;
  }
  DrawProbe& probe = g_probes[g_probe_count++];
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  probe.obj = obj;
  probe.x = coords.x1;
  probe.y = coords.y1;
  probe.width = lv_area_get_width(&coords);
  probe.height = lv_area_get_height(&coords);
  probe.children = lv_obj_get_child_count(obj);
  probe.tag = tag;
  probe.level = level;
  for (const lv_event_code_t code :
       {LV_EVENT_DRAW_MAIN_BEGIN, LV_EVENT_DRAW_POST_BEGIN, LV_EVENT_DRAW_POST_END, LV_EVENT_DELETE}) {
    lv_obj_add_event_cb(obj, probe_cb, code, &probe);
  }
}

void add_children(lv_obj_t* parent, char tag, uint8_t level) {
  const uint32_t count = parent ? lv_obj_get_child_count(parent) : 0;
  for (uint32_t i = 0; i < count; ++i) add_probe(lv_obj_get_child(parent, static_cast<int32_t>(i)), tag, level);
}

// Each area's top object search starts with the screen's cover check.
void search_cb(lv_event_t*) {
  MotionStats& m = g_motion_stats;
  if (m.frame_areas && !m.search_start_us) m.search_start_us = micros();
}

void add_probes(lv_obj_t* card) {
  lv_obj_add_event_cb(lv_obj_get_screen(card), search_cb, LV_EVENT_COVER_CHECK, nullptr);
  add_probe(card, 'c', 0);
  add_children(card, 'k', 0);
  lv_obj_t* grid = lv_obj_get_parent(card);
  if (!grid) return;
  add_probe(grid, 'g', 1);
  add_children(grid, 't', 1);
  uint8_t level = 1;
  for (lv_obj_t* parent = lv_obj_get_parent(grid); parent; parent = lv_obj_get_parent(parent)) {
    add_probe(parent, 'a', ++level);
    add_children(parent, 's', level - 1);
  }
  for (lv_obj_t* layer : {lv_display_get_layer_top(g_motion_display), lv_display_get_layer_sys(g_motion_display)}) {
    add_probe(layer, 'L', 0);
    add_children(layer, 'l', 0);
  }
}

void log_probes(uint32_t frames) {
  // The twelve most expensive objects (their trees), per dot frame.
  bool shown[kMaxProbes] = {};
  char line[900];
  int used = snprintf(line, sizeof(line), "[EnergyFlow] Dots draw per frame (%u probes):",
                      static_cast<unsigned>(g_probe_count));
  for (int rank = 0; rank < 12; ++rank) {
    int best = -1;
    for (size_t i = 0; i < g_probe_count; ++i) {
      if (shown[i] || !g_probes[i].sum_us) continue;
      if (best < 0 || g_probes[i].sum_us > g_probes[best].sum_us) best = static_cast<int>(i);
    }
    if (best < 0) break;
    shown[best] = true;
    const DrawProbe& probe = g_probes[best];
    used += snprintf(line + used, sizeof(line) - used, " %c%u %ldx%ld@%ld,%ld n=%lu %luus%s", probe.tag,
                     static_cast<unsigned>(probe.level), static_cast<long>(probe.width),
                     static_cast<long>(probe.height), static_cast<long>(probe.x), static_cast<long>(probe.y),
                     static_cast<unsigned long>(probe.children), static_cast<unsigned long>(probe.sum_us / frames),
                     probe.obj ? "" : " (gone)");
    if (used >= static_cast<int>(sizeof(line)) - 1) break;
  }
  Serial.println(line);
  for (size_t i = 0; i < g_probe_count; ++i) g_probes[i].sum_us = 0;
}

void log_motion(uint32_t now_ms) {
  MotionStats& m = g_motion_stats;
  if (!m.window_ms) m.window_ms = now_ms;
  if (now_ms - m.window_ms < kMotionLogMs) return;
  if (m.frames) {
    const uint32_t frames = m.frames;
    const uint32_t window = now_ms - m.window_ms;
    Serial.printf("[EnergyFlow] Dots %lu s: frames=%lu (%.1f/s) dots<=%lu areas avg=%.1f max=%lu px avg=%lu "
                  "render avg=%.2f max=%.2f ms layout avg=%lu us draw avg=%lu us flush avg=%lu us n=%.1f "
                  "tail avg=%lu us lines avg=%lu max=%lu us (line %.1f %lu us arc %.1f %lu us dot %.1f %lu us) "
                  "search avg=%lu us step avg=%lu max=%lu us gap max=%.1f ms\n",
                  static_cast<unsigned long>(window / 1000), static_cast<unsigned long>(frames),
                  frames * 1000.0f / window, static_cast<unsigned long>(m.dots_max),
                  static_cast<float>(m.areas) / frames, static_cast<unsigned long>(m.areas_max),
                  static_cast<unsigned long>(m.pixels / frames),
                  m.render_us / 1000.0f / frames, m.render_max_us / 1000.0f,
                  static_cast<unsigned long>(m.layout_us / frames), static_cast<unsigned long>(m.draw_us / frames),
                  static_cast<unsigned long>(m.flush_us / frames), static_cast<float>(m.flushes) / frames,
                  static_cast<unsigned long>(m.tail_us / frames),
                  static_cast<unsigned long>(m.lines_us / frames), static_cast<unsigned long>(m.lines_max_us),
                  static_cast<float>(m.piece_n[0]) / frames, static_cast<unsigned long>(m.piece_us[0] / frames),
                  static_cast<float>(m.piece_n[1]) / frames, static_cast<unsigned long>(m.piece_us[1] / frames),
                  static_cast<float>(m.piece_n[2]) / frames, static_cast<unsigned long>(m.piece_us[2] / frames),
                  static_cast<unsigned long>(m.search_us / frames), static_cast<unsigned long>(m.step_us / frames), static_cast<unsigned long>(m.step_max_us),
                  m.gap_max_us / 1000.0f);
  }
  if (m.frames) log_probes(m.frames);
  m = MotionStats{};
  m.window_ms = now_ms;
}

// The display's refresh: one step at its start (its areas are drawn in this
// same refresh), the next frame asked for while a dot is shown; nothing
// shown, nothing asked: LVGL rests its refresh until the UI changes again.
void motion_display_cb(lv_event_t* e) {
  MotionStats& m = g_motion_stats;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_REFR_START) {
    m.frame_areas = 0;
    const uint32_t started = micros();
    bool waiting = false;
    const uint32_t moved = step_dots(waiting);
    if (!moved) {
      if (waiting) lv_timer_resume(lv_display_get_refr_timer(g_motion_display));
      return;
    }
    const uint32_t now = micros();
    const uint32_t step = now - started;
    ++m.frames;
    m.step_us += step;
    if (step > m.step_max_us) m.step_max_us = step;
    if (moved > m.dots_max) m.dots_max = moved;
    if (m.last_ready_us && now - m.last_ready_us > m.gap_max_us) m.gap_max_us = now - m.last_ready_us;
    m.frame_start_us = now;
    m.frame_lines_us = 0;
    m.frame_areas = g_motion_display->inv_p;
    for (uint32_t i = 0; i < m.frame_areas; ++i) m.pixels += lv_area_get_size(&g_motion_display->inv_areas[i]);
  } else if (!m.frame_areas) {
    return;
  } else if (code == LV_EVENT_RENDER_START) {
    m.segment_start_us = micros();
    m.layout_us += m.segment_start_us - m.frame_start_us;
  } else if (code == LV_EVENT_FLUSH_START) {
    m.search_start_us = 0;
    m.flush_start_us = micros();
    if (m.segment_start_us) m.draw_us += m.flush_start_us - m.segment_start_us;
  } else if (code == LV_EVENT_FLUSH_FINISH) {
    m.segment_start_us = micros();
    m.flush_us += m.segment_start_us - m.flush_start_us;
    ++m.flushes;
  } else if (code == LV_EVENT_REFR_READY) {
    const uint32_t now = micros();
    if (m.segment_start_us) m.tail_us += now - m.segment_start_us;
    m.segment_start_us = 0;
    const uint32_t render = now - m.frame_start_us;
    m.render_us += render;
    if (render > m.render_max_us) m.render_max_us = render;
    m.areas += m.frame_areas;
    if (m.frame_areas > m.areas_max) m.areas_max = m.frame_areas;
    m.lines_us += m.frame_lines_us;
    if (m.frame_lines_us > m.lines_max_us) m.lines_max_us = m.frame_lines_us;
    m.frame_areas = 0;
    m.last_ready_us = now;
    log_motion(millis());
  }
}

// A flow runs: the refresh moves the dots from now on (the callbacks stay
// and return at once without a shown dot), the next frame starts them.
void wake_motion(lv_obj_t* card) {
  if (!g_motion_display) {
    g_motion_display = lv_obj_get_display(card);
    if (!g_motion_display) return;
    for (const lv_event_code_t code : {LV_EVENT_REFR_START, LV_EVENT_RENDER_START, LV_EVENT_FLUSH_START,
                                       LV_EVENT_FLUSH_FINISH, LV_EVENT_REFR_READY}) {
      lv_display_add_event_cb(g_motion_display, motion_display_cb, code, nullptr);
    }
    add_probes(card);
  }
  lv_timer_resume(lv_display_get_refr_timer(g_motion_display));
}

// Whether points first..last of a line, `width` around them, touch the area
// being drawn: a dot's step redraws a few pixels, and a piece outside them
// is not handed to LVGL at all.
bool piece_on_clip(const lv_layer_t* layer, const lv_area_t& canvas, const Polyline& path, uint8_t first,
                   uint8_t last, int width) {
  lv_area_t box = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
  for (uint8_t i = first; i <= last; ++i) {
    box.x1 = LV_MIN(box.x1, round_px(path.p[i].x));
    box.y1 = LV_MIN(box.y1, round_px(path.p[i].y));
    box.x2 = LV_MAX(box.x2, round_px(path.p[i].x));
    box.y2 = LV_MAX(box.y2, round_px(path.p[i].y));
  }
  lv_area_increase(&box, width, width);
  lv_area_move(&box, canvas.x1, canvas.y1);
  return lv_area_is_on(&box, &layer->_clip_area);
}

// Counts one drawing of a kind (0 straight, 1 bend, 2 dot) started at
// `started_us` while a dot step's frame is drawn.
void count_piece(int kind, uint32_t started_us) {
  MotionStats& m = g_motion_stats;
  if (!m.frame_areas) return;
  ++m.piece_n[kind];
  m.piece_us[kind] += micros() - started_us;
}

// Points first..last of a line as straight pieces (two or more points).
void draw_straight(lv_layer_t* layer, lv_draw_line_dsc_t& dsc, const lv_area_t& canvas, const Polyline& path,
                   uint8_t first, uint8_t last) {
  if (last <= first || !piece_on_clip(layer, canvas, path, first, last, dsc.width)) return;
  const uint32_t started_us = micros();
  lv_point_precise_t points[kMaxLinePoints];
  for (uint8_t i = first; i <= last; ++i) {
    points[i - first].x = canvas.x1 + round_px(path.p[i].x);
    points[i - first].y = canvas.y1 + round_px(path.p[i].y);
  }
  dsc.points = points;
  dsc.point_cnt = last - first + 1;
  lv_draw_line(layer, &dsc);
  count_piece(0, started_us);
}

// A line's bend: one true circle arc, the stroke centred on its radius,
// round ends; `cx`/`cy` its centre where it is drawn.
void bend_arc(const FlowTileState& st, const Polyline& path, int32_t cx, int32_t cy, uint32_t color,
              lv_draw_arc_dsc_t& dsc) {
  lv_draw_arc_dsc_init(&dsc);
  dsc.width = st.line_width;
  dsc.rounded = 1;
  dsc.color = lv_color_hex(color);
  dsc.center = {cx, cy};
  dsc.radius = static_cast<uint16_t>(path.bend.radius + st.line_width / 2);
  dsc.start_angle = path.bend.start_angle;
  dsc.end_angle = path.bend.end_angle;
}

// The box a bend's stroke covers, its round ends included (relative to the
// canvas): its quarter of the circle, a stroke width around.
lv_area_t bend_box(const FlowTileState& st, const Polyline& path) {
  const int32_t outer = path.bend.radius + st.line_width / 2;
  const int32_t cx = path.bend.cx, cy = path.bend.cy;
  const int quarter = path.bend.start_angle % 360;  // 0 right-down, 90 left-down, 180 left-up, 270 right-up
  const bool left = quarter == 90 || quarter == 180;
  const bool up = quarter >= 180;
  lv_area_t box = {left ? cx - outer : cx, up ? cy - outer : cy, left ? cx : cx + outer, up ? cy : cy + outer};
  const int32_t pad = st.line_width + 2;
  lv_area_increase(&box, pad, pad);
  return box;
}

// Off screen, never shown: draws the bend pictures.
lv_obj_t* g_bend_canvas = nullptr;

void bend_canvas_deleted(lv_event_t*) { g_bend_canvas = nullptr; }

// Draws a line's bend once into its picture in `color` (outside LVGL's
// rendering, from refresh); false without memory, the line then draws its
// arc in every frame as before.
bool render_bend(FlowTileState& st, uint8_t line, uint32_t color) {
  const Polyline& path = st.layout.line[line];
  if (!path.bend.present) return false;
  if (st.bend_picture[line] && st.bend_color[line] == color) return true;
  if (!g_bend_canvas) {
    g_bend_canvas = lv_canvas_create(nullptr);
    if (!g_bend_canvas) return false;
    lv_obj_add_event_cb(g_bend_canvas, bend_canvas_deleted, LV_EVENT_DELETE, nullptr);
  }
  const lv_area_t box = bend_box(st, path);
  lv_draw_buf_t*& picture = st.bend_picture[line];
  if (!picture) {
    picture = lv_draw_buf_create(lv_area_get_width(&box), lv_area_get_height(&box), LV_COLOR_FORMAT_ARGB8888,
                                 LV_STRIDE_AUTO);
    if (!picture) return false;
  }
  lv_image_cache_drop(picture);
  lv_draw_buf_clear(picture, nullptr);
  lv_canvas_set_draw_buf(g_bend_canvas, picture);
  lv_layer_t layer;
  lv_canvas_init_layer(g_bend_canvas, &layer);
  lv_draw_arc_dsc_t dsc;
  bend_arc(st, path, path.bend.cx - box.x1, path.bend.cy - box.y1, color, dsc);
  lv_draw_arc(&layer, &dsc);
  lv_canvas_finish_layer(g_bend_canvas, &layer);
  // The canvas keeps no reference to a picture that goes with its tile.
  lv_image_set_src(g_bend_canvas, nullptr);
  lv_image_cache_drop(picture);
  st.bend_area[line] = box;
  st.bend_color[line] = color;
  return true;
}

void canvas_draw_cb(lv_event_t* e) {
  FlowTileState* st = static_cast<FlowTileState*>(lv_event_get_user_data(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!st || !layer) return;
  const uint32_t started_us = micros();
  lv_area_t canvas;
  lv_obj_get_coords(st->canvas, &canvas);
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.width = st->line_width;
  line_dsc.round_start = 1;
  line_dsc.round_end = 1;
  // The idle lines first, the flows over them, then their dots.
  for (int pass = 0; pass < 2; ++pass) {
    for (uint8_t line = 0; line < kLineCount; ++line) {
      if (!st->line_shown[line] || st->line_live[line] != (pass == 1)) continue;
      lv_area_t box = st->line_box[line];
      lv_area_move(&box, canvas.x1, canvas.y1);
      if (!lv_area_is_on(&box, &layer->_clip_area)) continue;
      const Polyline& path = st->layout.line[line];
      const lv_color_t color = lv_color_hex(st->line_color[line]);
      line_dsc.color = color;
      if (!path.bend.present) {
        draw_straight(layer, line_dsc, canvas, path, 0, path.count - 1);
        continue;
      }
      // Straight, one true circle arc (the stroke centred on its radius),
      // straight.
      draw_straight(layer, line_dsc, canvas, path, 0, path.bend_first);
      if (piece_on_clip(layer, canvas, path, path.bend_first, path.bend_last, st->line_width)) {
        const uint32_t arc_started_us = micros();
        const lv_draw_buf_t* picture = st->bend_picture[line];
        if (picture && st->bend_color[line] == st->line_color[line]) {
          // Its picture, only the part being drawn.
          lv_draw_image_dsc_t image_dsc;
          lv_draw_image_dsc_init(&image_dsc);
          image_dsc.src = picture;
          lv_area_t area = st->bend_area[line];
          lv_area_move(&area, canvas.x1, canvas.y1);
          lv_draw_image(layer, &image_dsc, &area);
        } else {
          lv_draw_arc_dsc_t arc_dsc;
          bend_arc(*st, path, canvas.x1 + path.bend.cx, canvas.y1 + path.bend.cy, st->line_color[line], arc_dsc);
          lv_draw_arc(layer, &arc_dsc);
        }
        count_piece(1, arc_started_us);
      }
      draw_straight(layer, line_dsc, canvas, path, path.bend_last, path.count - 1);
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
    const uint32_t dot_started_us = micros();
    lv_draw_rect(layer, &dot_dsc, &area);
    count_piece(2, dot_started_us);
  }
  // Counted while a dot step's frame is drawn (the dots' log line).
  if (g_motion_stats.frame_areas) g_motion_stats.frame_lines_us += micros() - started_us;
}

// Redraws one line where it runs, its straight pieces and its bend, with its
// dot: a line switching on or off (the grid around its idle threshold) or
// changing its color redrew the whole card, 42-49 ms frames on the V2
// (b539 log).
void invalidate_line(const FlowTileState& st, uint8_t line) {
  const Polyline& path = st.layout.line[line];
  if (path.count < 2) return;
  lv_area_t canvas;
  lv_obj_get_coords(st.canvas, &canvas);
  const int margin = st.line_width + st.dot_radius + 1;
  auto invalidate = [&](uint8_t first, uint8_t last) {
    lv_area_t box = {LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN};
    for (uint8_t i = first; i <= last; ++i) {
      box.x1 = LV_MIN(box.x1, round_px(path.p[i].x));
      box.y1 = LV_MIN(box.y1, round_px(path.p[i].y));
      box.x2 = LV_MAX(box.x2, round_px(path.p[i].x));
      box.y2 = LV_MAX(box.y2, round_px(path.p[i].y));
    }
    lv_area_increase(&box, margin, margin);
    lv_area_move(&box, canvas.x1, canvas.y1);
    lv_obj_invalidate_area(st.canvas, &box);
  };
  if (!path.bend.present) {
    for (uint8_t i = 0; i + 1 < path.count; ++i) invalidate(i, i + 1);
    return;
  }
  for (uint8_t i = 0; i < path.bend_first; ++i) invalidate(i, i + 1);
  invalidate(path.bend_first, path.bend_last);
  for (uint8_t i = path.bend_last; i + 1 < path.count; ++i) invalidate(i, i + 1);
}

// Reads every node from the entity cache and shows it.
void refresh(FlowTileState& st) {
  st.dirty = false;
  const ui_theme::SurfaceScope surface = tile_surface::scope(st.card);
  Inputs in;
  in.has_pv = cached_watts(st.entities[kRolePv], in.pv);
  in.has_grid = cached_watts(st.entities[kRoleGrid], in.grid);
  in.has_battery = cached_watts(st.entities[kRoleBattery], in.battery);
  const float battery_shown = in.battery;  // as Home Assistant gives it
  in.has_home = cached_watts(st.entities[kRoleHome], in.home);
  if (st.grid_invert) in.grid = -in.grid;
  if (st.battery_invert) in.battery = -in.battery;
  const Flows flows = compute(in);

  set_text(st.node[kNodePv].value, in.has_pv ? power_text(in.pv) : String("--"));
  set_text(st.node[kNodeGrid].value, in.has_grid ? power_text(in.grid) : String("--"));
  set_text(st.node[kNodeBattery].value, in.has_battery ? signed_power_text(battery_shown) : String("--"));
  const bool home_known = st.entities[kRoleHome].length() ? in.has_home : flows.home_known;
  set_text(st.node[kNodeHome].value, home_known ? power_text(flows.home) : String("--"));
  float charge = 0.0f;
  const bool has_charge = cached_number(st.entities[kRoleBatteryCharge], charge);
  if (st.charge) {
    set_text(st.charge, has_charge ? i18n::format_number(configManager.getConfig().language, charge, 0) + " %"
                                   : String("--"));
  }
  set_icon(st.node[kNodeGrid].icon, "", grid_icon(in));
  // The battery shows its charge in the icon Home Assistant gives it; while
  // it charges (the tile's direction, past the idle threshold) the charging
  // icon for its level.
  const bool charging = in.has_battery && -in.battery >= kIdleWatts;
  const String& battery_icon_entity =
      st.entities[kRoleBatteryCharge].length() ? st.entities[kRoleBatteryCharge] : st.entities[kRoleBattery];
  set_icon(st.node[kNodeBattery].icon,
           charging ? charging_icon(has_charge, charge) : haBridgeConfig.findEntityIcon(battery_icon_entity),
           charging ? "battery-charging" : "battery");
  // Home in the color of the source that delivers most.
  color_node(st, st.node[kNodeHome],
             node_color(flows.home_source == kNodeCount ? kNodeGrid : flows.home_source));

  for (uint8_t line = 0; line < kLineCount; ++line) {
    const bool live = st.line_shown[line] && flows.watts[line] > 0.0f;
    const uint32_t color = live ? node_color(line_source(static_cast<Line>(line), flows)) : st.idle_color;
    const bool reverse = line == kLineBatteryGrid && flows.grid_to_battery;
    const bool changed = live != st.line_live[line] || color != st.line_color[line] || reverse != st.reverse[line];
    if (changed && live && (!st.line_live[line] || reverse != st.reverse[line])) st.phase[line] = 0.0f;
    st.line_live[line] = live;
    st.line_color[line] = color;
    st.reverse[line] = reverse;
    const float watts = flows.watts[line];
    st.step[line] = watts >= kStep2Watts ? 2 : 1;
    if (live) st.dot[line] = dot_point(st, line);
    if (changed && st.line_shown[line]) invalidate_line(st, line);
    if (st.line_shown[line]) render_bend(st, line, color);
  }
  if (any_live(st)) wake_motion(st.card);
}

void open_node_popup(lv_event_t* e, FlowTileState& st, uint8_t role, uint8_t node) {
  const String& entity = st.entities[role];
  if (!entity.length()) return;
  SensorPopupInit init;
  init.entity_id = entity;
  init.title = haBridgeConfig.findSensorName(entity);
  if (!init.title.length()) init.title = entity;
  String icon = normalizeMdiIconName(haBridgeConfig.findEntityIcon(entity));
  if (!icon.length()) icon = node == kNodePv ? "solar-power" : node == kNodeGrid ? "transmission-tower"
                             : node == kNodeHome ? "home-lightning-bolt" : "battery";
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
  for (lv_draw_buf_t*& picture : st->bend_picture) {
    if (!picture) continue;
    lv_image_cache_drop(picture);
    lv_draw_buf_destroy(picture);
    picture = nullptr;
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

// A node is the corner of a 1x1 tile: its disc is the tile's corner disc
// (on the V2 a square with round corners, concentric with the card corner),
// the ring around it the card corner itself (user 2026-10-24: "auf dem V2
// sind das nicht komplette Kreise"). Both take the tiles' shared radius
// styles, so they follow the tiles.
void apply_ring_shape(lv_obj_t* ring) { ui_surface_style::apply_radius(ring, tile_layout::scale_480(22), 0); }
void apply_disc_shape(lv_obj_t* disc) {
  ui_surface_style::apply_radius(disc, tile_icon_disc::radius_baseline() - tile_icon_disc::corner_shrink(), 0);
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
  apply_ring_shape(w.ring);
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
  apply_disc_shape(w.disc);
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

  set_icon(st->node[kNodePv].icon, "", "solar-power");
  set_icon(st->node[kNodeGrid].icon, "", "transmission-tower");
  set_icon(st->node[kNodeHome].icon, "", "home-lightning-bolt");
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
    apply_ring_shape(header.ring);
    lv_obj_set_style_bg_opa(header.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header.ring, lv_color_hex(card_color), 0);
    if (frame_lines) ui_surface_style::apply_global_tile_border(header.ring);
    header.disc = lv_obj_create(header.ring);
    lv_obj_remove_style_all(header.disc);
    lv_obj_remove_flag(header.disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(header.disc, disc, disc);
    lv_obj_center(header.disc);
    apply_disc_shape(header.disc);
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
