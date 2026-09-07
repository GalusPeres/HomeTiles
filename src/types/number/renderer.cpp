#include "src/types/number/renderer.h"
#include "src/types/value/value_control.h"
lv_obj_t* render_number_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid) {
  Tile display = tile;
  display.sensor_display_mode = 0;
  display.sensor_decimals = 0xFF;
  lv_obj_t* card = render_sensor_tile(parent, col, row, display, index, grid);
  refresh_editable_tile(grid, index);
  return card;
}
