#include "src/types/types_registry.h"
#include "src/types/value/value_control.h"
#include "src/network/bridge/ha_bridge_config.h"

#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "src/types/clock/renderer.h"
#include "src/types/empty/renderer.h"
#include "src/types/navigate/renderer.h"
#include "src/types/scene/renderer.h"
#include "src/types/sensor/renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/types/datetime/renderer.h"
#include "src/types/select/renderer.h"
#include "src/types/number/renderer.h"
#include "src/types/switch/renderer.h"
#include "src/types/text/renderer.h"
#include "src/types/energy/renderer.h"
#include "src/types/weather/renderer.h"
#include "src/types/media/renderer.h"
#include "src/types/climate/renderer.h"
#include "src/types/cover/renderer.h"
#include "src/types/camera/renderer.h"
#include "src/types/pixelanim/renderer.h"

#include "src/types/clock/web_handler.h"
#include "src/types/navigate/web_handler.h"
#include "src/types/scene/web_handler.h"
#include "src/types/sensor/web_handler.h"
#include "src/types/binary_sensor/web_handler.h"
#include "src/types/datetime/web_handler.h"
#include "src/types/select/web_handler.h"
#include "src/types/number/web_handler.h"
#include "src/types/switch/web_handler.h"
#include "src/types/text/web_handler.h"
#include "src/types/energy/web_handler.h"
#include "src/types/weather/web_handler.h"
#include "src/types/media/web_handler.h"
#include "src/types/climate/web_handler.h"
#include "src/types/cover/web_handler.h"
#include "src/types/camera/web_handler.h"
#include "src/types/pixelanim/web_handler.h"

#include "src/types/clock/web_html.h"
#include "src/types/navigate/web_html.h"
#include "src/types/scene/web_html.h"
#include "src/types/sensor/web_html.h"
#include "src/types/binary_sensor/web_html.h"
#include "src/types/datetime/web_html.h"
#include "src/types/select/web_html.h"
#include "src/types/number/web_html.h"
#include "src/types/switch/web_html.h"
#include "src/types/text/web_html.h"
#include "src/types/energy/web_html.h"
#include "src/types/weather/web_html.h"
#include "src/types/media/web_html.h"
#include "src/types/climate/web_html.h"
#include "src/types/cover/web_html.h"
#include "src/types/camera/web_html.h"
#include "src/types/pixelanim/web_html.h"
#include "src/types/settings/web_html.h"

#include "src/types/clock/web_scripts.h"
#include "src/types/navigate/web_scripts.h"
#include "src/types/scene/web_scripts.h"
#include "src/types/sensor/web_scripts.h"
#include "src/types/binary_sensor/web_scripts.h"
#include "src/types/datetime/web_scripts.h"
#include "src/types/select/web_scripts.h"
#include "src/types/number/web_scripts.h"
#include "src/types/switch/web_scripts.h"
#include "src/types/text/web_scripts.h"
#include "src/types/energy/web_scripts.h"
#include "src/types/weather/web_scripts.h"
#include "src/types/media/web_scripts.h"
#include "src/types/climate/web_scripts.h"
#include "src/types/cover/web_scripts.h"
#include "src/types/camera/web_scripts.h"
#include "src/types/pixelanim/web_scripts.h"
#include "src/types/settings/web_scripts.h"

#include "src/types/clock/web_styles.h"
#include "src/types/navigate/web_styles.h"
#include "src/types/scene/web_styles.h"
#include "src/types/sensor/web_styles.h"
#include "src/types/binary_sensor/web_styles.h"
#include "src/types/datetime/web_styles.h"
#include "src/types/select/web_styles.h"
#include "src/types/number/web_styles.h"
#include "src/types/switch/web_styles.h"
#include "src/types/text/web_styles.h"
#include "src/types/energy/web_styles.h"
#include "src/types/weather/web_styles.h"
#include "src/types/media/web_styles.h"
#include "src/types/climate/web_styles.h"
#include "src/types/cover/web_styles.h"
#include "src/types/camera/web_styles.h"
#include "src/types/pixelanim/web_styles.h"
#include "src/types/settings/web_styles.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/web/server/web_admin_utils.h"

