#include "src/types/value/value_control.h"
#include "src/core/power/battery_state.h"
#include "src/network/bridge/ha_bridge_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <stdio.h>
#include <lvgl.h>
#include <strings.h>
#include <ctype.h>
#include "src/core/config/batched_nvs_write.h"
#include "src/devices/device.h"
#include "src/io/hardware_io.h"
#include <vector>

static const char* PREF_NAMESPACE = "tab5_config";
static bool sensorExistsInList(const String& list, const String& candidate);
static bool aliasExistsInList(const String& list, const String& alias);
static String normalizeLineLocal(const String& line);
static int countListEntries(const String& text);
static int countMapEntries(const String& text);
static bool listEqualsIgnoringOrder(const String& a, const String& b);
static bool mapEqualsIgnoringOrder(const String& a, const String& b);
static bool bridgeConfigEquals(const HaBridgeConfigData& a, const HaBridgeConfigData& b);
static void parseSensorMetaSection(const String& body, String& units,
                                   String& names, String& values,
                                   String& state_kinds);
static void parseBinarySensorMetaSection(const String& body, String& names,
                                         String& values);
static void parseEntityNameSection(const String& body, const char* key, String& names);
static void parseIconMetaSections(const String& body, String& icons);
static void parseEnergySection(const String& body,
                               String& energy,
                               String& units,
                               String& names,
                               String& icons);
static int findMatchingJsonObjectEnd(const String& body, int object_start);
static int findMatchingJsonArrayEnd(const String& body, int array_start);
static bool extractStringField(const String& object, const char* key, String& out);
static String lookupKeyValue(const String& text, const String& key);
static void upsertKeyValueMap(String& text, const String& key, const String& value);
struct KeyValueUpdate {
  String key;
  String value;
};
static void upsertKeyValueMapBatch(String& text, const std::vector<KeyValueUpdate>& updates);
static bool removeKeyValueMapEntry(String& text, const String& key);
static void indexPut(HaEntityKeyMap& map, const String& key, const String& value);
static void indexErase(HaEntityKeyMap& map, const String& key);
static String decodeJsonEscapes(const String& value);
static void appendUtf8(String& out, uint32_t codepoint);
static bool isHexDigit(char c);
static uint8_t hexValue(char c);

HaBridgeConfig haBridgeConfig;

#if defined(DEVICE_ESP32_S3_RGB_480)
static constexpr bool kWriteOnlyChangedBridgeSlots = true;
#else
// Preserve the existing persistence behaviour on every other device profile.
static constexpr bool kWriteOnlyChangedBridgeSlots = false;
#endif

HaBridgeConfig::HaBridgeConfig() = default;

bool HaBridgeConfig::load() {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, true)) {
    return false;
  }

  data.sensors_text = "";
  data.numbers_text = "";
  data.selects_text = "";
  data.datetimes_text = "";
  data.binary_sensors_text = "";
  data.energy_text = "";
  data.weathers_text = "";
  data.lights_text = "";
  data.switches_text = "";
  data.media_players_text = "";
  data.climates_text = "";
  data.covers_text = "";
  data.cameras_text = "";
  data.scene_alias_text = "";
  data.sensor_units_map = "";
  data.sensor_names_map = "";
  data.sensor_values_map = "";
  data.sensor_state_kinds_map = "";
  data.entity_icons_map = "";
  for (size_t i = 0; i < HA_SENSOR_SLOT_COUNT; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "slot_s%u", static_cast<unsigned>(i));
    data.sensor_slots[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "title_s%u", static_cast<unsigned>(i));
    data.sensor_titles[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "unit_s%u", static_cast<unsigned>(i));
    data.sensor_custom_units[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "color_s%u", static_cast<unsigned>(i));
    data.sensor_colors[i] = prefs.getUInt(key, 0);
  }
  for (size_t i = 0; i < HA_SCENE_SLOT_COUNT; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "slot_c%u", static_cast<unsigned>(i));
    data.scene_slots[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "title_c%u", static_cast<unsigned>(i));
    data.scene_titles[i] = prefs.getString(key, "");
    snprintf(key, sizeof(key), "color_c%u", static_cast<unsigned>(i));
    data.scene_colors[i] = prefs.getUInt(key, 0);
  }

  prefs.end();
  rebuildEntityIndexes();
  return true;
}

bool HaBridgeConfig::save(const HaBridgeConfigData& incoming) {
  bool slot_data_changed = !kWriteOnlyChangedBridgeSlots;
  for (size_t i = 0; i < HA_SENSOR_SLOT_COUNT; ++i) {
    slot_data_changed = slot_data_changed ||
                        incoming.sensor_slots[i] != data.sensor_slots[i] ||
                        incoming.sensor_titles[i] != data.sensor_titles[i] ||
                        incoming.sensor_custom_units[i] !=
                            data.sensor_custom_units[i] ||
                        incoming.sensor_colors[i] != data.sensor_colors[i];
  }
  for (size_t i = 0; i < HA_SCENE_SLOT_COUNT; ++i) {
    slot_data_changed = slot_data_changed ||
                        incoming.scene_slots[i] != data.scene_slots[i] ||
                        incoming.scene_titles[i] != data.scene_titles[i] ||
                        incoming.scene_colors[i] != data.scene_colors[i];
  }

  bool legacy_data_present = false;
  {
    // A read-only probe never creates the NVS namespace. Most Bridge refreshes
    // only update volatile entity metadata; return before opening NVS RW when
    // no persistent slot or legacy key actually changed.
    Preferences read_prefs;
    if (read_prefs.begin(PREF_NAMESPACE, true)) {
      legacy_data_present = read_prefs.isKey("ha_sensors") ||
                            read_prefs.isKey("ha_scene_alias") ||
                            read_prefs.isKey("ha_sens_units") ||
                            read_prefs.isKey("ha_sens_names") ||
                            read_prefs.isKey("ha_sens_vals");
      read_prefs.end();
    }
  }

  if (!slot_data_changed && !legacy_data_present) {
    data = incoming;
    rebuildEntityIndexes();
    return true;
  }

  Device::ScopedStorageWrite storage_write(
      BatchedNvsWrite::kNeedsDisplayGuard);
  BatchedNvsWrite::Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE, false)) {
    return false;
  }

  // Do not persist discovery lists in NVS; MQTT supplies them at runtime.
  // Store only slot assignments to avoid exhausting NVS with large sensor lists.
  // prefs.putString("ha_sensors", incoming.sensors_text);
  // prefs.putString("ha_scene_alias", incoming.scene_alias_text);
  // prefs.putString("ha_sens_units", incoming.sensor_units_map);
  // prefs.putString("ha_sens_names", incoming.sensor_names_map);
  // prefs.putString("ha_sens_vals", incoming.sensor_values_map);
  // HA-Auto-Icons are runtime metadata. Manual tile icons are stored with the
  // tile config, but HA icon changes must not write to flash.
  if (!kWriteOnlyChangedBridgeSlots || legacy_data_present) {
    prefs.remove("ha_sensors");
    prefs.remove("ha_scene_alias");
    prefs.remove("ha_sens_units");
    prefs.remove("ha_sens_names");
    prefs.remove("ha_sens_vals");
  }
  for (size_t i = 0; i < HA_SENSOR_SLOT_COUNT; ++i) {
    char key[12];
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.sensor_slots[i] != data.sensor_slots[i]) {
      snprintf(key, sizeof(key), "slot_s%u", static_cast<unsigned>(i));
      prefs.putString(key, incoming.sensor_slots[i]);
    }
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.sensor_titles[i] != data.sensor_titles[i]) {
      snprintf(key, sizeof(key), "title_s%u", static_cast<unsigned>(i));
      prefs.putString(key, incoming.sensor_titles[i]);
    }
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.sensor_custom_units[i] != data.sensor_custom_units[i]) {
      snprintf(key, sizeof(key), "unit_s%u", static_cast<unsigned>(i));
      prefs.putString(key, incoming.sensor_custom_units[i]);
    }
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.sensor_colors[i] != data.sensor_colors[i]) {
      snprintf(key, sizeof(key), "color_s%u", static_cast<unsigned>(i));
      prefs.putUInt(key, incoming.sensor_colors[i]);
    }
  }
  for (size_t i = 0; i < HA_SCENE_SLOT_COUNT; ++i) {
    char key[12];
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.scene_slots[i] != data.scene_slots[i]) {
      snprintf(key, sizeof(key), "slot_c%u", static_cast<unsigned>(i));
      prefs.putString(key, incoming.scene_slots[i]);
    }
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.scene_titles[i] != data.scene_titles[i]) {
      snprintf(key, sizeof(key), "title_c%u", static_cast<unsigned>(i));
      prefs.putString(key, incoming.scene_titles[i]);
    }
    if (!kWriteOnlyChangedBridgeSlots ||
        incoming.scene_colors[i] != data.scene_colors[i]) {
      snprintf(key, sizeof(key), "color_c%u", static_cast<unsigned>(i));
      prefs.putUInt(key, incoming.scene_colors[i]);
    }
  }
  if (!BatchedNvsWrite::finish(prefs)) return false;

  data = incoming;
  rebuildEntityIndexes();
  return true;
}

