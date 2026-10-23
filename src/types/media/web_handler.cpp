#include "src/types/media/web_handler.h"

void apply_media_fields_from_request(WebServer& server, Tile& tile) {
  tile.sensor_entity = server.hasArg("media_entity") ? server.arg("media_entity") : "";
  tile.sensor_unit = "";
  tile.scene_alias = "";
  tile.key_macro = "";
  tile.image_path = "";
  tile.sensor_decimals = 0xFF;
  tile.sensor_value_font = 0;
  // The popup's layout (mediaPopupCoverCentered): 0 cover left, 1 centred.
  tile.sensor_display_mode = server.hasArg("media_layout") && server.arg("media_layout").toInt() == 1 ? 1 : 0;
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
