#include "src/network/bridge/entity_search.h"

#include <ArduinoJson.h>

#include <cstring>
#include <string>
#include <vector>

#include "src/network/bridge/entity_declaration_core.h"
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
  // The screensaver picture: an image or a camera (no tile type).
  return list == "images";
}

void publish(const char* leaf, const String& body) {
  const String topic = mqttTopics.deviceBase() + "/cmnd/" + leaf;
  networkManager.mqttEnqueuePublish(topic.c_str(), body.c_str(), false);
}

// Picker list of an icon color source entity (icon_sources in the Web Admin:
// sensors, binary sensors, lights and switches, climates, covers).
const char* listOfSource(const char* entity) {
  if (!entity || !strchr(entity, '.')) return nullptr;
  if (!strncmp(entity, "sensor.", 7)) return "sensors";
  if (!strncmp(entity, "binary_sensor.", 14)) return "binary_sensors";
  if (!strncmp(entity, "climate.", 8)) return "climates";
  if (!strncmp(entity, "cover.", 6)) return "covers";
  return "switches";
}

// The declaration: every entity the panel uses, from its tiles and settings
// alone. It never reads what the Bridge serves, so the Bridge's answer to one
// declaration cannot change the next one (entity_declaration_core.h). It is
// complete ("own"): the Bridge sends the panel nothing else beyond its own
// entry's releases.
entity_declaration::Declaration collectDeclaration() {
  entity_declaration::Declaration declaration;
  // The sensor slots are panel settings; applyJson clears one whose sensor
  // the Bridge stops sending, so they are declared too.
  for (const String& slot : haBridgeConfig.get().sensor_slots) declaration.add("sensors", slot.c_str());
  FolderEntitySlotView slots[TILES_PER_GRID];
  for (const FolderEntry& folder : tileConfig.getFolders()) {
    if (!tileConfig.getFolderEntitiesCached(folder.id, slots, TILES_PER_GRID)) continue;
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      declaration.add(listOfType(slots[i].type), slots[i].entity);
      declaration.add(listOfSource(slots[i].rule_entity), slots[i].rule_entity);
    }
  }
  const TileGridConfig& screensaver = screensaverConfig.tileGrid();
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = screensaver.tiles[i];
    declaration.add(listOfType(tile.type), tile.sensor_entity.c_str());
    const String source = tileIconSourceEntity(tile.type, tile.icon_colors);
    declaration.add(listOfSource(source.c_str()), source.c_str());
  }
  // The screensaver picture the Bridge sends (docs-dev/images.md).
  const ScreensaverConfigData& screensaver_config = screensaverConfig.get();
  if (screensaver_config.use_wallpapers && screensaver_uses_ha_picture(screensaver_config)) {
    declaration.add("images", screensaver_config.picture_entity.c_str());
  }
  return declaration;
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
  if (g_sent && version == g_sent_version && !(g_acked && g_acked_version == version)) {
    g_acked = true;
    g_acked_version = version;
    Serial.printf("[EntitySearch] Declaration %08lx acknowledged\n", static_cast<unsigned long>(version));
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
  const entity_declaration::Declaration declaration = collectDeclaration();
  const bool web_auth = web_admin_auth::enabled();
  const uint32_t version = declaration.version(web_auth);
  if (g_acked && version == g_acked_version) return;  // the Bridge has it
  if (!retry && g_sent && version == g_sent_version) return;  // sent, waiting
  size_t dropped = declaration.dropped();
  const std::vector<std::string> parts = declaration.parts(version, web_auth, &dropped);
  for (const std::string& part : parts) publish("tiles", String(part.c_str()));
  // Once per new declaration, new session or repeat (at most every 15 s).
  Serial.printf("[EntitySearch] Declaration %08lx sent: %u entities in %u part(s)%s\n",
                static_cast<unsigned long>(version), static_cast<unsigned>(declaration.count()),
                static_cast<unsigned>(parts.size()), retry ? ", repeated" : "");
  if (dropped) Serial.printf("[EntitySearch] %u tile values not declared\n", (unsigned)dropped);
  g_sent = true;
  g_sent_version = version;
  g_sent_ms = now;
}

}  // namespace entity_search
