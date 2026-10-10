#include "src/ui/popups/media/media_browse.h"

#include <ArduinoJson.h>
#include <esp_random.h>
#include <string.h>
#include <utility>

#include "src/network/mqtt/mqtt_handlers.h"

// Browse state: sessions, request ids, page assembly and the navigation
// stack. The list view lives in media_popup.cpp.

namespace {

// The Bridge sends at most 32 items per page. Keep at most 8 pages
// (256 items) so a huge library cannot exhaust memory on the panel.
constexpr int kMaxPages = 8;
// Echoed by the Bridge; reserved for future protocol changes.
constexpr uint32_t kRevision = 1;

enum class PendingAction : uint8_t { None, Start, Open, Back, Reload };

struct PendingRequest {
  PendingAction action = PendingAction::None;
  uint32_t request_id = 0;
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

const char* action_name(PendingAction action) {
  switch (action) {
    case PendingAction::None: return "none";
    case PendingAction::Start: return "start";
    case PendingAction::Open: return "open";
    case PendingAction::Back: return "back";
    case PendingAction::Reload: return "reload";
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
  String path;
  for (size_t i = 0; i < g_state.stack.size(); ++i) {
    path += (i == 0) ? "Root" : g_state.stack[i].title;
    if (i + 1 < g_state.stack.size()) path += " > ";
  }
  if (!path.length()) path = "-";
  Serial.printf("[MediaBrowse] State: %s, entity=%s, level=%s, items=%u%s\n",
                status_name(g_state.status), g_state.entity_id.c_str(),
                path.c_str(), static_cast<unsigned>(g_state.items.size()),
                g_state.truncated ? " (truncated)" : "");
  if (g_state.status == MediaBrowseStatus::Error) {
    Serial.printf("[MediaBrowse]   error=%s\n", g_state.error.c_str());
  }
  if (g_state.status == MediaBrowseStatus::Ready) {
    for (size_t i = 0; i < g_state.items.size(); ++i) {
      const MediaBrowseItem& item = g_state.items[i];
      Serial.printf("[MediaBrowse]   [%u] %s (%s)%s%s\n",
                    static_cast<unsigned>(i), item.title.c_str(),
                    item.media_class.c_str(),
                    item.can_expand ? " >" : "",
                    item.can_play ? " play" : "");
    }
  }
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

  const bool queued = mqttPublishMediaBrowse(
      g_state.entity_id.c_str(), g_state.session.c_str(),
      g_state.pending.request_id, kRevision, target.content_id.c_str(),
      target.content_type.c_str());

  Serial.printf("[MediaBrowse] Request %s req=%lu id='%s' type='%s' (%s)\n",
                action_name(action),
                static_cast<unsigned long>(g_state.pending.request_id),
                target.content_id.c_str(), target.content_type.c_str(),
                queued ? "sent" : "failed");

  if (!queued) {
    clear_pending();
    g_state.status = MediaBrowseStatus::Error;
    g_state.error = "mqtt_unavailable";
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
    for (auto& item : p.pages[i]) items.push_back(std::move(item));
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
                    uint32_t request_id, uint32_t revision) {
  if (g_state.pending.action == PendingAction::None) {
    Serial.printf("[MediaBrowse] %s req=%lu dropped: no request pending\n",
                  kind, static_cast<unsigned long>(request_id));
    return false;
  }
  if (!g_state.entity_id.equalsIgnoreCase(entity)) {
    Serial.printf("[MediaBrowse] %s dropped: entity %s != %s\n", kind, entity,
                  g_state.entity_id.c_str());
    return false;
  }
  if (g_state.session != session) {
    Serial.printf("[MediaBrowse] %s dropped: foreign session %s\n", kind,
                  session);
    return false;
  }
  if (request_id != g_state.pending.request_id) {
    Serial.printf("[MediaBrowse] %s dropped: stale req=%lu (waiting for %lu)\n",
                  kind, static_cast<unsigned long>(request_id),
                  static_cast<unsigned long>(g_state.pending.request_id));
    return false;
  }
  if (revision != kRevision) {
    Serial.printf("[MediaBrowse] %s dropped: revision %lu\n", kind,
                  static_cast<unsigned long>(revision));
    return false;
  }
  return true;
}

}  // namespace

// --- Navigation ---------------------------------------------------------

bool media_browse_start(const char* entity_id) {
  if (!entity_id || strncmp(entity_id, "media_player.", 13) != 0) {
    Serial.printf("[MediaBrowse] Start rejected: '%s' is no media_player\n",
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
  Serial.printf("[MediaBrowse] New session %s for %s\n",
                g_state.session.c_str(), entity_id);
  return send_request(PendingAction::Start, MediaBrowseLevel());
}

bool media_browse_open(size_t index) {
  if (g_state.status == MediaBrowseStatus::Idle || g_state.stack.empty()) {
    return false;
  }
  if (index >= g_state.items.size()) {
    Serial.printf("[MediaBrowse] Open rejected: index %u out of range\n",
                  static_cast<unsigned>(index));
    return false;
  }
  const MediaBrowseItem& item = g_state.items[index];
  if (!item.can_expand) {
    Serial.printf("[MediaBrowse] Open rejected: '%s' cannot expand\n",
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
    Serial.printf("[MediaBrowse] Play rejected: '%s' cannot play\n",
                  item.title.c_str());
    return false;
  }
  Serial.printf("[MediaBrowse] Play '%s' on %s\n", item.title.c_str(),
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

// --- MQTT entry points --------------------------------------------------

void media_browse_handle_catalog_message(const char* topic,
                                         const char* payload,
                                         size_t length) {
  if (!payload || !length) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[MediaBrowse] Catalog parse failed on %s: %s (%u bytes)\n",
                  topic ? topic : "?", err.c_str(),
                  static_cast<unsigned>(length));
    return;
  }

  const char* entity = doc["entity_id"] | "";
  const char* session = doc["session"] | "";
  const uint32_t request_id = doc["request_id"] | 0u;
  const uint32_t revision = doc["revision"] | 0u;
  const int page = doc["page"] | -1;
  const int pages = doc["pages"] | 0;

  if (!answer_matches("Catalog", entity, session, request_id, revision)) {
    return;
  }

  PendingRequest& p = g_state.pending;
  if (pages < 1 || page < 0 || page >= pages) {
    Serial.printf("[MediaBrowse] Catalog dropped: invalid page %d/%d\n", page,
                  pages);
    return;
  }
  if (p.pages_announced == 0) {
    p.pages_announced = pages;
    p.truncated = pages > kMaxPages;
    p.pages_expected = p.truncated ? kMaxPages : pages;
    if (p.truncated) {
      Serial.printf("[MediaBrowse] %d pages announced, keeping %d\n", pages,
                    kMaxPages);
    }
  } else if (pages != p.pages_announced) {
    Serial.printf("[MediaBrowse] Catalog dropped: page count changed %d -> %d\n",
                  p.pages_announced, pages);
    return;
  }
  if (page >= p.pages_expected) return;  // Beyond the kept pages.
  const uint32_t bit = 1u << page;
  if (p.pages_received & bit) {
    Serial.printf("[MediaBrowse] Catalog page %d duplicate, ignored\n", page);
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

  Serial.printf("[MediaBrowse] Catalog req=%lu page %d/%d: %u items\n",
                static_cast<unsigned long>(request_id), page + 1,
                p.pages_expected, static_cast<unsigned>(out.size()));

  const uint32_t all = (1u << p.pages_expected) - 1u;  // kMaxPages <= 8.
  if ((p.pages_received & all) == all) commit_pending();
}

void media_browse_handle_status_message(const char* payload, size_t length) {
  if (!payload || !length) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.printf("[MediaBrowse] Status parse failed (%u bytes)\n",
                  static_cast<unsigned>(length));
    return;
  }

  const char* status = doc["status"] | "";
  if (strcmp(status, "error") != 0) return;  // Only errors use this topic.

  const char* entity = doc["entity_id"] | "";
  const char* session = doc["session"] | "";
  const uint32_t request_id = doc["request_id"] | 0u;
  const uint32_t revision = doc["revision"] | 0u;
  if (!answer_matches("Error", entity, session, request_id, revision)) return;

  // Keep the previous level and items visible; only report the failure.
  clear_pending();
  g_state.status = MediaBrowseStatus::Error;
  g_state.error = doc["error"] | "unknown";
  if (!g_state.error.length()) g_state.error = "unknown";
  notify();
}
