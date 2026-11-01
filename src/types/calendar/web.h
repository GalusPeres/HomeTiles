#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "src/tiles/config/tile_config.h"

// Web Admin of the Calendar tile (`<tab>_calendar_fields`): up to four
// calendar pickers (calendar_entity, calendar_entity_2..4), the view, the
// first day of the week and the popup gesture. POST fields: the same names,
// calendar_view, calendar_week_start and popup_open_mode. The calendars are
// stored comma separated in Tile::sensor_entity, the view in
// sensor_display_mode and the first day of the week in sensor_decimals.

void append_calendar_fields_html(String& html, const String& tab_id);
// Applies the fields; false when a field names something else than a
// calendar.
bool apply_calendar_fields_from_request(WebServer& server, Tile& tile);
// CALENDAR_I18N for the previews.
void append_calendar_scripts(String& html);
void append_calendar_styles(String& html);
