#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <vector>

// The Vacuum tile's and popup's map: the Bridge's picture of the vacuum's
// map entity (an image entity as it changes, a camera's still every 10 s),
// rendered by the Bridge whole into a square (Contain) and shown in an image
// object with corners rounded towards the color behind it. Over the direct
// link only; until the first picture the square shows the map icon on the
// card's control surface. Loop task only.
namespace vacuum_map {

// A square map view of `edge` px in `parent` at x, y (parent coordinates),
// its corners rounded with the given radius. Freed with the object.
lv_obj_t* create(lv_obj_t* parent, int x, int y, int edge, int radius);
// The entity whose picture the view shows ("" none: the placeholder).
void set_entity(lv_obj_t* view, const char* entity);

// A picture of `entity_id` arrived or was cleared (bridge_images.cpp).
void arrived(const char* entity_id);
// The picture topics the map views want now (camera_picture::update_topics
// owns the subscription and adds these).
void collect_topics(std::vector<String>& entities, std::vector<String>& suffixes);
// Loop task: decodes at most one arrived picture per call.
void service();

}  // namespace vacuum_map
