#include "src/tiles/runtime/tile_icon_source.h"

#include <ArduinoJson.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/cover/renderer.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"

namespace tile_icon_source {
namespace {

// Domains shown by the Switch tile (Switch aliases, lights included).
bool switch_domain(const String& domain) {
  return domain == "light" || domain == "switch" || domain == "input_boolean" ||
         domain == "automation" || domain == "fan" || domain == "humidifier" ||
         domain == "remote" || domain == "siren";
}

// The raw state of a payload: the JSON "state" field, else the plain text.
String payload_state(const char* payload) {
  String state;
  if (!payload) return state;
  while (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n') ++payload;
  if (*payload != '{') {
    state = payload;
    state.trim();
    return state;
  }
  StaticJsonDocument<32> filter;
  filter["state"] = true;
  StaticJsonDocument<192> doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
    state = doc["state"] | "";
    state.trim();
  }
  return state;
}

bool state_known(const String& state) {
  String lower = state;
  lower.toLowerCase();
  return lower.length() && lower != "unavailable" && lower != "unknown" && lower != "none" &&
         lower != "null";
}

// "auto": the entity's own icon color as its tile shows it.
bool auto_color(const String& domain, const char* payload, uint32_t& rgb) {
  if (switch_domain(domain)) return switch_payload_icon_color(payload, rgb);
  if (domain == "climate") return climate_payload_icon_color(payload, rgb);
  if (domain == "cover") return cover_payload_icon_color(payload, rgb);
  if (domain == "binary_sensor") {
    const BinarySensorState state = parse_binary_sensor_payload(payload);
    if (!state.valid || !state.available ||
        (state.value != BinarySensorValue::On && state.value != BinarySensorValue::Off)) {
      return false;
    }
    rgb = binary_sensor_visual_color(state);
    return true;
  }
  return false;
}

// "rules": the bar and state colors on the entity state; Binary sensors also
// match their translated On/Off label like the Binary sensor tile.
bool rules_color(const String& record, const String& domain, const char* payload, uint32_t& rgb) {
  const String state = payload_state(payload);
  if (!state_known(state)) return false;
  const char* display = nullptr;
  if (domain == "binary_sensor") {
    const BinarySensorState parsed = parse_binary_sensor_payload(payload);
    if (parsed.value == BinarySensorValue::On || parsed.value == BinarySensorValue::Off) {
      display = i18n::binary_sensor_state_label(configManager.getConfig().language,
                                                binary_sensor_state_name(parsed.value),
                                                String(parsed.device_class));
    }
  }
  return tile_icon_colors::resolve(record.c_str(), state.c_str(), display, rgb);
}

}  // namespace

uint32_t color(const String& record, const char* payload) {
  uint32_t rgb = 0xFFFFFF;
  const char* entity = nullptr;
  size_t length = 0;
  const tile_icon_colors::SourceMode mode = tile_icon_colors::source(record.c_str(), entity, length);
  if (mode != tile_icon_colors::SourceMode::None && payload && *payload) {
    String domain;
    for (size_t i = 0; i < length && entity[i] != '.'; ++i) domain += entity[i];
    uint32_t source_rgb = 0;
    const bool found = mode == tile_icon_colors::SourceMode::Auto
                           ? auto_color(domain, payload, source_rgb)
                           : rules_color(record, domain, payload, source_rgb);
    if (found) return source_rgb & 0xFFFFFF;
  }
  // The fixed icon color: no state matches an empty text.
  uint32_t fixed = 0;
  if (record.length() && tile_icon_colors::resolve(record.c_str(), "", nullptr, fixed)) rgb = fixed;
  return rgb;
}

bool cached_payload(const String& entity, String& payload) {
  payload = "";
  if (!entity.length()) return false;
  if (tiles_get_cached_entity_payload(entity.c_str(), payload) && payload.length()) return true;
  payload = haBridgeConfig.findSensorInitialValue(entity);
  return payload.length() > 0;
}

uint32_t cached_color(const Tile& tile) {
  const String entity = tileIconSourceEntity(tile.type, tile.icon_colors);
  String payload;
  if (entity.length()) cached_payload(entity, payload);
  return color(tile.icon_colors, payload.c_str());
}

void apply_initial(lv_obj_t* icon, const Tile& tile) {
  if (!icon) return;
  const lv_color_t value = lv_color_hex(cached_color(tile));
  if (!lv_color_eq(lv_obj_get_style_text_color(icon, LV_PART_MAIN), value)) {
    tile_icon_disc::set_icon_color(icon, value);
  }
}

lv_obj_t* card_icon(lv_obj_t* card) {
  if (!card) return nullptr;
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (tile_icon_disc::is_disc(child)) return tile_icon_disc::icon_of(child);
  }
  return nullptr;
}

void refresh_card(lv_obj_t* card, const Tile& tile) {
  if (!tileTypeHasFixedIconColorOnly(tile.type)) return;
  apply_initial(card_icon(card), tile);
}

}  // namespace tile_icon_source
