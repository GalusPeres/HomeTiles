#pragma once

#include <WebServer.h>
#include "src/tiles/config/tile_config.h"

void apply_scene_fields_from_request(WebServer& server, Tile& tile);

// The Button tile's picker value: its entity id. A tile from before stores a
// Bridge alias; it shows as the entity the Bridge maps it to and is saved as
// that entity the next time (issue #138: aliases over 31 characters were cut).
String scene_picker_value(const Tile& tile);
