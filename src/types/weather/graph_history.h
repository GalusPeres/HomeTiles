#pragma once

#include <Arduino.h>

// In-RAM hourly weather cache with a LittleFS backup (no wear concern, see
// store_hour): the file is rewritten only when a slot really changes, about
// once an hour with a new forecast, so a reboot or OTA keeps the past.
//
// The bridge forecast only covers future hours, so the "Today" graph would
// start at the current hour. Every received forecast hour is filed here under
// its entity+date+hour; later the "Today" half (and the Web Admin preview)
// fills its missing past hours from this cache. History ("src":"hist")
// entries from a newer bridge still win: the panel merge prefers payload
// entries, the cache is only a fallback for slots the payload does not carry.
// Each entity keeps the two most recent dates: yesterday's "tomorrow" bucket
// is today's past, so midnight and reboots keep a whole day with no gap.
// English comments in this module (project rulebook).
namespace weather_history {

// One cached hour: the last seen values for an entity+date+hour.
struct Hour {
  bool has_temperature = false;
  float temperature = 0.0f;
  char condition[28] = {};
  char icon[36] = {};
  bool has_precipitation = false;
  float precipitation = 0.0f;
};

// File every received hour (forecast or hist) under entity+date+hour.
// Entries without any of temperature/condition/precipitation are ignored.
void store_hour(const char* entity_id, const char* date, int hour, const Hour& value);

// File the cache persists to (LittleFS, same "/_" prefix as other files).
extern const char* kCachePath;

// Cached hour for entity+date+hour; false when nothing was ever filed.
bool lookup_hour(const char* entity_id, const char* date, int hour, Hour& out);

// Cached temperature only; false when the slot has none.
bool cached_temperature(const char* entity_id, const char* date, int hour, float& out);

}  // namespace weather_history
