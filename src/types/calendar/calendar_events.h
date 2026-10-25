#pragma once

#include <ArduinoJson.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

// On the panel the events and the parse's JSON pool live in PSRAM: the
// default malloc puts blocks below 16 KB into the scarce internal heap
// (psram_allocator.h; the V2 has dropped to 66-80 KB free there), and a
// calendar keeps up to 96 events. Host tests build without it.
#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>

#include "src/core/memory/psram_allocator.h"
#define CALENDAR_EVENTS_PSRAM 1
#else
#define CALENDAR_EVENTS_PSRAM 0
#endif

// The Bridge's retained `events` state of a calendar entity
// ({ha_prefix}/calendar/<object>/events, docs-dev/calendar.md): the events of
// one Home Assistant calendar in a window around today, in Home Assistant's
// local time. Parsed defensively and bounded; nothing here allocates per
// frame or touches LVGL, so host tests run it as it is.
//
// {"v":1,"state":"on","from":"2026-09-24","to":"2026-12-31",
//  "e":[{"s":"2026-10-24","e":"2026-10-25","t":"Bio waste"},
//       {"s":"2026-10-24T10:00","e":"2026-10-24T11:30","t":"Long ride","l":"Pool"}]}
//
// A start without a time is an all-day event whose end date is exclusive,
// like Home Assistant's. Missing data stays apart from an empty calendar:
// `valid` false is no payload (or a broken one), `available` false a calendar
// Home Assistant does not have or reports unavailable.
namespace calendar_events {

constexpr size_t kMaxPayload = 16384;
constexpr size_t kMaxEvents = 96;
constexpr size_t kTitleBytes = 63;
constexpr size_t kLocationBytes = 47;
// Minute of the day of an all-day event.
constexpr int16_t kAllDay = -1;

struct Event {
  // Days since 1970-01-01 in Home Assistant's local time.
  int32_t start_day = 0;
  // The last day the event covers (inclusive). A timed event ending at
  // midnight ends the day before.
  int32_t end_day = 0;
  // Minutes after midnight; kAllDay for all-day events.
  int16_t start_min = kAllDay;
  int16_t end_min = kAllDay;
  char title[kTitleBytes + 1] = {};
  char location[kLocationBytes + 1] = {};

