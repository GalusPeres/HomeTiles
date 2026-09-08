#include "src/ui/navigation/view_navigation.h"
#include "src/ui/navigation/view_protocol.h"
#include "src/ui/ui_manager.h"
#include "src/ui/tabs/settings/tab_settings.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/core/power/power_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/network_manager.h"
#include "src/network/mqtt/mqtt_topics.h"
#include "src/network/bridge/ha_bridge_config.h"
#include <ArduinoJson.h>
#include <esp_random.h>
#include <memory>
#include <vector>

namespace {
constexpr size_t kMaxFolders = 128;
struct Target { uint16_t id; uint16_t folder; };
std::vector<Target, PsramAllocator<Target>> targets;
std::vector<uint16_t> folder_ids;
char session[33] = {};
hometiles_view::CommandGate gate;
uint32_t catalog_revision = 0;
size_t catalog_page = 0;
bool catalog_complete = false;
uint32_t last_catalog_ms = 0;
uint32_t last_state_ms = 0;
String last_state_key;
String popup_entity;
String command_topic;
String refresh_topic;
String state_topic;
lv_obj_t* popup_card = nullptr;
uint16_t popup_source = 0;
bool remote_opening = false;
struct Pending {
  bool active = false;
  uint16_t folder = 0;
  uint16_t tile = 0;
  uint32_t revision = 0;
  uint32_t started = 0;
  uint16_t waiting_folder = 0;
  uint16_t previous_folder = 0;
  bool waiting = false;
  bool pin = false;
  size_t step = 0;
  std::vector<uint16_t> path;
} pending;

String topic(const char* suffix) {
  return mqttTopics.deviceBase() + suffix;
}

bool popupSupported(const Tile& tile) {
  if (!tile.sensor_entity.length()) return false;
  switch (tile.type) {
    case TILE_NUMBER: case TILE_SELECT: case TILE_DATETIME:
    case TILE_SENSOR: case TILE_BINARY_SENSOR: case TILE_SWITCH:
    case TILE_WEATHER: case TILE_ENERGY: case TILE_MEDIA:
    case TILE_CLIMATE: case TILE_COVER: case TILE_CAMERA: return true;
    default: return false;
  }
}

String folderTarget(uint16_t folder) {
  return folder == TileConfig::rootFolderId() ? String("home")
                                             : "folder:" + String(folder);
}

String boundedLabel(const String& title, size_t bytes, bool suffix = false) {
  // Keep visual title wrapping out of HA Select options, where line breaks
  // would invalidate the whole editable list. Preserve the stored title.
  String value;
  for (size_t i = 0; i < title.length(); ++i) {
    const char c = title[i];
    if (c == '\r' || c == '\n') {
      if (value.length() && value[value.length() - 1] != ' ') value += ' ';
    } else {
      value += c;
    }
  }
  if (value.length() <= bytes) return value;
  size_t start = suffix ? value.length() - bytes : 0;
  size_t end = suffix ? value.length() : bytes;
  // Keep complete UTF-8 characters when limiting MQTT option labels.
  while (start < end && (static_cast<uint8_t>(value[start]) & 0xc0) == 0x80) ++start;
  while (end > start && end < value.length() &&
         (static_cast<uint8_t>(value[end]) & 0xc0) == 0x80) --end;
  return value.substring(start, end);
}

String folderLabel(uint16_t folder) {
  String label;
  for (size_t depth = 0; depth < kMaxFolders; ++depth) {
    const FolderEntry* entry = tileConfig.getFolder(folder);
    if (!entry) return String();
    const String name = folder == 0
        ? String(i18n::strings(configManager.getConfig().language).home)
        : String(entry->name);
    label = label.length() ? name + " / " + label : name;
    // Keep MQTT pages bounded, retaining the closest ancestors and unique ID.
    label = boundedLabel(label, 150, true);
    if (folder == 0) return label;
    if (entry->parent_id == folder) return String();
    folder = entry->parent_id;
  }
  return String();
}

void forgetPopup(lv_event_t* event) {
  if (lv_event_get_target(event) == popup_card) popup_card = nullptr;
}

void cancelPending() {
  if (pending.waiting) tiles_cancel_folder_switch(pending.waiting_folder);
  pending.active = false;
  pending.waiting = false;
  pending.path.clear();
}

void startCatalog() {
  catalog_revision = tileConfig.viewRevision();
  catalog_page = 0;
  catalog_complete = false;
  targets.clear();
  folder_ids.clear();
  for (const auto& folder : tileConfig.getFolders()) {
    if (folder_ids.size() == kMaxFolders) break;
    folder_ids.push_back(folder.id);
  }
}

void serviceCatalog(uint32_t now) {
  if (catalog_revision != tileConfig.viewRevision()) startCatalog();
  if (catalog_complete || camera_popup_is_busy() || now - last_catalog_ms < 250) return;
  last_catalog_ms = now;
  if (catalog_page >= folder_ids.size()) {
    catalog_complete = true;
    return;
  }
  const uint16_t folder = folder_ids[catalog_page];
  auto grid = std::unique_ptr<TileGridConfig>(new (std::nothrow) TileGridConfig());
  if (!grid || !tileConfig.loadFolderGrid(folder, *grid)) return;
  if (catalog_revision != tileConfig.viewRevision()) { startCatalog(); return; }
  const String path = folderLabel(folder);
  if (!path.length()) return;
  DynamicJsonDocument doc(2048 + TILES_PER_GRID * 384);
  doc["session"] = session;
  doc["revision"] = catalog_revision;
  doc["page"] = catalog_page;
  doc["pages"] = folder_ids.size();
  JsonArray options = doc.createNestedArray("targets");
  JsonObject page = options.createNestedObject();
  page["id"] = folderTarget(folder);
  page["label"] = folder == 0 ? path : path + " [f:" + String(folder) + "]";
  for (const Tile& tile : grid->tiles) {
    if (!tile.view_id || !popupSupported(tile)) continue;
    String name = tile.title;
    if (!name.length()) name = haBridgeConfig.findSensorName(tile.sensor_entity);
    if (!name.length()) name = tile.sensor_entity;
    name = boundedLabel(name, 80);
    JsonObject option = options.createNestedObject();
    option["id"] = "tile:" + String(tile.view_id);
    option["label"] = path + " / " + name + " [t:" + String(tile.view_id) + "]";
  }
  if (doc.overflowed()) return;
  String payload;
  serializeJson(doc, payload);
  const String destination = topic("/view/catalog/") + String(catalog_page);
  if (!networkManager.mqttEnqueuePublishWithLargeBuffer(
          destination.c_str(), payload.c_str(), true, 1000)) return;
  for (const Tile& tile : grid->tiles) {
    if (tile.view_id && popupSupported(tile)) targets.push_back({tile.view_id, folder});
  }
  ++catalog_page;
}

size_t firstRequiredFolderStep(const std::vector<uint16_t>& path) {
  // The visible folder already passed its access checks. Reuse it for a popup
  // or a descendant, just as a local tile tap does, without returning to Home.
  if (uiManager.activeTab() != 0 || is_pin_popup_visible() ||
      powerManager.isInSleep() || is_image_screensaver_visible() ||
      tiles_folder_switch_pending()) return 0;
  const uint16_t current = tileConfig.getActiveFolderId();
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == current) return i + 1;
  }
  return 0;
}

