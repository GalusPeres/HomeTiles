#include "src/types/timer/web_handler.h"

// The Timer tile stores only the shared fields (title, icon, icon colors,
// geometry): the timers come from Home Assistant. Fields of an earlier
// type of the tile go, so a later type change starts clean.
void apply_timer_fields_from_request(WebServer&, Tile& tile) {
  tile.sensor_entity = "";
  tile.scene_alias = "";
  tile.sensor_display_mode = 0;
}
