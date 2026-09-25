#pragma once

#include <Arduino.h>
#include <cstring>

namespace climate_visuals {

inline bool equals(const char* value, const char* expected) {
  return value && expected && strcmp(value, expected) == 0;
}

inline uint32_t mode_foreground_color(const char* mode) {
  if (equals(mode, "heat")) return 0xFF6F22;
  if (equals(mode, "cool")) return 0x2196F3;
  if (equals(mode, "dry")) return 0xFFB581;
  if (equals(mode, "fan_only")) return 0x00BCD4;
  if (equals(mode, "auto")) return 0x4CAF50;
  if (equals(mode, "heat_cool")) return 0xFFC107;
  if (equals(mode, "off")) return 0x808080;
  return 0xFFFFFF;
}

inline uint32_t mode_ring_color(const char* mode) {
  // Home Assistant uses a muted brown ring with a brighter peach foreground
  // for dehumidification.
  if (equals(mode, "dry")) return 0x8C664B;
  return mode_foreground_color(mode);
}

inline uint32_t state_foreground_color(
    const char* mode, const char* action) {
  if (equals(action, "heating") || equals(action, "preheating")) {
    return mode_foreground_color("heat");
  }
  if (equals(action, "cooling")) {
    return mode_foreground_color("cool");
  }
  if (equals(action, "drying")) {
    return mode_foreground_color("dry");
  }
  if (equals(action, "fan")) {
    return mode_foreground_color("fan_only");
  }
  if (equals(action, "defrosting")) {
    return mode_foreground_color("heat");
  }
  if (equals(mode, "off") || equals(action, "off")) {
    return mode_foreground_color("off");
  }
  return mode_foreground_color(mode);
}

// False when state_foreground_color() shows the off color, or the mode is
// empty or unknown: the climate entity is not running.
inline bool state_active(const char* mode, const char* action) {
  if (equals(action, "heating") || equals(action, "preheating") ||
      equals(action, "cooling") || equals(action, "drying") ||
      equals(action, "fan") || equals(action, "defrosting")) {
    return true;
  }
  return mode && *mode && !equals(mode, "off") && !equals(mode, "unknown") &&
         !equals(action, "off");
}

inline uint32_t mode_foreground_color(const String& mode) {
  return mode_foreground_color(mode.c_str());
}

inline uint32_t mode_ring_color(const String& mode) {
  return mode_ring_color(mode.c_str());
}

inline uint32_t state_foreground_color(
    const String& mode, const String& action) {
  return state_foreground_color(mode.c_str(), action.c_str());
}

}  // namespace climate_visuals
