#pragma once

#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/value/value_control.h"

enum class TileUpdateBudget : uint8_t {
  Active,
  DrainAll,
};

// Run on the main loop task, which owns the tile widgets and their queues.
// Keep the service order and active limits together when adding a tile type.
// The caller owns camera gating, idle timing, and on-demand graph processing.
// A compile-time budget preserves direct calls without a runtime registry.
template <TileUpdateBudget budget>
inline void process_tile_update_queues() {
  constexpr bool drain_all = budget == TileUpdateBudget::DrainAll;
  process_sensor_update_queue(drain_all ? 0 : 6);
  process_switch_update_queue(drain_all ? 0 : 6);
  process_climate_update_queue(drain_all ? 0 : 4);
  process_cover_update_queue(drain_all ? 0 : 4);
  process_binary_sensor_update_queue(drain_all ? 0 : 4);
  process_editable_updates(drain_all ? 0 : 4);
  process_weather_update_queue(drain_all ? 0 : 4);
  process_media_update_queue(drain_all ? 0 : 2);
}
