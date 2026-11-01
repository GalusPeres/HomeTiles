#pragma once

// Loop service of the Power flow tiles (renderer.cpp), without LVGL for the
// callers that only route states.
//
// A cached entity state changed (tiles_update_sensor_by_entity): every
// Power flow tile showing it, visible or in a cached folder grid, redraws
// from the cache on the next process_power_flow_updates(). True when one
// shows it.
bool power_flow_entity_changed(const char* entity_id);
// Main loop (process_tile_update_queues): applies the marked tiles.
void process_power_flow_updates();