void servicePending(uint32_t now) {
  if (!pending.active) return;
  if (pending.revision != tileConfig.viewRevision() ||
      now - pending.started > 60000 || !networkManager.isMqttConnected()) {
    cancelPending();
    return;
  }
  if (pending.waiting) {
    if (tiles_folder_switch_pending()) return;
    if (pending.pin && is_pin_popup_visible()) return;
    if (tileConfig.getActiveFolderId() != pending.waiting_folder) {
      cancelPending();
      return;
    }
    pending.waiting = false;
    pending.previous_folder = pending.waiting_folder;
    ++pending.step;
  }
  // A local navigation or lock after the command invalidates the reused path.
  if ((pending.step > 0 &&
       (tileConfig.getActiveFolderId() != pending.previous_folder ||
        uiManager.activeTab() != 0 || tiles_folder_switch_pending())) ||
      is_pin_popup_visible() || powerManager.isInSleep() ||
      is_image_screensaver_visible()) {
    cancelPending();
    return;
  }
  if (pending.step < pending.path.size()) {
    const uint16_t next = pending.path[pending.step];
    const FolderEntry* folder = tileConfig.getFolder(next);
    if (!folder) { cancelPending(); return; }
    pending.previous_folder = tileConfig.getActiveFolderId();
    pending.waiting_folder = next;
    pending.waiting = true;
    pending.pin = tileConfig.isFolderPinEnabled(next);
    uiManager.requestFolderAccess(next, folder->name, folder->icon_name);
    return;
  }
  if (!pending.tile) { cancelPending(); return; }
  if (tileConfig.getActiveFolderId() != pending.folder || uiManager.activeTab() != 0) {
    cancelPending();
    return;
  }
  remote_opening = true;
  const bool opened = tiles_open_view_popup(pending.tile);
  remote_opening = false;
  if (opened) cancelPending();
}

