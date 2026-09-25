#include "src/tiles/runtime/tile_icon_source.h"

#include <ArduinoJson.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/config/tile_tint.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/cover/renderer.h"
#include "src/types/value/value_control.h"
#include "src/ui/shared/ui_surface_style.h"
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
  return tile_icon_colors::resolve(record.c_str(), state.c_str(), display, rgb, false);
}

// Tile types whose own state path already applies the fixed icon color
// (tile_icon_color_rules.h) or that have no state colors at all.
bool type_applies_fixed_icon_color(int type) {
  return tileTypeIconColorsByValue(type) || tileTypeIconColorsByState(type) ||
         tileTypeIsEditableValue(type) || tileTypeHasFixedIconColorOnly(type);
}

constexpr lv_style_selector_t kTintStore = LV_PART_MAIN | LV_STATE_USER_4;

// Sets the card background in its normal and pressed states (pressed about
// 6 % brighter, like the tile renderers).
void apply_card_background(lv_obj_t* card, uint32_t rgb) {
  uint32_t pressed = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const uint32_t channel = ((rgb >> shift) & 0xFF) + 0x10;
    pressed |= (channel > 0xFF ? 0xFF : channel) << shift;
  }
  for (const lv_style_selector_t selector : {static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_DEFAULT),
                                              static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_FOCUSED)}) {
    lv_obj_set_style_bg_color(card, lv_color_hex(rgb), selector);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(rgb), selector);
  }
  for (const lv_style_selector_t selector : {static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED),
                                              static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_FOCUSED | LV_STATE_PRESSED)}) {
    lv_obj_set_style_bg_color(card, lv_color_hex(pressed), selector);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed), selector);
  }
}
// Tints a tile card for its rules (tile_tint.h): the first tint keeps the
// card's own color in an unused state selector, clear restores it.
void set_tile_tint(lv_obj_t* card, uint32_t color, uint8_t percent) {
  if (!card) return;
  lv_style_value_t stored;
  uint32_t base = 0;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) == LV_STYLE_RES_FOUND) {
    base = lv_color_to_u32(stored.color) & 0xFFFFFF;
  } else {
    const lv_color_t own = lv_obj_get_style_bg_color(card, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, own, kTintStore);
    base = lv_color_to_u32(own) & 0xFFFFFF;
  }
  const uint32_t tint = tile_tint::background(base, color, percent);
  if ((lv_color_to_u32(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) & 0xFFFFFF) == tint) return;
  apply_card_background(card, tint);
}

void clear_tile_tint(lv_obj_t* card) {
  if (!card) return;
  lv_style_value_t stored;
  if (lv_obj_get_local_style_prop(card, LV_STYLE_BG_COLOR, &stored, kTintStore) != LV_STYLE_RES_FOUND) return;
  lv_obj_remove_local_style_prop(card, LV_STYLE_BG_COLOR, kTintStore);
  apply_card_background(card, lv_color_to_u32(stored.color) & 0xFFFFFF);
}


String layer_entity(const Tile& tile, const tile_icon_colors::Source& layer) {
  if (layer.mode == tile_icon_colors::SourceMode::None || !layer.enabled) return String();
  if (layer.self) return tile.sensor_entity;
  String entity;
  entity.reserve(layer.entity_len);
  for (size_t i = 0; i < layer.entity_len; ++i) entity += layer.entity[i];
  return entity;
}

}  // namespace

bool cached_payload(const String& entity, String& payload) {
  payload = "";
  if (!entity.length()) return false;
  if (tiles_get_cached_entity_payload(entity.c_str(), payload) && payload.length()) return true;
  payload = haBridgeConfig.findSensorInitialValue(entity);
  return payload.length() > 0;
}

String rule_entity(const Tile& tile) {
  if (!tileTypeHasIconColors(tile.type) || !tile.icon_colors.length()) return String();
  return layer_entity(tile, tile_icon_colors::source_of(tile.icon_colors.c_str()));
}

bool rule_color(const Tile& tile, uint32_t& rgb) {
  if (!tileTypeHasIconColors(tile.type) || !tile.icon_colors.length()) return false;
  const tile_icon_colors::Source layer = tile_icon_colors::source_of(tile.icon_colors.c_str());
  const String entity = layer_entity(tile, layer);
  if (!entity.length()) return false;
  const bool automatic = layer.mode == tile_icon_colors::SourceMode::Auto;
  if (layer.self && tileTypeIsEditableValue(tile.type)) {
    // Number, Select and Date/Time keep their state in the editable cache.
    if (automatic) return false;
    const EditableValue value = parse_editable_value(haBridgeConfig.findEditableValue(entity));
    if (!value.valid || !value.has_state || !value.available || value.state == "unknown") return false;
    return tile_icon_colors::resolve(tile.icon_colors.c_str(), value.state.c_str(),
                                     editable_display_value(value).c_str(), rgb, false);
  }
  String payload;
  if (!cached_payload(entity, payload)) return false;
  String domain;
  const int dot = entity.indexOf('.');
  if (dot > 0) domain = entity.substring(0, dot);
  return automatic ? auto_color(domain, payload.c_str(), rgb)
                   : rules_color(tile.icon_colors, domain, payload.c_str(), rgb);
}

void apply_initial(lv_obj_t* icon, const Tile& tile) {
  if (!icon) return;
  uint32_t fixed = 0xFFFFFF;
  if (tile.icon_colors.length()) tile_icon_colors::resolve(tile.icon_colors.c_str(), "", nullptr, fixed);
  const lv_color_t value = lv_color_hex(fixed);
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
  if (!card || !tileTypeHasIconColors(tile.type)) return;
  const tile_icon_colors::Source layer = tile_icon_colors::source_of(tile.icon_colors.c_str());
  uint32_t rgb = 0;
  const bool colored = layer.mode != tile_icon_colors::SourceMode::None && layer.enabled &&
                       rule_color(tile, rgb);
  if (lv_obj_t* icon = card_icon(card)) {
    uint32_t fixed = 0;
    const bool force_fixed = !type_applies_fixed_icon_color(tile.type) && tile.icon_colors.length() &&
                             tile_icon_colors::resolve(tile.icon_colors.c_str(), "", nullptr, fixed);
    if (colored && layer.icon) {
      tile_icon_disc::force_icon_color(icon, lv_color_hex(rgb));
    } else if (force_fixed) {
      tile_icon_disc::force_icon_color(icon, lv_color_hex(fixed));
    } else {
      tile_icon_disc::release_icon_color(icon);
    }
  }
  if (colored && layer.tile) {
    set_tile_tint(card, rgb, layer.tile);
  } else {
    clear_tile_tint(card);
  }
  // Disc opacity follows the (tinted) background.
  const uint32_t count = lv_obj_get_child_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (tile_icon_disc::is_disc(child)) tile_icon_disc::apply_fill(child);
  }
}

}  // namespace tile_icon_source
