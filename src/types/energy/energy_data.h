#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <vector>

static constexpr uint8_t ENERGY_VALUES_MAX = 32;

struct EnergyEntryData {
  String id;
  String name;
  String unit;
  String category;
  String period;
  String start;
  bool is_cost = false;
  bool is_total = false;
  bool has_cost = false;
  int8_t sign = 1;
  float total = 0.0f;
  float cost = 0.0f;
  uint8_t value_count = 0;
  float values[ENERGY_VALUES_MAX] = {};
  bool value_valid[ENERGY_VALUES_MAX] = {};
};

void queue_energy_response(const char* payload, size_t len);
void process_energy_response_queue();
// period: "day" (today by the hour), "week" (this week by the day, from the
// panel's first weekday), "month" (this month by the day) or "year" (this
// year by the month).
bool energy_request_period(const char* period, bool force = false);
bool energy_request_day_for_tiles(bool force = false);
// Whether a request of any period waits for its response: the Energy popup
// loads its other periods one after another, the Bridge takes one request
// at a time and queues two (request_limits.py).
bool energy_request_waiting();
// Whether a period's last response came within the last minute.
bool energy_period_fresh(const char* period);
void energy_service_periodic();
bool energy_find_entry(const String& id, const char* period, EnergyEntryData& out);
// Get the unit from any previously received Energy period. Aggregate Energy
// IDs need not exist as normal HA entities and can therefore be absent from
// the general HaBridgeConfig unit index.
String energy_find_cached_unit(const String& id);
// The automatic icon of an Energy source: the Bridge configuration's, else its
// category's from the received Energy responses. The configuration names only
// the categories released under Energy Dashboard; the responses name every
// source the panel asked for.
String energy_source_icon(const String& id);
// Its name: the Bridge configuration's, else the received Energy responses'.
String energy_source_name(const String& id);
// The sources of the panel's own Energy tiles (every folder and the
// screensaver). The Web Admin's own energy list: the released categories
// and these, never every source a response named (user 2026-10-15: without
// a Web Admin password the picker offered all of them).
void energy_append_tile_entity_ids(std::vector<String>& ids);
// Append all IDs from previously received Energy responses: the previews'
// values of every source (a source just picked shows its value at once).
void energy_append_cached_entity_ids(std::vector<String>& ids);
