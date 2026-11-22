#pragma once

#include "src/tiles/runtime/tile_renderer.h"

lv_obj_t* render_camera_tile(lv_obj_t* parent,
                             int col,
                             int row,
                             const Tile& tile,
                             uint8_t index,
                             GridType grid_type);

// A Camera tile's entry as the tile and the popup show it: its own title, else
// its name, else the entity's; its own icon, else the entity's or its kind's
// ("" for "no icon").
String camera_entry_title(const camera_entries::Entry& entry);
String camera_entry_icon(const camera_entries::Entry& entry);
String camera_entry_list_icon(const camera_entries::Entry& entry);

// The popup switched a tile with "last chosen" to its entry `index` (RAM only).
void camera_tile_entry_chosen(uint32_t key, size_t index);
