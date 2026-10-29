#pragma once

#include <lvgl.h>

#include "src/tiles/config/tile_config.h"

// The Sensor tile's looks below the header (sensor_look.h) as one LVGL
// object in the card over the graph's box. Its shapes are A8 masks, one per
// color, computed again only when the value, the size or a rule's colors
// change, and drawn recolored with the colors of the moment: the lit places
// in the icon's color, the track and unlit places in the card's control fill
// (tile_icon_source::refresh_controls), so presses, tints and "Ausfuellen"
// need no new masks. A level's track is the object's own background, like
// the Switch bar. Owned by the object and freed with it.
namespace sensor_look_view {

// Creates the look for `tile` (its display mode one of sensor_look's looks)
// in `card` over `box` (card coordinates). `icon`: the label whose color the
// lit places take.
lv_obj_t* create(lv_obj_t* card, const Tile& tile, lv_obj_t* icon, const lv_area_t& box, float gauge_min,
                 float gauge_max);

// The top row (the card's content box) of a value `size` px high inside
// the look's arc or ring (sensor_look::value_top).
int32_t value_top(lv_obj_t* look, int32_t size, int32_t standard);

// A state update: the value on the gauge, NAN for none (an empty gauge).
// Redraws only when the shown amount changes.
void set_value(lv_obj_t* look, float value);

// After the icon's color changed: redraws when the lit color differs.
void sync_color(lv_obj_t* look);

}  // namespace sensor_look_view
