#ifndef TAB_TILES_UNIFIED_H
#define TAB_TILES_UNIFIED_H

#include <lvgl.h>
#include "src/tiles/runtime/tile_renderer.h"

// Unified tile tab functions - works for HOME, GAME, and WEATHER grids
void build_tiles_tab(lv_obj_t* parent, GridType grid_type, scene_publish_cb_t scene_cb);
void tiles_reload_layout(GridType grid_type);
void tiles_release_layout(GridType grid_type);
void tiles_release_all();
bool tiles_is_loaded(GridType grid_type);
void tiles_request_reload(GridType grid_type);
void tiles_request_reload_if_loaded(GridType grid_type);
void tiles_request_reload_all();
void tiles_request_icon_refresh();
void tiles_refresh_image_previews_for_key(GridType grid_type, const String& raw_key);
void tiles_request_release(GridType grid_type);
void tiles_request_release_all();
void tiles_process_reload_requests();
void tiles_process_pending_folder_switch();
void tiles_refresh_visible_from_cache();
void tiles_request_visible_cache_refresh();
void tiles_process_visible_cache_refresh(bool allow_now);
void tiles_request_bridge_cache_refresh();
void tiles_process_bridge_cache_refresh(bool allow_now);
void tiles_update_tile(GridType grid_type, uint8_t index);
void tiles_update_sensor_by_entity(GridType grid_type, const char* entity_id, const char* value);
void tiles_update_weather_by_entity(GridType grid_type, const char* entity_id, const char* payload);
bool tiles_get_cached_entity_payload(const char* entity_id, String& out);
// Reapplies the rules of visible tiles whose rule entity changed (loop task;
// tile_update_service.h). Editable values request their tile directly.
void process_icon_source_updates();
void tiles_request_rule_refresh(GridType grid_type, uint8_t index);
bool tiles_get_cached_entity_payload_signature(const char* entity_id,
                                               uint32_t& hash_out,
                                               size_t& length_out);
void tiles_cache_entity_payload(const char* entity_id, const char* payload);
bool tiles_open_view_popup(uint16_t view_id);
uint16_t tiles_view_id_for_object(lv_obj_t* object);
bool tiles_folder_switch_pending();
void tiles_switch_to_folder(uint16_t folder_id);
void tiles_cancel_folder_switch(uint16_t folder_id);
void tiles_invalidate_folder(uint16_t folder_id);

#endif // TAB_TILES_UNIFIED_H