struct DisplayedView { String current; const char* mode; };

DisplayedView displayedView() {
  String current;
  const char* mode = "folder";
  if (powerManager.isInSleep()) mode = "sleep";
  else if (is_image_screensaver_visible()) mode = "screensaver";
  else if (is_pin_popup_visible()) mode = "pin";
  else if (uiManager.activeTab() != 0) mode = "settings";
  else if (popup_card && !lv_obj_has_flag(popup_card, LV_OBJ_FLAG_HIDDEN)) {
    mode = "popup";
    if (popup_source) current = "tile:" + String(popup_source);
  } else current = folderTarget(tileConfig.getActiveFolderId());
  return {current, mode};
}

void publishState(uint32_t now) {
  const auto displayed = displayedView();
  const String& current = displayed.current;
  const char* mode = displayed.mode;
  const String key = String(mode) + current + String(catalog_revision) +
                     String(catalog_complete) + String(gate.last_sequence);
  if (key == last_state_key && now - last_state_ms < 5000) return;
  StaticJsonDocument<512> doc;
  doc["session"] = session;
  doc["revision"] = catalog_revision;
  doc["ready"] = catalog_complete;
  doc["current"] = current.length() ? current.c_str() : nullptr;
  doc["mode"] = mode;
  doc["sequence"] = gate.last_sequence;
  doc["uptime"] = now;
  String payload;
  serializeJson(doc, payload);
  if (networkManager.mqttEnqueuePublish(state_topic.c_str(), payload.c_str(), true)) {
    last_state_key = key;
    last_state_ms = now;
  }
}
}  // namespace

void viewNavigationClosePopups() {
  hide_settings_popup();
  hide_camera_popup();
  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_media_popup();
  hide_climate_popup();
  hide_cover_popup();
}

void viewNavigationSource(lv_obj_t* source) {
  if (!remote_opening) cancelPending();
  popup_source = tiles_view_id_for_object(source);
}

void viewNavigationPopupShown(lv_obj_t* card, const char* entity) {
  if (popup_card != card) {
    if (popup_card) lv_obj_remove_event_cb(popup_card, forgetPopup);
    popup_card = card;
    if (card) lv_obj_add_event_cb(card, forgetPopup, LV_EVENT_DELETE, nullptr);
  }
  popup_entity = entity;
  bool source_matches = false;
  for (const Tile& tile : tileConfig.getActiveGrid().tiles) {
    if (tile.view_id == popup_source && tile.sensor_entity == popup_entity) source_matches = true;
  }
  if (!source_matches) {
    popup_source = 0;
    for (const Tile& tile : tileConfig.getActiveGrid().tiles) {
      if (popupSupported(tile) && tile.sensor_entity == popup_entity) {
        popup_source = tile.view_id;
        break;
      }
    }
  }
}

