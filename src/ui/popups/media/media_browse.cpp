#include "src/ui/popups/media/media_browse.h"

#include <ArduinoJson.h>
#include <esp_random.h>
#include <string.h>
#include <utility>

#include "src/network/mqtt/mqtt_handlers.h"

// The Media popup's library (after memooox3's PR #87): sessions, request ids,
// page assembly and the navigation stack. The list view lives in
// media_popup.cpp.

namespace {

// The Bridge splits a level into pages of at most 12 KB and sends at most 256
// rows; keep at most 32 pages and 256 rows, so a huge library cannot exhaust
// memory on the panel.
constexpr int kMaxPages = 32;
constexpr size_t kMaxItems = 256;
// An answer that never comes ends the wait after this long.
constexpr uint32_t kAnswerTimeoutMs = 12000;

enum class PendingAction : uint8_t { None, Start, Open, Back, Reload };

struct PendingRequest {
  PendingAction action = PendingAction::None;
  uint32_t request_id = 0;
  uint32_t sent_ms = 0;
  MediaBrowseLevel target;
  int pages_announced = 0;  // "pages" of the first answer; 0 until then.
  int pages_expected = 0;   // Pages kept (announced, capped at kMaxPages).
  uint32_t pages_received = 0;  // Bit mask, one bit per page.
  std::vector<MediaBrowseItem> pages[kMaxPages];
  bool truncated = false;
};

struct BrowseState {
  String entity_id;
  String session;
  uint32_t next_request_id = 1;
  MediaBrowseStatus status = MediaBrowseStatus::Idle;
  std::vector<MediaBrowseLevel> stack;
  std::vector<MediaBrowseItem> items;
  String error;
  bool truncated = false;
  uint32_t generation = 0;
  uint32_t items_version = 0;
  PendingRequest pending;
  void (*listener)() = nullptr;
};

BrowseState g_state;
const String kEmpty;

const char* status_name(MediaBrowseStatus status) {
  switch (status) {
    case MediaBrowseStatus::Idle: return "idle";
    case MediaBrowseStatus::Loading: return "loading";
    case MediaBrowseStatus::Ready: return "ready";
    case MediaBrowseStatus::Error: return "error";
  }
  return "?";
}

String new_session_id() {
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lx%08lx",
           static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return String(buf);
}

void clear_pending() {
  for (auto& page : g_state.pending.pages) {
    std::vector<MediaBrowseItem>().swap(page);
  }
  g_state.pending = PendingRequest();
}

void log_state() {
  Serial.printf("[MediaLibrary] %s, %s, depth %u, %u rows%s%s%s\n", status_name(g_state.status),
                g_state.entity_id.c_str(), static_cast<unsigned>(g_state.stack.size()),
                static_cast<unsigned>(g_state.items.size()), g_state.truncated ? " (truncated)" : "",
                g_state.status == MediaBrowseStatus::Error ? ", error " : "",
                g_state.status == MediaBrowseStatus::Error ? g_state.error.c_str() : "");
}

void notify() {
  ++g_state.generation;
  log_state();
  if (g_state.listener) g_state.listener();
}

// Sends the request for target and marks it pending. The visible state stays
// unchanged until the answer is complete.
bool send_request(PendingAction action, const MediaBrowseLevel& target) {
  if (!g_state.entity_id.length() || !g_state.session.length()) return false;

  clear_pending();
  g_state.pending.action = action;
  g_state.pending.request_id = g_state.next_request_id++;
  g_state.pending.target = target;

  g_state.pending.sent_ms = millis();
  const bool queued = mqttPublishMediaBrowse(g_state.entity_id.c_str(), g_state.session.c_str(),
                                             g_state.pending.request_id, target.content_id.c_str(),
                                             target.content_type.c_str());

  if (!queued) {
    clear_pending();
    g_state.status = MediaBrowseStatus::Error;
    g_state.error = "offline";
    notify();
    return false;
  }
  g_state.status = MediaBrowseStatus::Loading;
  g_state.error = "";
  notify();
  return true;
}

// All pages of the pending answer are present: make them the visible level.
void commit_pending() {
  PendingRequest& p = g_state.pending;
  std::vector<MediaBrowseItem> items;
  size_t total = 0;
  for (int i = 0; i < p.pages_expected; ++i) total += p.pages[i].size();
  items.reserve(total);
  for (int i = 0; i < p.pages_expected; ++i) {
    for (auto& item : p.pages[i]) {
      if (items.size() >= kMaxItems) {
        p.truncated = true;
        break;
      }
      items.push_back(std::move(item));
    }
  }

  switch (p.action) {
    case PendingAction::Start:
      g_state.stack.clear();
      g_state.stack.push_back(p.target);
      break;
    case PendingAction::Open:
      g_state.stack.push_back(p.target);
      break;
    case PendingAction::Back:
      if (g_state.stack.size() > 1) g_state.stack.pop_back();
      break;
    case PendingAction::Reload:
    case PendingAction::None:
      if (g_state.stack.empty()) g_state.stack.push_back(p.target);
      break;
  }

  g_state.items.swap(items);
  ++g_state.items_version;
  g_state.truncated = p.truncated;
  g_state.status = MediaBrowseStatus::Ready;
  g_state.error = "";
  clear_pending();
  notify();
}

// Common checks for catalog and error answers. Logs why an answer is dropped.
bool answer_matches(const char* kind, const char* entity, const char* session,
                    uint32_t request_id) {
  if (g_state.pending.action == PendingAction::None) {
    Serial.printf("[MediaLibrary] %s req=%lu dropped: no request pending\n",
                  kind, static_cast<unsigned long>(request_id));
    return false;
  }
  if (!g_state.entity_id.equalsIgnoreCase(entity)) {
    Serial.printf("[MediaLibrary] %s dropped: entity %s != %s\n", kind, entity,
                  g_state.entity_id.c_str());
    return false;
  }
  if (g_state.session != session) {
    Serial.printf("[MediaLibrary] %s dropped: foreign session %s\n", kind,
                  session);
    return false;
  }
  if (request_id != g_state.pending.request_id) {
    Serial.printf("[MediaLibrary] %s dropped: stale req=%lu (waiting for %lu)\n",
                  kind, static_cast<unsigned long>(request_id),
                  static_cast<unsigned long>(g_state.pending.request_id));
    return false;
  }
  return true;
}

}  // namespace

