#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

// The Bridge's retained `detail` state of a Lock, Alarm panel or Fan tile
// (build/ha-dummy-sim/bridge-contract.md, "State"): one record for the three
// types, parsed defensively. A missing value stays apart from zero: `exists`
// false is an entity Home Assistant does not have (state null), `available`
// false also covers "unavailable"; "unknown" stays operable like in the Home
// Assistant frontend.
namespace device_detail {

// Home Assistant LockEntityFeature.
constexpr uint32_t kLockOpen = 1;
// Home Assistant AlarmControlPanelEntityFeature.
constexpr uint32_t kArmHome = 1;
constexpr uint32_t kArmAway = 2;
constexpr uint32_t kArmNight = 4;
constexpr uint32_t kArmCustomBypass = 16;
constexpr uint32_t kArmVacation = 32;
// Home Assistant FanEntityFeature.
constexpr uint32_t kFanSetSpeed = 1;
constexpr uint32_t kFanOscillate = 2;
constexpr uint32_t kFanDirection = 4;
constexpr uint32_t kFanPresetMode = 8;
constexpr uint32_t kFanTurnOff = 16;
constexpr uint32_t kFanTurnOn = 32;

constexpr size_t kMaxPresets = 16;
constexpr size_t kPresetBytes = 32;
constexpr size_t kMaxPayload = 2048;

struct Detail {
  bool valid = false;      // a detail payload was parsed
  bool exists = false;     // Home Assistant has the entity (state not null)
  bool available = false;  // exists and not "unavailable"
  char state[24] = {};     // Home Assistant state, lower case; "" when null
  uint32_t features = 0;
  // Lock and Alarm panel: Home Assistant's code format and where the panel
  // must ask for a code first.
  bool code_number = false;
  bool code_text = false;
  bool lock_code = false;
  bool unlock_code = false;
  bool arm_code = false;
  bool disarm_code = false;
  // The Bridge's permission to unlock/open or disarm without a code.
  bool unlock_allowed = true;
  bool disarm_allowed = true;
  // Fan.
  bool has_percentage = false;
  uint8_t percentage = 0;
  float percentage_step = 0;  // 0: not reported
  bool has_oscillating = false;
  bool oscillating = false;
  char direction[8] = {};       // "forward", "reverse" or "" (not reported)
  char preset_mode[kPresetBytes + 1] = {};
  uint8_t preset_count = 0;
  char presets[kMaxPresets][kPresetBytes + 1] = {};
};

inline bool flag(JsonVariantConst value, bool fallback) {
  return value.is<bool>() ? value.as<bool>() : fallback;
}

inline Detail parse(const char* payload, size_t length) {
  Detail out;
  if (!payload || !length || length > kMaxPayload) return out;
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload, length) || !doc.is<JsonObjectConst>()) return out;
  JsonVariantConst state = doc["state"];
  if (!state.isNull() && !state.is<const char*>()) return out;
  if (state.is<const char*>()) {
    const char* text = state.as<const char*>();
    if (std::strlen(text) >= sizeof(out.state)) return out;
    for (size_t i = 0; text[i]; ++i) {
      const char c = text[i];
      out.state[i] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    out.exists = out.state[0] != '\0';
  }
  out.available = out.exists && std::strcmp(out.state, "unavailable") != 0;
  JsonVariantConst features = doc["supported_features"];
  if (features.is<uint32_t>() && !features.is<bool>()) out.features = features.as<uint32_t>();
  const char* format = doc["code_format"] | static_cast<const char*>(nullptr);
  out.code_number = format && std::strcmp(format, "number") == 0;
  out.code_text = format && !out.code_number;
  out.lock_code = flag(doc["lock_code"], false);
  out.unlock_code = flag(doc["unlock_code"], false);
  out.arm_code = flag(doc["arm_code"], false);
  out.disarm_code = flag(doc["disarm_code"], false);
  out.unlock_allowed = flag(doc["unlock_allowed"], true);
  out.disarm_allowed = flag(doc["disarm_allowed"], true);

  JsonVariantConst percentage = doc["percentage"];
  if (!percentage.isNull() && !percentage.is<bool>() && percentage.is<double>()) {
    const double value = percentage.as<double>();
    if (std::isfinite(value)) {
      out.has_percentage = true;
      out.percentage = static_cast<uint8_t>(value < 0 ? 0 : value > 100 ? 100 : std::lround(value));
    }
  }
  JsonVariantConst step = doc["percentage_step"];
  if (!step.isNull() && !step.is<bool>() && step.is<double>()) {
    const double value = step.as<double>();
    if (std::isfinite(value) && value > 0 && value <= 100) out.percentage_step = static_cast<float>(value);
  }
  JsonVariantConst oscillating = doc["oscillating"];
  if (oscillating.is<bool>()) {
    out.has_oscillating = true;
    out.oscillating = oscillating.as<bool>();
  }
  const char* direction = doc["direction"] | "";
  if (std::strcmp(direction, "forward") == 0 || std::strcmp(direction, "reverse") == 0) {
    std::strcpy(out.direction, direction);
  }
  const char* preset = doc["preset_mode"] | "";
  if (std::strlen(preset) <= kPresetBytes) std::strcpy(out.preset_mode, preset);
  JsonArrayConst presets = doc["preset_modes"].as<JsonArrayConst>();
  for (JsonVariantConst item : presets) {
    if (out.preset_count >= kMaxPresets) break;
    const char* name = item | "";
    const size_t bytes = std::strlen(name);
    if (!bytes || bytes > kPresetBytes) continue;
    std::strcpy(out.presets[out.preset_count++], name);
  }
  out.valid = true;
  return out;
}

inline Detail parse(const char* payload) { return parse(payload, payload ? std::strlen(payload) : 0); }

inline bool is(const Detail& detail, const char* state) { return std::strcmp(detail.state, state) == 0; }

// The number of fixed Fan speeds (Home Assistant's percentage_step); 100
// without a step: a free percentage.
inline int fan_speed_count(const Detail& detail) {
  if (detail.percentage_step <= 0) return 100;
  const int count = static_cast<int>(std::lround(100.0f / detail.percentage_step));
  return count < 1 ? 1 : count > 100 ? 100 : count;
}

// The speed for a percentage (0 = off), and back.
inline int fan_speed_of(const Detail& detail, int percentage) {
  const int count = fan_speed_count(detail);
  if (percentage <= 0) return 0;
  const int speed = static_cast<int>(std::lround(percentage * count / 100.0f));
  return speed < 1 ? 1 : speed > count ? count : speed;
}

inline int fan_percentage_of(const Detail& detail, int speed) {
  const int count = fan_speed_count(detail);
  if (speed <= 0) return 0;
  if (speed >= count) return 100;
  return static_cast<int>(std::lround(speed * 100.0f / count));
}

}  // namespace device_detail