  bool all_day() const { return start_min == kAllDay; }
};

#if CALENDAR_EVENTS_PSRAM
using EventList = PsVector<Event>;
#else
using EventList = std::vector<Event>;
#endif

struct Calendar {
  bool valid = false;
  bool available = false;
  // The days the Bridge looked at (inclusive); no event outside them is known.
  int32_t from_day = 0;
  int32_t to_day = -1;
  EventList events;
};

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's
// days_from_civil).
inline int32_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

inline void civil_from_days(int32_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

// 0 = Sunday, like tm_wday.
inline int weekday(int32_t day) { return static_cast<int>(day >= -4 ? (day + 4) % 7 : (day + 5) % 7 + 6); }

inline unsigned days_in_month(int y, unsigned m) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : kDays[m - 1];
}

namespace detail {

inline bool digits(const char* text, size_t count, int& out) {
  out = 0;
  for (size_t i = 0; i < count; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    out = out * 10 + (text[i] - '0');
  }
  return true;
}

// Copies UTF-8 text into `out`, cutting at a character boundary and dropping
// control characters (a title never breaks a label into lines).
inline void copy_text(const char* text, char* out, size_t capacity) {
  size_t length = 0;
  if (text) {
    for (const char* c = text; *c && length + 1 < capacity; ++c) {
      const unsigned char byte = static_cast<unsigned char>(*c);
      out[length++] = byte < 0x20 || byte == 0x7F ? ' ' : static_cast<char>(byte);
    }
  }
  // Cut inside a multi-byte character (the next byte continues it): the
  // character goes as a whole. Cut on a boundary, every character stays.
  if (text && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
    while (length && (static_cast<unsigned char>(out[length - 1]) & 0xC0) == 0x80) --length;
    if (length && (static_cast<unsigned char>(out[length - 1]) & 0xC0) == 0xC0) --length;
  }
  out[length] = '\0';
}

#if CALENDAR_EVENTS_PSRAM
// ArduinoJson's pool for the parse, from PSRAM (internal only when PSRAM is
// out).
struct PsramJsonAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return block ? block : heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  void deallocate(void* block) override { heap_caps_free(block); }
  void* reallocate(void* block, size_t size) override {
    void* moved = heap_caps_realloc(block, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return moved ? moved : heap_caps_realloc(block, size, MALLOC_CAP_8BIT);
  }
};

inline ArduinoJson::Allocator* json_allocator() {
  static PsramJsonAllocator allocator;
  return &allocator;
}
#endif

}  // namespace detail

// "YYYY-MM-DD" -> day; "YYYY-MM-DDTHH:MM" (seconds allowed) -> day and minute.
// `minute` becomes kAllDay for a date alone.
inline bool parse_stamp(const char* text, int32_t& day, int16_t& minute) {
  if (!text) return false;
  const size_t length = strlen(text);
  int y = 0, m = 0, d = 0;
  if (length < 10 || text[4] != '-' || text[7] != '-' || !detail::digits(text, 4, y) ||
      !detail::digits(text + 5, 2, m) || !detail::digits(text + 8, 2, d)) {
    return false;
  }
  if (m < 1 || m > 12 || d < 1 || d > static_cast<int>(days_in_month(y, static_cast<unsigned>(m)))) return false;
  day = days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
  if (length == 10) {
    minute = kAllDay;
    return true;
  }
  int hh = 0, mm = 0;
  if (length < 16 || (text[10] != 'T' && text[10] != ' ') || text[13] != ':' || !detail::digits(text + 11, 2, hh) ||
      !detail::digits(text + 14, 2, mm) || hh > 23 || mm > 59) {
    return false;
  }
  minute = static_cast<int16_t>(hh * 60 + mm);
  return true;
}

inline bool event_before(const Event& a, const Event& b) {
  if (a.start_day != b.start_day) return a.start_day < b.start_day;
  if (a.start_min != b.start_min) return a.start_min < b.start_min;  // all-day (-1) first
  return strcmp(a.title, b.title) < 0;
}

inline Calendar parse(const char* payload, size_t length) {
  Calendar out;
  if (!payload || !length || length > kMaxPayload) return out;
#if CALENDAR_EVENTS_PSRAM
  JsonDocument doc(detail::json_allocator());
#else
  JsonDocument doc;
#endif
  if (deserializeJson(doc, payload, length) || !doc.is<JsonObjectConst>()) return out;
  JsonVariantConst version = doc["v"];
  if (!version.is<int>() || version.as<int>() != 1) return out;
  int16_t ignored = 0;
  if (!parse_stamp(doc["from"].as<const char*>(), out.from_day, ignored) ||
      !parse_stamp(doc["to"].as<const char*>(), out.to_day, ignored) || out.to_day < out.from_day ||
      out.to_day - out.from_day > 400) {
    return Calendar{};
  }
  JsonVariantConst state = doc["state"];
  const char* state_text = state.is<const char*>() ? state.as<const char*>() : nullptr;
  out.available = state_text && strcmp(state_text, "unavailable") != 0;
  out.valid = true;
  JsonArrayConst events = doc["e"].as<JsonArrayConst>();
  if (events.isNull()) return out;
  out.events.reserve(std::min<size_t>(events.size(), kMaxEvents));
  for (JsonVariantConst item : events) {
    if (out.events.size() >= kMaxEvents) break;
    if (!item.is<JsonObjectConst>()) continue;
    Event event;
    int32_t end_day = 0;
    int16_t end_min = 0;
    if (!parse_stamp(item["s"].as<const char*>(), event.start_day, event.start_min) ||
        !parse_stamp(item["e"].as<const char*>(), end_day, end_min)) {
      continue;
    }
    // Both ends the same kind: an all-day start with a timed end (or the
    // reverse) is not a Home Assistant event.
    if ((event.start_min == kAllDay) != (end_min == kAllDay)) continue;
    if (event.start_min == kAllDay) {
      // Home Assistant's all-day end date is exclusive.
      event.end_day = end_day > event.start_day ? end_day - 1 : event.start_day;
      event.end_min = kAllDay;
    } else {
      if (end_day < event.start_day || (end_day == event.start_day && end_min < event.start_min)) {
        end_day = event.start_day;
        end_min = event.start_min;
      }
      // An event ending at midnight belongs to the days before.
      event.end_day = end_min == 0 && end_day > event.start_day ? end_day - 1 : end_day;
      event.end_min = end_min == 0 && end_day > event.start_day ? 24 * 60 : end_min;
    }
    detail::copy_text(item["t"].as<const char*>(), event.title, sizeof(event.title));
    detail::copy_text(item["l"].as<const char*>(), event.location, sizeof(event.location));
    out.events.push_back(event);
  }
  std::sort(out.events.begin(), out.events.end(), event_before);
  return out;
}

}  // namespace calendar_events
