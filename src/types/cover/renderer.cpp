#include "src/types/cover/renderer.h"

#include <ArduinoJson.h>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/cover/cover_popup.h"

namespace {

struct CoverEventData {
  GridType grid_type = GridType::TAB0;
  uint8_t index = 0;
};

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

uint32_t cover_icon_color(const CoverState& state) {
  // Home Assistant's default theme uses --state-cover-active-color
  // (#926bc7) for every available Cover state except "closed". Unknown,
  // unavailable and closed Covers use --state-inactive-color (#9e9e9e).
  if (!state.valid || !state.available ||
      strcmp(state.state, "unknown") == 0 ||
      strcmp(state.state, "unavailable") == 0 ||
      strcmp(state.state, "closed") == 0) {
    return 0x9E9E9E;
  }
  return 0x926BC7;
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

  if (widget.value_label) {
    const String value = cover_value_text(state);
    lv_label_set_text(widget.value_label, value.c_str());
  }
  if (widget.icon_label) {
    lv_obj_set_style_text_color(
        widget.icon_label, lv_color_hex(cover_icon_color(state)), 0);
    if (widget.dynamic_icon) {
      lv_label_set_text(
          widget.icon_label, getMdiChar(fallback_icon(state)).c_str());
    }
  }
  update_cover_popup(popup_init(grid_type, index));
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
  const uint32_t color = tileBgColorOrDefault(tile, 0x2A2A2A);
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
  lv_obj_set_style_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

  CoverTileWidgets& widget =
      tile_renderer_get_cover_widgets(grid_type)[index];
  CoverState& state = tile_renderer_get_cover_states(grid_type)[index];
  widget = {};

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

  if (tile.title.length()) {
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

  if (tile.sensor_entity.length()) {
    String initial = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    if (initial.length()) apply_state(grid_type, index, initial.c_str());
  }

  if (grid_type != GridType::SCREENSAVER && tile.sensor_entity.length()) {
    CoverEventData* data = new CoverEventData{grid_type, index};
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
          CoverPopupInit init = popup_init(data->grid_type, data->index);
          if (!init.entity_id.length()) return;
          finish_press_before_popup(event);
          show_cover_popup(init);
        },
        event_code, data);
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          if (lv_event_get_code(event) == LV_EVENT_DELETE) {
            delete static_cast<CoverEventData*>(lv_event_get_user_data(event));
          }
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
    g_queue_tail = (g_queue_tail + 1) % kQueueSize;
  }
}
