#include "src/types/calendar/calendar_store.h"

#include <map>
#include <memory>
#include <string>

#include "src/types/calendar/calendar_tile.h"
#include "src/ui/popups/calendar/calendar_popup.h"

namespace calendar_store {
namespace {

// A panel shows a handful of calendars; the Bridge offers what it released.
constexpr size_t kMaxCalendars = 24;

struct Entry {
  std::string payload;
  bool pending = false;
  std::unique_ptr<calendar_events::Calendar> calendar;
};

std::map<std::string, Entry> g_entries;
size_t g_pending = 0;

bool valid_entity(const String& entity) {
  if (!entity.startsWith("calendar.") || entity.length() <= 9 || entity.length() > 128) return false;
  for (size_t i = 9; i < entity.length(); ++i) {
    const char c = entity[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

}  // namespace

void queue(const String& entity, const char* payload, size_t length) {
  if (!valid_entity(entity) || !payload) return;
  if (length > calendar_events::kMaxPayload) {
    static uint32_t last_log_ms = 0;
    const uint32_t now = millis();
    if (!last_log_ms || now - last_log_ms >= 10000) {
      last_log_ms = now;
      Serial.printf("[Calendar] Oversized events dropped: %s (%u bytes)\n", entity.c_str(),
                    static_cast<unsigned>(length));
    }
    return;
  }
  const std::string key(entity.c_str());
  auto it = g_entries.find(key);
  if (it == g_entries.end()) {
    if (g_entries.size() >= kMaxCalendars) return;
    it = g_entries.emplace(key, Entry{}).first;
  }
  Entry& entry = it->second;
  // A retained state arrives again after every reconnect: unchanged, nothing
  // needs parsing or redrawing.
  if (!entry.pending && entry.calendar && entry.payload.size() == length &&
      entry.payload.compare(0, length, payload, length) == 0) {
    return;
  }
  entry.payload.assign(payload, length);
  if (!entry.pending) {
    entry.pending = true;
    ++g_pending;
  }
}

void process(uint8_t budget) {
  if (!g_pending) return;
  uint8_t done = 0;
  for (auto& item : g_entries) {
    Entry& entry = item.second;
    if (!entry.pending) continue;
    if (budget && done >= budget) break;
    entry.pending = false;
    --g_pending;
    ++done;
    std::unique_ptr<calendar_events::Calendar> parsed(new calendar_events::Calendar(
        calendar_events::parse(entry.payload.data(), entry.payload.size())));
    if (!parsed->valid) {
      static uint32_t last_log_ms = 0;
      const uint32_t now = millis();
      if (!last_log_ms || now - last_log_ms >= 10000) {
        last_log_ms = now;
        Serial.printf("[Calendar] Unreadable events of %s ignored\n", item.first.c_str());
      }
    }
    entry.calendar = std::move(parsed);
    const String entity(item.first.c_str());
    calendar_tiles_refresh(entity);
    calendar_popup_refresh(entity);
  }
}

const calendar_events::Calendar* get(const String& entity) {
  const auto it = g_entries.find(std::string(entity.c_str()));
  return it == g_entries.end() ? nullptr : it->second.calendar.get();
}

String payload(const String& entity) {
  const auto it = g_entries.find(std::string(entity.c_str()));
  return it == g_entries.end() ? String() : String(it->second.payload.c_str());
}

void forget_except(const String& offered) {
  const String lines = "\n" + offered + "\n";
  for (auto it = g_entries.begin(); it != g_entries.end();) {
    if (lines.indexOf("\n" + String(it->first.c_str()) + "\n") >= 0) {
      ++it;
      continue;
    }
    if (it->second.pending) --g_pending;
    const String entity(it->first.c_str());
    it = g_entries.erase(it);
    calendar_tiles_refresh(entity);
    calendar_popup_refresh(entity);
  }
}

size_t split_entities(const String& list, String* out, size_t max) {
  size_t count = 0;
  int start = 0;
  const int length = static_cast<int>(list.length());
  while (start <= length && count < max) {
    int end = list.indexOf(',', start);
    if (end < 0) end = length;
    String entity = list.substring(start, end);
    entity.trim();
    start = end + 1;
    if (!valid_entity(entity)) continue;
    bool seen = false;
    for (size_t i = 0; i < count; ++i) seen = seen || out[i] == entity;
    if (!seen) out[count++] = entity;
  }
  return count;
}

}  // namespace calendar_store
