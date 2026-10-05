#include "src/network/bridge/entity_search.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/network_manager.h"
#include "src/network/secure/command_channel.h"
#include "src/tiles/config/tile_config.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/web/server/auth/web_admin_auth.h"

namespace entity_search {
namespace {

// The Bridge answers in at most this many sealed parts (entity_search.py).
constexpr uint8_t kMaxParts = 6;
constexpr size_t kMaxQuery = 64;
// The picker loads further pages while scrolling (entity_search.py MAX_OFFSET).
constexpr uint32_t kMaxOffset = 5000;
// A declaration part stays below command_channel's sealed body limit.
constexpr size_t kMaxPartBytes = 1800;
constexpr size_t kMaxDeclarationParts = 8;       // entity_search.py MAX_DECLARATION_PARTS
constexpr size_t kMaxDeclaredEntities = 300;     // entity_search.py MAX_PANEL_ENTITIES
constexpr uint32_t kReportDelayMs = 1500;
// Without the Bridge's acknowledgement the declaration is sent again.
constexpr uint32_t kDeclarationRetryMs = 15000;

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
// The declaration last sent and the one the Bridge acknowledged.
bool g_sent = false;
uint32_t g_sent_version = 0;
uint32_t g_sent_ms = 0;
bool g_acked = false;
uint32_t g_acked_version = 0;

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

// Picker list of an icon color source entity (icon_sources in the Web Admin:
// sensors, binary sensors, lights and switches, climates, covers).
const char* listOfSource(const String& entity) {
  if (entity.startsWith("sensor.")) return "sensors";
  if (entity.startsWith("binary_sensor.")) return "binary_sensors";
  if (entity.startsWith("climate.")) return "climates";
  if (entity.startsWith("cover.")) return "covers";
  return entity.indexOf('.') > 0 ? "switches" : nullptr;
}

struct Declared {
  const char* list;
  std::vector<String> ids;
};

void declare(std::vector<Declared>& lists, const char* list, const String& entity) {
  if (!list || !entity.length()) return;
  for (Declared& item : lists) {
    if (item.list == list) {
      if (!contains(item.ids, entity)) item.ids.push_back(entity);
      return;
    }
  }
  lists.push_back({list, {entity}});
}

// The declaration: every entity the tiles use, from the tiles alone. It never
// reads what the Bridge serves, so the Bridge's answer to one declaration
// cannot change the next one. Sorted, so the same tiles give the same
// declaration and the same version.
std::vector<Declared> collectDeclared() {
  std::vector<Declared> lists;
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const FolderEntry& folder : tileConfig.getFolders()) {
    if (!tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID)) continue;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      declare(lists, listOfType(slots[i].type), String(slots[i].entity));
      const String source(slots[i].rule_entity);
      declare(lists, listOfSource(source), source);
    }
  }
  const TileGridConfig& screensaver = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = screensaver.tiles[i];
    declare(lists, listOfType(tile.type), tile.sensor_entity);
    const String source = tileIconSourceEntity(tile.type, tile.icon_colors);
    declare(lists, listOfSource(source), source);
  }
  std::sort(lists.begin(), lists.end(),
            [](const Declared& a, const Declared& b) { return strcmp(a.list, b.list) < 0; });
  size_t total = 0;
  for (Declared& item : lists) {
    std::sort(item.ids.begin(), item.ids.end());
    if (total + item.ids.size() > kMaxDeclaredEntities) {
      Serial.printf("[EntitySearch] %u tile entities not declared (limit %u)\n",
                    (unsigned)(total + item.ids.size() - kMaxDeclaredEntities), (unsigned)kMaxDeclaredEntities);
      item.ids.resize(kMaxDeclaredEntities - total);
    }
    total += item.ids.size();
  }
  return lists;
}

uint32_t fnv1a(uint32_t hash, const String& text) {
  for (size_t i = 0; i < text.length(); ++i) {
    hash ^= static_cast<uint8_t>(text[i]);
    hash *= 16777619u;
  }
  return hash;
}

// {"v":version,"p":part,"n":parts,"lists":{list:[entity ids]},"web_auth":bool}
// in sealed-size parts. The version is a hash of the declared lists and the
// password claim, so an unchanged declaration keeps its version.
std::vector<String> buildDeclaration(uint32_t& version) {
  const std::vector<Declared> lists = collectDeclared();
  const bool web_auth = web_admin_auth::enabled();
  // Room for "v", "p", "n", "web_auth" and one list key with its brackets.
  constexpr size_t kPartOverhead = 96;
  version = fnv1a(2166136261u, web_auth ? "1" : "0");
  std::vector<JsonDocument> parts(1);
  size_t dropped = 0;
  for (const Declared& item : lists) {
    for (const String& id : item.ids) {
      const size_t added = strlen(item.list) + id.length() + kPartOverhead;
      if (added > kMaxPartBytes) {
        ++dropped;  // no Home Assistant entity id is this long
        continue;
      }
      if (measureJson(parts.back()) + added > kMaxPartBytes) {
        if (parts.size() == kMaxDeclarationParts) {
          ++dropped;
          continue;
        }
        parts.emplace_back();
      }
      JsonDocument& part = parts.back();
      JsonArray array = part["lists"][item.list].as<JsonArray>();
      if (array.isNull()) array = part["lists"][item.list].to<JsonArray>();
      array.add(id);
      version = fnv1a(version, String('|') + item.list + ',' + id);
    }
  }
  if (dropped) Serial.printf("[EntitySearch] %u tile entities not declared (size limit)\n", (unsigned)dropped);
  std::vector<String> bodies;
  for (size_t i = 0; i < parts.size(); ++i) {
    JsonDocument& part = parts[i];
    part["v"] = version;
    part["p"] = i;
    part["n"] = parts.size();
    if (part["lists"].isNull()) part["lists"].to<JsonObject>();
    part["web_auth"] = web_auth;
    String body;
    serializeJson(part, body);
    bodies.push_back(body);
  }
  return bodies;
}

}  // namespace

bool available() {
  return command_channel::state() == command_channel::PairingState::Active &&
         haBridgeConfig.supportsEntitySearch();
}

namespace {

bool declares() {
  return command_channel::state() == command_channel::PairingState::Active &&
         haBridgeConfig.supportsEntityDeclarations();
}

}  // namespace

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

void handleDeclarationAck(const uint8_t* body, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, body, length) != DeserializationError::Ok) return;
  const uint32_t version = doc["v"] | 0u;
  if (g_sent && version == g_sent_version) {
    g_acked = true;
    g_acked_version = version;
  }
}

void service() {
  const bool ready = command_channel::sessionReady();
  if (ready && !g_session_seen) {
    // A new session (also a restarted Bridge): declare again.
    g_sent = false;
    g_acked = false;
    scheduleTilesReport();
  }
  g_session_seen = ready;
  if (!ready || !declares()) return;
  const uint32_t now = millis();
  const bool unconfirmed = g_sent && !(g_acked && g_acked_version == g_sent_version);
  const bool retry = unconfirmed && now - g_sent_ms >= kDeclarationRetryMs;
  const bool due = g_report_pending && static_cast<int32_t>(now - g_report_due_ms) >= 0;
  if (!due && !retry) return;
  g_report_pending = false;
  uint32_t version = 0;
  const std::vector<String> parts = buildDeclaration(version);
  if (g_acked && version == g_acked_version) return;  // the Bridge has it
  if (!retry && g_sent && version == g_sent_version) return;  // sent, waiting
  for (const String& part : parts) publish("tiles", part);
  g_sent = true;
  g_sent_version = version;
  g_sent_ms = now;
}

}  // namespace entity_search