namespace {

const String kEmptyString;
const std::vector<String> kEmptyStrings;
const std::vector<SceneOption> kEmptyScenes;

const String& safeString(const String* value) {
  return value ? *value : kEmptyString;
}

const std::vector<String>& safeStrings(const std::vector<String>* value) {
  return value ? *value : kEmptyStrings;
}

const std::vector<SceneOption>& safeScenes(const std::vector<SceneOption>* value) {
  return value ? *value : kEmptyScenes;
}

lv_obj_t* render_number_wrapper(lv_obj_t* parent, int col, int row, const Tile& tile,
                                  uint8_t index, GridType grid, scene_publish_cb_t) {
  return render_number_tile(parent, col, row, tile, index, grid);
}
bool apply_number_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_number_fields_from_request(server, tile);
  return editable_entity_matches(tile.type, tile.sensor_entity);
}
void append_number_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_number_fields_html(html, safeString(ctx.tab_id), parseSensorList(haBridgeConfig.get().numbers_text));
}

lv_obj_t* render_select_wrapper(lv_obj_t* parent, int col, int row, const Tile& tile,
                                  uint8_t index, GridType grid, scene_publish_cb_t) {
  return render_select_tile(parent, col, row, tile, index, grid);
}
bool apply_select_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_select_fields_from_request(server, tile);
  return editable_entity_matches(tile.type, tile.sensor_entity);
}
void append_select_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_select_fields_html(html, safeString(ctx.tab_id), parseSensorList(haBridgeConfig.get().selects_text));
}

lv_obj_t* render_datetime_wrapper(lv_obj_t* parent, int col, int row, const Tile& tile,
                                  uint8_t index, GridType grid, scene_publish_cb_t) {
  return render_datetime_tile(parent, col, row, tile, index, grid);
}
bool apply_datetime_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_datetime_fields_from_request(server, tile);
  return editable_entity_matches(tile.type, tile.sensor_entity);
}
void append_datetime_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_datetime_fields_html(html, safeString(ctx.tab_id), parseSensorList(haBridgeConfig.get().datetimes_text));
}

