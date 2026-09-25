#pragma once

#include "src/tiles/runtime/tile_renderer.h"

lv_obj_t* render_cover_tile(lv_obj_t* parent, int col, int row,
                            const Tile& tile, uint8_t index,
                            GridType grid_type);

String cover_resolve_icon(const Tile& tile, const CoverState& state,
                          bool* dynamic_icon = nullptr);
void refresh_cover_popup_for_tile(GridType grid_type, uint8_t index);
// Icon color of a raw Cover state payload as the Cover tile shows it; false
// while the payload has no available state. `active` (when given) tells
// whether the Cover is not closed (open, opening, closing).
bool cover_payload_icon_color(const char* payload, uint32_t& rgb, bool* active = nullptr);
