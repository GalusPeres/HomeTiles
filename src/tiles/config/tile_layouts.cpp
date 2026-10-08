#include "src/tiles/config/tile_layouts.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>

#include "src/core/memory/psram_allocator.h"
#include "src/devices/device.h"

namespace tile_layouts {
namespace {

constexpr const char* kFile = "/_tile_grids/layouts.json";
constexpr const char* kDir = "/_tile_grids";

// One place in half cells. A classic entry marks a tile without a classic
// place; its place, if any, is its spot in the layout window's storage.
struct Entry {
  uint16_t folder;
  uint16_t view;
  uint8_t layout;
  uint8_t col2;
  uint8_t row2;
  uint8_t w2;
  uint8_t h2;
};

std::vector<Entry, PsramAllocator<Entry>> g_entries;
// Changes not written yet; the generation tells a change made while the file
// was being written.
bool g_dirty = false;
uint32_t g_generation = 0;

void touch() {
  g_dirty = true;
  ++g_generation;
}

SemaphoreHandle_t lock() {
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
  return mutex;
}

class Guard {
 public:
  Guard() : held_(lock() && xSemaphoreTake(lock(), portMAX_DELAY) == pdTRUE) {}
  ~Guard() {
    if (held_) xSemaphoreGive(lock());
  }

 private:
  bool held_;
};

uint8_t half(float value) {
  const float doubled = value * 2.0f + 0.5f;
  return doubled <= 0 ? 0 : doubled >= 255 ? 255 : static_cast<uint8_t>(doubled);
}

Entry* lookup(uint8_t layout, uint16_t folder, uint16_t view) {
  for (auto& entry : g_entries) {
    if (entry.layout == layout && entry.folder == folder && entry.view == view) return &entry;
  }
  return nullptr;
}

void erase_if_locked(bool (*match)(const Entry&, uint16_t, uint16_t), uint16_t a, uint16_t b) {
  const auto end = std::remove_if(g_entries.begin(), g_entries.end(),
                                  [&](const Entry& entry) { return match(entry, a, b); });
  if (end != g_entries.end()) {
    g_entries.erase(end, g_entries.end());
    touch();
  }
}

Layout layout_from_key(const char* key) {
  return strcmp(key, "portrait") == 0 ? Layout::kPortrait
         : strcmp(key, "bar") == 0    ? Layout::kBar
                                      : Layout::kClassic;
}

}  // namespace

void begin() {
  if (!Device::storageReady()) return;
  fs::FS& fs = Device::storageFS();
  // A reset between removing the old file and the rename leaves the new one.
  const String tmp = String(kFile) + ".tmp";
  if (!fs.exists(kFile) && fs.exists(tmp)) fs.rename(tmp, kFile);
  if (!fs.exists(kFile)) return;
  File file = fs.open(kFile, "r");
  if (!file) return;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    Serial.printf("[Layouts] %s unreadable (%s), places start empty\n", kFile, error.c_str());
    return;
  }
  Guard guard;
  g_entries.clear();
  for (const char* key : {"bar", "portrait", "classic_parked"}) {
    const uint8_t layout = static_cast<uint8_t>(layout_from_key(key));
    for (JsonPair folder : doc[key].as<JsonObject>()) {
      const uint16_t folder_id = static_cast<uint16_t>(atoi(folder.key().c_str()));
      for (JsonPair tile : folder.value().as<JsonObject>()) {
        JsonArrayConst place = tile.value().as<JsonArrayConst>();
        if (place.size() != 4) continue;
        g_entries.push_back({folder_id, static_cast<uint16_t>(atoi(tile.key().c_str())), layout,
                             half(place[0].as<float>()), half(place[1].as<float>()), half(place[2].as<float>()),
                             half(place[3].as<float>())});
      }
    }
  }
  // b265/b266 noted the classic tiles without a place as a plain list.
  for (JsonPair folder : doc["classic_hidden"].as<JsonObject>()) {
    const uint16_t folder_id = static_cast<uint16_t>(atoi(folder.key().c_str()));
    for (JsonVariantConst view : folder.value().as<JsonArrayConst>()) {
      g_entries.push_back({folder_id, view.as<uint16_t>(), static_cast<uint8_t>(Layout::kClassic), 0, 0, 0, 0});
    }
  }
  g_dirty = false;
  Serial.printf("[Layouts] %u places loaded\n", static_cast<unsigned>(g_entries.size()));
}

bool find(Layout layout, uint16_t folder_id, uint16_t view_id, Place& out) {
  if (!view_id) return false;
  Guard guard;
  const Entry* entry = lookup(static_cast<uint8_t>(layout), folder_id, view_id);
  if (!entry || !entry->w2 || !entry->h2) return false;
  out = {entry->col2 / 2.0f, entry->row2 / 2.0f, entry->w2 / 2.0f, entry->h2 / 2.0f};
  return true;
}

void set(Layout layout, uint16_t folder_id, uint16_t view_id, const Place& place) {
  if (!view_id) return;
  const Entry wanted{folder_id, view_id, static_cast<uint8_t>(layout), half(place.col), half(place.row),
                     half(place.span_w), half(place.span_h)};
  Guard guard;
  Entry* entry = lookup(wanted.layout, folder_id, view_id);
  if (!entry) {
    g_entries.push_back(wanted);
    touch();
  } else if (memcmp(entry, &wanted, sizeof(Entry)) != 0) {
    *entry = wanted;
    touch();
  }
}