lv_obj_t* render_sensor_wrapper(lv_obj_t* parent,
                                int col,
                                int row,
                                const Tile& tile,
                                uint8_t index,
                                GridType grid_type,
                                scene_publish_cb_t) {
  return render_sensor_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_binary_sensor_wrapper(lv_obj_t* parent,
                                       int col,
                                       int row,
                                       const Tile& tile,
                                       uint8_t index,
                                       GridType grid_type,
                                       scene_publish_cb_t) {
  return render_binary_sensor_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_scene_wrapper(lv_obj_t* parent,
                               int col,
                               int row,
                               const Tile& tile,
                               uint8_t index,
                               GridType,
                               scene_publish_cb_t scene_cb) {
  return render_scene_tile(parent, col, row, tile, index, scene_cb);
}

lv_obj_t* render_navigate_wrapper(lv_obj_t* parent,
                                  int col,
                                  int row,
                                  const Tile& tile,
                                  uint8_t index,
                                  GridType,
                                  scene_publish_cb_t) {
  return render_navigate_tile(parent, col, row, tile, index);
}

lv_obj_t* render_switch_wrapper(lv_obj_t* parent,
                                int col,
                                int row,
                                const Tile& tile,
                                uint8_t index,
                                GridType grid_type,
                                scene_publish_cb_t) {
  return render_switch_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_clock_wrapper(lv_obj_t* parent,
                               int col,
                               int row,
                               const Tile& tile,
                               uint8_t index,
                               GridType,
                               scene_publish_cb_t) {
  return render_clock_tile(parent, col, row, tile, index);
}

lv_obj_t* render_text_wrapper(lv_obj_t* parent,
                              int col,
                              int row,
                              const Tile& tile,
                              uint8_t index,
                              GridType,
                              scene_publish_cb_t) {
  return render_text_tile(parent, col, row, tile, index);
}

lv_obj_t* render_weather_wrapper(lv_obj_t* parent,
                                 int col,
                                 int row,
                                 const Tile& tile,
                                 uint8_t index,
                                 GridType grid_type,
                                 scene_publish_cb_t) {
  return render_weather_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_energy_wrapper(lv_obj_t* parent,
                                int col,
                                int row,
                                const Tile& tile,
                                uint8_t index,
                                GridType grid_type,
                                scene_publish_cb_t) {
  return render_energy_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_media_wrapper(lv_obj_t* parent,
                               int col,
                               int row,
                               const Tile& tile,
                               uint8_t index,
                               GridType grid_type,
                               scene_publish_cb_t) {
  return render_media_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_climate_wrapper(lv_obj_t* parent,
                                 int col,
                                 int row,
                                 const Tile& tile,
                                 uint8_t index,
                                 GridType grid_type,
                                 scene_publish_cb_t) {
  return render_climate_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_cover_wrapper(lv_obj_t* parent,
                               int col,
                               int row,
                               const Tile& tile,
                               uint8_t index,
                               GridType grid_type,
                               scene_publish_cb_t) {
  return render_cover_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_camera_wrapper(lv_obj_t* parent,
                                int col,
                                int row,
                                const Tile& tile,
                                uint8_t index,
                                GridType grid_type,
                                scene_publish_cb_t) {
  return render_camera_tile(parent, col, row, tile, index, grid_type);
}

lv_obj_t* render_empty_wrapper(lv_obj_t* parent,
                               int col,
                               int row,
                               const Tile&,
                               uint8_t,
                               GridType,
                               scene_publish_cb_t) {
  return render_empty_tile(parent, col, row);
}

lv_obj_t* render_pixelanim_wrapper(lv_obj_t* parent,
                                   int col,
                                   int row,
                                   const Tile& tile,
                                   uint8_t index,
                                   GridType,
                                   scene_publish_cb_t) {
  return render_pixelanim_tile(parent, col, row, tile, index);
}

bool apply_sensor_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_sensor_fields_from_request(server, tile);
  return true;
}

bool apply_binary_sensor_wrapper(WebServer& server, Tile& tile,
                                 const TileTypeApplyContext&) {
  apply_binary_sensor_fields_from_request(server, tile);
  return true;
}

bool apply_scene_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_scene_fields_from_request(server, tile);
  return true;
}

bool apply_navigate_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext& ctx) {
  if (!ctx.tile_config || !ctx.error_message) {
    return false;
  }
  return apply_navigate_fields_from_request(server, tile, ctx.folder_id, *ctx.tile_config, *ctx.error_message, ctx.previous_navigate_target);
}

bool apply_switch_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_switch_fields_from_request(server, tile);
  return true;
}

bool apply_clock_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_clock_fields_from_request(server, tile);
  return true;
}

bool apply_text_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_text_fields_from_request(server, tile);
  return true;
}

bool apply_weather_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_weather_fields_from_request(server, tile);
  return true;
}

bool apply_energy_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_energy_fields_from_request(server, tile);
  return true;
}

bool apply_media_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_media_fields_from_request(server, tile);
  return true;
}

bool apply_climate_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_climate_fields_from_request(server, tile);
  return true;
}

bool apply_cover_wrapper(WebServer& server, Tile& tile,
                         const TileTypeApplyContext&) {
  apply_cover_fields_from_request(server, tile);
  return true;
}

bool apply_camera_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_camera_fields_from_request(server, tile);
  return true;
}

bool apply_pixelanim_wrapper(WebServer& server, Tile& tile, const TileTypeApplyContext&) {
  apply_pixelanim_fields_from_request(server, tile);
  return true;
}

bool apply_settings_wrapper(WebServer&, Tile& tile, const TileTypeApplyContext&) {
  if (!tile.title.length()) {
    tile.title = i18n::strings(configManager.getConfig().language)
                     .tile_type_settings;
  }
  if (!tile.icon_name.length()) tile.icon_name = "cog";
  tile.sensor_decimals = 0xFF;
  tile.key_code = 0;
  tile.key_modifier = 0;
  tile.sensor_value_font = 0;
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  return true;
}

