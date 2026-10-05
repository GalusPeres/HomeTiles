#include "src/types/device/web.h"

#include <ArduinoJson.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/switch/web_html.h"
#include "src/web/server/web_admin_utils.h"

namespace {

const char* language() { return configManager.getConfig().language; }

const char* entity_domain(TileType type) {
  return type == TILE_LOCK ? "lock" : type == TILE_ALARM ? "alarm_control_panel" : "fan";
}

const char* entity_label(TileType type) {
  const i18n::DeviceLabel label = type == TILE_LOCK    ? i18n::DeviceLabel::EntityLock
                                  : type == TILE_ALARM ? i18n::DeviceLabel::EntityAlarm
                                                       : i18n::DeviceLabel::EntityFan;
  return i18n::device_label(language(), label);
}

}  // namespace

const char* device_field_prefix(TileType type) {
  return type == TILE_LOCK ? "lock" : type == TILE_ALARM ? "alarm" : "fan";
}

bool device_entity_matches(TileType type, const String& entity) {
  if (!entity.length()) return true;
  const int dot = entity.indexOf('.');
  if (dot < 1 || dot == static_cast<int>(entity.length()) - 1) return false;
  for (size_t i = dot + 1; i < entity.length(); ++i) {
    const char c = entity[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return entity.substring(0, dot) == entity_domain(type);
}

void append_device_fields_html(String& html, const String& tab_id, TileType type) {
  const auto& tr = i18n::strings(language());
  const char* prefix = device_field_prefix(type);
  html += "<div id=\"";
  html += tab_id;
  html += "_";
  html += prefix;
  html += "_fields\" class=\"type-fields\">";
  const String entity_field = String(prefix) + "_entity";
  appendEntityPickerField(html, tab_id, entity_field.c_str(), entity_label(type),
                          type == TILE_LOCK ? "locks" : type == TILE_ALARM ? "alarm_panels" : "fans");
  // The state size like the Switch tile (tile_header.h): the half-height
  // Sensor value sizes beside the disc, the Sensor value sizes from 1.5 rows.
  const SwitchChoice sizes[] = {{"0", tr.sensor_value_size_default}, {"1", "20"}, {"2", "24"},
                                {"5", "28"}, {"3", "32"}, {"4", "40"}};
  const String field = String(prefix) + "_value_font";
  append_switch_choice(html, tab_id, field.c_str(), tr.sensor_value_size, sizes, 6);
  if (tab_id != "screensaver") {
    html += "<label>";
    html += tr.popup_open;
    html += "</label><select id=\"";
    html += tab_id;
    html += "_";
    html += prefix;
    html += "_popup_open_mode\"><option value=\"1\">";
    html += tr.short_press;
    html += "</option><option value=\"0\">";
    html += tr.long_press;
    html += "</option></select>";
  }
  html += "</div>\n";
}

bool apply_device_fields_from_request(WebServer& server, Tile& tile) {
  const TileType type = static_cast<TileType>(tile.type);
  const String field = String(device_field_prefix(type)) + "_entity";
  tile.sensor_entity = server.hasArg(field) ? server.arg(field) : "";
  tile.sensor_entity.trim();
  const uint8_t popup_mode =
      server.hasArg("popup_open_mode") && server.arg("popup_open_mode").toInt() == TILE_POPUP_OPEN_LONG_PRESS
          ? TILE_POPUP_OPEN_LONG_PRESS
          : TILE_POPUP_OPEN_SHORT_PRESS;
  setTilePopupOpenMode(tile, popup_mode);
  tile.sensor_unit = "";
  tile.sensor_decimals = 0xFF;
  if (server.hasArg("sensor_value_font")) {
    const int font = server.arg("sensor_value_font").toInt();
    tile.sensor_value_font = font >= 1 && font <= SENSOR_VALUE_FONT_MAX ? static_cast<uint8_t>(font) : 0;
  }
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  tile.key_code = 0;
  tile.key_modifier = 0;
  return device_entity_matches(type, tile.sensor_entity);
}

void append_device_scripts(String& html) {
  // The preview's words from the central translations, as one JSON object
  // (escaped for a script element).
  DynamicJsonDocument doc(4096);
  const char* lang = language();
  const auto& tr = i18n::strings(lang);
  JsonObject lock = doc.createNestedObject("lock");
  for (const char* state :
       {"locked", "unlocked", "locking", "unlocking", "open", "opening", "jammed", "unavailable", "unknown"}) {
    lock[state] = i18n::lock_state_label(lang, state);
  }
  JsonObject alarm = doc.createNestedObject("alarm");
  for (const char* state : {"disarmed", "armed_home", "armed_away", "armed_night", "armed_vacation",
                            "armed_custom_bypass", "pending", "arming", "disarming", "triggered", "unavailable",
                            "unknown"}) {
    alarm[state] = i18n::alarm_state_label(lang, state);
  }
  JsonObject fan = doc.createNestedObject("fan");
  fan["on"] = tr.light_on;
  fan["off"] = tr.light_off;
  fan["speed"] = i18n::device_label(lang, i18n::DeviceLabel::FanSpeed);
  fan["unavailable"] = i18n::entity_state_label(lang, "unavailable");
  fan["unknown"] = i18n::entity_state_label(lang, "unknown");
  String json;
  serializeJson(doc, json);
  json.replace("<", "\\u003c");
  html += "<script>const DEVICE_I18N=Object.freeze(";
  html += json;
  html += ");</script>\n";
}

void append_device_styles(String&) {}
