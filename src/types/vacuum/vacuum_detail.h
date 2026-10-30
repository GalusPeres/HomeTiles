#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

// The Bridge's retained `detail` state of a Vacuum tile (vacuum_helpers.py,
// build_vacuum_detail), parsed defensively. A missing value stays apart from
// zero: `exists` false is an entity Home Assistant does not have (state
// null), `available` false also covers "unavailable"; battery, cleaned area
// and cleaning time each carry their own has_ flag.
namespace vacuum_detail {

// Home Assistant VacuumEntityFeature.
constexpr uint32_t kPause = 4;
constexpr uint32_t kStop = 8;
constexpr uint32_t kReturnHome = 16;
constexpr uint32_t kFanSpeed = 32;
constexpr uint32_t kLocate = 512;
constexpr uint32_t kCleanSpot = 1024;
constexpr uint32_t kStart = 8192;
constexpr uint32_t kCleanArea = 16384;

constexpr size_t kMaxSpeeds = 16;
constexpr size_t kSpeedBytes = 32;
constexpr size_t kMaxAreas = 16;
constexpr size_t kAreaIdBytes = 48;
constexpr size_t kAreaNameBytes = 48;
constexpr size_t kEntityBytes = 71;
constexpr size_t kMaxPayload = 4096;

struct Area {
  char id[kAreaIdBytes + 1] = {};
  char name[kAreaNameBytes + 1] = {};
};

struct Detail {
  bool valid = false;      // a detail payload was parsed
  bool exists = false;     // Home Assistant has the entity (state not null)
  bool available = false;  // exists and not "unavailable"
  char state[16] = {};     // Home Assistant state, lower case; "" when null
  uint32_t features = 0;
  char fan_speed[kSpeedBytes + 1] = {};
  uint8_t speed_count = 0;
  char speeds[kMaxSpeeds][kSpeedBytes + 1] = {};
  bool has_battery = false;
  uint8_t battery = 0;
  uint8_t area_count = 0;
  Area areas[kMaxAreas] = {};
  // The map picture's entity (image.* or camera.*); "" without one.
  char map[kEntityBytes + 1] = {};
  bool has_cleaned_area = false;
  float cleaned_area = 0;
  char area_unit[8] = {};  // "m²" or "ft²"
  bool has_cleaning_time = false;
  uint16_t cleaning_minutes = 0;
};

inline bool copy_text(char* out, size_t capacity, const char* text) {
  if (!text || !*text) return false;
  const size_t bytes = std::strlen(text);
  if (bytes >= capacity) return false;
  std::memcpy(out, text, bytes + 1);
  return true;
}

inline bool number(JsonVariantConst value, double& out) {
  if (value.isNull() || value.is<bool>() || !value.is<double>()) return false;
  out = value.as<double>();
  return std::isfinite(out);
}

// Parses into `out` (callers keep it off the stack: about 2.4 KB) using a
// document the caller owns (heap). False leaves `out` invalid.
inline bool parse(const char* payload, size_t length, JsonDocument& doc, Detail& out) {
  out = Detail{};
  if (!payload || !length || length > kMaxPayload) return false;
  doc.clear();
  if (deserializeJson(doc, payload, length) || !doc.is<JsonObjectConst>()) return false;
  JsonVariantConst state = doc["state"];
  if (!state.isNull() && !state.is<const char*>()) return false;
  if (state.is<const char*>()) {
    const char* text = state.as<const char*>();
    if (std::strlen(text) >= sizeof(out.state)) return false;
    for (size_t i = 0; text[i]; ++i) {
      const char c = text[i];
      out.state[i] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    out.exists = out.state[0] != '\0';
  }
  out.available = out.exists && std::strcmp(out.state, "unavailable") != 0;
  JsonVariantConst features = doc["supported_features"];
  if (features.is<uint32_t>() && !features.is<bool>()) out.features = features.as<uint32_t>();
  copy_text(out.fan_speed, sizeof(out.fan_speed), doc["fan_speed"] | static_cast<const char*>(nullptr));
  for (JsonVariantConst item : doc["fan_speed_list"].as<JsonArrayConst>()) {
    if (out.speed_count >= kMaxSpeeds) break;
    if (copy_text(out.speeds[out.speed_count], sizeof(out.speeds[0]), item | static_cast<const char*>(nullptr))) {
      ++out.speed_count;
    }
  }
  double value = 0;
  if (number(doc["battery"], value)) {
    out.has_battery = true;
    out.battery = static_cast<uint8_t>(value < 0 ? 0 : value > 100 ? 100 : std::lround(value));
  }
  for (JsonVariantConst item : doc["areas"].as<JsonArrayConst>()) {
    if (out.area_count >= kMaxAreas) break;
    Area& area = out.areas[out.area_count];
    if (!copy_text(area.id, sizeof(area.id), item["id"] | static_cast<const char*>(nullptr))) continue;
    if (!copy_text(area.name, sizeof(area.name), item["name"] | static_cast<const char*>(nullptr))) {
      copy_text(area.name, sizeof(area.name), area.id);
    }
    ++out.area_count;
  }
  const char* map = doc["map"] | static_cast<const char*>(nullptr);
  if (map && (std::strncmp(map, "image.", 6) == 0 || std::strncmp(map, "camera.", 7) == 0)) {
    copy_text(out.map, sizeof(out.map), map);
  }
  JsonVariantConst area = doc["cleaned_area"];
  if (number(area["value"], value) && value >= 0 &&
      copy_text(out.area_unit, sizeof(out.area_unit), area["unit"] | static_cast<const char*>(nullptr))) {
    out.has_cleaned_area = true;
    out.cleaned_area = static_cast<float>(value);
  }
  JsonVariantConst time = doc["cleaning_time"];
  if (number(time["value"], value) && value >= 0 && value <= 65535) {
    out.has_cleaning_time = true;
    out.cleaning_minutes = static_cast<uint16_t>(std::lround(value));
  }
  out.valid = true;
  return true;
}

inline bool is(const Detail& detail, const char* state) { return std::strcmp(detail.state, state) == 0; }

// The main button: Pause while cleaning (when the entity can pause), Clean
// otherwise (start also resumes a paused run).
inline bool main_pauses(const Detail& detail) {
  return is(detail, "cleaning") && (detail.features & kPause) != 0;
}
inline bool can_start(const Detail& detail) { return (detail.features & kStart) != 0; }

}  // namespace vacuum_detail