bool apply_back_wrapper(WebServer&, Tile& tile, const TileTypeApplyContext&) {
  if (!tile.icon_name.length()) tile.icon_name = "arrow-left";
  tile.sensor_decimals = 0xFF;
  tile.key_code = 0;
  tile.key_modifier = 0;
  tile.sensor_value_font = 0;
  tile.sensor_display_mode = 0;
  tile.sensor_gauge_min = 0;
  tile.sensor_gauge_max = 100;
  return true;
}

void append_sensor_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_sensor_fields_html(html, safeString(ctx.tab_id), safeStrings(ctx.sensor_options));
}

void append_binary_sensor_fields_wrapper(String& html,
                                         const TileTypeWebContext& ctx) {
  append_binary_sensor_fields_html(
      html, safeString(ctx.tab_id), safeStrings(ctx.binary_sensor_options));
}

void append_scene_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_scene_fields_html(html, safeString(ctx.tab_id), safeScenes(ctx.scene_options));
}

void append_navigate_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_navigate_fields_html(html, safeString(ctx.tab_id), safeString(ctx.navigate_options_html));
}

void append_switch_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_switch_fields_html(html, safeString(ctx.tab_id), safeStrings(ctx.switch_options));
}

void append_clock_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_clock_fields_html(html, safeString(ctx.tab_id));
}

void append_text_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_text_fields_html(html, safeString(ctx.tab_id));
}

void append_weather_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_weather_fields_html(html, safeString(ctx.tab_id), safeStrings(ctx.weather_options));
}

void append_energy_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_energy_fields_html(html, safeString(ctx.tab_id), safeStrings(ctx.energy_options));
}

void append_media_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_media_fields_html(html, safeString(ctx.tab_id), safeStrings(ctx.media_options));
}

void append_climate_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_climate_fields_html(
      html, safeString(ctx.tab_id), safeStrings(ctx.climate_options));
}

void append_settings_fields_wrapper(String& html,
                                    const TileTypeWebContext& ctx) {
  append_settings_access_fields_html(html, safeString(ctx.tab_id));
}

void append_cover_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_cover_fields_html(
      html, safeString(ctx.tab_id), safeStrings(ctx.cover_options));
}

void append_camera_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_camera_fields_html(
      html, safeString(ctx.tab_id), safeStrings(ctx.camera_options));
}

void append_pixelanim_fields_wrapper(String& html, const TileTypeWebContext& ctx) {
  append_pixelanim_fields_html(html, safeString(ctx.tab_id));
}