void remove(Layout layout, uint16_t folder_id, uint16_t view_id) {
  Guard guard;
  const uint8_t key = static_cast<uint8_t>(layout);
  const auto end = std::remove_if(g_entries.begin(), g_entries.end(), [&](const Entry& entry) {
    return entry.layout == key && entry.folder == folder_id && entry.view == view_id;
  });
  if (end != g_entries.end()) {
    g_entries.erase(end, g_entries.end());
    touch();
  }
}

bool has_folder(Layout layout, uint16_t folder_id) {
  Guard guard;
  const uint8_t key = static_cast<uint8_t>(layout);
  return std::any_of(g_entries.begin(), g_entries.end(),
                     [&](const Entry& entry) { return entry.layout == key && entry.folder == folder_id; });
}

bool has_layout(Layout layout) {
  Guard guard;
  const uint8_t key = static_cast<uint8_t>(layout);
  return std::any_of(g_entries.begin(), g_entries.end(), [&](const Entry& entry) { return entry.layout == key; });
}

bool has_any(uint16_t folder_id) {
  Guard guard;
  return std::any_of(g_entries.begin(), g_entries.end(),
                     [&](const Entry& entry) { return entry.folder == folder_id; });
}

bool classic_hidden(uint16_t folder_id, uint16_t view_id) {
  if (!view_id) return false;
  Guard guard;
  return lookup(static_cast<uint8_t>(Layout::kClassic), folder_id, view_id) != nullptr;
}

void set_classic_hidden(uint16_t folder_id, uint16_t view_id, bool hidden) {
  if (!view_id) return;
  if (!hidden) {
    remove(Layout::kClassic, folder_id, view_id);
    return;
  }
  Guard guard;
  if (lookup(static_cast<uint8_t>(Layout::kClassic), folder_id, view_id)) return;
  g_entries.push_back({folder_id, view_id, static_cast<uint8_t>(Layout::kClassic), 0, 0, 0, 0});
  touch();
}

void rekey(uint16_t folder_id, uint16_t from_view, uint16_t to_view) {
  if (!from_view || !to_view || from_view == to_view) return;
  Guard guard;
  for (auto& entry : g_entries) {
    if (entry.folder != folder_id || entry.view != from_view) continue;
    entry.view = to_view;
    touch();
  }
}

void retain(uint16_t folder_id, const std::vector<uint16_t>& views) {
  Guard guard;
  const auto end = std::remove_if(g_entries.begin(), g_entries.end(), [&](const Entry& entry) {
    return entry.folder == folder_id && std::find(views.begin(), views.end(), entry.view) == views.end();
  });
  if (end != g_entries.end()) {
    g_entries.erase(end, g_entries.end());
    touch();
  }
}

void drop_folder(uint16_t folder_id) {
  Guard guard;
  erase_if_locked([](const Entry& entry, uint16_t folder, uint16_t) { return entry.folder == folder; }, folder_id, 0);
}

void append_json(String& out) {
  // Grouped by layout and folder for the file and the Web Admin.
  std::vector<Entry, PsramAllocator<Entry>> sorted;
  {
    Guard guard;
    sorted = g_entries;
  }
  std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
    if (a.layout != b.layout) return a.layout < b.layout;
    if (a.folder != b.folder) return a.folder < b.folder;
    return a.view < b.view;
  });
  auto number = [&out](uint8_t half_steps) {
    out += String(half_steps / 2);
    if (half_steps & 1) out += ".5";
  };
  out += "{";
  bool first_layout = true;
  for (Layout layout : {Layout::kBar, Layout::kPortrait, Layout::kClassic}) {
    if (!first_layout) out += ",";
    first_layout = false;
    out += "\"";
    out += layout == Layout::kClassic ? "classic_parked" : grid_layout::key(layout);
    out += "\":{";
    int folder = -1;
    bool first_tile = true;
    for (const Entry& entry : sorted) {
      if (entry.layout != static_cast<uint8_t>(layout)) continue;
      if (entry.folder != folder) {
        if (folder >= 0) out += "},";
        folder = entry.folder;
        out += "\"";
        out += String(entry.folder);
        out += "\":{";
        first_tile = true;
      }
      if (!first_tile) out += ",";
      first_tile = false;
      out += "\"";
      out += String(entry.view);
      out += "\":[";
      number(entry.col2);
      out += ",";
      number(entry.row2);
      out += ",";
      number(entry.w2);
      out += ",";
      number(entry.h2);
      out += "]";
    }
    if (folder >= 0) out += "}";
    out += "}";
  }
  out += "}";
}

bool commit() {
  uint32_t generation = 0;
  {
    Guard guard;
    if (!g_dirty) return true;
    generation = g_generation;
  }
  if (!Device::storageReady()) return false;
  String json;
  json.reserve(512);
  append_json(json);
  fs::FS& fs = Device::storageFS();
  Device::storageWriteBegin();
  if (!fs.exists(kDir)) fs.mkdir(kDir);
  const String tmp = String(kFile) + ".tmp";
  bool ok = false;
  File file = fs.open(tmp, "w");
  if (file) {
    ok = file.print(json) == json.length();
    file.close();
  }
  if (ok) {
    // Some backends do not replace on rename: the old file goes first.
    if (fs.exists(kFile)) fs.remove(kFile);
    ok = fs.rename(tmp, kFile);
  }
  Device::storageWriteEnd();
  if (!ok) {
    Serial.printf("[Layouts] Cannot write %s\n", kFile);
    return false;
  }
  Guard guard;
  if (g_generation == generation) g_dirty = false;
  return true;
}

}  // namespace tile_layouts
