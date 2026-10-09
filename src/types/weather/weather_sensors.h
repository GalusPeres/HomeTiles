#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/runtime/tile_icon_source.h"

// Weather tiles can show a temperature and/or humidity sensor instead of the
// weather entity's own current values (Tile::weather_*_sensor). The tile and
// its popup read the sensors' latest states from the entity cache. A
// configured sensor always wins: without a numeric state (not received yet,
// unknown, unavailable) its value shows "--" instead of silently falling back
// to the weather entity. The forecast always comes from the weather entity.
// The Web Admin preview mirrors this in types/weather/admin.js.
namespace weather_sensors {

// Numeric state of a sensor, like parse_sensor_number (tile_renderer.cpp).
inline bool value(const String& entity, float& out) {
  String payload;
  if (!tile_icon_source::cached_payload(entity, payload)) return false;
  payload.trim();
  payload.replace(",", ".");
  char* end = nullptr;
  const float parsed = strtof(payload.c_str(), &end);
  if (!end || end == payload.c_str() || isnan(parsed) || isinf(parsed)) return false;
  out = parsed;
  return true;
}

// FNV-1a over the sensors' cached payloads, folded into `hash`, so a sensor
// change re-renders an unchanged weather payload.
inline uint32_t mix_hash(const String& temperature_sensor,
                         const String& humidity_sensor, uint32_t hash) {
  for (const String* entity : {&temperature_sensor, &humidity_sensor}) {
    if (!entity->length()) continue;
    String payload;
    tile_icon_source::cached_payload(*entity, payload);
    hash ^= 0x9E3779B9u;
    for (const char* c = payload.c_str(); *c; ++c) {
      hash ^= static_cast<uint8_t>(*c);
      hash *= 16777619u;
    }
  }
  return hash;
}

// A configured temperature sensor replaces the weather temperature.
inline void apply_temperature(const String& sensor, float& temperature,
                              bool& has_temperature) {
  if (!sensor.length()) return;
  float numeric = 0.0f;
  has_temperature = value(sensor, numeric);
  if (has_temperature) temperature = numeric;
}

// A configured humidity sensor appends " · 45 %" (or " · --") to the
// temperature text.
inline void append_humidity(const String& sensor, String& text) {
  if (!sensor.length()) return;
  float numeric = 0.0f;
  text += " \xC2\xB7 ";
  if (value(sensor, numeric)) {
    text += i18n::format_number(configManager.getConfig().language, numeric, 0);
    text += " %";
  } else {
    text += "--";
  }
}

}  // namespace weather_sensors