bool HaBridgeConfig::hasData() const {
  return data.sensors_text.length() > 0 ||
         data.numbers_text.length() > 0 ||
         data.selects_text.length() > 0 ||
         data.datetimes_text.length() > 0 ||
         data.binary_sensors_text.length() > 0 ||
         data.energy_text.length() > 0 ||
         data.weathers_text.length() > 0 ||
         data.lights_text.length() > 0 ||
         data.switches_text.length() > 0 ||
         data.media_players_text.length() > 0 ||
         data.climates_text.length() > 0 ||
         data.covers_text.length() > 0 ||
         data.cameras_text.length() > 0 ||
         data.scene_alias_text.length() > 0;
}

// Entity lookups use the index with O(log n) key comparisons instead of
// lookupKeyValue()'s linear blob scan. Bridge cache refresh calls
// findSensorInitialValue() once per tile across all folders, about 150
// times in a burst. One scan of a blob grown over uptime already blocked
// for several milliseconds.
String HaBridgeConfig::findSensorUnit(const String& entity_id) const {
  return findSensorUnit(entity_id.c_str());
}

String HaBridgeConfig::findSensorName(const String& entity_id) const {
  return findSensorName(entity_id.c_str());
}

String HaBridgeConfig::findSensorInitialValue(const String& entity_id) const {
  return findSensorInitialValue(entity_id.c_str());
}

String HaBridgeConfig::findSensorStateKind(const String& entity_id) const {
  return findSensorStateKind(entity_id.c_str());
}

String HaBridgeConfig::findEntityIcon(const String& entity_id) const {
  return findEntityIcon(entity_id.c_str());
}

String HaBridgeConfig::findSensorUnit(const char* entity_id) const {
  if (!entity_id || !entity_id[0]) return String();
  auto it = units_index_.find(entity_id);
  return (it != units_index_.end()) ? String(it->second.c_str()) : String();
}

String HaBridgeConfig::findSensorName(const char* entity_id) const {
  if (!entity_id || !entity_id[0]) return String();
  auto it = names_index_.find(entity_id);
  return (it != names_index_.end()) ? String(it->second.c_str()) : String();
}

String HaBridgeConfig::findSensorInitialValue(const char* entity_id) const {
  if (!entity_id || !entity_id[0]) return String();
  auto it = values_index_.find(entity_id);
  return (it != values_index_.end()) ? String(it->second.c_str()) : String();
}

String HaBridgeConfig::findSensorStateKind(const char* entity_id) const {
  if (!entity_id || !entity_id[0]) return String();
  auto it = state_kinds_index_.find(entity_id);
  return (it != state_kinds_index_.end()) ? String(it->second.c_str())
                                          : String();
}

String HaBridgeConfig::findEntityIcon(const char* entity_id) const {
  if (!entity_id || !entity_id[0]) return String();
  auto it = icons_index_.find(entity_id);
  return (it != icons_index_.end()) ? String(it->second.c_str()) : String();
}

String HaBridgeConfig::findSceneEntity(const String& alias) const {
  return lookupKeyValue(data.scene_alias_text, alias);
}

String HaBridgeConfig::buildJsonPayload(const char* device_id,
                                        const char* base_topic,
                                        const char* ha_prefix) const {
  if (!device_id || !*device_id || !base_topic || !*base_topic || !ha_prefix || !*ha_prefix) {
    return String();
  }

  String json = "{";
  json += "\"device_id\":\"";
  appendJsonEscaped(json, device_id);
  json += "\",\"base_topic\":\"";
  appendJsonEscaped(json, base_topic);
  json += "\",\"ha_prefix\":\"";
  appendJsonEscaped(json, ha_prefix);
  // Suggest the device name, manufacturer and model to the Bridge so its
  // optional device-name fields do not remain empty until edited manually.
  // The Bridge still gives an explicitly configured name precedence; see
  // entry_device_name().
  json += "\",\"device_name\":\"";
  appendJsonEscaped(json, Device::displayName());
  json += "\",\"manufacturer\":\"HomeTiles\",\"model\":\"";
  appendJsonEscaped(json, Device::profile().key);
  json += "\",\"sensors\":";
  appendSensorsJson(json, data.sensors_text);
  if (data.has_configured_sensors) {
    json += ",\"configured_sensors\":";
    appendSensorsJson(json, data.configured_sensors_text);
  }
  json += ",\"capabilities\":{\"view_navigation\":true,\"battery_soc\":";
  json += batteryStateSupportsMeasurement() ? "true" : "false";
  json += ",\"legacy_external_temperature\":false}";
  if (data.has_editable_lists) {
    json += ",\"numbers\":";
    appendSensorsJson(json, data.numbers_text);
    json += ",\"selects\":";
    appendSensorsJson(json, data.selects_text);
    json += ",\"datetimes\":";
    appendSensorsJson(json, data.datetimes_text);
  }
  json += ",\"binary_sensors\":";
  appendSensorsJson(json, data.binary_sensors_text);

  json += ",\"scene_map\":";
  appendSceneMapJson(json, data.scene_alias_text);
  hardwareIo.appendBridgeJson(json);
  json += "}";
  return json;
}

void HaBridgeConfig::appendJsonEscaped(String& out, const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '\"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
}

String HaBridgeConfig::normalizeLine(const String& line) {
  String t = line;
  t.replace("\r", "");
  t.trim();
  return t;
}

void HaBridgeConfig::appendSensorsJson(String& out, const String& text) {
  out += "[";
  bool first = true;
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String entry = normalizeLine(text.substring(start, end));
    if (entry.length()) {
      if (!first) out += ",";
      out += "\"";
      appendJsonEscaped(out, entry);
      out += "\"";
      first = false;
    }
    start = end + 1;
  }
  out += "]";
}

void HaBridgeConfig::appendSceneMapJson(String& out, const String& text) {
  out += "{";
  bool first = true;
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String entry = normalizeLine(text.substring(start, end));
    if (entry.length()) {
      int eq = entry.indexOf('=');
      if (eq > 0) {
        String alias = entry.substring(0, eq);
        String scene = entry.substring(eq + 1);
        alias.trim();
        scene.trim();
        alias.toLowerCase();
        if (alias.length() && scene.length()) {
          if (!first) out += ",";
          out += "\"";
          appendJsonEscaped(out, alias);
          out += "\":\"";
          appendJsonEscaped(out, scene);
          out += "\"";
          first = false;
        }
      }
    }
    start = end + 1;
  }
  out += "}";
}

