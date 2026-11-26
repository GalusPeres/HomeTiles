#include "src/types/weather/graph_web_handler.h"

#include <Arduino.h>

#include "src/tiles/config/tile_config.h"

bool apply_weather_graph_fields(WebServer& server, Tile& tile,
                                const TileTypeApplyContext& ctx) {
  (void)ctx;
  tile.sensor_entity = server.hasArg("weather_graph_entity")
                           ? server.arg("weather_graph_entity")
                           : (server.hasArg("weather_entity") ? server.arg("weather_entity") : "");
  tile.sensor_entity.trim();
  tile.sensor_unit = "";
  tile.sensor_decimals = 0xFF;
  tile.sensor_value_font = 0;
  // The graph has no popup and no weather sensor overrides.
  tile.weather_temperature_sensor = "";
  tile.weather_humidity_sensor = "";
  tile.sensor_display_mode =
      server.hasArg("weather_graph_day") && server.arg("weather_graph_day").toInt() == 1 ? 1 : 0;
  const int pair_id = server.hasArg("weather_graph_pair_id")
                          ? server.arg("weather_graph_pair_id").toInt()
                          : (tile.sensor_gauge_max > 0 && tile.sensor_gauge_max <= 0xFFFF
                                 ? tile.sensor_gauge_max
                                 : 0);
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = pair_id > 0 ? pair_id : 0;
  tile.sensor_gauge_decimals = 0;
  return true;
}