void viewNavigationConnected() {
  cancelPending();
  snprintf(session, sizeof(session), "%08lx%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  command_topic = topic("/cmnd/view");
  refresh_topic = topic("/cmnd/view_refresh");
  state_topic = topic("/stat/view");
  gate = {};
  last_state_key = "";
  startCatalog();
  networkManager.mqttEnqueueSubscribe(topic("/cmnd/view").c_str());
  networkManager.mqttEnqueueSubscribe(topic("/cmnd/view_refresh").c_str());
  // Clear a retained command left by a third-party publisher. Session and
  // deadline checks also reject a replay already delivered by the broker.
  networkManager.mqttEnqueuePublish(topic("/cmnd/view").c_str(), "", true);
}

bool viewNavigationHandleMessage(const char* incoming_topic, const char* payload, size_t length) {
  if (refresh_topic == incoming_topic) {
    startCatalog();
    last_state_key = "";
    return true;
  }
  if (command_topic != incoming_topic) return false;
  if (!length || length > 512 || !networkManager.isMqttConnected()) return true;
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, payload, length)) return true;
  if (!doc["sequence"].is<uint32_t>() || !doc["deadline"].is<uint32_t>() ||
      !doc["target"].is<const char*>() || !doc["revision"].is<uint32_t>()) return true;
  if (!gate.accept(doc["session"] | "", session, doc["sequence"], millis(), doc["deadline"])) return true;
  if (!catalog_complete || doc["revision"].as<uint32_t>() != tileConfig.viewRevision()) return true;
  const String requested = doc["target"].as<const char*>();
  uint16_t folder = 0;
  uint16_t tile_id = 0;
  bool found = requested == "home";
  for (uint16_t id : folder_ids) {
    if (requested == folderTarget(id)) { folder = id; found = true; break; }
  }
  for (const Target& target : targets) {
    if (requested == "tile:" + String(target.id)) {
      folder = target.folder; tile_id = target.id; found = true; break;
    }
  }
  if (!found || !tileConfig.folderExists(folder)) return true;
  std::vector<uint16_t> path;
  uint16_t ancestor = folder;
  for (size_t depth = 0; depth < kMaxFolders; ++depth) {
    const FolderEntry* entry = tileConfig.getFolder(ancestor);
    if (!entry) return true;
    path.insert(path.begin(), ancestor);
    if (ancestor == 0) break;
    if (entry->parent_id == ancestor || depth + 1 == kMaxFolders) return true;
    ancestor = entry->parent_id;
  }
  const size_t first_step = firstRequiredFolderStep(path);
  cancelPending();
  viewNavigationClosePopups();
  hide_pin_popup();
  hide_image_screensaver();
  powerManager.wakeFromDisplaySleep("view command");
  displayManager.resetActivityTimer();
  pending.active = true;
  pending.folder = folder;
  pending.tile = tile_id;
  pending.revision = tileConfig.viewRevision();
  pending.started = millis();
  pending.previous_folder = tileConfig.getActiveFolderId();
  pending.step = first_step;
  pending.path = std::move(path);
  return true;
}

void viewNavigationService() {
  if (!networkManager.isMqttConnected()) { cancelPending(); return; }
  if (!session[0]) return;
  const uint32_t now = millis();
  static uint32_t last_service = 0;
  if (now - last_service < 100) return;
  last_service = now;
  if (popup_card && !lv_obj_has_flag(popup_card, LV_OBJ_FLAG_HIDDEN) && popup_source) {
    bool still_configured = false;
    for (const Tile& tile : tileConfig.getActiveGrid().tiles) {
      if (tile.view_id == popup_source && popupSupported(tile) &&
          tile.sensor_entity == popup_entity) still_configured = true;
    }
    if (!still_configured) viewNavigationClosePopups();
  }
  servicePending(now);
  serviceCatalog(now);
  publishState(now);
}
