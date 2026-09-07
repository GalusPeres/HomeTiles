#include "src/types/number/web_handler.h"

void apply_number_fields_from_request(WebServer& server, Tile& tile) {
  tile.sensor_entity = server.hasArg("number_entity")
                           ? server.arg("number_entity")
                           : tile.sensor_entity;
  const uint8_t popup_mode =
      server.hasArg("popup_open_mode") &&
              server.arg("popup_open_mode").toInt() ==
                  TILE_POPUP_OPEN_LONG_PRESS
          ? TILE_POPUP_OPEN_LONG_PRESS
          : TILE_POPUP_OPEN_SHORT_PRESS;
  setTilePopupOpenMode(tile, popup_mode);

  tile.sensor_unit = "";
  tile.sensor_decimals = 0xFF;
  if (server.hasArg("sensor_value_font")) {
    const int font = server.arg("sensor_value_font").toInt();
    tile.sensor_value_font = font >= 0 && font <= 4 ? font : 2;
  }
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  tile.sensor_gauge_arc = 100;
  tile.sensor_gauge_size = 350;
  tile.sensor_gauge_y_offset = 12;
  tile.sensor_value_y_offset = 0;
  tile.sensor_graph_height = 60;
  tile.key_code = 0;
  tile.key_modifier = 0;
}
