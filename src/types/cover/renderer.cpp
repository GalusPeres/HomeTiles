#include "src/ui/shared/ui_surface_style.h"
#include "src/types/cover/renderer.h"

#include <ArduinoJson.h>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/level_bar.h"
#include "src/tiles/runtime/tile_header.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/shared/command_pacer.h"

namespace {

struct CoverEventData {
  GridType grid_type = GridType::TAB0;
  uint8_t index = 0;
  String entity_id;
};

// Home Assistant's --state-cover-active-color and --state-inactive-color:
// icon and position fill take the first for every known state, closed
// included, and the second for unknown and unavailable (stateColorCss), like
// the Cover popup's sliders.
constexpr uint32_t kCoverActive = 0x926BC7;
constexpr uint32_t kCoverInactive = 0x9E9E9E;

struct CoverUpdate {
  GridType grid_type = GridType::TAB0;
  uint8_t grid_index = 0;
  String payload;
  bool valid = false;
};

constexpr uint8_t kQueueSize = 16;
CoverUpdate g_queue[kQueueSize];
volatile uint8_t g_queue_head = 0;
volatile uint8_t g_queue_tail = 0;

uint32_t fnv1a(const char* value) {
  uint32_t hash = 2166136261UL;
  if (!value) return hash;
  while (*value) {
    hash ^= static_cast<uint8_t>(*value++);
    hash *= 16777619UL;
  }
  return hash;
}

uint8_t clamp_percent(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

bool read_int(JsonVariantConst root, JsonVariantConst attrs,
              const char* key, int& value) {
  JsonVariantConst item = root[key];
  if (item.isNull() && !attrs.isNull()) item = attrs[key];
  if (item.isNull()) return false;
  if (item.is<int>() || item.is<long>() || item.is<float>() ||
      item.is<double>()) {
    value = item.as<int>();
    return true;
  }
  if (item.is<const char*>()) {
    const char* text = item.as<const char*>();
    if (!text || !*text) return false;
    char* end = nullptr;
    long parsed = strtol(text, &end, 10);
    if (end == text) return false;
    value = static_cast<int>(parsed);
    return true;
  }
  return false;
}

const char* read_text(JsonVariantConst root, JsonVariantConst attrs,
                      const char* key, const char* fallback = "") {
  const char* value = root[key] | static_cast<const char*>(nullptr);
  if ((!value || !*value) && !attrs.isNull()) {
    value = attrs[key] | static_cast<const char*>(nullptr);
  }
  return (value && *value) ? value : fallback;
}

CoverState parse_cover_payload(const char* payload) {
  CoverState out;
  if (!payload || !*payload) return out;

  DynamicJsonDocument doc(1024);
  const DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    String plain(payload);
    plain.trim();
    plain.toLowerCase();
    if (!plain.length()) return out;
    out.valid = true;
    out.available = plain != "unavailable";
    strlcpy(out.state, plain.c_str(), sizeof(out.state));
    out.supported_features =
        COVER_FEATURE_OPEN | COVER_FEATURE_CLOSE | COVER_FEATURE_STOP;
    return out;
  }

  JsonVariantConst root = doc.as<JsonVariantConst>();
  JsonVariantConst attrs = root["attributes"];
  const char* state = read_text(root, attrs, "state", "unknown");
  String normalized_state(state);
  normalized_state.trim();
  normalized_state.toLowerCase();
  strlcpy(out.state, normalized_state.c_str(), sizeof(out.state));

  JsonVariantConst available_value = root["available"];
  out.available = normalized_state != "unavailable" &&
                  (available_value.isNull() || available_value.as<bool>());
  JsonVariantConst assumed = root["assumed_state"];
  if (assumed.isNull() && !attrs.isNull()) assumed = attrs["assumed_state"];
  out.assumed_state = !assumed.isNull() && assumed.as<bool>();

  int number = 0;
  if (read_int(root, attrs, "current_position", number)) {
    out.has_position = true;
    out.position = clamp_percent(number);
  }
  if (read_int(root, attrs, "current_tilt_position", number)) {
    out.has_tilt_position = true;
    out.tilt_position = clamp_percent(number);
  }
  if (read_int(root, attrs, "supported_features", number)) {
    if (number < 0) number = 0;
    if (number > 255) number = 255;
    out.supported_features = static_cast<uint8_t>(number);
  } else {
    out.supported_features =
        COVER_FEATURE_OPEN | COVER_FEATURE_CLOSE | COVER_FEATURE_STOP;
    if (out.has_position) out.supported_features |= COVER_FEATURE_SET_POSITION;
    if (out.has_tilt_position) {
      out.supported_features |=
          COVER_FEATURE_OPEN_TILT | COVER_FEATURE_CLOSE_TILT |
          COVER_FEATURE_STOP_TILT | COVER_FEATURE_SET_TILT_POSITION;
    }
  }
  strlcpy(out.device_class,
          read_text(root, attrs, "device_class", ""),
          sizeof(out.device_class));
  out.valid = true;
  return out;
}

String fallback_icon(const CoverState& state) {
  String device_class(state.device_class);
  device_class.toLowerCase();
  const bool closed = strcmp(state.state, "closed") == 0;
  const bool closing = strcmp(state.state, "closing") == 0;
  const bool opening = strcmp(state.state, "opening") == 0;
  auto resolve = [&](const char* default_icon, const char* closed_icon,
                     const char* closing_icon = nullptr,
                     const char* opening_icon = nullptr) -> String {
    if (closed && closed_icon) return closed_icon;
    if (closing && closing_icon) return closing_icon;
    if (opening && opening_icon) return opening_icon;
    return default_icon;
  };

  if (device_class == "blind") {
    return resolve("blinds-horizontal", "blinds-horizontal-closed",
                   "arrow-down-box", "arrow-up-box");
  }
  if (device_class == "curtain") {
    return resolve("curtains", "curtains-closed",
                   "arrow-collapse-horizontal", "arrow-split-vertical");
  }
  if (device_class == "damper") {
    return resolve("circle", "circle-slice-8");
  }
  if (device_class == "door") {
    return resolve("door-open", "door-closed");
  }
  if (device_class == "garage") {
    return resolve("garage-open", "garage", "arrow-down-box",
                   "arrow-up-box");
  }
  if (device_class == "gate") {
    return resolve("gate-open", "gate", "arrow-right", "arrow-right");
  }
  if (device_class == "shade") {
    return resolve("roller-shade", "roller-shade-closed",
                   "arrow-down-box", "arrow-up-box");
  }
  if (device_class == "shutter") {
    return resolve("window-shutter-open", "window-shutter",
                   "arrow-down-box", "arrow-up-box");
  }
  if (device_class == "window") {
    return resolve("window-open", "window-closed", "arrow-down-box",
                   "arrow-up-box");
  }
  return resolve("window-open", "window-closed", "arrow-down-box",
                 "arrow-up-box");
}

bool is_standard_cover_icon(const String& icon_name) {
  static constexpr const char* kStateIcons[] = {
      "window-open",
      "window-closed",
      "arrow-down-box",
      "arrow-up-box",
      "blinds-horizontal",
      "blinds-horizontal-closed",
      "curtains",
      "curtains-closed",
      "arrow-collapse-horizontal",
      "arrow-split-vertical",
      "circle",
      "circle-slice-8",
      "door-open",
      "door-closed",
      "garage-open",
      "garage",
      "gate-open",
      "gate",
      "arrow-right",
      "roller-shade",
      "roller-shade-closed",
      "window-shutter-open",
      "window-shutter",
  };
  const String normalized = normalizeMdiIconName(icon_name);
  for (const char* state_icon : kStateIcons) {
    if (normalized.equalsIgnoreCase(state_icon)) return true;
  }
  return false;
}

// Every known, available state, closed included, uses Home Assistant's
// --state-cover-active-color (#926bc7) like the position bar: the icon itself
// already shows open or closed (user 2026-10-01). Unknown and unavailable
// Covers use --state-inactive-color (#9e9e9e).
bool cover_icon_active(const CoverState& state) {
  return state.valid && state.available && strcmp(state.state, "unknown") != 0 &&
         strcmp(state.state, "unavailable") != 0;
}

uint32_t cover_icon_color(const CoverState& state) {
  return cover_icon_active(state) ? kCoverActive : kCoverInactive;
}

String cover_value_text(const CoverState& state) {
  const String display_state = state.available ? String(state.state)
                                                : String("unavailable");
  String text = state.valid
                    ? String(i18n::cover_state_label(
                          configManager.getConfig().language, display_state))
                    : String("--");
  text += '\n';
  text += state.has_position ? String(state.position) + "%" : String("--%");
  return text;
}

// The header's state line, like Home Assistant's tile card: "Open · 58 %".
String cover_state_line(const CoverState& state, bool has_position, uint8_t position) {
  if (!state.valid) return "--";
  const String display_state = state.available ? String(state.state) : String("unavailable");
  String text = i18n::cover_state_label(configManager.getConfig().language, display_state);
  if (state.available && has_position) {
    text += " \xC2\xB7 ";
    text += String(position);
    text += " %";
  }
  return text;
}

// The bar shows the closed part like Home Assistant's cover position feature
// and the popup's shutter (user 2026-10-01): 75 % open fills a quarter, fully
// closed fills the bar, fully open keeps the smallest piece with the handle.
uint8_t cover_fill_level(uint8_t position) { return position >= 99 ? 1 : static_cast<uint8_t>(100 - position); }

// The position under a touch point: the far left (level 0 or 1) is fully
// open.
uint8_t cover_position_at(uint8_t level) { return level <= 1 ? 100 : static_cast<uint8_t>(100 - level); }

// The state line through the shared header (tile_header::set_state): full
// tiles step the font down instead of shortening the text.
void set_state_line(CoverTileWidgets& widget, const String& line) {
  tile_header::set_state(widget.state_label, line.c_str(), widget.compact ? nullptr : widget.state_font,
                         widget.state_width, widget.state_center);
}

// ---------------------------------------------------------------------------
// Position bar: the Switch dimmer's box, drawing and touch mapping
// (level_bar.h): the press jumps to the finger and pressing follows. Unlike
// the dimmer, the position goes out once, on release, like Home Assistant's
// cover position slider (ha-state-control-cover-position: value-changed
// only): commands while dragging restart a motor every time, and a Cover that
// answers each command at once (a template without state) showed the target
// as its state before it moved. Releases stay command_pacer::kIntervalMs
// apart, and the tile holds its own bar value for kRemoteBlockMs against
// echoes of earlier commands. State line and icon show only what Home
// Assistant reports.

constexpr uint32_t kRemoteBlockMs = 3000;

struct CoverDrag {
  CoverEventData* data = nullptr;
  lv_point_t press = {0, 0};
  bool dragging = false;
  uint8_t value = 0;
  uint32_t block_until = 0;
};

CoverDrag g_drag;
command_pacer::Pacer g_pacer;
lv_timer_t* g_final_timer = nullptr;
lv_timer_t* g_release_timer = nullptr;
String g_final_entity;
uint8_t g_final_value = 0;

void show_view(GridType grid_type, uint8_t index);

// The tile's own position while the finger holds the bar or released it
// moments ago.
bool held_value(GridType grid_type, uint8_t index, uint8_t& value) {
  if (!g_drag.data || g_drag.data->grid_type != grid_type || g_drag.data->index != index) return false;
  if (!g_drag.dragging && static_cast<int32_t>(g_drag.block_until - millis()) <= 0) return false;
  value = g_drag.value;
  return true;
}

void release_timer_cb(lv_timer_t*) {
  g_release_timer = nullptr;
  CoverEventData* data = g_drag.data;
  if (!data || g_drag.dragging) return;
  g_drag.block_until = millis();
  show_view(data->grid_type, data->index);
}

void start_hold() {
  g_drag.dragging = false;
  g_drag.block_until = millis() + kRemoteBlockMs;
  if (g_release_timer) lv_timer_delete(g_release_timer);
  g_release_timer = lv_timer_create(release_timer_cb, kRemoteBlockMs, nullptr);
  if (g_release_timer) lv_timer_set_repeat_count(g_release_timer, 1);
}

void send_position(const String& entity_id, uint8_t value) {
  if (entity_id.length()) mqttPublishCoverCommand(entity_id.c_str(), "set_cover_position", value);
  g_pacer.sent(millis(), value);
}

void final_timer_cb(lv_timer_t*) {
  g_final_timer = nullptr;
  send_position(g_final_entity, g_final_value);
}

// Release: the one command of the gesture, at least one pacer interval after
// the previous release.
void commit_position(const String& entity_id, uint8_t value) {
  if (g_final_timer && !g_final_entity.equalsIgnoreCase(entity_id)) {
    lv_timer_delete(g_final_timer);
    g_final_timer = nullptr;
    send_position(g_final_entity, g_final_value);
  }
  const uint32_t wait = g_pacer.wait(millis());
  if (wait == 0) {
    if (g_final_timer) {
      lv_timer_delete(g_final_timer);
      g_final_timer = nullptr;
    }
    send_position(entity_id, value);
    return;
  }
  g_final_entity = entity_id;
  g_final_value = value;
  if (g_final_timer) return;
  g_final_timer = lv_timer_create(final_timer_cb, wait, nullptr);
  if (g_final_timer) {
    lv_timer_set_repeat_count(g_final_timer, 1);
  } else {
    send_position(entity_id, value);
  }
}

void bar_draw_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
  CoverTileWidgets* widget = static_cast<CoverTileWidgets*>(lv_event_get_user_data(e));
  if (!widget || !widget->bar || !widget->available) return;
  const lv_color_t card = lv_obj_get_style_bg_color(lv_obj_get_parent(widget->bar), LV_PART_MAIN);
  level_bar::draw_fill(lv_event_get_layer(e), widget->bar, widget->level, widget->bar_base,
                       lv_color_hex(widget->fill_color), card);
}

