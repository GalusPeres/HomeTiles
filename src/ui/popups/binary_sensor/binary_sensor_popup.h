#pragma once

#include <Arduino.h>
#include <stdint.h>

struct BinarySensorPopupInit {
  String entity_id;
  String title;
  String icon_name;
  String state;
  String device_class;
  uint64_t last_changed = 0;
  bool available = true;
  bool icon_override = false;
  uint32_t bg_color = 0;
  // Per-tile icon colors for the popup header icon (tile_icon_colors.h).
  String icon_colors;
  bool forced_icon = false;
  uint32_t forced_icon_color = 0xFFFFFF;
};

// Binary sensors reuse the established Sensor popup surface and lifecycle.
// Only the body changes from a numeric chart to a state timeline and activity
// list, so no second full-screen overlay has to remain allocated.
void show_binary_sensor_popup(const BinarySensorPopupInit& init);

// Main-loop queue helper for live state dispatched from inbound MQTT.
// Apply only the newest update while the matching popup is visible;
// shared pending state is not synchronized for worker-task access.
void queue_binary_sensor_popup_state(const String& entity_id,
                                     const String& state,
                                     bool available,
                                     const String& device_class,
                                     uint64_t last_changed,
                                     const String& icon_name);
