#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct Tile;

// The rule layer of every tile ("Rules" in the Web Admin, the "src" line of
// tile_icon_colors.h). A rule takes its color from the tile's own entity or
// another entity:
//   auto   the entity's own icon color as its tile shows it (light color,
//          on/off, climate mode, cover state);
//   rules  the color bar and state colors on the entity's state.
// The color can force the icon color (over the type's state color) and tint
// the tile background (tile_tint.h). Tile types that do not apply the fixed
// icon color themselves (Switch, Climate, Cover, Media, Weather) get it
// forced here as well. The Web Admin preview mirrors this in icon-colors.js.
namespace tile_icon_source {

// Latest cached payload of an entity (live MQTT cache, then the Bridge's
// initial values). False when nothing is known.
bool cached_payload(const String& entity, String& payload);

// The rule color of a tile from the cached state of its rule entity; false
// without an enabled rule, a known state or a result. `active` (when given)
// is false while an Entity color entity is off, closed or not running.
bool rule_color(const Tile& tile, uint32_t& rgb, bool* active = nullptr);

// The entity whose state drives the tile's enabled rules (own or other), or
// "" without enabled rules.
String rule_entity(const Tile& tile);

// Build time: the fixed icon color of tiles without their own state colors
// (icon-and-title tiles, Clock, Text), else white.
void apply_initial(lv_obj_t* icon, const Tile& tile);

// The MDI icon label of a rendered card (through its disc), or nullptr for
// image icons and tiles without an icon.
lv_obj_t* card_icon(lv_obj_t* card);

// Applies the rules to a rendered card: forced icon color, tile tint and the
// discs that follow the background. Skips unchanged values.
void refresh_card(lv_obj_t* card, const Tile& tile);

// The background a popup inherits from its tile: the rules' tint when the
// card (`obj` or up to three of its parents) is tinted, else `fallback`.
uint32_t popup_background(lv_obj_t* obj, uint32_t fallback);

}  // namespace tile_icon_source
