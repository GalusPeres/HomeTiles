#pragma once

#include "src/types/types_registry.h"

bool apply_weather_graph_fields(WebServer& server, Tile& tile,
                                const TileTypeApplyContext& ctx);
