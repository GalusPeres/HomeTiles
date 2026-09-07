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
};

// Ordinary HA sensors with textual states use the same discrete history view
// as binary sensors. Numeric sensors, including temporarily unavailable ones
// with a configured unit, keep the established graph.
bool sensor_popup_should_use_state_history(const String& value,
                                           const String& unit);

void show_sensor_popup(const SensorPopupInit& init);
void preload_sensor_popup();
void hide_sensor_popup();

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