const TileTypeDescriptor kTileTypes[] = {
  {
    TILE_EMPTY,
    "Leer",
    "empty",
    nullptr,
    "none",
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    0,
    false,
    render_empty_wrapper,
    nullptr,
    nullptr,
    nullptr,
    nullptr
  },
  {
    TILE_SENSOR,
    "Sensor",
    "sensor",
    "sensor",
    "sensor",
    nullptr,
    "loadSensorFields",
    "saveSensorFields",
    "resetSensorFields",
    0x2A2A2A,
    false,
    render_sensor_wrapper,
    apply_sensor_wrapper,
    append_sensor_fields_wrapper,
    append_sensor_styles,
    append_sensor_scripts
  },
  {
    TILE_NUMBER, "Number", "number", "number", "number", nullptr,
    "loadNumberFields", "saveNumberFields", "resetNumberFields", 0x2A2A2A, false,
    render_number_wrapper, apply_number_wrapper, append_number_fields_wrapper,
    append_number_styles, append_number_scripts
  },
  {
    TILE_SELECT, "Select", "select", "select", "select", nullptr,
    "loadSelectFields", "saveSelectFields", "resetSelectFields", 0x2A2A2A, false,
    render_select_wrapper, apply_select_wrapper, append_select_fields_wrapper,
    append_select_styles, append_select_scripts
  },
  {
    TILE_DATETIME, "DateTime", "datetime", "datetime", "datetime", nullptr,
    "loadDateTimeFields", "saveDateTimeFields", "resetDateTimeFields", 0x2A2A2A, false,
    render_datetime_wrapper, apply_datetime_wrapper, append_datetime_fields_wrapper,
    append_datetime_styles, append_datetime_scripts
  },
  {
    TILE_BINARY_SENSOR,
    "Binary Sensor",
    "binary_sensor",
    "binary_sensor",
    "binary_sensor",
    nullptr,
    "loadBinarySensorFields",
    "saveBinarySensorFields",
    "resetBinarySensorFields",
    0x2A2A2A,
    false,
    render_binary_sensor_wrapper,
    apply_binary_sensor_wrapper,
    append_binary_sensor_fields_wrapper,
    append_binary_sensor_styles,
    append_binary_sensor_scripts
  },
  {
    TILE_ENERGY,
    "Energie",
    "energy",
    "energy",
    "sensor",
    nullptr,
    "loadEnergyFields",
    "saveEnergyFields",
    "resetEnergyFields",
    0x2A2A2A,
    false,
    render_energy_wrapper,
    apply_energy_wrapper,
    append_energy_fields_wrapper,
    append_energy_styles,
    append_energy_scripts
  },
  {
    TILE_WEATHER,
    "Wetter",
    "weather",
    "weather",
    "weather",
    nullptr,
    "loadWeatherFields",
    "saveWeatherFields",
    "resetWeatherFields",
    0x2A2A2A,
    false,
    render_weather_wrapper,
    apply_weather_wrapper,
    append_weather_fields_wrapper,
    append_weather_styles,
    append_weather_scripts
  },
  {
    TILE_SCENE,
    "Szene",
    "scene",
    "scene",
    "none",
    nullptr,
    "loadSceneFields",
    "saveSceneFields",
    "resetSceneFields",
    0x2A2A2A,
    false,
    render_scene_wrapper,
    apply_scene_wrapper,
    append_scene_fields_wrapper,
    append_scene_styles,
    append_scene_scripts
  },
  {
    TILE_FOLDER,
    "Ordner",
    "navigate",
    "navigate",
    "none",
    nullptr,
    "loadNavigateFields",
    "saveNavigateFields",
    "resetNavigateFields",
    0x2A2A2A,
    false,
    render_navigate_wrapper,
    apply_navigate_wrapper,
    append_navigate_fields_wrapper,
    append_navigate_styles,
    append_navigate_scripts
  },
  {
    TILE_SWITCH,
    "Schalter",
    "switch",
    "switch",
    "switch",
    nullptr,
    "loadSwitchFields",
    "saveSwitchFields",
    "resetSwitchFields",
    0x2A2A2A,
    false,
    render_switch_wrapper,
    apply_switch_wrapper,
    append_switch_fields_wrapper,
    append_switch_styles,
    append_switch_scripts
  },
  {
    TILE_MEDIA,
    "Media",
    "media",
    "media",
    "media",
    nullptr,
    "loadMediaFields",
    "saveMediaFields",
    "resetMediaFields",
    0x2A2A2A,
    false,
    render_media_wrapper,
    apply_media_wrapper,
    append_media_fields_wrapper,
    append_media_styles,
    append_media_scripts
  },
  {
    TILE_CLIMATE,
    "Climate",
    "climate",
    "climate",
    "climate",
    nullptr,
    "loadClimateFields",
    "saveClimateFields",
    "resetClimateFields",
    0x2A2A2A,
    false,
    render_climate_wrapper,
    apply_climate_wrapper,
    append_climate_fields_wrapper,
    append_climate_styles,
    append_climate_scripts
  },
  {
    TILE_COVER,
    "Cover",
    "cover",
    "cover",
    "cover",
    nullptr,
    "loadCoverFields",
    "saveCoverFields",
    "resetCoverFields",
    0x2A2A2A,
    false,
    render_cover_wrapper,
    apply_cover_wrapper,
    append_cover_fields_wrapper,
    append_cover_styles,
    append_cover_scripts
  },
#if !defined(DEVICE_ESP32_S3_RGB_480)
  {
    TILE_CAMERA,
    "Camera",
    "camera",
    "camera",
    "camera",
    nullptr,
    "loadCameraFields",
    "saveCameraFields",
    "resetCameraFields",
    0x2A2A2A,
    false,
    render_camera_wrapper,
    apply_camera_wrapper,
    append_camera_fields_wrapper,
    append_camera_styles,
    append_camera_scripts
  },
#endif
  {
    TILE_PIXELANIM,
    "Animation",
    "animation",
    "animation",
    "none",
    nullptr,
    "loadAnimationFields",
    "saveAnimationFields",
    "resetAnimationFields",
    0x000000,
    false,
    render_pixelanim_wrapper,
    apply_pixelanim_wrapper,
    append_pixelanim_fields_wrapper,
    append_pixelanim_styles,
    append_pixelanim_scripts
  },
  {
    TILE_CLOCK,
    "Uhr",
    "clock",
    "clock",
    "clock",
    nullptr,
    "loadClockFields",
    "saveClockFields",
    "resetClockFields",
    0x2A2A2A,
    false,
    render_clock_wrapper,
    apply_clock_wrapper,
    append_clock_fields_wrapper,
    append_clock_styles,
    append_clock_scripts
  },
  {
    TILE_TEXT,
    "Text",
    "text",
    "text",
    "text",
    nullptr,
    "loadTextFields",
    "saveTextFields",
    "resetTextFields",
    0x2A2A2A,
    false,
    render_text_wrapper,
    apply_text_wrapper,
    append_text_fields_wrapper,
    append_text_styles,
    append_text_scripts
  },
  {
    TILE_SETTINGS,
    "Settings",
    "navigate",
    "settings_access",
    "none",
    nullptr,
    nullptr,
    nullptr,
    "resetNavigateFields",
    0x2A2A2A,
    true,
    render_navigate_wrapper,
    apply_settings_wrapper,
    append_settings_fields_wrapper,
    append_settings_styles,
    append_settings_scripts
  },
  {
    TILE_BACK,
    "Zurück",
    "navigate",
    "navigate",
    "none",
    nullptr,
    nullptr,
    nullptr,
    "resetNavigateFields",
    0x2A2A2A,
    true,
    render_navigate_wrapper,
    apply_back_wrapper,
    nullptr,
    nullptr,
    nullptr
  }
};

