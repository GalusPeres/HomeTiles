#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include "src/types/tile_type_policy.h"
#include "src/tiles/runtime/tile_renderer.h"

constexpr size_t EDITABLE_PAYLOAD_MAX = 24576;
struct EditableValue {
  String kind, state, unit, mode, session, revision;
  std::vector<String> options;
  double minimum = 0, maximum = 0, step = 0;
  uint64_t last_changed = 0;
  bool valid = false, available = false, writable = false, has_state = false;
};
EditableValue parse_editable_value(const String& payload);
bool editable_entity_matches(TileType type, const String& entity);
String editable_display_value(const EditableValue& value);
void append_editable_translations(String& html, const char* name);
void refresh_editable_tile(GridType grid, uint8_t index);
void queue_editable_value(const String& entity, const char* payload);
void process_editable_updates(uint8_t budget = 4);
uint32_t editable_value_generation();
void editable_configuration_changed();
String editable_request_history(const String& entity, uint16_t hours);
bool editable_handle_ack(const char* topic, const char* payload, size_t length);

// The Sensor popup owns the reused controls and their event data.
struct EditableControl;
EditableControl* editable_control_create(lv_obj_t* row, lv_obj_t* card);
void editable_control_open(EditableControl*, const String& entity);
void editable_control_refresh(EditableControl*);
void editable_control_close(EditableControl*);
void editable_control_delete(EditableControl*);

int editable_control_height(const String& kind);
