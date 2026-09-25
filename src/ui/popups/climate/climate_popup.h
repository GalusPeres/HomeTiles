#pragma once

#include <Arduino.h>
#include <lvgl.h>

enum ClimateSupportedFeature : uint16_t {
  CLIMATE_FEATURE_TARGET_TEMPERATURE = 1U << 0,
  CLIMATE_FEATURE_TARGET_TEMPERATURE_RANGE = 1U << 1,
  CLIMATE_FEATURE_TARGET_HUMIDITY = 1U << 2,
  CLIMATE_FEATURE_FAN_MODE = 1U << 3,
  CLIMATE_FEATURE_PRESET_MODE = 1U << 4,
  CLIMATE_FEATURE_SWING_MODE = 1U << 5,
  CLIMATE_FEATURE_TURN_OFF = 1U << 7,
  CLIMATE_FEATURE_TURN_ON = 1U << 8,
  CLIMATE_FEATURE_SWING_HORIZONTAL_MODE = 1U << 9,
};

struct ClimatePopupInit {
  String entity_id;
  String title;
  String icon_name;
  String hvac_mode;
  String hvac_action;
  String hvac_modes;
  String preset_mode;
  String preset_modes;
  String fan_mode;
  String fan_modes;
  String swing_mode;
  String swing_modes;
  String swing_horizontal_mode;
  String swing_horizontal_modes;
  String temperature_unit;
  bool icon_visible = true;
  bool dynamic_icon = true;
  bool available = true;
  bool has_supported_features = false;
  uint16_t supported_features = 0;
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
  bool has_current_temperature = false;
  bool has_current_humidity = false;
  bool has_target_temperature = false;
  bool has_target_humidity = false;
  bool has_target_range = false;
  // The opening tile's current background, including a rules tint; 0 means
  // popup_surface::kDefaultCard. Only openings apply it, state updates keep
  // the color of the last opening.
  uint32_t bg_color = 0;
};

void show_climate_popup(const ClimatePopupInit& init);
void update_climate_popup(const ClimatePopupInit& init);
void preload_climate_popup();
void hide_climate_popup();
void climate_popup_follow_tile_color(uint32_t color);