// A drag step: the bar and the state line follow the finger; only the
// changed columns redraw (level_bar::invalidate_change).
void show_local_position(CoverEventData* data, CoverTileWidgets& widget, uint8_t value) {
  const uint8_t fill = cover_fill_level(value);
  if (widget.level != fill) {
    const uint8_t old_level = widget.level;
    widget.level = fill;
    level_bar::invalidate_change(widget.bar, widget.bar_base, old_level, fill);
  }
  if (widget.state_label) {
    const CoverState& state = tile_renderer_get_cover_states(data->grid_type)[data->index];
    set_state_line(widget, cover_state_line(state, true, value));
  }
}

void bar_event_cb(lv_event_t* e) {
  CoverEventData* data = static_cast<CoverEventData*>(lv_event_get_user_data(e));
  if (!data || !data->entity_id.length() || data->index >= TILES_PER_GRID) return;
  CoverTileWidgets& widget = tile_renderer_get_cover_widgets(data->grid_type)[data->index];
  if (!widget.bar) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (!widget.available) {
    if (g_drag.data == data) g_drag.dragging = false;
    return;
  }
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t point = g_drag.press;
  if (indev) lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    g_drag.data = data;
    g_drag.press = point;
    g_drag.dragging = true;
  }
  if (g_drag.data != data || !g_drag.dragging) return;
  const uint8_t value = cover_position_at(level_bar::value_at(widget.bar, widget.bar_base, point));
  const bool changed = value != g_drag.value || code == LV_EVENT_PRESSED;
  g_drag.value = value;
  if (changed) show_local_position(data, widget, value);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    start_hold();
    commit_position(data->entity_id, value);
  }
}

