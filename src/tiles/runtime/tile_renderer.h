#ifndef TILE_RENDERER_H
#define TILE_RENDERER_H

#include <lvgl.h>
#include "src/tiles/config/tile_config.h"
#include "src/types/sensor/widgets.h"
#include "src/types/switch/state.h"
#include "src/types/cover/state.h"
#include "src/types/binary_sensor/state.h"
#include "src/types/climate/state.h"
#include "src/types/weather/widgets.h"
#include "src/types/media/widgets.h"

// Forward declarations
typedef void (*scene_publish_cb_t)(const char* scene_alias);

enum class GridType : uint8_t {
  TAB0 = 0,
  TAB1 = 1,
  TAB2 = 2,
  SCREENSAVER = 3
};

struct TileWidgetCache {
  SensorTileWidgets sensors[TILES_PER_GRID];
  SwitchTileWidgets switches[TILES_PER_GRID];
  SwitchState switch_states[TILES_PER_GRID];
  ClimateTileWidgets climate[TILES_PER_GRID];
  ClimateState climate_states[TILES_PER_GRID];
  CoverTileWidgets covers[TILES_PER_GRID];
  CoverState cover_states[TILES_PER_GRID];
  BinarySensorTileWidgets binary_sensors[TILES_PER_GRID];
  BinarySensorState binary_sensor_states[TILES_PER_GRID];
  WeatherTileWidgets weather[TILES_PER_GRID];
  MediaTileWidgets media[TILES_PER_GRID];
};

// Allocate the large, cold renderer bookkeeping arrays. On ESP32-P4 these
// live in PSRAM; non-P4 profiles keep their established static storage.
bool tile_renderer_init_cold_storage();

// Renders a complete tile grid.
void render_tile_grid(lv_obj_t* parent, const TileGridConfig& config, GridType grid_type,
                      scene_publish_cb_t scene_cb = nullptr, lv_obj_t** out_tile_objs = nullptr);

// Renders one tile according to its type and returns the created object.
lv_obj_t* render_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type, scene_publish_cb_t scene_cb);

// Type-specific render functions
lv_obj_t* render_sensor_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);
lv_obj_t* render_scene_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, scene_publish_cb_t scene_cb);
lv_obj_t* render_key_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);
lv_obj_t* render_navigate_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index);
lv_obj_t* render_switch_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);
lv_obj_t* render_clock_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index);
lv_obj_t* render_text_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index);
lv_obj_t* render_counter_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);
lv_obj_t* render_weather_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);
lv_obj_t* render_media_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);
lv_obj_t* render_empty_tile(lv_obj_t* parent, int col, int row);

// Update entry points for sensor values.
void update_sensor_tile_value(GridType grid_type, uint8_t grid_index, const char* value, const char* unit = nullptr);
void reset_sensor_widget(GridType grid_type, uint8_t grid_index);
void reset_sensor_widgets(GridType grid_type);

// Thread-safe sensor update queue (MQTT callback -> main loop).
void queue_sensor_tile_update(GridType grid_type, uint8_t grid_index, const char* value, const char* unit = nullptr);
void process_sensor_update_queue(uint8_t max_updates = 0);  // 0 drains the queue

// Switch widget reset functions
void reset_switch_widget(GridType grid_type, uint8_t grid_index);
void reset_switch_widgets(GridType grid_type);

// Thread-safe switch update queue (MQTT callback -> main loop).
// A batched mask must contain slots for one entity. max_updates limits applied
// slots, not payload batches; a partly applied payload resumes without reparse.
// Single-slot cache updates are layout-generation-bound so hidden folder
// preloading keeps working without delivering stale state after a rebuild.
void queue_switch_tile_update(GridType grid_type, uint8_t grid_index, const char* payload);
void queue_switch_tile_updates(GridType grid_type, uint64_t grid_indices,
                               const char* payload);
void process_switch_update_queue(uint8_t max_updates = 0);  // 0 drains the queue

void reset_climate_widget(GridType grid_type, uint8_t grid_index);
void reset_climate_widgets(GridType grid_type);
void queue_climate_tile_update(GridType grid_type, uint8_t grid_index, const char* payload);
void process_climate_update_queue(uint8_t max_updates = 0);
String climate_tile_base_icon(const Tile& tile);
String climate_visual_icon(
    const ClimateState& state, const String& base_icon = "thermostat");
uint32_t climate_visual_color(const ClimateState& state);

CoverTileWidgets* tile_renderer_get_cover_widgets(GridType grid_type);
CoverState* tile_renderer_get_cover_states(GridType grid_type);
void reset_cover_widget(GridType grid_type, uint8_t grid_index);
void reset_cover_widgets(GridType grid_type);
void queue_cover_tile_update(GridType grid_type, uint8_t grid_index,
                             const char* payload);
void process_cover_update_queue(uint8_t max_updates = 0);

BinarySensorTileWidgets* tile_renderer_get_binary_sensor_widgets(
    GridType grid_type);
BinarySensorState* tile_renderer_get_binary_sensor_states(GridType grid_type);
void reset_binary_sensor_widget(GridType grid_type, uint8_t grid_index);
void reset_binary_sensor_widgets(GridType grid_type);
void queue_binary_sensor_tile_update(GridType grid_type, uint8_t grid_index,
                                     const char* payload);
void queue_binary_sensor_tile_updates(GridType grid_type,
                                      uint64_t grid_indices,
                                      const char* payload);
void process_binary_sensor_update_queue(uint8_t max_updates = 0);

void reset_weather_widget(GridType grid_type, uint8_t grid_index);
void reset_weather_widgets(GridType grid_type);
void tile_renderer_invalidate_weather_payload(GridType grid_type);
void queue_weather_tile_update(GridType grid_type, uint8_t grid_index, const char* payload);
void process_weather_update_queue(uint8_t max_updates = 0);  // 0 drains the queue

// UI-thread lookup for the deferred popup body; the caller copies pixels immediately.
const lv_image_dsc_t* tile_renderer_find_media_cover(const String& entity_id, uint32_t& hash);
void reset_media_widget(GridType grid_type, uint8_t grid_index);
void reset_media_widgets(GridType grid_type);
void queue_media_tile_update(GridType grid_type, uint8_t grid_index, const char* payload);
void process_media_update_queue(uint8_t max_updates = 0);  // 0 drains the queue

// Thread-safe tile graph history queue (MQTT callback -> main loop).
void queue_tile_graph_history(const char* entity_id, const char* payload, size_t len);
void process_tile_graph_queue();  // Call from the main loop before lv_timer_handler().
void request_tile_graph_history(const char* entity_id);  // Request history for an entity.

void tile_renderer_snapshot_tab0(TileWidgetCache* out);
void tile_renderer_restore_tab0(const TileWidgetCache* in);

#endif // TILE_RENDERER_H