static void parseArraySection(const String& body, String& out) {
  int start = body.indexOf('[');
  int end = body.indexOf(']', start);
  if (start < 0 || end < start) {
    out = "";
    return;
  }
  String result;
  String list = body.substring(start + 1, end);
  int pos = 0;
  while (pos < list.length()) {
    int q1 = list.indexOf('"', pos);
    if (q1 < 0) break;
    int q2 = list.indexOf('"', q1 + 1);
    if (q2 < 0) break;
    String value = list.substring(q1 + 1, q2);
    value.trim();
    if (value.length()) {
      if (result.length()) result += '\n';
      result += value;
    }
    pos = q2 + 1;
  }
  out = result;
}

static void parseObjectSection(const String& body, String& out) {
  int start = body.indexOf('{');
  int end = findMatchingJsonObjectEnd(body, start);
  if (start < 0 || end < start) {
    out = "";
    return;
  }
  String result;
  String obj = body.substring(start + 1, end);
  int pos = 0;
  while (pos < obj.length()) {
    int k1 = obj.indexOf('"', pos);
    if (k1 < 0) break;
    int k2 = obj.indexOf('"', k1 + 1);
    if (k2 < 0) break;
    String key = obj.substring(k1 + 1, k2);
    int colon = obj.indexOf(':', k2);
    if (colon < 0) break;
    int v1 = obj.indexOf('"', colon);
    if (v1 < 0) break;
    int v2 = obj.indexOf('"', v1 + 1);
    if (v2 < 0) break;
    String value = obj.substring(v1 + 1, v2);
    key.trim();
    value.trim();
    if (key.length() && value.length()) {
      if (result.length()) result += '\n';
      result += key + "=" + value;
    }
    pos = v2 + 1;
  }
  out = result;
}

static int findMatchingJsonObjectEnd(const String& body, int object_start) {
  if (object_start < 0 || object_start >= body.length() || body.charAt(object_start) != '{') {
    return -1;
  }

  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  for (int i = object_start; i < body.length(); ++i) {
    char c = body.charAt(i);

    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) return i;
      if (depth < 0) return -1;
    }
  }

  return -1;
}

static int findMatchingJsonArrayEnd(const String& body, int array_start) {
  if (array_start < 0 || array_start >= body.length() || body.charAt(array_start) != '[') return -1;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (int i = array_start; i < body.length(); ++i) {
    const char c = body.charAt(i);
    if (in_string) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') in_string = true;
    else if (c == '[') ++depth;
    else if (c == ']' && --depth == 0) return i;
  }
  return -1;
}

bool HaBridgeConfig::applyJson(const char* json_payload, bool* out_reload, bool* out_icons_changed) {
  if (out_reload) {
    *out_reload = false;
  }
  if (out_icons_changed) {
    *out_icons_changed = false;
  }
  if (!json_payload || !*json_payload) {
    return false;
  }

  uint32_t t_copy0 = millis();
  String json = json_payload;
  HaBridgeConfigData merged = data;
  uint32_t t_start = millis();
  // merged = data deep-copies every String field of HaBridgeConfigData (7 text
  // blobs + 4 growing "key=value" maps) BEFORE any of the split-timed sections
  // below even start -- none of the "arrays=/meta=/energy=" numbers printed
  // further down include this. If it's ever the dominant cost, splitting it
  // out here is what will show it.
  if (t_start - t_copy0 >= 5) {
    Serial.printf("[Bridge] applyJson copy: %ums\n", (unsigned)(t_start - t_copy0));
  }

  int sensors_idx = json.indexOf("\"sensors\"");
  if (sensors_idx >= 0) {
    parseArraySection(json.substring(sensors_idx), merged.sensors_text);
  }

  int configured_sensors_idx = json.indexOf("\"configured_sensors\"");
  if (configured_sensors_idx >= 0) {
    parseArraySection(json.substring(configured_sensors_idx), merged.configured_sensors_text);
    merged.has_configured_sensors = true;
  } else if (sensors_idx >= 0) {
    merged.has_configured_sensors = false;
    merged.configured_sensors_text = "";
  }

  const int numbers_idx = json.indexOf("\"numbers\"");
  if (numbers_idx >= 0) parseArraySection(json.substring(numbers_idx), merged.numbers_text);
  const int selects_idx = json.indexOf("\"selects\"");
  if (selects_idx >= 0) parseArraySection(json.substring(selects_idx), merged.selects_text);
  const int datetimes_idx = json.indexOf("\"datetimes\"");
  if (numbers_idx >= 0 || selects_idx >= 0 || datetimes_idx >= 0) merged.has_editable_lists = true;
  if (datetimes_idx >= 0) parseArraySection(json.substring(datetimes_idx), merged.datetimes_text);
  int binary_sensors_idx = json.indexOf("\"binary_sensors\"");
  if (binary_sensors_idx >= 0) {
    parseArraySection(json.substring(binary_sensors_idx),
                      merged.binary_sensors_text);
  }

  int energy_idx = json.indexOf("\"energy\"");
  // A Bridge reply without an Energy block does not request deletion.
  // HA energy may still be initializing or the reply may be partial.
  // Preserve the last valid source list.

  int weathers_idx = json.indexOf("\"weathers\"");
  if (weathers_idx >= 0) {
    parseArraySection(json.substring(weathers_idx), merged.weathers_text);
  }

  int lights_idx = json.indexOf("\"lights\"");
  if (lights_idx >= 0) {
    parseArraySection(json.substring(lights_idx), merged.lights_text);
  }

  int switches_idx = json.indexOf("\"switches\"");
  if (switches_idx >= 0) {
    parseArraySection(json.substring(switches_idx), merged.switches_text);
  }

  int media_players_idx = json.indexOf("\"media_players\"");
  if (media_players_idx >= 0) {
    parseArraySection(json.substring(media_players_idx), merged.media_players_text);
  } else {
    merged.media_players_text = "";
  }

  int climates_idx = json.indexOf("\"climates\"");
  if (climates_idx >= 0) {
    parseArraySection(json.substring(climates_idx), merged.climates_text);
  }

  int covers_idx = json.indexOf("\"covers\"");
  if (covers_idx >= 0) {
    parseArraySection(json.substring(covers_idx), merged.covers_text);
  }

  int cameras_idx = json.indexOf("\"cameras\"");
  if (cameras_idx >= 0) {
    parseArraySection(json.substring(cameras_idx), merged.cameras_text);
  } else {
    merged.cameras_text = "";
  }

  int scene_idx = json.indexOf("\"scene_map\"");
  if (scene_idx >= 0) {
    parseObjectSection(json.substring(scene_idx), merged.scene_alias_text);
  }

  // Do not call lvglServiceDuringBlockingWork() inside applyJson(). Rendering
  // also processes touch, allowing navigation and grid-reload callbacks to
  // reenter this parser. A measured meta=3930ms spike included two nested
  // navigation clicks. Actual array, metadata and energy work now totals
  // about 80-100 ms; pause animation once and process touch on the next
  // normal loop pass instead of introducing seconds of reentrant rendering.
  uint32_t t_arrays = millis();

  const String prev_icons = data.entity_icons_map;
  parseSensorMetaSection(json, merged.sensor_units_map,
                         merged.sensor_names_map, merged.sensor_values_map,
                         merged.sensor_state_kinds_map);
  parseBinarySensorMetaSection(
      json, merged.sensor_names_map, merged.sensor_values_map);
  parseEntityNameSection(json, "media_player_meta", merged.sensor_names_map);
  parseEntityNameSection(json, "climate_meta", merged.sensor_names_map);
  parseEntityNameSection(json, "cover_meta", merged.sensor_names_map);
  parseEntityNameSection(json, "camera_meta", merged.sensor_names_map);
  parseEntityNameSection(json, "editable_meta", merged.sensor_names_map);
  parseIconMetaSections(json, merged.entity_icons_map);
  if (!merged.entity_icons_map.length() && prev_icons.length()) {
    merged.entity_icons_map = prev_icons;
  }

  uint32_t t_meta = millis();

  if (energy_idx >= 0) {
    parseEnergySection(json.substring(energy_idx),
                       merged.energy_text,
                       merged.sensor_units_map,
                       merged.sensor_names_map,
                       merged.entity_icons_map);
  }
  bool icon_map_changed = !mapEqualsIgnoringOrder(prev_icons, merged.entity_icons_map);
  if (out_icons_changed) {
    *out_icons_changed = icon_map_changed;
  }

  uint32_t t_energy = millis();
  Serial.printf("[Bridge] applyJson split: arrays=%ums meta=%ums energy=%ums\n",
                (unsigned)(t_arrays - t_start), (unsigned)(t_meta - t_arrays), (unsigned)(t_energy - t_meta));

  for (size_t i = 0; i < HA_SENSOR_SLOT_COUNT; ++i) {
    if (merged.sensor_slots[i].length() &&
        !sensorExistsInList(merged.sensors_text, merged.sensor_slots[i])) {
      merged.sensor_slots[i] = "";
      merged.sensor_titles[i] = "";
      merged.sensor_custom_units[i] = "";
    }
  }
  for (size_t i = 0; i < HA_SCENE_SLOT_COUNT; ++i) {
    if (merged.scene_slots[i].length() &&
        !aliasExistsInList(merged.scene_alias_text, merged.scene_slots[i])) {
      merged.scene_slots[i] = "";
      merged.scene_titles[i] = "";
    }
  }

  bool needs_reload = !bridgeConfigEquals(merged, data);
  if (needs_reload) {
    bool ok = save(merged);
    if (ok) {
      Serial.printf("[Bridge] Configuration received from Home Assistant: "
                    "sensors=%d binary=%d energy=%d weather=%d lights=%d switches=%d "
                    "media=%d climate=%d covers=%d cameras=%d scenes=%d\n",
                    countListEntries(data.sensors_text),
                    countListEntries(data.binary_sensors_text),
                    countListEntries(data.energy_text),
                    countListEntries(data.weathers_text),
                    countListEntries(data.lights_text),
                    countListEntries(data.switches_text),
                    countListEntries(data.media_players_text),
                    countListEntries(data.climates_text),
                    countListEntries(data.covers_text),
                    countListEntries(data.cameras_text),
                    countMapEntries(data.scene_alias_text));
      if (out_reload) {
        *out_reload = true;
      }
    }
    return ok;
  }

  // No structural change: keep the latest meta in memory without triggering a reload.
  data = merged;
  rebuildEntityIndexes();
  return true;
}

