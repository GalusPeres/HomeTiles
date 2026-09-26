#include "src/types/energy/web_handler.h"

void apply_energy_fields_from_request(WebServer& server, Tile& tile) {
  if (server.hasArg("energy_entity")) {
    tile.sensor_entity = server.arg("energy_entity");
  } else if (server.hasArg("sensor_entity")) {
    tile.sensor_entity = server.arg("sensor_entity");
  }

  tile.sensor_unit = server.hasArg("sensor_unit") ? server.arg("sensor_unit") : "";

  uint8_t decimals = 1;
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
  tile.sensor_gauge_arc = 100;
  tile.sensor_gauge_size = 350;
  tile.sensor_gauge_y_offset = 12;
  tile.sensor_graph_height = 60;

  if (server.hasArg("sensor_value_y_offset")) {
    String raw = server.arg("sensor_value_y_offset");
    raw.trim();
    if (raw.length() > 0) {
      int val = raw.toInt();
      if (val < -100) val = -100;
      if (val > 200) val = 200;
      tile.sensor_value_y_offset = static_cast<int16_t>(val);
    }
  } else if (tile.sensor_value_y_offset < -100 || tile.sensor_value_y_offset > 200) {
    tile.sensor_value_y_offset = 0;
  }
}