const TileTypeDescriptor* find_descriptor(TileType type) {
  for (const auto& entry : kTileTypes) {
    if (entry.type == type) return &entry;
  }
  return nullptr;
}

}  // namespace

const TileTypeDescriptor* get_tile_type_descriptor(TileType type) {
  return find_descriptor(type);
}

uint32_t get_tile_type_default_bg(TileType type) {
  const TileTypeDescriptor* desc = find_descriptor(type);
  return desc ? desc->default_bg_color : 0;
}

const char* get_tile_type_css_class(TileType type) {
  const TileTypeDescriptor* desc = find_descriptor(type);
  return desc ? desc->css_class : nullptr;
}

const char* get_tile_type_preview_kind(TileType type) {
  const TileTypeDescriptor* desc = find_descriptor(type);
  return desc ? desc->preview_kind : nullptr;
}

void append_tile_type_fields_html(String& html, const TileTypeWebContext& ctx) {
  for (const auto& entry : kTileTypes) {
    if (entry.append_fields) {
      entry.append_fields(html, ctx);
    }
  }
}

void append_tile_type_styles(String& html) {
  for (const auto& entry : kTileTypes) {
    if (entry.append_styles) {
      entry.append_styles(html);
    }
  }
}

void append_tile_type_scripts(String& html) {
  for (const auto& entry : kTileTypes) {
    if (entry.append_scripts) {
      entry.append_scripts(html);
    }
  }
}

