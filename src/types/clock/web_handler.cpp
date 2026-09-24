#include "src/types/clock/web_handler.h"
#include "src/types/clock/clock_format.h"

void apply_clock_fields_from_request(WebServer& server, Tile& tile) {
  bool show_time = true;
  bool show_date = false;
  if (server.hasArg("clock_show_time")) {
    show_time = (server.arg("clock_show_time").toInt() == 1);
  }
  if (server.hasArg("clock_show_date")) {
    show_date = (server.arg("clock_show_date").toInt() == 1);
  }
  uint8_t flags = 0;
  if (show_time) flags |= 1;
  if (show_date) flags |= 2;
  if (flags == 0) flags = 1;
  tile.sensor_decimals = flags;

  auto normalizeClockFont = [](int raw, uint8_t fallback, uint8_t maximum) {
    switch (raw) {
      case 20:
      case 24:
      case 28:
      case 32:
      case 40:
      case 48:
      case 56:
      case 64:
      case 72:
      case 80:
      case 96:
        return static_cast<uint8_t>(raw > maximum ? maximum : raw);
      default:
        return fallback;
    }
  };

  tile.key_code = server.hasArg("key_code")
                      ? normalizeClockFont(server.arg("key_code").toInt(), 40,
                                           96)
                      : static_cast<uint8_t>(40);
  tile.key_modifier = server.hasArg("key_modifier")
                          ? normalizeClockFont(
                                server.arg("key_modifier").toInt(), 20, 72)
                          : static_cast<uint8_t>(20);

  // Remove the legacy clock-specific wallpaper selection on the next save.
  // The screensaver now has its own global configuration.
  tile.scene_alias = "";

  tile.sensor_value_font = 0;
  if (server.hasArg("tile_border")) {
    tile.sensor_display_mode = server.arg("tile_border").toInt() == 0 ? 1 : 0;
  }
  tile.sensor_gauge_min = server.hasArg("clock_time_format")
                              ? clock_tile::normalize_time_format(server.arg("clock_time_format").toInt())
                              : clock_tile::TIME_FORMAT_AUTO;
  tile.sensor_gauge_max = server.hasArg("clock_date_format")
                              ? clock_tile::normalize_date_format(server.arg("clock_date_format").toInt())
                              : clock_tile::DATE_FORMAT_AUTO;
}