bool HaBridgeConfig::applyIconUpdate(const char* json_payload) {
  if (!json_payload || !*json_payload) return false;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, json_payload);
  if (err) return false;

  JsonObject obj = doc.as<JsonObject>();
  if (obj.isNull()) return false;

  bool changed = false;
  for (JsonPair kv : obj) {
    const char* entity_key = kv.key().c_str();
    if (!entity_key || !*entity_key) continue;

    String entity_id(entity_key);
    String next_icon;
    if (!kv.value().isNull()) {
      const char* icon = kv.value().as<const char*>();
      if (icon) {
        next_icon = icon;
        next_icon.trim();
      }
    }

    String current_icon = findEntityIcon(entity_id);
    if (!next_icon.length()) {
      if (current_icon.length() && removeKeyValueMapEntry(data.entity_icons_map, entity_id)) {
        indexErase(icons_index_, entity_id);
        changed = true;
      }
      continue;
    }

    if (!current_icon.equals(next_icon)) {
      upsertKeyValueMap(data.entity_icons_map, entity_id, next_icon);
      indexPut(icons_index_, entity_id, next_icon);
      changed = true;
    }
  }
  return changed;
}

static bool sensorExistsInList(const String& list, const String& candidate) {
  if (!candidate.length()) return true;
  int start = 0;
  while (start < list.length()) {
    int end = list.indexOf('\n', start);
    if (end < 0) end = list.length();
    String line = list.substring(start, end);
    line.trim();
    if (line.equalsIgnoreCase(candidate)) {
      return true;
    }
    start = end + 1;
  }
  return false;
}

static bool aliasExistsInList(const String& list, const String& alias) {
  if (!alias.length()) return true;
  int start = 0;
  while (start < list.length()) {
    int end = list.indexOf('\n', start);
    if (end < 0) end = list.length();
    String line = list.substring(start, end);
    int eq = line.indexOf('=');
    if (eq > 0) {
      String key = line.substring(0, eq);
      key.trim();
      key.toLowerCase();
      if (key == alias || key.equalsIgnoreCase(alias)) {
        return true;
      }
    }
    start = end + 1;
  }
  return false;
}

static String normalizeLineLocal(const String& line) {
  String t = line;
  t.replace("\r", "");
  t.trim();
  return t;
}

static int countListEntries(const String& text) {
  int count = 0;
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = normalizeLineLocal(text.substring(start, end));
    if (line.length()) {
      ++count;
    }
    start = end + 1;
  }
  return count;
}

static int countMapEntries(const String& text) {
  int count = 0;
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = normalizeLineLocal(text.substring(start, end));
    if (line.length()) {
      int eq = line.indexOf('=');
      if (eq > 0) {
        String key = line.substring(0, eq);
        String value = line.substring(eq + 1);
        key.trim();
        value.trim();
        if (key.length() && value.length()) {
          ++count;
        }
      }
    }
    start = end + 1;
  }
  return count;
}

static bool listEqualsIgnoringOrder(const String& a, const String& b) {
  if (countListEntries(a) != countListEntries(b)) return false;

  int start = 0;
  while (start < a.length()) {
    int end = a.indexOf('\n', start);
    if (end < 0) end = a.length();
    String line = normalizeLineLocal(a.substring(start, end));
    if (line.length() && !sensorExistsInList(b, line)) {
      return false;
    }
    start = end + 1;
  }

  start = 0;
  while (start < b.length()) {
    int end = b.indexOf('\n', start);
    if (end < 0) end = b.length();
    String line = normalizeLineLocal(b.substring(start, end));
    if (line.length() && !sensorExistsInList(a, line)) {
      return false;
    }
    start = end + 1;
  }

  return true;
}

static bool mapEqualsIgnoringOrder(const String& a, const String& b) {
  if (countMapEntries(a) != countMapEntries(b)) return false;

  int start = 0;
  while (start < a.length()) {
    int end = a.indexOf('\n', start);
    if (end < 0) end = a.length();
    String line = normalizeLineLocal(a.substring(start, end));
    if (line.length()) {
      int eq = line.indexOf('=');
      if (eq > 0) {
        String key = line.substring(0, eq);
        String value = line.substring(eq + 1);
        key.trim();
        value.trim();
        if (key.length()) {
          String other = lookupKeyValue(b, key);
          if (!other.equals(value)) {
            return false;
          }
        }
      }
    }
    start = end + 1;
  }

  start = 0;
  while (start < b.length()) {
    int end = b.indexOf('\n', start);
    if (end < 0) end = b.length();
    String line = normalizeLineLocal(b.substring(start, end));
    if (line.length()) {
      int eq = line.indexOf('=');
      if (eq > 0) {
        String key = line.substring(0, eq);
        String value = line.substring(eq + 1);
        key.trim();
        value.trim();
        if (key.length()) {
          String other = lookupKeyValue(a, key);
          if (!other.equals(value)) {
            return false;
          }
        }
      }
    }
    start = end + 1;
  }

  return true;
}