// Localized tile type name for the select list, the browser registry and the
// accessible names of the grid tiles. All of them must show the same label, so a
// new tile type is translated in one place.
static const char* localized_tile_type_label(const TileTypeDescriptor& entry) {
  const char* language = configManager.getConfig().language;
  const auto& tr = i18n::strings(language);
  switch (entry.type) {
    case TILE_EMPTY: return tr.tile_type_empty;
    case TILE_SENSOR: return tr.tile_type_sensor;
    case TILE_NUMBER: return i18n::locale(language).editable_labels[0];
    case TILE_SELECT: return i18n::locale(language).editable_labels[1];
    case TILE_DATETIME: return i18n::locale(language).editable_labels[2];
    case TILE_BINARY_SENSOR: return i18n::binary_sensor_label(language, 0);
    case TILE_ENERGY: return tr.tile_type_energy;
    case TILE_WEATHER: return tr.tile_type_weather;
    case TILE_SCENE: return tr.tile_type_scene;
    case TILE_FOLDER: return tr.tile_type_folder;
    case TILE_SWITCH: return tr.tile_type_switch;
    case TILE_MEDIA: return tr.tile_type_media;
    case TILE_COVER: return i18n::cover_label(language, 0);
    case TILE_CAMERA: return tr.camera_tile_type;
    case TILE_CLIMATE: return i18n::climate_tile_type_label(language);
    case TILE_CLOCK: return tr.tile_type_clock;
    case TILE_TEXT: return tr.tile_type_text;
    case TILE_SETTINGS: return tr.tile_type_settings;
    case TILE_BACK: return tr.tile_type_back;
    default: return entry.label;
  }
}

const char* get_tile_type_localized_label(TileType type) {
  const TileTypeDescriptor* entry = get_tile_type_descriptor(type);
  return entry ? localized_tile_type_label(*entry) : "";
}

void append_tile_type_select_options(String& html) {
  for (const auto& entry : kTileTypes) {
    if (entry.locked) continue;
    const char* label = localized_tile_type_label(entry);
    html += "<option value=\"";
    html += String(static_cast<unsigned>(entry.type));
    html += "\">";
    html += label;
    html += "</option>";
  }
}

void append_tile_type_registry_js(String& html) {
  html += "  const TILE_TYPE_REGISTRY = {";
  for (const auto& entry : kTileTypes) {
    const char* label = localized_tile_type_label(entry);
    html += "\"";
    html += String(static_cast<unsigned>(entry.type));
    html += "\":{";
    if (label && label[0]) {
      html += "label:\"";
      html += label;
      html += "\",";
    }
    if (entry.css_class && entry.css_class[0]) {
      html += "css:\"";
      html += entry.css_class;
      html += "\",";
    }
    if (entry.fields_suffix && entry.fields_suffix[0]) {
      html += "fields:\"";
      html += entry.fields_suffix;
      html += "\",";
    }
    if (entry.preview_kind && entry.preview_kind[0]) {
      html += "preview:\"";
      html += entry.preview_kind;
      html += "\",";
    }
    if (entry.js_on_select && entry.js_on_select[0]) {
      html += "onSelect:\"";
      html += entry.js_on_select;
      html += "\",";
    }
    if (entry.js_load && entry.js_load[0]) {
      html += "load:\"";
      html += entry.js_load;
      html += "\",";
    }
    if (entry.js_save && entry.js_save[0]) {
      html += "save:\"";
      html += entry.js_save;
      html += "\",";
    }
    if (entry.js_reset && entry.js_reset[0]) {
      html += "reset:\"";
      html += entry.js_reset;
      html += "\",";
    }
    // Emit defaultBg even when the colour is pure black (0x000000): the animation
    // tile uses black/transparent as its default, and without an explicit
    // defaultBg the WebUI falls back to a grey (#353535) and keeps resetting the
    // tile to grey on edit.
    if (entry.default_bg_color || entry.type == TILE_PIXELANIM) {
      char color_hex[10] = {0};
      const uint32_t color24 = static_cast<uint32_t>(entry.default_bg_color) & 0x00FFFFFFu;
      snprintf(color_hex, sizeof(color_hex), "#%06" PRIX32, color24);
      html += "defaultBg:\"";
      html += color_hex;
      html += "\",";
    }
    html += "locked:";
    html += entry.locked ? "true" : "false";
    html += "},";
  }
  html += "};\n";
}
