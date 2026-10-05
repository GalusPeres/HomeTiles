#include "src/network/bridge/entity_search.h"

#include <ArduinoJson.h>

#include <cstring>
#include <vector>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/network/secure/command_channel.h"
#include "src/tiles/config/tile_config.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/web/server/auth/web_admin_auth.h"
#include "src/web/server/web_admin_utils.h"

namespace entity_search {
namespace {

// The Bridge answers in at most this many sealed parts (entity_search.py).
constexpr uint8_t kMaxParts = 6;
constexpr size_t kMaxQuery = 64;
// The picker loads further pages while scrolling (entity_search.py MAX_OFFSET).
constexpr uint32_t kMaxOffset = 5000;
// Below command_channel's sealed body limit with the JSON around the lists.
constexpr size_t kMaxReportBytes = 1900;
constexpr uint32_t kReportDelayMs = 1500;

struct Answer {
  uint32_t id = 0;
  uint8_t parts = 0;
  uint32_t received = 0;  // bit per part
  bool full = false;
  bool more = false;
  String items[kMaxParts];  // each part's results without the brackets
};

Answer g_answer;
uint32_t g_next_id = 1;
bool g_report_pending = true;
uint32_t g_report_due_ms = 0;
bool g_session_seen = false;
String g_last_report;
// "list:entity" of every entity in the last sent report.
std::vector<String> g_reported;

// Picker list -> tile type whose entity belongs to it.
struct ListTile {
  const char* list;
  TileType type;
};
const ListTile kListTiles[] = {
    {"sensors", TILE_SENSOR},   {"binary_sensors", TILE_BINARY_SENSOR}, {"numbers", TILE_NUMBER},
    {"selects", TILE_SELECT},   {"datetimes", TILE_DATETIME},           {"weathers", TILE_WEATHER},
    {"switches", TILE_SWITCH},  {"media", TILE_MEDIA},                  {"climates", TILE_CLIMATE},
    {"covers", TILE_COVER},     {"cameras", TILE_CAMERA},               {"locks", TILE_LOCK},
    {"alarm_panels", TILE_ALARM}, {"fans", TILE_FAN},
};

const char* listOfType(TileType type) {
  for (const ListTile& entry : kListTiles) {
    if (entry.type == type) return entry.list;
  }
  return nullptr;
}

bool knownList(const String& list) {
  for (const ListTile& entry : kListTiles) {
    if (list == entry.list) return true;
  }
  return false;
}

// The Bridge's released entities of one list, as the panel stored them.
std::vector<String> releasedOf(const char* list) {
  const HaBridgeConfigData& ha = haBridgeConfig.get();
  if (!strcmp(list, "switches")) {
    std::vector<String> ids = parseSensorList(ha.lights_text);
    for (const String& id : parseSensorList(ha.switches_text)) ids.push_back(id);
    return ids;
  }
  const String* text = !strcmp(list, "sensors")          ? &ha.sensors_text
                       : !strcmp(list, "binary_sensors") ? &ha.binary_sensors_text
                       : !strcmp(list, "numbers")        ? &ha.numbers_text
                       : !strcmp(list, "selects")        ? &ha.selects_text
                       : !strcmp(list, "datetimes")      ? &ha.datetimes_text
                       : !strcmp(list, "weathers")       ? &ha.weathers_text
                       : !strcmp(list, "media")          ? &ha.media_players_text
                       : !strcmp(list, "climates")       ? &ha.climates_text
                       : !strcmp(list, "covers")         ? &ha.covers_text
                       : !strcmp(list, "cameras")        ? &ha.cameras_text
                       : !strcmp(list, "locks")          ? &ha.locks_text
                       : !strcmp(list, "alarm_panels")   ? &ha.alarm_panels_text
                       : !strcmp(list, "fans")           ? &ha.fans_text
                                                         : nullptr;
  return text ? parseSensorList(*text) : std::vector<String>();
}

bool contains(const std::vector<String>& ids, const String& id) {
  for (const String& item : ids) {
    if (item.equalsIgnoreCase(id)) return true;
  }
  return false;
}

void publish(const char* leaf, const String& body) {
  const String topic = mqttTopics.deviceBase() + "/cmnd/" + leaf;
  networkManager.mqttEnqueuePublish(topic.c_str(), body.c_str(), false);
}

// {"lists":{list:[entity ids beyond the released ones]},"web_auth":bool}
// The Bridge serves reported entities in its released lists, so an entity
// once reported stays in the report while a tile uses it; otherwise the next
// configuration would drop it from the report and the Bridge would remove it
// again, over and over. `keys` receives "list:entity" of what is reported.
String buildTilesReport(std::vector<String>& keys) {
  keys.clear();
  struct Extra {
    const char* list;
    std::vector<String> ids;
    std::vector<String> released;
  };
  std::vector<Extra> extras;
  auto add = [&](TileType type, const char* entity) {
    const char* list = listOfType(type);
    if (!list || !entity || !entity[0]) return;
    Extra* slot = nullptr;
    for (Extra& item : extras) {
      if (item.list == list) slot = &item;
    }
    if (!slot) {
      extras.push_back({list, {}, releasedOf(list)});
      slot = &extras.back();
    }
    const String id(entity);
    const bool reported = contains(g_reported, String(list) + ':' + id);
    if ((reported || !contains(slot->released, id)) && !contains(slot->ids, id)) slot->ids.push_back(id);
  };
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const FolderEntry& folder : tileConfig.getFolders()) {
    if (!tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID)) continue;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) add(slots[i].type, slots[i].entity);
  }
  const TileGridConfig& screensaver = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    add(screensaver.tiles[i].type, screensaver.tiles[i].sensor_entity.c_str());
  }

  JsonDocument doc;
  JsonObject lists = doc["lists"].to<JsonObject>();
  doc["web_auth"] = web_admin_auth::enabled();
  size_t dropped = 0;
  for (const Extra& item : extras) {
    if (item.ids.empty()) continue;
    JsonArray array = lists[item.list].to<JsonArray>();
    for (const String& id : item.ids) {
      // Keep the report one sealed message; a panel with that many extra
      // entities keeps the first ones.
      if (measureJson(doc) + id.length() + 4 > kMaxReportBytes) {
        ++dropped;
        continue;
      }
      array.add(id);
      keys.push_back(String(item.list) + ':' + id);
    }
  }
  if (dropped) Serial.printf("[EntitySearch] %u tile entities not reported (size limit)\n", (unsigned)dropped);
  String body;
  serializeJson(doc, body);
  return body;
}

}  // namespace

