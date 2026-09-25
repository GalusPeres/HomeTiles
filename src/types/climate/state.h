#pragma once

#include <stdint.h>
#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/config/tile_config.h"

struct ClimateState {
  bool valid = false;
  bool available = true;
  bool has_supported_features = false;
  bool has_current_temperature = false;
  bool has_current_humidity = false;
  bool has_target_temperature = false;
  bool has_target_humidity = false;
  bool has_target_range = false;
  float current_temperature = 0.0f;
  float current_humidity = 0.0f;
  float target_temperature = 20.0f;
  float target_humidity = 50.0f;
  float target_temp_low = 18.0f;
  float target_temp_high = 24.0f;
  float min_temp = 7.0f;
  float max_temp = 35.0f;
  float min_humidity = 30.0f;
  float max_humidity = 99.0f;
  float target_temp_step = 0.5f;
  float target_humidity_step = 1.0f;
  uint16_t supported_features = 0;
  char hvac_mode[16] = {};
  char hvac_action[16] = {};
  char fan_mode[16] = {};
  char swing_mode[16] = {};
  char swing_horizontal_mode[16] = {};
  char temperature_unit[8] = {};
  uint8_t hvac_modes_mask = 0;
  uint8_t preset_mode_id = 0xFF;
  uint8_t preset_modes_mask = 0;
  // Home Assistant's own option names as comma-separated lists, including
  // vendor modes such as "1" or "silent" that have no fixed name here.
  char fan_modes[96] = {};
  char swing_modes[64] = {};
  char swing_horizontal_modes[64] = {};
};

enum ClimateHvacModeMask : uint8_t {
  CLIMATE_MODE_OFF = 1U << 0,
  CLIMATE_MODE_HEAT = 1U << 1,
  CLIMATE_MODE_COOL = 1U << 2,
  CLIMATE_MODE_HEAT_COOL = 1U << 3,
  CLIMATE_MODE_AUTO = 1U << 4,
  CLIMATE_MODE_DRY = 1U << 5,
  CLIMATE_MODE_FAN_ONLY = 1U << 6
};

inline String climateHvacModesCsv(uint8_t mask) {
  String modes;
  auto append = [&](uint8_t bit, const char* name) {
    if ((mask & bit) == 0) return;
    if (modes.length()) modes += ',';
    modes += name;
  };
  append(CLIMATE_MODE_OFF, "off");
  append(CLIMATE_MODE_HEAT, "heat");
  append(CLIMATE_MODE_COOL, "cool");
  append(CLIMATE_MODE_HEAT_COOL, "heat_cool");
  append(CLIMATE_MODE_AUTO, "auto");
  append(CLIMATE_MODE_DRY, "dry");
  append(CLIMATE_MODE_FAN_ONLY, "fan_only");
  return modes;
}

enum ClimatePresetMask : uint8_t {
  CLIMATE_PRESET_NONE = 1U << 0,
  CLIMATE_PRESET_ECO = 1U << 1,
  CLIMATE_PRESET_AWAY = 1U << 2,
  CLIMATE_PRESET_BOOST = 1U << 3,
  CLIMATE_PRESET_COMFORT = 1U << 4,
  CLIMATE_PRESET_HOME = 1U << 5,
  CLIMATE_PRESET_SLEEP = 1U << 6,
  CLIMATE_PRESET_ACTIVITY = 1U << 7
};

inline const char* climatePresetName(uint8_t id) {
  static const char* const names[] = {
      "none", "eco", "away", "boost", "comfort", "home", "sleep", "activity"};
  return id < 8 ? names[id] : "";
}

inline String climatePresetModesCsv(uint8_t mask) {
  String modes;
  for (uint8_t id = 0; id < 8; ++id) {
    if ((mask & (1U << id)) == 0) continue;
    if (modes.length()) modes += ',';
    modes += climatePresetName(id);
  }
  return modes;
}

enum ClimateFanModeMask : uint16_t {
  CLIMATE_FAN_AUTO = 1U << 0,
  CLIMATE_FAN_LOW = 1U << 1,
  CLIMATE_FAN_MEDIUM = 1U << 2,
  CLIMATE_FAN_HIGH = 1U << 3,
  CLIMATE_FAN_ON = 1U << 4,
  CLIMATE_FAN_OFF = 1U << 5,
  CLIMATE_FAN_TOP = 1U << 6,
  CLIMATE_FAN_MIDDLE = 1U << 7,
  CLIMATE_FAN_FOCUS = 1U << 8,
  CLIMATE_FAN_DIFFUSE = 1U << 9
};

inline String climateFanModesCsv(uint16_t mask) {
  static const char* const names[] = {
      "auto", "low", "medium", "high", "on",
      "off", "top", "middle", "focus", "diffuse"};
  String modes;
  for (uint8_t id = 0; id < 10; ++id) {
    if ((mask & (1U << id)) == 0) continue;
    if (modes.length()) modes += ',';
    modes += names[id];
  }
  return modes;
}

enum ClimateSwingModeMask : uint8_t {
  CLIMATE_SWING_OFF = 1U << 0,
  CLIMATE_SWING_ON = 1U << 1,
  CLIMATE_SWING_VERTICAL = 1U << 2,
  CLIMATE_SWING_HORIZONTAL = 1U << 3,
  CLIMATE_SWING_BOTH = 1U << 4
};

inline String climateSwingModesCsv(uint8_t mask) {
  static const char* const names[] = {
      "off", "on", "vertical", "horizontal", "both"};
  String modes;
  for (uint8_t id = 0; id < 5; ++id) {
    if ((mask & (1U << id)) == 0) continue;
    if (modes.length()) modes += ',';
    modes += names[id];
  }
  return modes;
}

enum ClimateHorizontalSwingModeMask : uint8_t {
  CLIMATE_SWING_H_OFF = 1U << 0,
  CLIMATE_SWING_H_ON = 1U << 1,
  CLIMATE_SWING_H_LEFT = 1U << 2,
  CLIMATE_SWING_H_CENTER = 1U << 3,
  CLIMATE_SWING_H_RIGHT = 1U << 4,
  CLIMATE_SWING_H_SWING = 1U << 5,
  CLIMATE_SWING_H_WIDE = 1U << 6
};

inline String climateHorizontalSwingModesCsv(uint8_t mask) {
  static const char* const names[] = {
      "off", "on", "left", "center", "right", "swing", "wide"};
  String modes;
  for (uint8_t id = 0; id < 7; ++id) {
    if ((mask & (1U << id)) == 0) continue;
    if (modes.length()) modes += ',';
    modes += names[id];
  }
  return modes;
}

struct ClimateTileWidgets {
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* value_label = nullptr;
  static constexpr uint8_t kMaxSlots = 6;
  lv_obj_t* slot_roots[kMaxSlots] = {};
  uint8_t slot_kinds[kMaxSlots] = {};
  uint8_t slot_layouts[kMaxSlots] = {};
  ClimateTileItemGeometry slot_geometry[kMaxSlots] = {};
  uint8_t active_slot_count = 0;
  bool dynamic_icon = true;
  uint32_t last_payload_hash = 0;
};
