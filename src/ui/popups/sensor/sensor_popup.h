#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct SensorPopupInit {
  String entity_id;
  String title;
  String icon_name;
  String value;
  String unit;
  bool lock_unit = false;
  uint8_t decimals = 0xFF;
  uint32_t bg_color = 0;
  bool editable = false;
  bool binary_mode = false;
  bool state_history_mode = false;
  String binary_device_class;
  uint64_t binary_last_changed = 0;
  bool binary_available = true;
  bool binary_icon_override = false;
  // The tile's per-tile icon colors (tile_icon_colors.h): the header icon
  // takes the same color as the tile icon for the current state.
  String icon_colors;
  // Rules that force the tile icon color (another entity, entity color):
  // the header shows the tile icon's color from the opening, and the icon of
  // their matching rule (tile_rule_icon.h codepoint, 0 = the tile's own).
  bool forced_icon = false;
  uint32_t forced_icon_color = 0xFFFFFF;
  uint32_t forced_icon_glyph = 0;
};

// Ordinary HA sensors with textual states use the same discrete history view
// as binary sensors. Numeric sensors, including temporarily unavailable ones
// with a configured unit, keep the established graph.
bool sensor_popup_should_use_state_history(const String& value,
                                           const String& unit);

void show_sensor_popup(const SensorPopupInit& init);
// A finger came down on a tile whose popup shows this numeric sensor's graph
// (the Bridge's answer took ~125 ms, the popup ~110 ms to show, user
// 2026-10-29: the graph still came after the popup): its 24 h history is
// asked now, and the popup opened by the tap finds the answer, or the request
// on its way, instead of asking only then.
void sensor_popup_prefetch_history(const String& entity_id);
void preload_sensor_popup();
void hide_sensor_popup();
void sensor_popup_follow_tile_color(uint32_t color);

// Main-loop queue helpers for state/history dispatched from inbound MQTT.
// These shared pending values are not synchronized for worker-task access.
void queue_sensor_popup_value(const char* entity_id, const char* value, const char* unit, uint8_t decimals = 0xFF);
void queue_sensor_popup_history(const char* entity_id, const char* payload, size_t len);
void queue_sensor_popup_binary_state(const String& entity_id,
                                     const String& state,
                                     bool available,
                                     const String& device_class,
                                     uint64_t last_changed,
                                     const String& icon_name);
void process_sensor_popup_queue();
void queue_sensor_popup_icon_refresh();
