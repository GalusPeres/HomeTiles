#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct Tile;

// Icon colors of the icon-and-title tiles (Scene, Folder, Back, Camera). They
// have no state of their own; their icon colors record (tile_icon_colors.h)
// may name a source entity:
//   "src auto <entity>"   the icon takes the entity's own icon color as its
//                         tile shows it (light color, on/off, climate mode,
//                         cover state);
//   "src rules <entity>"  the color bar and state colors evaluate the
//                         entity's state.
// Without a source, an available state or a result, the fixed icon color
// applies, else white.
namespace tile_icon_source {

// Icon color for a record and the source entity's raw state payload (may be
// empty or nullptr).
uint32_t color(const String& record, const char* payload);

// Latest cached payload of an entity (live MQTT cache, then the Bridge's
// initial values). False when nothing is known.
bool cached_payload(const String& entity, String& payload);

// Color from the cached payload of the tile's source entity.
uint32_t cached_color(const Tile& tile);

// Sets the icon color at build time; the disc reads it when its options
// apply.
void apply_initial(lv_obj_t* icon, const Tile& tile);

// The MDI icon label of a rendered icon-and-title card (through its disc),
// or nullptr for image icons and tiles without an icon.
lv_obj_t* card_icon(lv_obj_t* card);

// Recolors a rendered card from the cached payload; skips unchanged colors.
void refresh_card(lv_obj_t* card, const Tile& tile);

}  // namespace tile_icon_source
