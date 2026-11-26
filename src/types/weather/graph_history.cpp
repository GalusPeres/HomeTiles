#include "src/types/weather/graph_history.h"

#include <LittleFS.h>

#include <cstring>

namespace weather_history {
namespace {

constexpr uint8_t kEntities = 6;
constexpr uint8_t kHours = 24;
// Day buckets per entity: the payload carries two dates at once (today and
// tomorrow), and yesterday's "tomorrow" is today's past. One date per entity
// would wipe today's hours on every payload, so each entity keeps two.
constexpr uint8_t kDayBuckets = 2;
// Backstop between flash rewrites. In practice store_hour only marks the
// cache dirty when a slot really changes (about once an hour with a new
// forecast), so this almost never triggers; it just caps bursts, e.g. the
// first fill after boot or a date rollover.
constexpr uint32_t kSaveIntervalMs = 600000;
constexpr char kMagic[4] = {'W', 'G', 'H', '2'};
constexpr uint8_t kVersion = 2;

struct EntityCache {
  bool used = false;
  char entity[64] = {};
  char dates[kDayBuckets][11] = {};
  Hour hours[kDayBuckets][kHours];
};

EntityCache s_cache[kEntities];
bool s_loaded = false;
bool s_dirty = false;
uint32_t s_last_save_ms = 0;
uint8_t s_evict_next = 0;

bool valid_hour(int hour) { return hour >= 0 && hour < kHours; }

bool valid_date(const char* date) { return date && strlen(date) == 10; }

void copy_text(char* out, size_t out_size, const char* in) {
  if (!out || out_size == 0) return;
  out[0] = '\0';
  if (!in) return;
  strncpy(out, in, out_size - 1);
  out[out_size - 1] = '\0';
}

bool same_entity(const char* a, const char* b) {
  if (!a || !b) return false;
  return strncmp(a, b, 64) == 0;
}

EntityCache* find_entity(const char* entity_id) {
  for (uint8_t i = 0; i < kEntities; ++i) {
    if (s_cache[i].used && same_entity(s_cache[i].entity, entity_id)) return &s_cache[i];
  }
  return nullptr;
}

EntityCache* alloc_entity(const char* entity_id) {
  for (uint8_t i = 0; i < kEntities; ++i) {
    if (!s_cache[i].used) {
      s_cache[i].used = true;
      copy_text(s_cache[i].entity, sizeof(s_cache[i].entity), entity_id);
      for (uint8_t b = 0; b < kDayBuckets; ++b) {
        s_cache[i].dates[b][0] = '\0';
        for (uint8_t h = 0; h < kHours; ++h) s_cache[i].hours[b][h] = Hour{};
      }
      return &s_cache[i];
    }
  }
  // Full: evict round-robin. Weather entities are few; this only bounds RAM.
  EntityCache* slot = &s_cache[s_evict_next % kEntities];
  s_evict_next = static_cast<uint8_t>((s_evict_next + 1) % kEntities);
  slot->used = true;
  copy_text(slot->entity, sizeof(slot->entity), entity_id);
  for (uint8_t b = 0; b < kDayBuckets; ++b) {
    slot->dates[b][0] = '\0';
    for (uint8_t h = 0; h < kHours; ++h) slot->hours[b][h] = Hour{};
  }
  return slot;
}

// Bucket holding `date`, or -1. Empty buckets (date[0] == 0) never match.
int find_bucket(const EntityCache* slot, const char* date) {
  for (uint8_t b = 0; b < kDayBuckets; ++b) {
    if (slot->dates[b][0] != '\0' && strncmp(slot->dates[b], date, 11) == 0) return b;
  }
  return -1;
}

// Bucket for a new date: an empty one, else the oldest (YYYY-MM-DD compares
// chronologically). Never wipes the other bucket.
int alloc_bucket(EntityCache* slot, const char* date) {
  for (uint8_t b = 0; b < kDayBuckets; ++b) {
    if (slot->dates[b][0] == '\0') {
      copy_text(slot->dates[b], sizeof(slot->dates[b]), date);
      return b;
    }
  }
  uint8_t oldest = 0;
  for (uint8_t b = 1; b < kDayBuckets; ++b) {
    if (strncmp(slot->dates[b], slot->dates[oldest], 11) < 0) oldest = b;
  }
  copy_text(slot->dates[oldest], sizeof(slot->dates[oldest]), date);
  for (uint8_t h = 0; h < kHours; ++h) slot->hours[oldest][h] = Hour{};
  return oldest;
}

void save_now() {
  File file = LittleFS.open(kCachePath, FILE_WRITE);
  if (!file) return;
  file.write(reinterpret_cast<const uint8_t*>(kMagic), sizeof(kMagic));
  file.write(&kVersion, 1);
  uint8_t count = 0;
  for (uint8_t i = 0; i < kEntities; ++i) {
    if (s_cache[i].used) ++count;
  }
  file.write(&count, 1);
  for (uint8_t i = 0; i < kEntities; ++i) {
    if (!s_cache[i].used) continue;
    file.write(reinterpret_cast<const uint8_t*>(s_cache[i].entity), sizeof(s_cache[i].entity));
    for (uint8_t b = 0; b < kDayBuckets; ++b) {
      file.write(reinterpret_cast<const uint8_t*>(s_cache[i].dates[b]),
                 sizeof(s_cache[i].dates[b]));
      for (uint8_t h = 0; h < kHours; ++h) {
        const Hour& slot = s_cache[i].hours[b][h];
        const uint8_t flags = static_cast<uint8_t>((slot.has_temperature ? 0x01 : 0) |
                                                  (slot.has_precipitation ? 0x02 : 0));
        file.write(&flags, 1);
        const int32_t temp_tenths =
            slot.has_temperature ? static_cast<int32_t>(lroundf(slot.temperature * 10.0f)) : 0;
        file.write(reinterpret_cast<const uint8_t*>(&temp_tenths), sizeof(temp_tenths));
        const int32_t precip_tenths =
            slot.has_precipitation ? static_cast<int32_t>(lroundf(slot.precipitation * 10.0f)) : 0;
        file.write(reinterpret_cast<const uint8_t*>(&precip_tenths), sizeof(precip_tenths));
        file.write(reinterpret_cast<const uint8_t*>(slot.condition), sizeof(slot.condition));
        file.write(reinterpret_cast<const uint8_t*>(slot.icon), sizeof(slot.icon));
      }
    }
  }
  file.close();
  s_dirty = false;
  s_last_save_ms = millis();
}

bool read_exact(File& file, uint8_t* out, size_t length) {
  size_t done = 0;
  while (done < length) {
    const int got = file.read(out + done, length - done);
    if (got <= 0) return false;
    done += static_cast<size_t>(got);
  }
  return true;
}

void load_now() {
  s_loaded = true;
  File file = LittleFS.open(kCachePath, FILE_READ);
  if (!file) return;
  char magic[4] = {};
  uint8_t version = 0;
  uint8_t count = 0;
  if (!read_exact(file, reinterpret_cast<uint8_t*>(magic), sizeof(magic)) ||
      memcmp(magic, kMagic, sizeof(kMagic)) != 0 || !read_exact(file, &version, 1) ||
      version != kVersion || !read_exact(file, &count, 1)) {
    file.close();
    return;
  }
  if (count > kEntities) {
    file.close();
    return;
  }
  for (uint8_t i = 0; i < count; ++i) {
    EntityCache slot;
    slot.used = true;
    if (!read_exact(file, reinterpret_cast<uint8_t*>(slot.entity), sizeof(slot.entity))) {
      file.close();
      return;
    }
    slot.entity[sizeof(slot.entity) - 1] = '\0';
    if (!slot.entity[0]) {
      file.close();
      return;
    }
    for (uint8_t b = 0; b < kDayBuckets; ++b) {
      if (!read_exact(file, reinterpret_cast<uint8_t*>(slot.dates[b]), sizeof(slot.dates[b]))) {
        file.close();
        return;
      }
      slot.dates[b][sizeof(slot.dates[b]) - 1] = '\0';
      if (slot.dates[b][0] != '\0' && strlen(slot.dates[b]) != 10) {
        file.close();
        return;
      }
      for (uint8_t h = 0; h < kHours; ++h) {
        uint8_t flags = 0;
        int32_t temp_tenths = 0;
        int32_t precip_tenths = 0;
        if (!read_exact(file, &flags, 1) ||
            !read_exact(file, reinterpret_cast<uint8_t*>(&temp_tenths), sizeof(temp_tenths)) ||
            !read_exact(file, reinterpret_cast<uint8_t*>(&precip_tenths), sizeof(precip_tenths)) ||
            !read_exact(file, reinterpret_cast<uint8_t*>(slot.hours[b][h].condition),
                        sizeof(slot.hours[b][h].condition)) ||
            !read_exact(file, reinterpret_cast<uint8_t*>(slot.hours[b][h].icon),
                        sizeof(slot.hours[b][h].icon))) {
          file.close();
          return;
        }
        slot.hours[b][h].condition[sizeof(slot.hours[b][h].condition) - 1] = '\0';
        slot.hours[b][h].icon[sizeof(slot.hours[b][h].icon) - 1] = '\0';
        slot.hours[b][h].has_temperature = (flags & 0x01) != 0;
        slot.hours[b][h].temperature = temp_tenths / 10.0f;
        slot.hours[b][h].has_precipitation = (flags & 0x02) != 0;
        slot.hours[b][h].precipitation = precip_tenths / 10.0f;
      }
    }
    s_cache[i] = slot;
  }
  file.close();
}

void ensure_loaded() {
  if (!s_loaded) load_now();
}

void maybe_save() {
  if (!s_dirty) return;
  const uint32_t now = millis();
  if (now - s_last_save_ms < kSaveIntervalMs && s_last_save_ms != 0) return;
  save_now();
}

}  // namespace

const char* kCachePath = "/_weather_hist.bin";

void store_hour(const char* entity_id, const char* date, int hour, const Hour& value) {
  if (!entity_id || !entity_id[0] || !valid_date(date) || !valid_hour(hour)) return;
  if (!value.has_temperature && !value.condition[0] && !value.has_precipitation) return;
  ensure_loaded();
  EntityCache* slot = find_entity(entity_id);
  if (!slot) slot = alloc_entity(entity_id);
  int bucket = find_bucket(slot, date);
  if (bucket < 0) bucket = alloc_bucket(slot, date);
  // Forecasts arrive far more often than they change (about once an hour):
  // rewrite flash only when the slot really differs.
  const Hour& old = slot->hours[bucket][hour];
  const bool same = old.has_temperature == value.has_temperature &&
                    (!value.has_temperature ||
                     lroundf(old.temperature * 10.0f) == lroundf(value.temperature * 10.0f)) &&
                    old.has_precipitation == value.has_precipitation &&
                    (!value.has_precipitation ||
                     lroundf(old.precipitation * 10.0f) == lroundf(value.precipitation * 10.0f)) &&
                    strncmp(old.condition, value.condition, sizeof(old.condition)) == 0 &&
                    strncmp(old.icon, value.icon, sizeof(old.icon)) == 0;
  if (same) return;
  slot->hours[bucket][hour] = value;
  s_dirty = true;
  maybe_save();
}

bool lookup_hour(const char* entity_id, const char* date, int hour, Hour& out) {
  out = Hour{};
  if (!entity_id || !entity_id[0] || !date || !valid_hour(hour)) return false;
  ensure_loaded();
  EntityCache* slot = find_entity(entity_id);
  if (!slot) return false;
  const int bucket = find_bucket(slot, date);
  if (bucket < 0) return false;
  out = slot->hours[bucket][hour];
  return out.has_temperature || out.condition[0] != '\0' || out.has_precipitation;
}

bool cached_temperature(const char* entity_id, const char* date, int hour, float& out) {
  out = 0.0f;
  Hour slot;
  if (!lookup_hour(entity_id, date, hour, slot)) return false;
  if (!slot.has_temperature) return false;
  out = slot.temperature;
  return true;
}

}  // namespace weather_history