static bool bridgeConfigEquals(const HaBridgeConfigData& a, const HaBridgeConfigData& b) {
  if (!listEqualsIgnoringOrder(a.sensors_text, b.sensors_text)) return false;
  if (!listEqualsIgnoringOrder(a.numbers_text, b.numbers_text)) return false;
  if (!listEqualsIgnoringOrder(a.selects_text, b.selects_text)) return false;
  if (!listEqualsIgnoringOrder(a.datetimes_text, b.datetimes_text)) return false;
  if (!listEqualsIgnoringOrder(a.binary_sensors_text, b.binary_sensors_text)) {
    return false;
  }
  if (!listEqualsIgnoringOrder(a.energy_text, b.energy_text)) return false;
  if (!listEqualsIgnoringOrder(a.weathers_text, b.weathers_text)) return false;
  if (!listEqualsIgnoringOrder(a.lights_text, b.lights_text)) return false;
  if (!listEqualsIgnoringOrder(a.switches_text, b.switches_text)) return false;
  if (!listEqualsIgnoringOrder(a.media_players_text, b.media_players_text)) return false;
  if (!listEqualsIgnoringOrder(a.climates_text, b.climates_text)) return false;
  if (!listEqualsIgnoringOrder(a.covers_text, b.covers_text)) return false;
  if (!listEqualsIgnoringOrder(a.cameras_text, b.cameras_text)) return false;
  if (!mapEqualsIgnoringOrder(a.scene_alias_text, b.scene_alias_text)) return false;

  for (size_t i = 0; i < HA_SENSOR_SLOT_COUNT; ++i) {
    if (!a.sensor_slots[i].equals(b.sensor_slots[i])) return false;
    if (!a.sensor_titles[i].equals(b.sensor_titles[i])) return false;
    if (!a.sensor_custom_units[i].equals(b.sensor_custom_units[i])) return false;
    if (a.sensor_colors[i] != b.sensor_colors[i]) return false;
  }
  for (size_t i = 0; i < HA_SCENE_SLOT_COUNT; ++i) {
    if (!a.scene_slots[i].equals(b.scene_slots[i])) return false;
    if (!a.scene_titles[i].equals(b.scene_titles[i])) return false;
    if (a.scene_colors[i] != b.scene_colors[i]) return false;
  }
  return true;
}

static bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

static uint8_t hexValue(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
  return 0;
}

static void appendUtf8(String& out, uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    out += static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    out += static_cast<char>(0xC0 | (codepoint >> 6));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (codepoint >> 12));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (codepoint >> 18));
    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

static String decodeJsonEscapes(const String& value) {
  if (!value.length()) return value;
  String out;
  out.reserve(value.length());
  const size_t len = value.length();

  for (size_t i = 0; i < len; ++i) {
    char c = value.charAt(i);
    if (c != '\\' || i + 1 >= len) {
      out += c;
      continue;
    }

    char esc = value.charAt(i + 1);
    if (esc == 'u' && (i + 5) < len) {
      if (isHexDigit(value.charAt(i + 2)) &&
          isHexDigit(value.charAt(i + 3)) &&
          isHexDigit(value.charAt(i + 4)) &&
          isHexDigit(value.charAt(i + 5))) {
        uint16_t code = (hexValue(value.charAt(i + 2)) << 12) |
                        (hexValue(value.charAt(i + 3)) << 8) |
                        (hexValue(value.charAt(i + 4)) << 4) |
                        (hexValue(value.charAt(i + 5)));

        i += 5;

        if (code >= 0xD800 && code <= 0xDBFF) {
          if ((i + 6) < len && value.charAt(i + 1) == '\\' && value.charAt(i + 2) == 'u' &&
              isHexDigit(value.charAt(i + 3)) && isHexDigit(value.charAt(i + 4)) &&
              isHexDigit(value.charAt(i + 5)) && isHexDigit(value.charAt(i + 6))) {
            uint16_t low = (hexValue(value.charAt(i + 3)) << 12) |
                           (hexValue(value.charAt(i + 4)) << 8) |
                           (hexValue(value.charAt(i + 5)) << 4) |
                           (hexValue(value.charAt(i + 6)));
            if (low >= 0xDC00 && low <= 0xDFFF) {
              uint32_t codepoint = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
              appendUtf8(out, codepoint);
              i += 6;
              continue;
            }
          }
        }

        appendUtf8(out, code);
        continue;
      }
    }

    switch (esc) {
      case '\"': out += '\"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      default: out += esc; break;
    }
    i += 1;
  }

  return out;
}

static bool extractStringField(const String& object, const char* key, String& out) {
  if (!key || !*key) return false;
  String pattern = String("\"") + key + "\"";
  int idx = object.indexOf(pattern);
  if (idx < 0) return false;
  int colon = object.indexOf(':', idx);
  if (colon < 0) return false;
  int q1 = object.indexOf('"', colon);
  if (q1 < 0) return false;
  int q2 = object.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  out = object.substring(q1 + 1, q2);
  out = decodeJsonEscapes(out);
  out.trim();
  return out.length() > 0;
}

static const char* energyIconForCategory(const String& category, const String& id, const String& unit) {
  String cat = category;
  cat.trim();
  cat.toLowerCase();
  String entity = id;
  entity.toLowerCase();
  String u = unit;
  u.toLowerCase();

  if (entity.endsWith("_cost") || u == "eur" || u == "euro") return "currency-eur";
  if (cat == "solar") return "solar-power";
  if (cat == "grid") return "transmission-tower";
  if (cat == "battery") return "battery-charging";
  if (cat == "gas") return "fire";
  if (cat == "water" || cat == "device_water") return "water";
  return "lightning-bolt";
}

static void appendUniqueListEntry(String& list, const String& entry) {
  if (!entry.length()) return;
  if (sensorExistsInList(list, entry)) return;
  if (list.length()) list += '\n';
  list += entry;
}

static void parseEnergySection(const String& body,
                               String& energy,
                               String& units,
                               String& names,
                               String& icons) {
  energy = "";
  int energy_idx = body.indexOf("\"energy\"");
  int array_start = body.indexOf('[', energy_idx >= 0 ? energy_idx : 0);
  int array_end = body.indexOf(']', array_start);
  if (array_start < 0 || array_end < array_start) {
    return;
  }

  // Collect all per-entry updates first and apply each map (names/units/
  // icons) in a single combined pass afterwards (upsertKeyValueMapBatch)
  // instead of calling upsertKeyValueMap() up to 3x per entry -- with ~20-30
  // energy entries that used to rebuild the whole map up to 90 times,
  // measured at 600+ms once the persistent map had grown over a session's
  // uptime (see upsertKeyValueMapBatch's comment for why the combined pass
  // is so much cheaper).
  std::vector<KeyValueUpdate> name_updates;
  std::vector<KeyValueUpdate> unit_updates;
  std::vector<KeyValueUpdate> icon_updates;

  // Do not service LVGL per entry here. Batching reduced actual work to
  // 14 ms parsing plus 2 ms map updates, but every pump rendered a roughly
  // 42 ms animation frame and processed touch. Folder-navigation callbacks
  // then reentered Bridge parsing, measured as 827 ms of energy-section
  // pumping with unresponsive buttons. One roughly 100 ms animation pause
  // is preferable to seconds of reentrant intermediate rendering.
  const uint32_t t_total0 = millis();
  unsigned entry_count = 0;

  String segment = body.substring(array_start + 1, array_end);
  int obj_start = segment.indexOf('{');
  while (obj_start >= 0) {
    int obj_end = segment.indexOf('}', obj_start);
    if (obj_end < 0) break;

    String object = segment.substring(obj_start, obj_end + 1);
    String id;
    if (extractStringField(object, "id", id)) {
      appendUniqueListEntry(energy, id);
      ++entry_count;

      String name;
      if (extractStringField(object, "name", name)) {
        name_updates.push_back({id, name});
      }

      String unit;
      if (extractStringField(object, "unit", unit)) {
        unit_updates.push_back({id, unit});
      }

      String category;
      extractStringField(object, "category", category);
      icon_updates.push_back({id, energyIconForCategory(category, id, unit)});
    }

    obj_start = segment.indexOf('{', obj_end + 1);
  }

  const uint32_t t_batch0 = millis();
  upsertKeyValueMapBatch(names, name_updates);
  upsertKeyValueMapBatch(units, unit_updates);
  upsertKeyValueMapBatch(icons, icon_updates);

  const uint32_t t_end = millis();
  if (t_end - t_total0 >= 10) {
    Serial.printf("[Bridge]   energy split: total=%ums parse=%ums batch=%ums entries=%u\n",
                  (unsigned)(t_end - t_total0),
                  (unsigned)(t_batch0 - t_total0),
                  (unsigned)(t_end - t_batch0),
                  entry_count);
  }
}