CoverPopupInit popup_init(GridType grid_type, uint8_t index) {
  CoverPopupInit init;
  const Tile* tile = tile_renderer_get_tile_config(grid_type, index);
  if (!tile || tile->type != TILE_COVER) return init;
  init.entity_id = tile->sensor_entity;
  init.title = tile->title.length() ? tile->title : tile->sensor_entity;
  init.state = tile_renderer_get_cover_states(grid_type)[index];
  init.icon_visible = !isMdiIconDisabled(tile->icon_name);
  init.icon_name = cover_resolve_icon(*tile, init.state);
  return init;
}

void apply_state(GridType grid_type, uint8_t index, const char* payload) {
  if (index >= TILES_PER_GRID || !payload) return;
  CoverTileWidgets* widgets = tile_renderer_get_cover_widgets(grid_type);
  CoverState* states = tile_renderer_get_cover_states(grid_type);
  CoverTileWidgets& widget = widgets[index];
  const uint32_t hash = fnv1a(payload);
  if (hash == widget.last_payload_hash) return;

  CoverState state = parse_cover_payload(payload);
  if (!state.valid) return;
  widget.last_payload_hash = hash;
  states[index] = state;

  show_view(grid_type, index);
  if (widget.icon_label) {
    tile_icon_disc::set_icon_color(
        widget.icon_label, lv_color_hex(cover_icon_color(state)));
    if (widget.dynamic_icon) {
      lv_label_set_text(
          widget.icon_label, getMdiChar(fallback_icon(state)).c_str());
    }
  }
  update_cover_popup(popup_init(grid_type, index));
}