// --- Navigation ---------------------------------------------------------

bool media_browse_start(const char* entity_id) {
  if (!entity_id || strncmp(entity_id, "media_player.", 13) != 0) {
    Serial.printf("[MediaLibrary] Start rejected: '%s' is no media_player\n",
                  entity_id ? entity_id : "");
    return false;
  }
  clear_pending();
  g_state.entity_id = entity_id;
  g_state.session = new_session_id();
  g_state.stack.clear();
  std::vector<MediaBrowseItem>().swap(g_state.items);
  ++g_state.items_version;
  g_state.truncated = false;
  g_state.error = "";
  Serial.printf("[MediaLibrary] New session %s for %s\n",
                g_state.session.c_str(), entity_id);
  return send_request(PendingAction::Start, MediaBrowseLevel());
}

bool media_browse_open(size_t index) {
  if (g_state.status == MediaBrowseStatus::Idle || g_state.stack.empty()) {
    return false;
  }
  if (index >= g_state.items.size()) {
    Serial.printf("[MediaLibrary] Open rejected: index %u out of range\n",
                  static_cast<unsigned>(index));
    return false;
  }
  const MediaBrowseItem& item = g_state.items[index];
  if (!item.can_expand) {
    Serial.printf("[MediaLibrary] Open rejected: '%s' cannot expand\n",
                  item.title.c_str());
    return false;
  }
  MediaBrowseLevel target;
  target.content_id = item.content_id;
  target.content_type = item.content_type;
  target.title = item.title;
  return send_request(PendingAction::Open, target);
}

bool media_browse_play(size_t index) {
  if (!g_state.entity_id.length() || index >= g_state.items.size()) {
    return false;
  }
  const MediaBrowseItem& item = g_state.items[index];
  if (!item.can_play) {
    Serial.printf("[MediaLibrary] Play rejected: '%s' cannot play\n",
                  item.title.c_str());
    return false;
  }
  Serial.printf("[MediaLibrary] Play '%s' on %s\n", item.title.c_str(),
                g_state.entity_id.c_str());
  return mqttPublishMediaPlay(g_state.entity_id.c_str(),
                              item.content_id.c_str(),
                              item.content_type.c_str());
}

bool media_browse_back() {
  if (g_state.stack.size() < 2) return false;
  // Request the parent; the stack only shrinks once its answer arrived.
  const MediaBrowseLevel parent = g_state.stack[g_state.stack.size() - 2];
  return send_request(PendingAction::Back, parent);
}

bool media_browse_reload() {
  if (!g_state.entity_id.length()) return false;
  if (g_state.stack.empty()) {
    return send_request(PendingAction::Start, MediaBrowseLevel());
  }
  return send_request(PendingAction::Reload, g_state.stack.back());
}

void media_browse_reset() {
  clear_pending();
  g_state.entity_id = "";
  g_state.session = "";
  g_state.stack.clear();
  std::vector<MediaBrowseItem>().swap(g_state.items);
  ++g_state.items_version;
  g_state.truncated = false;
  g_state.error = "";
  g_state.status = MediaBrowseStatus::Idle;
  notify();
}

// --- State for the UI ---------------------------------------------------