static void parseSensorMetaSection(const String& body, String& units,
                                   String& names, String& values,
                                   String& state_kinds) {
  units = "";
  names = "";
  values = "";
  state_kinds = "";
  int meta_idx = body.indexOf("\"sensor_meta\"");
  if (meta_idx < 0) {
    return;
  }
  int array_start = body.indexOf('[', meta_idx);
  int array_end = body.indexOf(']', array_start);
  if (array_start < 0 || array_end < array_start) {
    return;
  }
  String segment = body.substring(array_start + 1, array_end);
  int obj_start = segment.indexOf('{');
  while (obj_start >= 0) {
    int obj_end = segment.indexOf('}', obj_start);
    if (obj_end < 0) break;
    String object = segment.substring(obj_start, obj_end + 1);
    String entity;
    if (!extractStringField(object, "entity_id", entity)) {
      obj_start = segment.indexOf('{', obj_end + 1);
      continue;
    }
    String unit;
    if (extractStringField(object, "unit", unit)) {
      if (units.length()) units += '\n';
      units += entity + "=" + unit;
    }
    String name;
    if (extractStringField(object, "name", name)) {
      if (names.length()) names += '\n';
      names += entity + "=" + name;
    }
    String value;
    if (extractStringField(object, "value", value)) {
      if (values.length()) values += '\n';
      values += entity + "=" + value;
    }
    String state_kind;
    if (extractStringField(object, "state_kind", state_kind)) {
      state_kind.trim();
      state_kind.toLowerCase();
      if (state_kind == "number" || state_kind == "state") {
        if (state_kinds.length()) state_kinds += '\n';
        state_kinds += entity + "=" + state_kind;
      }
    }
    obj_start = segment.indexOf('{', obj_end + 1);
  }
}

static void parseBinarySensorMetaSection(const String& body, String& names,
                                         String& values) {
  const int meta_idx = body.indexOf("\"binary_sensor_meta\"");
  if (meta_idx < 0) return;
  const int array_start = body.indexOf('[', meta_idx);
  const int array_end = body.indexOf(']', array_start);
  if (array_start < 0 || array_end < array_start) return;

  const String segment = body.substring(array_start + 1, array_end);
  int object_start = segment.indexOf('{');
  while (object_start >= 0) {
    const int object_end = findMatchingJsonObjectEnd(segment, object_start);
    if (object_end < 0) break;
    const String object = segment.substring(object_start, object_end + 1);

    DynamicJsonDocument item_doc(1024);
    if (deserializeJson(item_doc, object) == DeserializationError::Ok) {
      JsonObjectConst item = item_doc.as<JsonObjectConst>();
      const char* entity =
          item["entity_id"] | static_cast<const char*>(nullptr);
      if (entity && *entity) {
        const char* name = item["name"] | static_cast<const char*>(nullptr);
        if (name && *name) upsertKeyValueMap(names, entity, name);

        const char* raw_state =
            item["state"] | static_cast<const char*>(nullptr);
        String state = raw_state ? String(raw_state) : String();
        state.trim();
        state.toLowerCase();

        DynamicJsonDocument state_doc(384);
        if (state == "on" || state == "off" || state == "unknown" ||
            state == "unavailable") {
          state_doc["state"] = state;
        } else {
          // The metadata array is authoritative for configured binary
          // sensors. An entity without an HA state is explicitly missing;
          // retain JSON null so an older on/off value cannot survive a fresh
          // bridge snapshot or be confused with unknown/unavailable.
          state_doc["state"] = nullptr;
        }
        if (item.containsKey("available")) {
          JsonVariantConst available = item["available"];
          if (available.is<bool>()) {
            state_doc["available"] = available.as<bool>();
          } else if (available.isNull()) {
            state_doc["available"] = nullptr;
          }
        }
        if (item.containsKey("device_class")) {
          JsonVariantConst device_class = item["device_class"];
          if (device_class.is<const char*>()) {
            String normalized = device_class.as<const char*>();
            normalized.trim();
            normalized.toLowerCase();
            state_doc["device_class"] = normalized.substring(0, 23);
          } else if (device_class.isNull()) {
            state_doc["device_class"] = nullptr;
          }
        }
        if (item.containsKey("icon")) {
          JsonVariantConst icon = item["icon"];
          if (icon.is<const char*>()) {
            String normalized = icon.as<const char*>();
            normalized.trim();
            state_doc["icon"] = normalized.substring(0, 39);
          } else if (icon.isNull()) {
            state_doc["icon"] = nullptr;
          }
        }
        if (item.containsKey("last_changed")) {
          state_doc["last_changed"].set(item["last_changed"]);
        }
        String state_payload;
        serializeJson(state_doc, state_payload);
        upsertKeyValueMap(values, entity, state_payload);
      }
    }
    object_start = segment.indexOf('{', object_end + 1);
  }
}

static void parseEntityNameSection(const String& body, const char* key, String& names) {
  if (!key || !*key) return;
  String pattern = String("\"") + key + "\"";
  int meta_idx = body.indexOf(pattern);
  if (meta_idx < 0) return;
  int array_start = body.indexOf('[', meta_idx);
  int array_end = findMatchingJsonArrayEnd(body, array_start);
  if (array_start < 0 || array_end < array_start) return;

  String segment = body.substring(array_start + 1, array_end);
  int obj_start = segment.indexOf('{');
  while (obj_start >= 0) {
    int obj_end = findMatchingJsonObjectEnd(segment, obj_start);
    if (obj_end < 0) break;
    String object = segment.substring(obj_start, obj_end + 1);
    String entity;
    String name;
    if (extractStringField(object, "entity_id", entity) &&
        extractStringField(object, "name", name)) {
      upsertKeyValueMap(names, entity, name);
    }
    obj_start = segment.indexOf('{', obj_end + 1);
  }
}

static void parseEntityIconSection(const String& body, const char* key, String& icons) {
  if (!key || !*key) return;
  String pattern = String("\"") + key + "\"";
  int meta_idx = body.indexOf(pattern);
  if (meta_idx < 0) {
    return;
  }
  int array_start = body.indexOf('[', meta_idx);
  // View states contain stable IDs such as [t:13]. Delimiters inside JSON
  // strings must not discard this entity or the entities following it.
  int array_end = findMatchingJsonArrayEnd(body, array_start);
  if (array_start < 0 || array_end < array_start) {
    return;
  }
  String segment = body.substring(array_start + 1, array_end);
  int obj_start = segment.indexOf('{');
  while (obj_start >= 0) {
    int obj_end = findMatchingJsonObjectEnd(segment, obj_start);
    if (obj_end < 0) break;
    String object = segment.substring(obj_start, obj_end + 1);
    String entity;
    if (!extractStringField(object, "entity_id", entity)) {
      obj_start = segment.indexOf('{', obj_end + 1);
      continue;
    }
    String icon;
    if (extractStringField(object, "icon", icon)) {
      if (icons.length()) icons += '\n';
      icons += entity + "=" + icon;
    }
    obj_start = segment.indexOf('{', obj_end + 1);
  }
}

static void parseIconMetaSections(const String& body, String& icons) {
  icons = "";
  parseEntityIconSection(body, "sensor_meta", icons);
  parseEntityIconSection(body, "binary_sensor_meta", icons);
  parseEntityIconSection(body, "editable_meta", icons);
  parseEntityIconSection(body, "weather_meta", icons);
  parseEntityIconSection(body, "light_meta", icons);
  parseEntityIconSection(body, "switch_meta", icons);
  parseEntityIconSection(body, "scene_meta", icons);
  parseEntityIconSection(body, "media_player_meta", icons);
  parseEntityIconSection(body, "climate_meta", icons);
  parseEntityIconSection(body, "cover_meta", icons);
  parseEntityIconSection(body, "camera_meta", icons);
}

