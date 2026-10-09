#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct WeatherPopupInit {
  String entity_id;
  String title;
  uint32_t bg_color = 0;
  // The tile icon's current color: the header icon and its disc glow take it.
  uint32_t icon_color = 0xFFFFFF;
  // Weather setting "Colored weather icons"; a rule forcing the tile icon
  // color draws the header icon in that color.
  bool colored_icons = true;
  bool icon_forced = false;
  // The tile's optional temperature/humidity sensors (weather_sensors.h).
  String temperature_sensor;
  String humidity_sensor;
};

void show_weather_popup(const WeatherPopupInit& init);
// True only when the hidden, prebuilt popup already represents the newest
// cached payload for this entity in the currently selected language.
bool weather_popup_has_current_cached_payload(const char* entity_id);
void preload_weather_popup();
void hide_weather_popup();
void weather_popup_follow_tile_color(uint32_t color);
void weather_popup_refresh_language();

// Main-loop queue helper for state dispatched from inbound MQTT.
// Pending state and popup objects are not synchronized for worker-task access.
void queue_weather_popup_payload(const char* entity_id, const char* payload);
// A sensor state changed: a visible popup showing that sensor refreshes its
// header from the cached weather payload in process_weather_popup_queue().
void queue_weather_popup_sensor_refresh(const char* entity_id);
void process_weather_popup_queue();
