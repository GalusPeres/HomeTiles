#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <vector>

// The Vacuum tile's and popup's map: the Bridge's picture of the vacuum's
// map entity (an image entity as it changes; a camera's still at the tile's
// interval, Web Admin "vacuum_every"),
// rendered by the Bridge whole into a square (Contain, transparent parts on
// black) in the hardware decoder's steps of 16, shown as an averaged copy in
// the square's size with its corners rounded towards the color behind it,
// like the Media popup's cover. Over the direct link only; until the first
// picture the square shows the map icon on black. Loop task only.
namespace vacuum_map {

// A square map view of `edge` px in `parent` at x, y (parent coordinates),
// its corners rounded with the given radius. Freed with the object; its
// picture stays a while for a square built again in the same size.
lv_obj_t* create(lv_obj_t* parent, int x, int y, int edge, int radius);
// The color the corners are rounded towards. Without it the nearest opaque
// parent's; the popup names its card's color, because its body is
// transparent (the shared popup frame draws the card) and the search ended
// at the black screen (user 2026-10-30: square corners on b623), like the
// Media popup's cover rounded towards colors.card.
void set_under(lv_obj_t* view, uint32_t rgb);
// The entity whose picture the view shows ("" none: the placeholder);
// `every`: a camera map's interval in seconds (every_of).
void set_entity(lv_obj_t* view, const char* entity, uint8_t every);
// A stored interval (Tile::image_slideshow_sec) within the Bridge's 3..60 s.
uint8_t every_of(uint16_t seconds);

// A picture fetched ahead of its square: the popup's map while its tile is
// on screen, so the popup opens with it (like the Media tile holds its
// cover for the popup). `owner` is the tile (one wish per owner); an empty
// entity drops the wish.
void want(const void* owner, lv_obj_t* card, const char* entity, uint16_t edge, uint8_t every);
void unwant(const void* owner);
// Shows a stored picture of the square's entity at once (a popup's first
// frame, a tile built again), instead of waiting for the loop's decode.
void show_now(lv_obj_t* view);

// A picture of `entity_id` arrived or was cleared (bridge_images.cpp).
void arrived(const char* entity_id);
// The picture topics the map views want now (camera_picture::update_topics
// owns the subscription and adds these).
void collect_topics(std::vector<String>& entities, std::vector<String>& suffixes);
// Loop task: decodes at most one arrived picture per call.
void service();

}  // namespace vacuum_map
