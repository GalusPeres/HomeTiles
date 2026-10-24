#pragma once

#include <Arduino.h>

#include "src/types/calendar/calendar_events.h"

// The events of every calendar the Bridge offers (its `events` topic,
// docs-dev/calendar.md). MQTT hands in the raw payload; the loop parses it
// once and tells the Calendar tiles and the popup. Everything runs on the
// loop task, like device_control.
namespace calendar_store {

// MQTT: an `events` state arrived ({ha_prefix}/calendar/<object>/events).
// The newest payload per calendar waits for process(); an oversized one is
// dropped with a rate-limited log.
void queue(const String& entity, const char* payload, size_t length);
// Loop: parses waiting payloads (at most `budget`, 0 = all) and refreshes the
// tiles and the popup showing them.
void process(uint8_t budget);

// The parsed calendar, nullptr while none arrived. Valid until the next
// process() or forget().
const calendar_events::Calendar* get(const String& entity);
// The last payload as received (Web Admin previews), "" while none.
String payload(const String& entity);
// Drops calendars the Bridge no longer offers (one id per line in `offered`).
void forget_except(const String& offered);

// Splits a tile's calendar list ("calendar.a,calendar.b"): at most `max`
// trimmed ids of the calendar domain, in order, without duplicates.
size_t split_entities(const String& list, String* out, size_t max);

}  // namespace calendar_store
