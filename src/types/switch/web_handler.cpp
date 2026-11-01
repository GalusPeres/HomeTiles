#include "src/types/switch/web_handler.h"
#include "src/types/switch/layout.h"

void apply_switch_fields_from_request(WebServer& server, Tile& tile) {
  tile.sensor_entity = server.hasArg("switch_entity") ? server.arg("switch_entity") : "";
  uint8_t style = 0;
  uint8_t popup_mode = TILE_POPUP_OPEN_SHORT_PRESS;
  if (server.hasArg("switch_style")) {
    // Layout (switch_layout.h): 0 icon button, 1 switch, 2 dimmer,
    // 3 automatic; anything else falls back to the icon button. With
    // "bar_orientation" 1 a bar layout stands upright (bar_orientation.h).
    const int raw = server.arg("switch_style").toInt();
    const switch_layout::Layout layout =
        (raw >= 0 && raw <= switch_layout::kLayoutMax) ? static_cast<switch_layout::Layout>(raw)
                                                       : switch_layout::Layout::IconButton;
    const bool upright = server.hasArg("bar_orientation") && server.arg("bar_orientation").toInt() == 1;
    style = switch_layout::to_stored(layout, upright);
  }
  if (server.hasArg("popup_open_mode")) {
    popup_mode = (server.arg("popup_open_mode").toInt() == TILE_POPUP_OPEN_SHORT_PRESS)
                     ? TILE_POPUP_OPEN_SHORT_PRESS
                     : TILE_POPUP_OPEN_LONG_PRESS;
  }
  tile.sensor_decimals = style;
  setTilePopupOpenMode(tile, popup_mode);
  // State size like a half-height Sensor value (compact_sensor_layout).
  if (server.hasArg("sensor_value_font")) {
    const int font = server.arg("sensor_value_font").toInt();
    tile.sensor_value_font =
        font >= 1 && font <= SENSOR_VALUE_FONT_MAX ? static_cast<uint8_t>(font) : 0;
  }
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
}