MediaBrowseStatus media_browse_status() { return g_state.status; }
const String& media_browse_entity() { return g_state.entity_id; }
const String& media_browse_session() { return g_state.session; }
const std::vector<MediaBrowseItem>& media_browse_items() {
  return g_state.items;
}
size_t media_browse_depth() { return g_state.stack.size(); }
const MediaBrowseLevel* media_browse_current_level() {
  return g_state.stack.empty() ? nullptr : &g_state.stack.back();
}
bool media_browse_can_go_back() { return g_state.stack.size() > 1; }
const String& media_browse_error() {
  return g_state.status == MediaBrowseStatus::Error ? g_state.error : kEmpty;
}
bool media_browse_truncated() { return g_state.truncated; }
uint32_t media_browse_generation() { return g_state.generation; }
uint32_t media_browse_items_version() { return g_state.items_version; }
void media_browse_set_listener(void (*listener)()) {
  g_state.listener = listener;
}

void media_browse_poll(uint32_t now_ms) {
  if (g_state.pending.action == PendingAction::None) return;
  if (now_ms - g_state.pending.sent_ms < kAnswerTimeoutMs) return;
  // No answer (an old Bridge, the player went away): stop waiting and keep
  // the previous level.
  clear_pending();
  g_state.status = MediaBrowseStatus::Error;
  g_state.error = "timeout";
  notify();
}

// --- MQTT entry points --------------------------------------------------

void media_browse_handle_catalog_message(const char* topic,
                                         const char* payload,
                                         size_t length) {
  if (!payload || !length) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[MediaLibrary] Catalog parse failed on %s: %s (%u bytes)\n",
                  topic ? topic : "?", err.c_str(),
                  static_cast<unsigned>(length));
    return;
  }

  const char* entity = doc["entity_id"] | "";
  const char* session = doc["session"] | "";
  const uint32_t request_id = doc["request_id"] | 0u;
  const int page = doc["page"] | -1;
  const int pages = doc["pages"] | 0;

  if (!answer_matches("Catalog", entity, session, request_id)) {
    return;
  }

  PendingRequest& p = g_state.pending;
  if (pages < 1 || page < 0 || page >= pages) {
    Serial.printf("[MediaLibrary] Catalog dropped: invalid page %d/%d\n", page,
                  pages);
    return;
  }
  if (p.pages_announced == 0) {
    p.pages_announced = pages;
    p.truncated = pages > kMaxPages;
    p.pages_expected = p.truncated ? kMaxPages : pages;
    if (p.truncated) {
      Serial.printf("[MediaLibrary] %d pages announced, keeping %d\n", pages,
                    kMaxPages);
    }
  } else if (pages != p.pages_announced) {
    Serial.printf("[MediaLibrary] Catalog dropped: page count changed %d -> %d\n",
                  p.pages_announced, pages);
    return;
  }
  if (page >= p.pages_expected) return;  // Beyond the kept pages.
  const uint32_t bit = 1u << page;
  if (p.pages_received & bit) {
    Serial.printf("[MediaLibrary] Catalog page %d duplicate, ignored\n", page);
    return;
  }

  std::vector<MediaBrowseItem>& out = p.pages[page];
  JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  out.reserve(items.size());
  for (JsonObjectConst src : items) {
    MediaBrowseItem item;
    item.title = src["title"] | "";
    item.media_class = src["media_class"] | "";
    item.content_id = src["media_content_id"] | "";
    item.content_type = src["media_content_type"] | "";
    item.thumbnail = src["thumbnail"] | "";
    item.can_play = src["can_play"] | false;
    item.can_expand = src["can_expand"] | false;
    item.can_search = src["can_search"] | false;
    if (!item.title.length()) item.title = item.content_id;
    out.push_back(std::move(item));
  }
  p.pages_received |= bit;

  const uint32_t all = p.pages_expected >= 32 ? 0xFFFFFFFFu : (1u << p.pages_expected) - 1u;
  if ((p.pages_received & all) == all) commit_pending();
}

void media_browse_handle_status_message(const char* payload, size_t length) {
  if (!payload || !length) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.printf("[MediaLibrary] Status parse failed (%u bytes)\n",
                  static_cast<unsigned>(length));
    return;
  }

  const char* status = doc["status"] | "";
  if (strcmp(status, "error") != 0) return;  // Only errors use this topic.

  const char* entity = doc["entity_id"] | "";
  const char* session = doc["session"] | "";
  const uint32_t request_id = doc["request_id"] | 0u;
  if (!answer_matches("Error", entity, session, request_id)) return;

  // Keep the previous level and items visible; only report the failure.
  clear_pending();
  g_state.status = MediaBrowseStatus::Error;
  g_state.error = doc["error"] | "unknown";
  if (!g_state.error.length()) g_state.error = "unknown";
  notify();
}
