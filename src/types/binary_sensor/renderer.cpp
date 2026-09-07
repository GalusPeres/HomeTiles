#include "src/types/binary_sensor/renderer.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/binary_sensor/binary_sensor_popup.h"

namespace {

struct BinarySensorEventData {
  GridType grid_type = GridType::TAB0;
  uint8_t index = 0;
};

struct BinarySensorUpdate {
  GridType grid_type = GridType::TAB0;
  uint64_t grid_indices = 0;
  String entity_id;
  String payload;
  BinarySensorState parsed_state;
  uint32_t layout_generation = 0;
  bool require_entity_match = false;
  bool parsed = false;
  bool valid = false;
};

struct BinarySensorIconPair {
  const char* device_class;
  const char* off_icon;
  const char* on_icon;
};

// These pairs mirror Home Assistant Core's binary_sensor icon translations.
constexpr BinarySensorIconPair kIconPairs[] = {
    {"", "radiobox-blank", "checkbox-marked-circle"},
    {"battery", "battery", "battery-outline"},
    {"battery_charging", "battery", "battery-charging"},
    {"carbon_monoxide", "smoke-detector", "smoke-detector-alert"},
    {"cold", "thermometer", "snowflake"},
    {"connectivity", "close-network-outline", "check-network-outline"},
    {"door", "door-closed", "door-open"},
    {"garage_door", "garage", "garage-open"},
    {"gas", "check-circle", "alert-circle"},
    {"heat", "thermometer", "fire"},
    {"light", "brightness-5", "brightness-7"},
    {"lock", "lock", "lock-open"},
    {"moisture", "water-off", "water"},
    {"motion", "motion-sensor-off", "motion-sensor"},
    {"moving", "octagon", "arrow-right"},
    {"occupancy", "home-outline", "home"},
    {"opening", "square", "square-outline"},
    {"plug", "power-plug-off", "power-plug"},
    {"power", "power-plug-off", "power-plug"},
    {"presence", "home-outline", "home"},
    {"problem", "check-circle", "alert-circle"},
    {"running", "stop", "play"},
    {"safety", "check-circle", "alert-circle"},
    {"smoke", "smoke-detector-variant", "smoke-detector-variant-alert"},
    {"sound", "music-note-off", "music-note"},
    {"tamper", "check-circle", "alert-circle"},
    {"update", "package", "package-up"},
    {"vibration", "crop-portrait", "vibrate"},
    {"window", "window-closed", "window-open"},
};

constexpr uint8_t kGridCount = 4;
constexpr uint8_t kQueueSize = 32;
static_assert(TILES_PER_GRID <= 64,
              "Binary sensor update masks require at most 64 tile slots");

BinarySensorTileWidgets g_widgets[kGridCount][TILES_PER_GRID];
BinarySensorState g_states[kGridCount][TILES_PER_GRID];
uint32_t g_layout_generation[kGridCount] = {1, 1, 1, 1};
BinarySensorUpdate g_queue[kQueueSize];
volatile uint8_t g_queue_head = 0;
volatile uint8_t g_queue_tail = 0;
uint32_t g_queue_overflow_count = 0;

uint8_t grid_index(GridType grid_type) {
  const uint8_t index = static_cast<uint8_t>(grid_type);
  return index < kGridCount ? index : 0;
}

uint32_t layout_generation(GridType grid_type) {
  return g_layout_generation[grid_index(grid_type)];
}

void advance_layout_generation(GridType grid_type) {
  uint32_t& generation = g_layout_generation[grid_index(grid_type)];
  if (++generation == 0) generation = 1;
}

uint32_t fnv1a(const char* value) {
  uint32_t hash = 2166136261UL;
  if (!value) return hash;
  while (*value) {
    hash ^= static_cast<uint8_t>(*value++);
    hash *= 16777619UL;
  }
  return hash;
}

bool parse_state_value(const String& input, BinarySensorValue& value) {
  String state = input;
  state.trim();
  state.toLowerCase();
  if (state == "off") {
    value = BinarySensorValue::Off;
    return true;
  }
  if (state == "on") {
    value = BinarySensorValue::On;
    return true;
  }
  if (state == "unknown") {
    value = BinarySensorValue::Unknown;
    return true;
  }
  if (state == "unavailable") {
    value = BinarySensorValue::Unavailable;
    return true;
  }
  return false;
}

const BinarySensorIconPair& icon_pair(const BinarySensorState& state) {
  String device_class(state.device_class);
  device_class.trim();
  device_class.toLowerCase();
  for (size_t i = 1; i < sizeof(kIconPairs) / sizeof(kIconPairs[0]); ++i) {
    if (device_class == kIconPairs[i].device_class) return kIconPairs[i];
  }
  return kIconPairs[0];
}

String fallback_icon(const BinarySensorState& state) {
  const BinarySensorIconPair& pair = icon_pair(state);
  const bool active = state.valid && state.available &&
                      state.value == BinarySensorValue::On;
  return active ? String(pair.on_icon) : String(pair.off_icon);
}

String state_label(const BinarySensorState& state) {
  if (!state.valid || state.value == BinarySensorValue::Missing) return "--";
  const String display_state = state.available
                                   ? String(binary_sensor_state_name(state.value))
                                   : String("unavailable");
  return i18n::binary_sensor_state_label(
      configManager.getConfig().language, display_state,
      String(state.device_class));
}

bool has_explicit_icon_setting(const Tile& tile) {
  String setting = tile.icon_name;
  setting.trim();
  return setting.length() > 0;
}

BinarySensorPopupInit popup_init(GridType grid_type, uint8_t index) {
  BinarySensorPopupInit init;
  const Tile* tile = tile_renderer_get_tile_config(grid_type, index);
  if (!tile || tile->type != TILE_BINARY_SENSOR ||
      index >= TILES_PER_GRID) {
    return init;
  }

  const BinarySensorState& state =
      tile_renderer_get_binary_sensor_states(grid_type)[index];
  init.entity_id = tile->sensor_entity;
  init.title = tile->title;
  if (!init.title.length()) {
    init.title = haBridgeConfig.findSensorName(tile->sensor_entity);
  }
  if (!init.title.length()) init.title = tile->sensor_entity;
  init.icon_name = binary_sensor_resolve_icon(*tile, state);
  init.state = state.valid ? binary_sensor_state_name(state.value) : "";
  init.device_class = state.device_class;
  init.last_changed = state.has_last_changed ? state.last_changed : 0;
  init.available = state.valid ? state.available : true;
  init.icon_override = has_explicit_icon_setting(*tile);
  init.bg_color = tileBgColorOrDefault(*tile, 0x2A2A2A);
  return init;
}

void apply_state(GridType grid_type, uint8_t index,
                 const BinarySensorState& state, uint32_t payload_hash) {
  if (index >= TILES_PER_GRID || !state.valid) return;
  BinarySensorTileWidgets& widgets =
      tile_renderer_get_binary_sensor_widgets(grid_type)[index];
  if (widgets.last_payload_hash == payload_hash) return;

  widgets.last_payload_hash = payload_hash;
  tile_renderer_get_binary_sensor_states(grid_type)[index] = state;
  if (widgets.state_label) {
    const String label = state_label(state);
    lv_label_set_text(widgets.state_label, label.c_str());
  }
  if (widgets.icon_label) {
    lv_obj_set_style_text_color(
        widgets.icon_label,
        lv_color_hex(binary_sensor_visual_color(state)), 0);
    if (widgets.dynamic_icon) {
      const Tile* tile = tile_renderer_get_tile_config(grid_type, index);
      if (tile) {
        const String icon = binary_sensor_resolve_icon(*tile, state);
        if (icon.length()) {
          lv_label_set_text(widgets.icon_label, getMdiChar(icon).c_str());
          lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
  }
}

void invalidate_queued_slot(GridType grid_type, uint8_t index) {
  if (index >= TILES_PER_GRID) return;
  const uint64_t bit = uint64_t{1} << index;
  uint8_t cursor = g_queue_tail;
  while (cursor != g_queue_head) {
    BinarySensorUpdate& pending = g_queue[cursor];
    if (pending.valid && pending.grid_type == grid_type) {
      pending.grid_indices &= ~bit;
    }
    cursor = (cursor + 1) % kQueueSize;
  }
}

uint64_t capture_targets(GridType grid_type, uint64_t indices,
                         String& entity_id) {
  entity_id.remove(0);
  uint64_t verified = 0;
  for (uint8_t index = 0; index < TILES_PER_GRID; ++index) {
    const uint64_t bit = uint64_t{1} << index;
    if ((indices & bit) == 0) continue;
    const Tile* tile = tile_renderer_get_tile_config(grid_type, index);
    if (!tile || tile->type != TILE_BINARY_SENSOR ||
        !tile->sensor_entity.length()) {
      continue;
    }
    if (!entity_id.length()) entity_id = tile->sensor_entity;
    if (!tile->sensor_entity.equalsIgnoreCase(entity_id)) continue;
    verified |= bit;
  }
  return verified;
}

void enqueue_update(GridType grid_type, uint64_t indices,
                    const String& entity_id, bool require_entity_match,
                    const char* payload) {
  if (indices == 0 || !payload) return;
  const uint32_t generation = layout_generation(grid_type);

  uint8_t cursor = g_queue_tail;
  while (cursor != g_queue_head) {
    BinarySensorUpdate& pending = g_queue[cursor];
    const bool same_target =
        pending.require_entity_match == require_entity_match &&
        (require_entity_match
             ? pending.entity_id.equalsIgnoreCase(entity_id)
             : pending.grid_indices == indices);
    if (pending.valid && pending.grid_type == grid_type && same_target) {
      pending.grid_indices = indices;
      pending.entity_id = entity_id;
      pending.payload = payload;
      pending.layout_generation = generation;
      pending.parsed = false;
      return;
    }
    cursor = (cursor + 1) % kQueueSize;
  }

  const uint8_t next = (g_queue_head + 1) % kQueueSize;
  if (next == g_queue_tail) {
    if ((g_queue_overflow_count++ % 10) == 0) {
      Serial.println(
          "[Queue] Full; replacing the oldest binary sensor update");
    }
    g_queue_tail = (g_queue_tail + 1) % kQueueSize;
  }

  BinarySensorUpdate& update = g_queue[g_queue_head];
  update.grid_type = grid_type;
  update.grid_indices = indices;
  update.entity_id = entity_id;
  update.payload = payload;
  update.layout_generation = generation;
  update.require_entity_match = require_entity_match;
  update.parsed = false;
  update.valid = true;
  g_queue_head = next;
}

}  // namespace

BinarySensorState parse_binary_sensor_payload(const char* payload) {
  BinarySensorState out;
  if (!payload || !*payload) return out;
  if (strnlen(payload, BINARY_SENSOR_PAYLOAD_MAX + 1) >
      BINARY_SENSOR_PAYLOAD_MAX) {
    return out;
  }

  DynamicJsonDocument doc(1024);
  const DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    BinarySensorValue value = BinarySensorValue::Missing;
    if (!parse_state_value(String(payload), value)) return out;
    out.valid = true;
    out.value = value;
    out.available = value != BinarySensorValue::Unavailable;
    return out;
  }

  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return out;
  BinarySensorValue value = BinarySensorValue::Missing;
  JsonVariantConst state_variant = root["state"];
  if (!root.containsKey("state")) return out;
  if (!state_variant.isNull()) {
    const char* raw_state =
        state_variant | static_cast<const char*>(nullptr);
    if (!raw_state || !parse_state_value(String(raw_state), value)) return out;
  }

  out.valid = true;
  out.value = value;
  out.available = value != BinarySensorValue::Missing &&
                  value != BinarySensorValue::Unavailable;

  JsonVariantConst available = root["available"];
  if (!available.isNull() && available.is<bool>()) {
    out.has_available = true;
    out.available = available.as<bool>();
  }
  if (value == BinarySensorValue::Missing ||
      value == BinarySensorValue::Unavailable) {
    out.available = false;
  }

  JsonVariantConst device_class = root["device_class"];
  if (!device_class.isNull() && device_class.is<const char*>()) {
    String normalized = device_class.as<const char*>();
    normalized.trim();
    out.has_device_class = true;
    strlcpy(out.device_class, normalized.c_str(), sizeof(out.device_class));
  }

  JsonVariantConst icon = root["icon"];
  if (!icon.isNull() && icon.is<const char*>()) {
    const String normalized = normalizeMdiIconName(icon.as<const char*>());
    if (normalized.length()) {
      out.has_icon = true;
      strlcpy(out.icon_name, normalized.c_str(), sizeof(out.icon_name));
    }
  }

  JsonVariantConst last_changed = root["last_changed"];
  if (!last_changed.isNull()) {
    if (last_changed.is<unsigned long long>()) {
      out.last_changed = last_changed.as<unsigned long long>();
      out.has_last_changed = true;
    } else if (last_changed.is<long long>()) {
      const long long value_signed = last_changed.as<long long>();
      if (value_signed >= 0) {
        out.last_changed = static_cast<uint64_t>(value_signed);
        out.has_last_changed = true;
      }
    } else if (last_changed.is<double>()) {
      const double value_double = last_changed.as<double>();
      if (std::isfinite(value_double) && value_double >= 0.0 &&
          value_double < 18446744073709551616.0 &&
          std::floor(value_double) == value_double) {
        out.last_changed = static_cast<uint64_t>(value_double);
        out.has_last_changed = true;
      }
    }
  }
  return out;
}

const char* binary_sensor_state_name(BinarySensorValue value) {
  switch (value) {
    case BinarySensorValue::Off: return "off";
    case BinarySensorValue::On: return "on";
    case BinarySensorValue::Unknown: return "unknown";
    case BinarySensorValue::Unavailable: return "unavailable";
    default: return "";
  }
}

String binary_sensor_visual_icon(const BinarySensorState& state,
                                 const String& entity_icon) {
  if (state.has_icon) {
    const String state_icon = normalizeMdiIconName(state.icon_name);
    if (state_icon.length()) return state_icon;
  }
  const String normalized = normalizeMdiIconName(entity_icon);
  if (normalized.length()) return normalized;
  return fallback_icon(state);
}

String binary_sensor_resolve_icon(const Tile& tile,
                                  const BinarySensorState& state,
                                  bool* dynamic_icon) {
  if (dynamic_icon) *dynamic_icon = false;
  if (isMdiIconDisabled(tile.icon_name)) return "";

  const String configured = normalizeMdiIconName(tile.icon_name);
  if (configured.length()) return configured;

  if (dynamic_icon) *dynamic_icon = true;
  if (state.has_icon) {
    const String state_icon = normalizeMdiIconName(state.icon_name);
    if (state_icon.length()) return state_icon;
  }
  const String entity_icon =
      normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  if (entity_icon.length()) return entity_icon;
  return fallback_icon(state);
}

uint32_t binary_sensor_visual_color(const BinarySensorState& state) {
  return state.valid && state.available &&
                 state.value == BinarySensorValue::On
             ? 0xFFC107
             : 0x9E9E9E;
}

BinarySensorTileWidgets* tile_renderer_get_binary_sensor_widgets(
    GridType grid_type) {
  return g_widgets[grid_index(grid_type)];
}

BinarySensorState* tile_renderer_get_binary_sensor_states(GridType grid_type) {
  return g_states[grid_index(grid_type)];
}

void reset_binary_sensor_widget(GridType grid_type, uint8_t grid_index_value) {
  if (grid_index_value >= TILES_PER_GRID) return;
  invalidate_queued_slot(grid_type, grid_index_value);
  tile_renderer_get_binary_sensor_widgets(grid_type)[grid_index_value] = {};
  tile_renderer_get_binary_sensor_states(grid_type)[grid_index_value] = {};
}

void reset_binary_sensor_widgets(GridType grid_type) {
  advance_layout_generation(grid_type);
  BinarySensorTileWidgets* widgets =
      tile_renderer_get_binary_sensor_widgets(grid_type);
  BinarySensorState* states = tile_renderer_get_binary_sensor_states(grid_type);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    widgets[i] = {};
    states[i] = {};
  }
}

lv_obj_t* render_binary_sensor_tile(lv_obj_t* parent, int col, int row,
                                    const Tile& tile, uint8_t index,
                                    GridType grid_type) {
  if (!parent || index >= TILES_PER_GRID) return nullptr;

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

  BinarySensorTileWidgets& widgets =
      tile_renderer_get_binary_sensor_widgets(grid_type)[index];
  BinarySensorState& state =
      tile_renderer_get_binary_sensor_states(grid_type)[index];
  widgets = {};
  state = {};

  String initial_payload;
  if (tile.sensor_entity.length()) {
    initial_payload =
        haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    if (initial_payload.length()) {
      state = parse_binary_sensor_payload(initial_payload.c_str());
      if (state.valid) widgets.last_payload_hash = fnv1a(initial_payload.c_str());
    }
  }

  const bool icon_visible = !isMdiIconDisabled(tile.icon_name);
  const String icon =
      binary_sensor_resolve_icon(tile, state, &widgets.dynamic_icon);
  widgets.dynamic_icon = icon_visible && widgets.dynamic_icon;
  if (icon_visible && icon.length() && FONT_MDI_ICONS) {
    widgets.icon_label = lv_label_create(card);
    set_label_style(widgets.icon_label,
                    lv_color_hex(binary_sensor_visual_color(state)),
                    FONT_MDI_ICONS);
    lv_label_set_text(widgets.icon_label, getMdiChar(icon).c_str());
    lv_obj_align(widgets.icon_label, LV_ALIGN_TOP_LEFT,
                 tile_layout::scale_480(-8),
                 tile_layout::scale_480(-8));
  }

  if (tile.title.length()) {
    widgets.title_label = lv_label_create(card);
    set_label_style(widgets.title_label, lv_color_white(),
                    tile_layout::header_title_font());
    lv_obj_set_width(widgets.title_label, LV_PCT(70));
    lv_obj_set_style_text_align(widgets.title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(widgets.title_label, LV_LABEL_LONG_DOT);
    hometiles_title::tile(widgets.title_label, tile.title.c_str(), true);
    lv_obj_align(widgets.title_label, LV_ALIGN_TOP_RIGHT,
                 tile_layout::scale_480(4), tile_layout::scale_480(4));
  }

  widgets.state_label = lv_label_create(card);
  set_label_style(widgets.state_label, lv_color_white(),
                  tile_layout::header_title_font());
  lv_label_set_long_mode(widgets.state_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(widgets.state_label, LV_PCT(100));
  lv_obj_set_style_text_align(widgets.state_label, LV_TEXT_ALIGN_CENTER, 0);
  const String initial_label = state_label(state);
  lv_label_set_text(widgets.state_label, initial_label.c_str());
  lv_obj_align(widgets.state_label, LV_ALIGN_CENTER, 0,
               tile_layout::scale(28));

  if (grid_type != GridType::SCREENSAVER && tile.sensor_entity.length()) {
    BinarySensorEventData* data =
        new BinarySensorEventData{grid_type, index};
    const lv_event_code_t event_code =
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          const lv_event_code_t code = lv_event_get_code(event);
          if (code != LV_EVENT_SHORT_CLICKED &&
              code != LV_EVENT_LONG_PRESSED) {
            return;
          }
          BinarySensorEventData* data =
              static_cast<BinarySensorEventData*>(lv_event_get_user_data(event));
          if (!data) return;
          BinarySensorPopupInit init = popup_init(data->grid_type, data->index);
          if (!init.entity_id.length()) return;
          finish_press_before_popup(event);
          show_binary_sensor_popup(init);
        },
        event_code, data);
    lv_obj_add_event_cb(
        card,
        [](lv_event_t* event) {
          if (lv_event_get_code(event) == LV_EVENT_DELETE) {
            delete static_cast<BinarySensorEventData*>(
                lv_event_get_user_data(event));
          }
        },
        LV_EVENT_DELETE, data);
  }
  return card;
}

void queue_binary_sensor_tile_updates(GridType grid_type,
                                      uint64_t grid_indices,
                                      const char* payload) {
  if (grid_indices == 0 || !payload) return;
  if (strnlen(payload, BINARY_SENSOR_PAYLOAD_MAX + 1) >
      BINARY_SENSOR_PAYLOAD_MAX) {
    return;
  }
  String entity_id;
  grid_indices = capture_targets(grid_type, grid_indices, entity_id);
  if (grid_indices == 0 || !entity_id.length()) return;
  enqueue_update(grid_type, grid_indices, entity_id, true, payload);
}

void queue_binary_sensor_tile_update(GridType grid_type, uint8_t grid_index_value,
                                     const char* payload) {
  if (grid_index_value >= TILES_PER_GRID || !payload) return;
  if (strnlen(payload, BINARY_SENSOR_PAYLOAD_MAX + 1) >
      BINARY_SENSOR_PAYLOAD_MAX) {
    return;
  }
  enqueue_update(grid_type, uint64_t{1} << grid_index_value, String(), false,
                 payload);
}

void process_binary_sensor_update_queue(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_queue_tail != g_queue_head) {
    if (max_updates != 0 && processed >= max_updates) return;

    BinarySensorUpdate& update = g_queue[g_queue_tail];
    if (!update.valid) {
      g_queue_tail = (g_queue_tail + 1) % kQueueSize;
      continue;
    }
    if (update.layout_generation != layout_generation(update.grid_type)) {
      update.grid_indices = 0;
    }
    if (update.grid_indices != 0 && !update.parsed) {
      update.parsed_state =
          parse_binary_sensor_payload(update.payload.c_str());
      update.parsed = true;
      if (!update.parsed_state.valid) update.grid_indices = 0;
    }

    const uint32_t payload_hash = fnv1a(update.payload.c_str());
    for (uint8_t index = 0;
         index < TILES_PER_GRID && update.grid_indices != 0; ++index) {
      const uint64_t bit = uint64_t{1} << index;
      if ((update.grid_indices & bit) == 0) continue;
      update.grid_indices &= ~bit;
      if (update.require_entity_match) {
        const Tile* tile = tile_renderer_get_tile_config(update.grid_type, index);
        if (!tile || tile->type != TILE_BINARY_SENSOR ||
            !tile->sensor_entity.equalsIgnoreCase(update.entity_id)) {
          continue;
        }
      }
      apply_state(update.grid_type, index, update.parsed_state, payload_hash);
      ++processed;
      if (max_updates != 0 && processed >= max_updates &&
          update.grid_indices != 0) {
        return;
      }
    }

    if (update.grid_indices != 0) continue;
    update.valid = false;
    update.parsed = false;
    update.require_entity_match = false;
    update.layout_generation = 0;
    update.entity_id.remove(0);
    update.payload.remove(0);
    g_queue_tail = (g_queue_tail + 1) % kQueueSize;
  }
}
