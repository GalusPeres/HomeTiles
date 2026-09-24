#include "src/types/text/web_handler.h"

void apply_text_fields_from_request(WebServer& server, Tile& tile) {
  tile.scene_alias = server.hasArg("text_value") ? server.arg("text_value") : "";
  int font_raw = server.hasArg("text_value_font") ? server.arg("text_value_font").toInt() : 0;
  if (font_raw < 0) font_raw = 0;
  if (font_raw > 4) font_raw = 4;
  tile.sensor_value_font = static_cast<uint8_t>(font_raw);
  tile.sensor_decimals = 0xFF;
  if (server.hasArg("tile_border")) {
    tile.sensor_display_mode = server.arg("tile_border").toInt() == 0 ? 1 : 0;
  }
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
}
