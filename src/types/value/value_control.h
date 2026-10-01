#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include "src/core/memory/psram_allocator.h"
#include "src/types/tile_type_policy.h"
#include "src/tiles/runtime/tile_renderer.h"

constexpr size_t EDITABLE_PAYLOAD_MAX = 24576;
struct EditableValue {
  String kind, state, unit, mode, session, revision;
  // Up to 64 Select options stay with the open control; PSRAM keeps the list
  // buffer out of the S3's internal heap.
  PsVector<String> options;
  double minimum = 0, maximum = 0, step = 0;
  uint64_t last_changed = 0;
  bool valid = false, available = false, writable = false, has_state = false;
};
EditableValue parse_editable_value(const String& payload);
bool editable_entity_matches(TileType type, const String& entity);
String editable_display_value(const EditableValue& value);
void append_editable_translations(String& html, const char* name);
// The Sensor tile that shows a Number, Select or Date/Time value.
Tile editable_display_tile(const Tile& tile);
void refresh_editable_tile(GridType grid, uint8_t index);
void queue_editable_value(const String& entity, const char* payload);
void process_editable_updates(uint8_t budget = 4);
uint32_t editable_value_generation();
void editable_configuration_changed();
String editable_request_history(const String& entity, uint16_t hours);
bool editable_handle_ack(const char* topic, const char* payload, size_t length);

// The Sensor popup owns the reused controls and their event data.
struct EditableControl;
// `icon`: the popup's header icon, whose color the control surfaces follow
// (popup_nav_style.h).
EditableControl* editable_control_create(lv_obj_t* row, lv_obj_t* card, lv_obj_t* icon = nullptr);
// Restyles the surfaces after the card or header icon color changed.
void editable_control_follow_colors(EditableControl*);
void editable_control_open(EditableControl*, const String& entity);
void editable_control_refresh(EditableControl*);
void editable_control_close(EditableControl*);
bool editable_control_is_interacting(const EditableControl*);
void editable_control_delete(EditableControl*);

int editable_control_height(const String& kind);
