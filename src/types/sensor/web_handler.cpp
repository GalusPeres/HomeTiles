#include "src/types/sensor/web_handler.h"

void apply_sensor_fields_from_request(WebServer& server, Tile& tile) {
  tile.sensor_entity = server.hasArg("sensor_entity") ? server.arg("sensor_entity") : "";
  tile.sensor_unit = server.hasArg("sensor_unit") ? server.arg("sensor_unit") : "";

  uint8_t decimals = 0xFF;
  if (server.hasArg("sensor_decimals")) {
    String decStr = server.arg("sensor_decimals");
    decStr.trim();
    if (decStr.length() > 0) {
      int dec = decStr.toInt();
      if (dec < 0) dec = 0;
      if (dec > 6) dec = 6;
      decimals = static_cast<uint8_t>(dec);
    }
  }
  tile.sensor_decimals = decimals;
  uint8_t value_font = 0;
  if (server.hasArg("sensor_value_font")) {
    int raw = server.arg("sensor_value_font").toInt();
    value_font = (raw >= 1 && raw <= SENSOR_VALUE_FONT_MAX) ? static_cast<uint8_t>(raw) : 0;
  }
  tile.sensor_value_font = value_font;
  uint8_t popup_mode = TILE_POPUP_OPEN_SHORT_PRESS;
  if (server.hasArg("popup_open_mode")) {
    popup_mode = (server.arg("popup_open_mode").toInt() == TILE_POPUP_OPEN_SHORT_PRESS)
                     ? TILE_POPUP_OPEN_SHORT_PRESS
                     : TILE_POPUP_OPEN_LONG_PRESS;
  }
  setTilePopupOpenMode(tile, popup_mode);
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  if (server.hasArg("sensor_display_mode")) {
    int mode = server.arg("sensor_display_mode").toInt();
    if (mode >= 0 && mode <= 2) tile.sensor_display_mode = static_cast<uint8_t>(mode);
  }
  if (server.hasArg("sensor_gauge_min")) {
    String raw = server.arg("sensor_gauge_min");
    raw.trim();
    if (raw.length() > 0) tile.sensor_gauge_min = raw.toInt();
  }
  if (server.hasArg("sensor_gauge_max")) {
    String raw = server.arg("sensor_gauge_max");
    raw.trim();
    if (raw.length() > 0) tile.sensor_gauge_max = raw.toInt();
  }
  if (tile.sensor_gauge_max <= tile.sensor_gauge_min) {
    tile.sensor_gauge_min = 0;
    tile.sensor_gauge_max = 100;
  }
  // Gauge appearance settings - preserve existing values if no new value sent
  if (server.hasArg("sensor_gauge_arc")) {
    String raw = server.arg("sensor_gauge_arc");
    raw.trim();
    if (raw.length() > 0) {
      int val = raw.toInt();
      if (val >= 90 && val <= 359) tile.sensor_gauge_arc = static_cast<uint16_t>(val);
    }
  } else if (tile.sensor_gauge_arc < 90 || tile.sensor_gauge_arc > 359) {
    tile.sensor_gauge_arc = 100;  // Default only if current value is invalid
  }
  if (server.hasArg("sensor_gauge_size")) {
    String raw = server.arg("sensor_gauge_size");
    raw.trim();
    if (raw.length() > 0) {
      int val = raw.toInt();
      if (val >= 100 && val <= 800) tile.sensor_gauge_size = static_cast<uint16_t>(val);
    }
  } else if (tile.sensor_gauge_size < 100 || tile.sensor_gauge_size > 800) {
    tile.sensor_gauge_size = 350;  // Default only if current value is invalid
  }
  if (server.hasArg("sensor_gauge_y_offset")) {
    String raw = server.arg("sensor_gauge_y_offset");
    raw.trim();
    if (raw.length() > 0) {
      int val = raw.toInt();
      if (val >= -100 && val <= 200) tile.sensor_gauge_y_offset = static_cast<int16_t>(val);
    }
  } else if (tile.sensor_gauge_y_offset < -100 || tile.sensor_gauge_y_offset > 200) {
    tile.sensor_gauge_y_offset = 12;  // Default only if current value is invalid
  }
  // Value Y-Offset settings - preserve existing values if no new value sent
  if (server.hasArg("sensor_value_y_offset")) {
    String raw = server.arg("sensor_value_y_offset");
    raw.trim();
    if (raw.length() > 0) {
      int val = raw.toInt();
      if (val >= -100 && val <= 200) tile.sensor_value_y_offset = static_cast<int16_t>(val);
    }
  } else if (tile.sensor_value_y_offset < -100 || tile.sensor_value_y_offset > 200) {
    tile.sensor_value_y_offset = 0;  // Default only if current value is invalid
  }
  // Graph height settings - preserve existing values if no new value sent
  if (server.hasArg("sensor_graph_height")) {
    String raw = server.arg("sensor_graph_height");
    raw.trim();
    if (raw.length() > 0) {
      int val = raw.toInt();
      if (val >= 20 && val <= 200) tile.sensor_graph_height = static_cast<uint16_t>(val);
    }
  } else if (tile.sensor_graph_height < 20 || tile.sensor_graph_height > 200) {
    tile.sensor_graph_height = 60;  // Default only if current value is invalid
  }
}