static String lookupKeyValue(const String& text, const String& key) {
  if (!key.length()) return "";
  // The old version allocated 2-3 new String objects (substring()) PER LINE
  // scanned. sensor_values_map etc. can hold every sensor HA exposes, not
  // just the ones on tiles -- with a bridge-cache refresh calling this once
  // per tile across every folder, a single call landing on a match near the
  // end of a long map measured as a multi-hundred-ms block by itself, which
  // no amount of yielding between calls can fix (the block IS one call).
  // Scan the underlying buffer directly instead; only the winning line's
  // value gets allocated (one substring() call, at the very end).
  const char* buf = text.c_str();
  const int len = static_cast<int>(text.length());
  const char* key_buf = key.c_str();
  const int key_len = static_cast<int>(key.length());

  int line_start = 0;
  while (line_start < len) {
    int line_end = line_start;
    while (line_end < len && buf[line_end] != '\n') ++line_end;

    int eq = line_start;
    while (eq < line_end && buf[eq] != '=') ++eq;

    if (eq > line_start && eq < line_end) {
      int lhs_start = line_start;
      int lhs_end = eq;
      while (lhs_start < lhs_end && isspace(static_cast<unsigned char>(buf[lhs_start]))) ++lhs_start;
      while (lhs_end > lhs_start && isspace(static_cast<unsigned char>(buf[lhs_end - 1]))) --lhs_end;

      if (lhs_end - lhs_start == key_len && strncasecmp(buf + lhs_start, key_buf, key_len) == 0) {
        int rhs_start = eq + 1;
        int rhs_end = line_end;
        while (rhs_start < rhs_end && isspace(static_cast<unsigned char>(buf[rhs_start]))) ++rhs_start;
        while (rhs_end > rhs_start && isspace(static_cast<unsigned char>(buf[rhs_end - 1]))) --rhs_end;
        return text.substring(rhs_start, rhs_end);
      }
    }
    line_start = line_end + 1;
  }
  return "";
}

static void upsertKeyValueMap(String& text, const String& key, const String& value) {
  if (!key.length() || !value.length()) return;

  // Same allocation-per-line problem as lookupKeyValue() above (2 substring()
  // calls per scanned line), except this one also rebuilds the whole map on
  // every call -- parseEnergySection() calls this up to 3x per entry, so with
  // sensor_values_map-sized maps a single call could itself be a large block.
  // Scan the raw buffer for the matching line; only allocate a substring for
  // lines that get copied through unchanged (one call per kept line instead
  // of two), and reserve the output buffer once instead of growing it
  // incrementally.
  const char* buf = text.c_str();
  const int len = static_cast<int>(text.length());
  const char* key_buf = key.c_str();
  const int key_len = static_cast<int>(key.length());

  String newMap;
  newMap.reserve(static_cast<unsigned int>(len) + key.length() + value.length() + 2);
  bool found = false;

  int line_start = 0;
  while (line_start < len) {
    int line_end = line_start;
    while (line_end < len && buf[line_end] != '\n') ++line_end;

    int eq = line_start;
    while (eq < line_end && buf[eq] != '=') ++eq;

    bool is_match = false;
    if (eq > line_start && eq < line_end) {
      int lhs_start = line_start;
      int lhs_end = eq;
      while (lhs_start < lhs_end && isspace(static_cast<unsigned char>(buf[lhs_start]))) ++lhs_start;
      while (lhs_end > lhs_start && isspace(static_cast<unsigned char>(buf[lhs_end - 1]))) --lhs_end;
      is_match = (lhs_end - lhs_start == key_len && strncasecmp(buf + lhs_start, key_buf, key_len) == 0);
    }

    if (is_match) {
      if (newMap.length()) newMap += '\n';
      newMap += key;
      newMap += '=';
      newMap += value;
      found = true;
    } else if (line_end > line_start) {
      if (newMap.length()) newMap += '\n';
      newMap += text.substring(line_start, line_end);
    }

    line_start = line_end + 1;
  }

  if (!found) {
    if (newMap.length()) newMap += '\n';
    newMap += key;
    newMap += '=';
    newMap += value;
  }

  text = newMap;
}

// upsertKeyValueMap() rebuilds (copies) the WHOLE map on every call. Callers
// that update many keys from a loop (parseEnergySection: up to 3 maps x
// ~20-30 entries; parseEntityNameSection: 1 map x N media players) used to
// call it once per key -- against an already-large persistent map (grows
// over a session's uptime as more distinct entities get merged in) that is
// O(N * map_size) total copying, measured at 600+ms for a single applyJson()
// once the map had grown. Collecting all updates first and doing ONE
// combined pass drops the copy volume to O(map_size) (each line gets copied
// at most once); matching a line against the pending updates is still O(N)
// per line, but that's cheap key comparisons, not full-map memcpy, so it
// stays negligible even for map_size in the thousands.
static void upsertKeyValueMapBatch(String& text, const std::vector<KeyValueUpdate>& updates) {
  if (updates.empty()) return;

  const char* buf = text.c_str();
  const int len = static_cast<int>(text.length());
  std::vector<bool> applied(updates.size(), false);

  String newMap;
  size_t extra = 0;
  for (const auto& u : updates) extra += u.key.length() + u.value.length() + 2;
  newMap.reserve(static_cast<unsigned int>(len) + extra);

  int line_start = 0;
  while (line_start < len) {
    int line_end = line_start;
    while (line_end < len && buf[line_end] != '\n') ++line_end;

    int eq = line_start;
    while (eq < line_end && buf[eq] != '=') ++eq;

    int match_idx = -1;
    if (eq > line_start && eq < line_end) {
      int lhs_start = line_start;
      int lhs_end = eq;
      while (lhs_start < lhs_end && isspace(static_cast<unsigned char>(buf[lhs_start]))) ++lhs_start;
      while (lhs_end > lhs_start && isspace(static_cast<unsigned char>(buf[lhs_end - 1]))) --lhs_end;
      const int lhs_len = lhs_end - lhs_start;
      // Don't stop at the first match: if the same key appears more than
      // once in updates, the LAST one should win (matches calling
      // upsertKeyValueMap() once per entry in original list order).
      for (size_t i = 0; i < updates.size(); ++i) {
        if (updates[i].key.length() == (unsigned)lhs_len &&
            strncasecmp(buf + lhs_start, updates[i].key.c_str(), lhs_len) == 0) {
          match_idx = static_cast<int>(i);
        }
      }
    }

    if (match_idx >= 0) {
      if (newMap.length()) newMap += '\n';
      newMap += updates[match_idx].key;
      newMap += '=';
      newMap += updates[match_idx].value;
      applied[match_idx] = true;
    } else if (line_end > line_start) {
      if (newMap.length()) newMap += '\n';
      newMap += text.substring(line_start, line_end);
    }

    line_start = line_end + 1;
  }

  for (size_t i = 0; i < updates.size(); ++i) {
    if (applied[i] || !updates[i].key.length() || !updates[i].value.length()) continue;
    if (newMap.length()) newMap += '\n';
    newMap += updates[i].key;
    newMap += '=';
    newMap += updates[i].value;
  }

  text = newMap;
}

static bool removeKeyValueMapEntry(String& text, const String& key) {
  if (!key.length() || !text.length()) return false;

  String newMap = "";
  bool removed = false;

  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();

    String line = text.substring(start, end);
    int eq = line.indexOf('=');
    String entity = eq > 0 ? line.substring(0, eq) : line;
    entity.trim();

    if (entity.equalsIgnoreCase(key)) {
      removed = true;
    } else if (line.length() > 0) {
      if (newMap.length()) newMap += '\n';
      newMap += line;
    }

    start = end + 1;
  }

  if (removed) {
    text = newMap;
  }
  return removed;
}

