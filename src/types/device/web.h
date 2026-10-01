#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include <vector>

#include "src/tiles/config/tile_config.h"

// Web Admin of the Lock, Alarm panel and Fan tiles: one field set per type
// (`<tab>_<prefix>_fields`, prefix lock, alarm or fan) with the entity, the
// state size like the Switch tile and the popup gesture, and the POST fields
// <prefix>_entity, sensor_value_font and popup_open_mode.

// "lock", "alarm" or "fan".
const char* device_field_prefix(TileType type);
// The entities the Bridge offers for the type (Bridge keys locks,
// alarm_panels, fans).
std::vector<String> device_entity_options(TileType type);
// An empty entity, or an entity of the type's Home Assistant domain.
bool device_entity_matches(TileType type, const String& entity);

void append_device_fields_html(String& html, const String& tab_id, TileType type);
// Applies the type's fields; false for an entity of another domain.
bool apply_device_fields_from_request(WebServer& server, Tile& tile);
// DEVICE_I18N for the previews (emitted once, with the Lock type).
void append_device_scripts(String& html);
void append_device_styles(String& html);
