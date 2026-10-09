#include "src/types/weather/web_handler.h"

void apply_weather_fields_from_request(WebServer& server, Tile& tile) {
  tile.sensor_entity = server.hasArg("weather_entity") ? server.arg("weather_entity") : "";
  // Optional sensors for the current temperature/humidity; a request without
  // the field keeps the stored sensor, an empty value uses the weather
  // entity's own value again.
  if (server.hasArg("weather_temperature_sensor")) {
    tile.weather_temperature_sensor = server.arg("weather_temperature_sensor");
  }
  if (server.hasArg("weather_humidity_sensor")) {
    tile.weather_humidity_sensor = server.arg("weather_humidity_sensor");
  }
  normalizeWeatherSensors(tile);
  tile.sensor_unit = "";
  tile.sensor_decimals = 0xFF;
  tile.sensor_value_font = 0;
  uint8_t popup_mode = TILE_POPUP_OPEN_SHORT_PRESS;
  if (server.hasArg("popup_open_mode")) {
    popup_mode = (server.arg("popup_open_mode").toInt() == TILE_POPUP_OPEN_SHORT_PRESS)
                     ? TILE_POPUP_OPEN_SHORT_PRESS
                     : TILE_POPUP_OPEN_LONG_PRESS;
  }
  setTilePopupOpenMode(tile, popup_mode);
  // 0 draws filled, colored weather icons, 1 the white MDI outlines
  // (weatherColoredIcons); a request without the field keeps the choice.
  if (server.hasArg("weather_colored_icons")) {
    tile.sensor_display_mode = server.arg("weather_colored_icons").toInt() == 0 ? 1 : 0;
  } else if (tile.sensor_display_mode != 1) {
    tile.sensor_display_mode = 0;
  }
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
}