// Maintain the index alongside blob updates in registerSensorMeta() and
// related paths. Transparent find(key.c_str()) avoids a temporary
// PsString; allocate only when inserting a new entry.
static void indexPut(HaEntityKeyMap& map, const String& key, const String& value) {
  auto it = map.find(key.c_str());
  if (it != map.end()) {
    it->second.assign(value.c_str(), value.length());
  } else {
    map.emplace(PsString(key.c_str(), key.length()),
                PsString(value.c_str(), value.length()));
  }
}

static void indexErase(HaEntityKeyMap& map, const String& key) {
  auto it = map.find(key.c_str());
  if (it != map.end()) map.erase(it);
}

// Parse a "key=value\n" blob into an index. Trimming matches
// lookupKeyValue(); emplace() keeps the first duplicate key, matching
// the blob scan even for malformed duplicate entries.
static void rebuildIndexFromBlob(const String& text, HaEntityKeyMap& out) {
  out.clear();
  const char* buf = text.c_str();
  const int len = static_cast<int>(text.length());

  int line_start = 0;
  while (line_start < len) {
    int line_end = line_start;
    while (line_end < len && buf[line_end] != '\n') ++line_end;

    int eq = line_start;
    while (eq < line_end && buf[eq] != '=') ++eq;

    if (eq > line_start && eq < line_end) {
      int lhs_start = line_start;
      int lhs_end = eq;
      while (lhs_start < lhs_end && isspace(static_cast<unsigned char>(buf[lhs_start]))) ++lhs_start;
      while (lhs_end > lhs_start && isspace(static_cast<unsigned char>(buf[lhs_end - 1]))) --lhs_end;

      int rhs_start = eq + 1;
      int rhs_end = line_end;
      while (rhs_start < rhs_end && isspace(static_cast<unsigned char>(buf[rhs_start]))) ++rhs_start;
      while (rhs_end > rhs_start && isspace(static_cast<unsigned char>(buf[rhs_end - 1]))) --rhs_end;

      if (lhs_end > lhs_start) {
        // Build PSRAM strings directly from the blob buffer without temporary
        // Arduino String copies in internal heap.
        out.emplace(PsString(buf + lhs_start, static_cast<size_t>(lhs_end - lhs_start)),
                    PsString(buf + rhs_start, static_cast<size_t>(rhs_end - rhs_start)));
      }
    }
    line_start = line_end + 1;
  }
}

// Approximate index size: red-black tree nodes (three pointers and a
// color word, 16 bytes on 32-bit targets), plus two PsString objects.
// Values longer than the 15-character SSO limit allocate PSRAM separately.
static size_t indexApproxBytes(const HaEntityKeyMap& m) {
  size_t bytes = 0;
  for (const auto& kv : m) {
    bytes += 16 + sizeof(kv);
    if (kv.first.capacity() > 15) bytes += kv.first.capacity() + 1;
    if (kv.second.capacity() > 15) bytes += kv.second.capacity() + 1;
  }
  return bytes;
}

void HaBridgeConfig::rebuildEntityIndexes() {
  pruneEditableValues();
  rebuildIndexFromBlob(data.sensor_units_map, units_index_);
  rebuildIndexFromBlob(data.sensor_names_map, names_index_);
  rebuildIndexFromBlob(data.sensor_values_map, values_index_);
  rebuildIndexFromBlob(data.sensor_state_kinds_map, state_kinds_index_);
  rebuildIndexFromBlob(data.entity_icons_map, icons_index_);
  // Report actual index size and PSRAM placement; rebuilding must not
  // reduce free internal heap.
  const size_t total_bytes = indexApproxBytes(units_index_) + indexApproxBytes(names_index_) +
                             indexApproxBytes(values_index_) + indexApproxBytes(state_kinds_index_) +
                             indexApproxBytes(icons_index_);
  Serial.printf("[Bridge] Entity index rebuilt: units=%u names=%u values=%u kinds=%u icons=%u (~%u KB PSRAM) | internal free: %u KB\n",
                (unsigned)units_index_.size(), (unsigned)names_index_.size(),
                (unsigned)values_index_.size(), (unsigned)state_kinds_index_.size(),
                (unsigned)icons_index_.size(),
                (unsigned)((total_bytes + 1023) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
}

void HaBridgeConfig::registerSensorMeta(const String& entity_id, const String& name, const String& unit) {
  if (entity_id.length() == 0) return;

  if (!sensorExistsInList(data.sensors_text, entity_id)) {
    if (data.sensors_text.length()) data.sensors_text += '\n';
    data.sensors_text += entity_id;
  }

  upsertKeyValueMap(data.sensor_names_map, entity_id, name);
  upsertKeyValueMap(data.sensor_units_map, entity_id, unit);
  // Update the index with the blob. upsertKeyValueMap() ignores empty
  // values; match that behavior so index and blob cannot diverge.
  if (name.length()) indexPut(names_index_, entity_id, name);
  if (unit.length()) indexPut(units_index_, entity_id, unit);
}

void HaBridgeConfig::updateEntityMeta(const String& entity_id, const String& name, const String& unit, const String& icon) {
  if (entity_id.length() == 0) return;
  if (name.length()) {
    upsertKeyValueMap(data.sensor_names_map, entity_id, name);
    indexPut(names_index_, entity_id, name);
  }
  if (unit.length()) {
    upsertKeyValueMap(data.sensor_units_map, entity_id, unit);
    indexPut(units_index_, entity_id, unit);
  }
  if (icon.length()) {
    upsertKeyValueMap(data.entity_icons_map, entity_id, icon);
    indexPut(icons_index_, entity_id, icon);
  }
}

void HaBridgeConfig::updateSensorValue(const String& entity_id, const String& value) {
  if (entity_id.length() == 0) return;

  // Parse sensor_values_map and update/add the value
  String& valuesMap = data.sensor_values_map;
  String newMap = "";
  bool found = false;

  int start = 0;
  while (start < valuesMap.length()) {
    int end = valuesMap.indexOf('\n', start);
    if (end < 0) end = valuesMap.length();

    String line = valuesMap.substring(start, end);
    int eq = line.indexOf('=');

    if (eq > 0) {
      String entity = line.substring(0, eq);
      entity.trim();

      if (entity.equalsIgnoreCase(entity_id)) {
        // Update existing entry
        if (newMap.length()) newMap += '\n';
        newMap += entity_id + "=" + value;
        found = true;
      } else {
        // Keep existing entry
        if (newMap.length()) newMap += '\n';
        newMap += line;
      }
    } else if (line.length() > 0) {
      // Keep non-empty lines without '='
      if (newMap.length()) newMap += '\n';
      newMap += line;
    }

    start = end + 1;
  }

  // Add new entry if not found
  if (!found) {
    if (newMap.length()) newMap += '\n';
    newMap += entity_id + "=" + value;
  }

  valuesMap = newMap;
  indexPut(values_index_, entity_id, value);
}

String HaBridgeConfig::findEditableValue(const String& entity_id) const {
  auto it = editable_values_index_.find(entity_id.c_str());
  return it == editable_values_index_.end() ? String() : String(it->second.c_str());
}

void HaBridgeConfig::updateEditableValue(const String& entity_id, const String& payload) {
  if (!entity_id.length() || payload.length() > 24576) return;
  auto it = editable_values_index_.find(entity_id.c_str());
  if (it == editable_values_index_.end() && editable_values_index_.size() >= 128) return;
  editable_values_index_[PsString(entity_id.c_str())] = PsString(payload.c_str());
}

void HaBridgeConfig::pruneEditableValues() {
  if (!data.has_editable_lists) return;
  const String selected = "\n" + data.numbers_text + "\n" + data.selects_text + "\n" + data.datetimes_text + "\n";
  bool removed = false;
  for (auto it = editable_values_index_.begin(); it != editable_values_index_.end();) {
    if (selected.indexOf("\n" + String(it->first.c_str()) + "\n") < 0) {
      it = editable_values_index_.erase(it); removed = true;
    } else ++it;
  }
  if (removed) editable_configuration_changed();
}