// Value text, state line and position bar for the reported state; a bar the
// finger holds (or released moments ago) keeps its own position.
void show_view(GridType grid_type, uint8_t index) {
  if (index >= TILES_PER_GRID) return;
  CoverTileWidgets& widget = tile_renderer_get_cover_widgets(grid_type)[index];
  const CoverState& state = tile_renderer_get_cover_states(grid_type)[index];
  if (widget.value_label) {
    const String value = cover_value_text(state);
    lv_label_set_text(widget.value_label, value.c_str());
  }
  uint8_t position = state.position;
  bool has_position = state.has_position;
  uint8_t held = 0;
  if (state.available && held_value(grid_type, index, held)) {
    position = held;
    has_position = true;
  }
  const uint8_t level = state.available && has_position ? cover_fill_level(position) : 0;
  if (widget.state_label) set_state_line(widget, cover_state_line(state, has_position, position));
  if (!widget.bar) return;
  // A Cover that reports no position control keeps the header without a bar.
  const bool positionable = !state.valid || (state.supported_features & COVER_FEATURE_SET_POSITION);
  if (lv_obj_has_flag(widget.bar, LV_OBJ_FLAG_HIDDEN) == positionable) {
    if (positionable) lv_obj_remove_flag(widget.bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(widget.bar, LV_OBJ_FLAG_HIDDEN);
  }
  const uint32_t fill_color = cover_icon_color(state);
  if (widget.level != level || widget.available != state.available || widget.fill_color != fill_color) {
    widget.level = level;
    widget.available = state.available;
    widget.fill_color = fill_color;
    lv_obj_invalidate(widget.bar);
  }
}

}  // namespace

