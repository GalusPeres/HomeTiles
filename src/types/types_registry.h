#pragma once

#include <Arduino.h>
#include <vector>
#include "src/tiles/runtime/tile_renderer.h"
#include "src/web/server/web_admin_utils.h"

class WebServer;
class TileConfig;

struct TileTypeWebContext {
  const String* tab_id = nullptr;
  const std::vector<String>* sensor_options = nullptr;
  const std::vector<String>* binary_sensor_options = nullptr;
  const std::vector<String>* energy_options = nullptr;
  const std::vector<String>* weather_options = nullptr;
  const std::vector<SceneOption>* scene_options = nullptr;
  const std::vector<String>* switch_options = nullptr;
  const std::vector<String>* media_options = nullptr;
  const std::vector<String>* climate_options = nullptr;
  const std::vector<String>* cover_options = nullptr;
  const std::vector<String>* camera_options = nullptr;
  const String* navigate_options_html = nullptr;
};

struct TileTypeApplyContext {
  uint16_t folder_id = 0;
  TileConfig* tile_config = nullptr;
  String* error_message = nullptr;
  // Folder this tile already pointed to before the edit (0 if it was not a
  // folder tile). Lets the navigate apply reuse the existing folder on a
  // rename instead of orphaning it and creating a duplicate.
  uint16_t previous_navigate_target = 0;
};

using TileRenderFn = lv_obj_t* (*)(lv_obj_t* parent,
                                   int col,
                                   int row,
                                   const Tile& tile,
                                   uint8_t index,
                                   GridType grid_type,
                                   scene_publish_cb_t scene_cb);

using TileApplyFn = bool (*)(WebServer& server, Tile& tile, const TileTypeApplyContext& ctx);
using TileAppendFieldsFn = void (*)(String& html, const TileTypeWebContext& ctx);
using TileAppendFn = void (*)(String& html);

struct TileTypeDescriptor {
  TileType type;
  const char* label;
  const char* css_class;
  const char* fields_suffix;
  const char* preview_kind;
  const char* js_on_select;
  const char* js_load;
  const char* js_save;
  const char* js_reset;
  uint32_t default_bg_color;
  bool locked;
  TileRenderFn render;
  TileApplyFn apply;
  TileAppendFieldsFn append_fields;
  TileAppendFn append_styles;
  TileAppendFn append_scripts;
};

const TileTypeDescriptor* get_tile_type_descriptor(TileType type);
uint32_t get_tile_type_default_bg(TileType type);
// True for types whose default background is the global default tile color.
bool tile_type_follows_default_tile_color(TileType type);
const char* get_tile_type_css_class(TileType type);
const char* get_tile_type_preview_kind(TileType type);
// Localized type name in the configured language, shared by the tile type
// select list, the browser registry and the accessible names of the grid tiles.
const char* get_tile_type_localized_label(TileType type);

void append_tile_type_fields_html(String& html, const TileTypeWebContext& ctx);
void append_tile_type_styles(String& html);
void append_tile_type_scripts(String& html);
void append_tile_type_select_options(String& html);
void append_tile_type_registry_js(String& html);
