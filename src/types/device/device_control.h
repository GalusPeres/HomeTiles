#pragma once

#include <Arduino.h>

#include "src/types/device/device_detail.h"
#include "src/types/tile_type.h"

// Lock, Alarm panel and Fan tiles share one runtime (bridge-contract.md v2):
// the retained `detail` state per entity, the commands to the Bridge and the
// Bridge's answers to Lock and Alarm commands. Everything runs on the loop
// task (MQTT inbound drainer, LVGL events and timers).
namespace device_control {

// The latest detail state of an entity; `valid` false while none arrived.
device_detail::Detail detail(const String& entity);

// MQTT: a `detail` state arrived ({ha_prefix}/<domain>/<object>/detail).
void queue_detail(const String& entity, const char* payload, size_t length);
// MQTT: the Bridge answered a Lock ({base}/stat/lock) or Alarm
// ({base}/stat/alarm) command.
void queue_result(TileType type, const char* payload, size_t length);
// Loop: applies queued states and answers to the tiles and the popup.
void process(uint8_t budget = 4);

// The state a just sent command aims for ("locked", "unlocked", an armed_*
// state or "disarmed"), shown until the device reports another state, the
// command fails or 10 s pass, like Home Assistant's lock toggle and alarm
// modes. "" while none.
const char* pending_target(const String& entity);

// Rule 1: Lock and Alarm need the command channel pairing and a Web Admin
// password.
bool panel_secured();
// Why the panel may not run `action` now (nullptr action: the device in
// general), or nullptr. Translated text.
const char* blocked_reason(TileType type, const device_detail::Detail& detail, const char* action);
// Home Assistant asks for a code before this action.
bool needs_code(TileType type, const device_detail::Detail& detail, const char* action);

// Sends a Lock action ("lock", "unlock", "open") or an Alarm action
// ("arm_home", ..., "disarm") sealed, with an optional code. Returns the
// command id, or "" with `error` set to a translated reason. The code buffer
// is never kept.
String send_access(TileType type, const String& entity, const char* action, const char* code,
                   const char** error = nullptr);

// Fan commands (plain while unpaired, sealed when paired); only actions the
// entity's feature bits allow reach Home Assistant (the Bridge checks again).
void fan_action(const String& entity, const char* action);
void fan_percentage(const String& entity, uint8_t percentage);
void fan_preset(const String& entity, const char* preset);
void fan_oscillate(const String& entity, bool oscillating);
void fan_direction(const String& entity, const char* direction);

// The Alarm modes in Home Assistant's order (ALARM_MODES): state, command,
// feature bit, MDI icon and DeviceLabel index.
struct AlarmMode {
  const char* state;
  const char* action;
  uint32_t feature;
  const char* icon;
  uint8_t label;
};
constexpr size_t kAlarmModeCount = 5;
const AlarmMode& alarm_mode(size_t index);
// The target state of an Alarm action ("disarmed" for "disarm").
const char* alarm_target(const char* action);

}  // namespace device_control