String cover_resolve_icon(const Tile& tile, const CoverState& state,
                          bool* dynamic_icon) {
  if (dynamic_icon) *dynamic_icon = false;
  if (isMdiIconDisabled(tile.icon_name)) return "";

  String icon = normalizeMdiIconName(tile.icon_name);
  if (icon.length()) return icon;

  icon = normalizeMdiIconName(
      haBridgeConfig.findEntityIcon(tile.sensor_entity));
  // Bridge v0.6.34/v0.6.35 publishes Home Assistant's state-dependent Cover
  // fallback through the asynchronous icon topic. Treat those known standard
  // icons as dynamic and derive them atomically from the retained Cover state.
  // A non-standard registry/entity icon remains a fixed user override.
  if (icon.length() && !is_standard_cover_icon(icon)) return icon;

  if (dynamic_icon) *dynamic_icon = true;
  return fallback_icon(state);
}

void refresh_cover_popup_for_tile(GridType grid_type, uint8_t index) {
  update_cover_popup(popup_init(grid_type, index));
}

lv_obj_t* render_cover_tile(lv_obj_t* parent, int col, int row,
                            const Tile& tile, uint8_t index,
                            GridType grid_type) {
  lv_obj_t* card = lv_button_create(parent);
  const uint32_t color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(
      card, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(
      card, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(
      card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  const uint32_t pressed_color = brighten_rgb_color(color, 0x10);
  lv_obj_set_style_bg_color(
      card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(
      card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(
      card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  CoverTileWidgets& widget =
      tile_renderer_get_cover_widgets(grid_type)[index];
  CoverState& state = tile_renderer_get_cover_states(grid_type)[index];
  widget = {};

  // Half height: the Sensor compact layout. A full tile of a Cover with a
  // position (or not reported yet): the header with the state line and the
  // position bar below (user 2026-10-01, like Home Assistant's tile card
  // with the cover position feature). A Cover without a position keeps the
  // centered state and position.
  const bool compact = tile_geometry::compact_cover(tile.type, tile.span_w, tile.span_h);
  const String initial = tile.sensor_entity.length()
                             ? haBridgeConfig.findSensorInitialValue(tile.sensor_entity)
                             : String();
  const CoverState reported = initial.length() ? parse_cover_payload(initial.c_str()) : CoverState{};
  const bool positionable = !reported.valid || (reported.supported_features & COVER_FEATURE_SET_POSITION);
  const bool header = compact || positionable;

  const bool icon_visible = !isMdiIconDisabled(tile.icon_name);
  String configured_icon =
      cover_resolve_icon(tile, state, &widget.dynamic_icon);
  widget.dynamic_icon = icon_visible && widget.dynamic_icon;

  if (icon_visible && FONT_MDI_ICONS) {
    widget.icon_label = lv_label_create(card);
    set_label_style(widget.icon_label,
                    lv_color_hex(cover_icon_color(state)), FONT_MDI_ICONS);
    lv_label_set_text(widget.icon_label, getMdiChar(configured_icon).c_str());
    lv_obj_align(widget.icon_label, LV_ALIGN_TOP_LEFT,
                 tile_layout::scale_480(-8),
                 tile_layout::scale_480(-8));
  }

  CoverEventData* data = nullptr;
  if (tile.sensor_entity.length()) {
    data = new CoverEventData{grid_type, index, tile.sensor_entity};
  }

  if (header) {
    // The shared header (tile_header.h) like the Switch tile: title and
    // state beside the disc; from 1.5 rows the title top right and the state
    // at the chosen Sensor value size centered between the disc and the bar.
    const bool tall = !compact && tile.span_h > 1.0f;
    const tile_header::Header text = tile_header::create(card, tile, tall, level_bar::box(tile).top);
    widget.title_label = text.title;
    widget.state_label = text.state;
    widget.state_font = text.state_font;
    widget.state_width = text.state_width;
    widget.state_center = text.state_center;
    widget.compact = compact;
    if (compact) {
      compact_sensor_layout::apply(card, widget.icon_label, widget.title_label, widget.state_label, tile);
    } else {
      if (widget.icon_label) tile_icon_disc::add_round(card, widget.icon_label);
      widget.bar = level_bar::create(card, tile);
      widget.bar_base = static_cast<int16_t>(level_bar::box(tile).base);
      if (widget.bar) {
        lv_obj_add_event_cb(widget.bar, bar_draw_cb, LV_EVENT_DRAW_MAIN, &widget);
        if (data && grid_type != GridType::SCREENSAVER) {
          // Only these codes: the bar's own DELETE comes after the card freed
          // the event data.
          for (const lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                             LV_EVENT_PRESS_LOST}) {
            lv_obj_add_event_cb(widget.bar, bar_event_cb, code, data);
          }
        } else {
          lv_obj_remove_flag(widget.bar, LV_OBJ_FLAG_CLICKABLE);
        }
        tile_icon_source::refresh_controls(card);
      }
    }
  }

  if (!header && tile.title.length()) {
    widget.title_label = lv_label_create(card);
    set_label_style(widget.title_label, lv_color_white(),
                    tile_layout::header_title_font());
    lv_obj_set_width(widget.title_label, LV_PCT(70));
    lv_obj_set_style_text_align(widget.title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(widget.title_label, LV_LABEL_LONG_DOT);
    hometiles_title::tile(widget.title_label, tile.title.c_str(), true);
    lv_obj_align(widget.title_label, LV_ALIGN_TOP_RIGHT,
                 tile_layout::scale_480(4),
                 tile_layout::scale_480(4));
  }
  if (!header) {
    // After the title exists, so the disc can lift the whole header.
    if (widget.icon_label) tile_icon_disc::add_round(card, widget.icon_label);

    // Same value block as a Sensor tile, but with the HA Cover state and
    // position on two lines (for example "Open\n40%").
    widget.value_label = lv_label_create(card);
    set_label_style(widget.value_label, lv_color_white(),
                    tile_layout::header_title_font());
    lv_label_set_long_mode(widget.value_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(widget.value_label, LV_PCT(100));
    lv_obj_set_style_text_align(widget.value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(widget.value_label, 8, 0);
    const String initial_value = cover_value_text(state);
    lv_label_set_text(widget.value_label, initial_value.c_str());
    lv_obj_align(widget.value_label, LV_ALIGN_CENTER, 0,
                 tile_layout::scale(28));
  }

  if (initial.length()) apply_state(grid_type, index, initial.c_str());

  if (data && grid_type != GridType::SCREENSAVER) {
    const lv_event_code_t event_code =
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          const lv_event_code_t code = lv_event_get_code(event);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          CoverEventData* data = static_cast<CoverEventData*>(
              lv_event_get_user_data(event));
          if (!data) return;
          const Tile* tile =
              tile_renderer_get_tile_config(data->grid_type, data->index);
          CoverPopupInit init = popup_init(data->grid_type, data->index);
          if (!tile || !init.entity_id.length()) return;
          // For now the popup keeps the global tile color and does not follow the
          // tile (tile_icon_source::forget_popup_source); icon and circle match it.
          init.bg_color = tileDefaultBgColor();
          tile_icon_source::forget_popup_source(static_cast<lv_obj_t*>(lv_event_get_current_target(event)));
          finish_press_before_popup(event);
          show_cover_popup(init);
        },
        event_code, data);
  }
  if (data) {
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
          CoverEventData* data = static_cast<CoverEventData*>(lv_event_get_user_data(event));
          if (g_drag.data == data) {
            if (g_release_timer) {
              lv_timer_delete(g_release_timer);
              g_release_timer = nullptr;
            }
            g_drag = CoverDrag{};
          }
          delete data;
        },
        LV_EVENT_DELETE, data);
  }
  return card;
}

void queue_cover_tile_update(GridType grid_type, uint8_t grid_index,
                             const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  uint8_t cursor = g_queue_tail;
  while (cursor != g_queue_head) {
    CoverUpdate& pending = g_queue[cursor];
    if (pending.valid && pending.grid_type == grid_type &&
        pending.grid_index == grid_index) {
      pending.payload = payload;
      return;
    }
    cursor = (cursor + 1) % kQueueSize;
  }
  const uint8_t next = (g_queue_head + 1) % kQueueSize;
  if (next == g_queue_tail) {
    g_queue[g_queue_tail].payload = static_cast<const char*>(nullptr);
    g_queue[g_queue_tail].valid = false;
    g_queue_tail = (g_queue_tail + 1) % kQueueSize;
    Serial.println("[Queue] Cover full, oldest update replaced");
  }
  CoverUpdate& update = g_queue[g_queue_head];
  update.grid_type = grid_type;
  update.grid_index = grid_index;
  update.payload = payload;
  update.valid = true;
  g_queue_head = next;
}

void process_cover_update_queue(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_queue_tail != g_queue_head &&
         (max_updates == 0 || processed < max_updates)) {
    CoverUpdate& update = g_queue[g_queue_tail];
    if (update.valid) {
      apply_state(update.grid_type, update.grid_index,
                  update.payload.c_str());
      update.valid = false;
      ++processed;
    }
    update.payload = static_cast<const char*>(nullptr);
    g_queue_tail = (g_queue_tail + 1) % kQueueSize;
  }
}

bool cover_payload_icon_color(const char* payload, uint32_t& rgb, bool* active) {
  if (!payload || !*payload) return false;
  const CoverState state = parse_cover_payload(payload);
  if (!state.valid || !state.available) return false;
  rgb = cover_icon_color(state);
  // Active exactly when cover_icon_color() shows the active color.
  if (active) *active = cover_icon_active(state);
  return true;
}
