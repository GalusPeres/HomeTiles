#pragma once

#include <Arduino.h>

#include "src/types/vacuum/vacuum_detail.h"

// The Vacuum tile's runtime (vacuum_helpers.py in the Bridge): the retained
// `detail` state per vacuum, parsed once per change into a small cache in
// PSRAM, and the commands to the Bridge on {base}/cmnd/vacuum. The device
// runtime (device_control) receives the detail messages and refreshes tiles
// and popup. Loop task only.
namespace vacuum_control {

// The latest detail of a vacuum; an invalid detail while none arrived.
const vacuum_detail::Detail& detail(const String& entity);

// device_control::queue_detail for a vacuum.* entity: parses and caches the
// payload; false for a malformed one (logged, rate limited).
bool accept_detail(const String& entity, const char* payload, size_t length);

// "start", "pause", "stop", "return_to_base", "locate" or "clean_spot".
// Only actions the entity's feature bits allow reach Home Assistant (the
// Bridge checks again).
void send(const String& entity, const char* action);
void set_fan_speed(const String& entity, const char* speed);
// Cleans the given Home Assistant areas (ids from the detail's areas).
void clean_areas(const String& entity, const char* const* area_ids, size_t count);

}  // namespace vacuum_control
