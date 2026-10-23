#pragma once

#include "src/tiles/runtime/tile_renderer.h"

// The tile's own icon: its configured one, else Home Assistant's entity
// icon, else "television". Read when it is needed: the Bridge's icons may
// arrive after the tile was built (the icon refresh changes the tile then).
String media_tile_icon_name(const Tile& tile);

lv_obj_t* render_media_tile(lv_obj_t* parent,
                            int col,
                            int row,
                            const Tile& tile,
                            uint8_t index,
                            GridType grid_type);