bool available() {
  return command_channel::state() == command_channel::PairingState::Active &&
         haBridgeConfig.supportsEntitySearch();
}

uint32_t start(const String& query, const String& list, uint32_t offset) {
  if (!available() || !knownList(list)) return 0;
  if (offset > kMaxOffset) offset = kMaxOffset;
  const uint32_t id = g_next_id++;
  if (!g_next_id) g_next_id = 1;
  g_answer.id = id;
  g_answer.parts = 0;
  g_answer.received = 0;
  g_answer.full = false;
  g_answer.more = false;
  for (String& part : g_answer.items) part = "";
  JsonDocument doc;
  doc["id"] = id;
  doc["q"] = query.length() > kMaxQuery ? query.substring(0, kMaxQuery) : query;
  doc["list"] = list;
  if (offset) doc["o"] = offset;
  doc["web_auth"] = web_admin_auth::enabled();
  String body;
  serializeJson(doc, body);
  publish("entities", body);
  return id;
}

void handleAnswer(const uint8_t* body, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, body, length) != DeserializationError::Ok) return;
  const uint32_t id = doc["id"] | 0u;
  const int part = doc["p"] | -1;
  const int parts = doc["n"] | 0;
  if (!id || id != g_answer.id || parts < 1 || parts > kMaxParts || part < 0 || part >= parts) return;
  JsonArray results = doc["r"].as<JsonArray>();
  if (results.isNull()) return;
  String items;
  serializeJson(results, items);
  // "[...]" -> "..." so the parts join into one list.
  g_answer.items[part] = items.length() >= 2 ? items.substring(1, items.length() - 1) : String();
  g_answer.parts = static_cast<uint8_t>(parts);
  g_answer.received |= 1u << part;
  g_answer.full = doc["full"] | false;
  g_answer.more = g_answer.more || (doc["more"] | false);
}

bool result(uint32_t id, String& json) {
  if (!id || id != g_answer.id || !g_answer.parts ||
      g_answer.received != (1u << g_answer.parts) - 1u) {
    return false;
  }
  json = "{\"full\":";
  json += g_answer.full ? "true" : "false";
  json += ",\"more\":";
  json += g_answer.more ? "true" : "false";
  json += ",\"r\":[";
  bool first = true;
  for (uint8_t i = 0; i < g_answer.parts; ++i) {
    if (!g_answer.items[i].length()) continue;
    if (!first) json += ',';
    json += g_answer.items[i];
    first = false;
  }
  json += "]}";
  return true;
}

void scheduleTilesReport() {
  g_report_pending = true;
  g_report_due_ms = millis() + kReportDelayMs;
}

void service() {
  const bool ready = command_channel::sessionReady();
  if (ready && !g_session_seen) {
    // A new session (also a restarted Bridge, which keeps the extras in
    // memory only): report again.
    g_last_report = "";
    scheduleTilesReport();
  }
  g_session_seen = ready;
  if (!g_report_pending || !ready || !available()) return;
  if (static_cast<int32_t>(millis() - g_report_due_ms) < 0) return;
  g_report_pending = false;
  std::vector<String> keys;
  const String body = buildTilesReport(keys);
  if (body == g_last_report) return;
  publish("tiles", body);
  g_last_report = body;
  g_reported.swap(keys);
}

}  // namespace entity_search
