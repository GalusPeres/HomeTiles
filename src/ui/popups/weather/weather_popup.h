#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct WeatherPopupInit {
  String entity_id;
  String title;
  uint32_t bg_color = 0;
  // The tile icon's current color: the header icon and its disc glow take it.
  uint32_t icon_color = 0xFFFFFF;
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
void process_weather_popup_queue();
