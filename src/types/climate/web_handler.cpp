#include "src/types/climate/web_handler.h"

void apply_climate_fields_from_request(WebServer& server, Tile& tile) {
  tile.sensor_entity =
      server.hasArg("climate_entity") ? server.arg("climate_entity") : "";
  uint8_t popup_mode = TILE_POPUP_OPEN_SHORT_PRESS;
  if (server.hasArg("popup_open_mode")) {
    popup_mode =
        server.arg("popup_open_mode").toInt() == TILE_POPUP_OPEN_LONG_PRESS
            ? TILE_POPUP_OPEN_LONG_PRESS
            : TILE_POPUP_OPEN_SHORT_PRESS;
  }
  setTilePopupOpenMode(tile, popup_mode);
  tile.sensor_unit = "";
  tile.sensor_decimals = 1;
  tile.sensor_value_font = 0;
  // Layout (climateTileShowsValue): a request without the field keeps it.
  if (server.hasArg("climate_view")) {
    tile.sensor_display_mode = server.arg("climate_view").toInt() == 1 ? 1 : 0;
  } else if (tile.sensor_display_mode != 1) {
    tile.sensor_display_mode = 0;
  }
  const String packed_slots =
      server.hasArg("climate_slots_packed")
          ? server.arg("climate_slots_packed")
          : (server.hasArg("sensor_gauge_min")
                 ? server.arg("sensor_gauge_min")
                 : String(tile.sensor_gauge_min));
  long packed_value = packed_slots.toInt();
  if (packed_value < 0) packed_value = 0;
  if (packed_value >
      static_cast<long>(CLIMATE_TILE_CONTENT_PACKED_MASK)) {
    packed_value =
        static_cast<long>(CLIMATE_TILE_CONTENT_PACKED_MASK);
  }
  tile.sensor_gauge_min = static_cast<int32_t>(packed_value);
  const String packed_layouts =
      server.hasArg("climate_layouts_packed")
          ? server.arg("climate_layouts_packed")
          : String(getClimateTileLayoutsPacked(tile));
  long layout_value = packed_layouts.toInt();
  if (layout_value < 0) layout_value = 0;
  if (layout_value >
      static_cast<long>(CLIMATE_TILE_LAYOUT_PACKED_VALUE_MASK)) {
    layout_value =
        static_cast<long>(CLIMATE_TILE_LAYOUT_PACKED_VALUE_MASK);
  }
  tile.sensor_gauge_max = static_cast<int32_t>(
      CLIMATE_TILE_LAYOUT_PACKED_MAGIC |
      static_cast<uint32_t>(layout_value));

  if (server.hasArg("climate_geometry")) {
    const String previous = tile.scene_alias;
    tile.scene_alias = server.arg("climate_geometry");
    uint64_t geometry = 0;
    if (!parseClimateTileGeometry(tile, geometry)) {
      tile.scene_alias =
          climateTileGeometryHasPrefix(previous) ? previous : "";
    }
  }
}
