#include "src/types/media/artwork_scale.h"
#include "src/types/media/artwork_payload.h"
#include "src/types/media/content_layout.h"
#include "src/types/media/update_schedule.h"
#include "src/types/media/update_service.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/core/json_scan.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/climate/visuals.h"
#include "src/types/climate/renderer.h"
#include "src/types/binary_sensor/renderer.h"
#include "src/ui/ui_manager.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/types/types_registry.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/dma2d_arbiter.h"
#include "src/core/i18n/i18n.h"
#include "src/io/hardware_io.h"
#include "src/web/server/web_admin.h"
#include "src/ui/screensaver/screensaver_config.h"
#include "src/ui/shared/ui_surface_style.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <Network.h>
#include <NetworkClientSecure.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <libb64/cdecode.h>
#include <libs/tjpgd/tjpgd.h>
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include <driver/jpeg_decode.h>
#include <soc/soc_caps.h>
#endif
#include <math.h>
#include <new>
#include <stdlib.h>
#include <time.h>
#include <vector>

// LVGL 9.5 no longer exports lv_image_cache_drop() through lvgl.h;
// its declaration is only in the instance header.
#include <misc/cache/instance/lv_image_cache.h>

/* === Layout constants === */

/* === Global update state === */
SensorTileWidgets g_tab0_sensors[TILES_PER_GRID];
SensorTileWidgets g_tab1_sensors[TILES_PER_GRID];
SensorTileWidgets g_tab2_sensors[TILES_PER_GRID];
SensorTileWidgets g_screensaver_sensors[TILES_PER_GRID];

SwitchTileWidgets g_tab0_switches[TILES_PER_GRID];
SwitchTileWidgets g_tab1_switches[TILES_PER_GRID];
SwitchTileWidgets g_tab2_switches[TILES_PER_GRID];
SwitchTileWidgets g_screensaver_switches[TILES_PER_GRID];

static constexpr uint8_t SWITCH_GRID_COUNT = 4;
static uint32_t g_switch_layout_generation[SWITCH_GRID_COUNT] = {1, 1, 1, 1};

static uint32_t switch_layout_generation(GridType grid_type) {
  const uint8_t index = static_cast<uint8_t>(grid_type);
  return index < SWITCH_GRID_COUNT ? g_switch_layout_generation[index] : 0;
}

static void advance_switch_layout_generation(GridType grid_type) {
  const uint8_t index = static_cast<uint8_t>(grid_type);
  if (index >= SWITCH_GRID_COUNT) return;
  if (++g_switch_layout_generation[index] == 0) {
    g_switch_layout_generation[index] = 1;
  }
}

static void invalidate_queued_switch_slot(GridType grid_type,
                                          uint8_t grid_index);

#if defined(CONFIG_IDF_TARGET_ESP32P4)
WeatherTileWidgets* g_tab0_weather = nullptr;
WeatherTileWidgets* g_tab1_weather = nullptr;
WeatherTileWidgets* g_tab2_weather = nullptr;

MediaTileWidgets* g_tab0_media = nullptr;
MediaTileWidgets* g_tab1_media = nullptr;
MediaTileWidgets* g_tab2_media = nullptr;
MediaTileWidgets* g_screensaver_media = nullptr;
static bool g_cold_state_init_attempted = false;
#else
WeatherTileWidgets g_tab0_weather[TILES_PER_GRID];
WeatherTileWidgets g_tab1_weather[TILES_PER_GRID];
WeatherTileWidgets g_tab2_weather[TILES_PER_GRID];

MediaTileWidgets g_tab0_media[TILES_PER_GRID];
MediaTileWidgets g_tab1_media[TILES_PER_GRID];
MediaTileWidgets g_tab2_media[TILES_PER_GRID];
MediaTileWidgets g_screensaver_media[TILES_PER_GRID];
#endif

SwitchState g_tab0_switch_states[TILES_PER_GRID];
SwitchState g_tab1_switch_states[TILES_PER_GRID];
SwitchState g_tab2_switch_states[TILES_PER_GRID];
SwitchState g_screensaver_switch_states[TILES_PER_GRID];

CoverTileWidgets g_tab0_covers[TILES_PER_GRID];
CoverTileWidgets g_tab1_covers[TILES_PER_GRID];
CoverTileWidgets g_tab2_covers[TILES_PER_GRID];
CoverTileWidgets g_screensaver_covers[TILES_PER_GRID];
CoverState g_tab0_cover_states[TILES_PER_GRID];
CoverState g_tab1_cover_states[TILES_PER_GRID];
CoverState g_tab2_cover_states[TILES_PER_GRID];
CoverState g_screensaver_cover_states[TILES_PER_GRID];

ClimateTileWidgets g_tab0_climate[TILES_PER_GRID];
ClimateTileWidgets g_tab1_climate[TILES_PER_GRID];
ClimateTileWidgets g_tab2_climate[TILES_PER_GRID];
static ClimateState* g_tab0_climate_states = nullptr;
static ClimateState* g_tab1_climate_states = nullptr;
static ClimateState* g_tab2_climate_states = nullptr;
static ClimateState g_climate_emergency_states[TILES_PER_GRID];

static ClimateState* allocate_climate_states(const char* grid_name) {
  void* memory = heap_caps_malloc(
      sizeof(ClimateState) * TILES_PER_GRID,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool internal_fallback = false;
  if (!memory) {
    memory = heap_caps_malloc(
        sizeof(ClimateState) * TILES_PER_GRID,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    internal_fallback = memory != nullptr;
  }
  if (!memory) {
    Serial.printf(
        "[Climate] WARN: No state storage for %s, using emergency buffer\n",
        grid_name ? grid_name : "?");
    return g_climate_emergency_states;
  }
  ClimateState* states = static_cast<ClimateState*>(memory);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    new (&states[i]) ClimateState();
  }
  if (internal_fallback) {
    Serial.printf(
        "[Climate] WARN: %s state uses %u bytes of internal RAM\n",
        grid_name ? grid_name : "?",
        static_cast<unsigned>(sizeof(ClimateState) * TILES_PER_GRID));
  }
  return states;
}

bool tile_renderer_init_cold_storage() {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (g_tab0_weather && g_tab0_media) return true;
  if (g_cold_state_init_attempted) return false;
  g_cold_state_init_attempted = true;

  constexpr size_t kWeatherCount = TILES_PER_GRID * 3U;
  constexpr size_t kMediaCount = TILES_PER_GRID * 4U;
  auto* weather = static_cast<WeatherTileWidgets*>(heap_caps_malloc(
      sizeof(WeatherTileWidgets) * kWeatherCount,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!weather) {
    Serial.printf(
        "[Tiles/Mem] ERROR: Weather state (%u bytes) not allocated in PSRAM\n",
        static_cast<unsigned>(sizeof(WeatherTileWidgets) * kWeatherCount));
    return false;
  }
  for (size_t i = 0; i < kWeatherCount; ++i) {
    new (&weather[i]) WeatherTileWidgets();
  }

  auto* media = static_cast<MediaTileWidgets*>(heap_caps_malloc(
      sizeof(MediaTileWidgets) * kMediaCount,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!media) {
    Serial.printf(
        "[Tiles/Mem] ERROR: Media state (%u bytes) not allocated in PSRAM\n",
        static_cast<unsigned>(sizeof(MediaTileWidgets) * kMediaCount));
    for (size_t i = 0; i < kWeatherCount; ++i) weather[i].~WeatherTileWidgets();
    heap_caps_free(weather);
    return false;
  }
  for (size_t i = 0; i < kMediaCount; ++i) {
    new (&media[i]) MediaTileWidgets();
  }

  g_tab0_weather = weather;
  g_tab1_weather = weather + TILES_PER_GRID;
  g_tab2_weather = weather + TILES_PER_GRID * 2U;
  g_tab0_media = media;
  g_tab1_media = media + TILES_PER_GRID;
  g_tab2_media = media + TILES_PER_GRID * 2U;
  g_screensaver_media = media + TILES_PER_GRID * 3U;
  Serial.printf(
      "[Tiles/Mem] Weather=%u bytes Media=%u bytes in PSRAM\n",
      static_cast<unsigned>(sizeof(WeatherTileWidgets) * kWeatherCount),
      static_cast<unsigned>(sizeof(MediaTileWidgets) * kMediaCount));
#endif
  return true;
}

SensorTileWidgets* tile_renderer_get_sensor_widgets(GridType grid_type) {
  if (grid_type == GridType::SCREENSAVER) return g_screensaver_sensors;
  if (grid_type == GridType::TAB1) return g_tab1_sensors;
  if (grid_type == GridType::TAB2) return g_tab2_sensors;
  return g_tab0_sensors;
}

SwitchTileWidgets* tile_renderer_get_switch_widgets(GridType grid_type) {
  if (grid_type == GridType::SCREENSAVER) return g_screensaver_switches;
  if (grid_type == GridType::TAB1) return g_tab1_switches;
  if (grid_type == GridType::TAB2) return g_tab2_switches;
  return g_tab0_switches;
}

WeatherTileWidgets* tile_renderer_get_weather_widgets(GridType grid_type) {
  if (!tile_renderer_init_cold_storage()) return nullptr;
  if (grid_type == GridType::TAB1) return g_tab1_weather;
  if (grid_type == GridType::TAB2) return g_tab2_weather;
  return g_tab0_weather;
}

MediaTileWidgets* tile_renderer_get_media_widgets(GridType grid_type) {
  if (!tile_renderer_init_cold_storage()) return nullptr;
  if (grid_type == GridType::SCREENSAVER) return g_screensaver_media;
  if (grid_type == GridType::TAB1) return g_tab1_media;
  if (grid_type == GridType::TAB2) return g_tab2_media;
  return g_tab0_media;
}

void tile_renderer_forget_media_widgets(const MediaCoverRef* ref) {
  if (!ref) return;
  MediaTileWidgets* const grids[] = {
      g_tab0_media, g_tab1_media, g_tab2_media, g_screensaver_media};
  for (MediaTileWidgets* grid : grids) {
    for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
      if (grid[i].cover_ref == ref) {
        // Every lv_obj pointer in this slot belongs to the same card and is
        // invalid after its deletion. Reset the complete entry.
        grid[i] = MediaTileWidgets{};
      }
    }
  }
}

SwitchState* tile_renderer_get_switch_states(GridType grid_type) {
  if (grid_type == GridType::SCREENSAVER) return g_screensaver_switch_states;
  if (grid_type == GridType::TAB1) return g_tab1_switch_states;
  if (grid_type == GridType::TAB2) return g_tab2_switch_states;
  return g_tab0_switch_states;
}

CoverTileWidgets* tile_renderer_get_cover_widgets(GridType grid_type) {
  if (grid_type == GridType::SCREENSAVER) return g_screensaver_covers;
  if (grid_type == GridType::TAB1) return g_tab1_covers;
  if (grid_type == GridType::TAB2) return g_tab2_covers;
  return g_tab0_covers;
}

CoverState* tile_renderer_get_cover_states(GridType grid_type) {
  if (grid_type == GridType::SCREENSAVER) return g_screensaver_cover_states;
  if (grid_type == GridType::TAB1) return g_tab1_cover_states;
  if (grid_type == GridType::TAB2) return g_tab2_cover_states;
  return g_tab0_cover_states;
}

ClimateTileWidgets* tile_renderer_get_climate_widgets(GridType grid_type) {
  if (grid_type == GridType::TAB1) return g_tab1_climate;
  if (grid_type == GridType::TAB2) return g_tab2_climate;
  return g_tab0_climate;
}

ClimateState* tile_renderer_get_climate_states(GridType grid_type) {
  if (grid_type == GridType::TAB1) {
    if (!g_tab1_climate_states) {
      g_tab1_climate_states = allocate_climate_states("TAB1");
    }
    return g_tab1_climate_states;
  }
  if (grid_type == GridType::TAB2) {
    if (!g_tab2_climate_states) {
      g_tab2_climate_states = allocate_climate_states("TAB2");
    }
    return g_tab2_climate_states;
  }
  if (!g_tab0_climate_states) {
    g_tab0_climate_states = allocate_climate_states("TAB0");
  }
  return g_tab0_climate_states;
}

const Tile* tile_renderer_get_tile_config(GridType grid_type, uint8_t index) {
  if (index >= TILES_PER_GRID) return nullptr;
  if (grid_type == GridType::SCREENSAVER) {
    return screensaverConfig.tile(index);
  }
  return &tileConfig.getActiveGrid().tiles[index];
}


bool is_light_entity_id(const String& entity_id);

static void clear_sensor_widgets(GridType grid_type) {
  SensorTileWidgets* target = g_tab0_sensors;
  if (grid_type == GridType::SCREENSAVER) target = g_screensaver_sensors;
  else if (grid_type == GridType::TAB1) target = g_tab1_sensors;
  else if (grid_type == GridType::TAB2) target = g_tab2_sensors;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    target[i].value_label = nullptr;
    target[i].unit_label = nullptr;
    target[i].gauge = nullptr;
    target[i].gauge_min = 0;
    target[i].gauge_max = 100;
    target[i].chart = nullptr;
    target[i].series = nullptr;
  }
}

void reset_sensor_widget(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  SensorTileWidgets* target = g_tab0_sensors;
  if (grid_type == GridType::SCREENSAVER) target = g_screensaver_sensors;
  else if (grid_type == GridType::TAB1) target = g_tab1_sensors;
  else if (grid_type == GridType::TAB2) target = g_tab2_sensors;
  target[grid_index] = {};
}

void reset_sensor_widgets(GridType grid_type) {
  clear_sensor_widgets(grid_type);
}

static void clear_switch_widgets(GridType grid_type) {
  advance_switch_layout_generation(grid_type);
  SwitchTileWidgets* target = g_tab0_switches;
  SwitchState* state_target = g_tab0_switch_states;
  if (grid_type == GridType::SCREENSAVER) {
    target = g_screensaver_switches;
    state_target = g_screensaver_switch_states;
  } else if (grid_type == GridType::TAB1) {
    target = g_tab1_switches;
    state_target = g_tab1_switch_states;
  } else if (grid_type == GridType::TAB2) {
    target = g_tab2_switches;
    state_target = g_tab2_switch_states;
  }
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    target[i].icon_label = nullptr;
    target[i].title_label = nullptr;
    target[i].switch_obj = nullptr;
    state_target[i] = {};
  }
}

void reset_switch_widget(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  invalidate_queued_switch_slot(grid_type, grid_index);
  SwitchTileWidgets* target = g_tab0_switches;
  SwitchState* state_target = g_tab0_switch_states;
  if (grid_type == GridType::SCREENSAVER) {
    target = g_screensaver_switches;
    state_target = g_screensaver_switch_states;
  } else if (grid_type == GridType::TAB1) {
    target = g_tab1_switches;
    state_target = g_tab1_switch_states;
  } else if (grid_type == GridType::TAB2) {
    target = g_tab2_switches;
    state_target = g_tab2_switch_states;
  }
  target[grid_index] = {};
  state_target[grid_index] = {};
}

void reset_switch_widgets(GridType grid_type) {
  clear_switch_widgets(grid_type);
}

void reset_cover_widget(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  tile_renderer_get_cover_widgets(grid_type)[grid_index] = {};
  tile_renderer_get_cover_states(grid_type)[grid_index] = {};
}

void reset_cover_widgets(GridType grid_type) {
  CoverTileWidgets* widgets = tile_renderer_get_cover_widgets(grid_type);
  CoverState* states = tile_renderer_get_cover_states(grid_type);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    widgets[i] = {};
    states[i] = {};
  }
}

void reset_climate_widget(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  ClimateTileWidgets* widgets = tile_renderer_get_climate_widgets(grid_type);
  ClimateState* states = tile_renderer_get_climate_states(grid_type);
  widgets[grid_index] = {};
  states[grid_index] = {};
}

void reset_climate_widgets(GridType grid_type) {
  ClimateTileWidgets* widgets = tile_renderer_get_climate_widgets(grid_type);
  ClimateState* states = tile_renderer_get_climate_states(grid_type);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    widgets[i] = {};
    states[i] = {};
  }
}

static void clear_weather_widgets(GridType grid_type) {
  WeatherTileWidgets* target = g_tab0_weather;
  if (grid_type == GridType::TAB1) target = g_tab1_weather;
  else if (grid_type == GridType::TAB2) target = g_tab2_weather;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    target[i] = {};
  }
}

void reset_weather_widget(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  WeatherTileWidgets* target = g_tab0_weather;
  if (grid_type == GridType::TAB1) target = g_tab1_weather;
  else if (grid_type == GridType::TAB2) target = g_tab2_weather;
  target[grid_index] = {};
}

void reset_weather_widgets(GridType grid_type) {
  clear_weather_widgets(grid_type);
}

// Drop only the cached payload hashes, keeping the widget pointers intact.
// Used when a grid is restored from a folder cache: the widgets are reused, so
// without this the hash short-circuit in update_weather_tile_state() would skip
// re-applying the cached payload and the tile could stay blank ("--").
void tile_renderer_invalidate_weather_payload(GridType grid_type) {
  WeatherTileWidgets* target = g_tab0_weather;
  if (grid_type == GridType::TAB1) target = g_tab1_weather;
  else if (grid_type == GridType::TAB2) target = g_tab2_weather;
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    target[i].last_payload_hash = 0;
  }
}

static void clear_media_widgets(GridType grid_type) {
  MediaTileWidgets* target = tile_renderer_get_media_widgets(grid_type);
  for (size_t i = 0; i < TILES_PER_GRID; ++i) {
    target[i] = {};
  }
}

void reset_media_widget(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  MediaTileWidgets* target = tile_renderer_get_media_widgets(grid_type);
  target[grid_index] = {};
}

void reset_media_widgets(GridType grid_type) {
  clear_media_widgets(grid_type);
}

void tile_renderer_snapshot_tab0(TileWidgetCache* out) {
  if (!out) return;
  ClimateState* climate_states =
      tile_renderer_get_climate_states(GridType::TAB0);
  memcpy(out->sensors, g_tab0_sensors, sizeof(out->sensors));
  memcpy(out->switches, g_tab0_switches, sizeof(out->switches));
  memcpy(out->switch_states, g_tab0_switch_states,
         sizeof(out->switch_states));
  memcpy(out->climate, g_tab0_climate, sizeof(out->climate));
  memcpy(out->climate_states, climate_states, sizeof(out->climate_states));
  memcpy(out->covers, g_tab0_covers, sizeof(out->covers));
  memcpy(out->cover_states, g_tab0_cover_states,
         sizeof(out->cover_states));
  memcpy(out->binary_sensors,
         tile_renderer_get_binary_sensor_widgets(GridType::TAB0),
         sizeof(out->binary_sensors));
  memcpy(out->binary_sensor_states,
         tile_renderer_get_binary_sensor_states(GridType::TAB0),
         sizeof(out->binary_sensor_states));
  memcpy(out->weather, g_tab0_weather, sizeof(out->weather));
  memcpy(out->media, g_tab0_media, sizeof(out->media));
}

void tile_renderer_restore_tab0(const TileWidgetCache* in) {
  if (!in) return;
  advance_switch_layout_generation(GridType::TAB0);
  ClimateState* climate_states =
      tile_renderer_get_climate_states(GridType::TAB0);
  memcpy(g_tab0_sensors, in->sensors, sizeof(in->sensors));
  memcpy(g_tab0_switches, in->switches, sizeof(in->switches));
  memcpy(g_tab0_switch_states, in->switch_states,
         sizeof(in->switch_states));
  memcpy(g_tab0_climate, in->climate, sizeof(in->climate));
  memcpy(climate_states, in->climate_states, sizeof(in->climate_states));
  memcpy(g_tab0_covers, in->covers, sizeof(in->covers));
  memcpy(g_tab0_cover_states, in->cover_states,
         sizeof(in->cover_states));
  reset_binary_sensor_widgets(GridType::TAB0);
  memcpy(tile_renderer_get_binary_sensor_widgets(GridType::TAB0),
         in->binary_sensors, sizeof(in->binary_sensors));
  memcpy(tile_renderer_get_binary_sensor_states(GridType::TAB0),
         in->binary_sensor_states, sizeof(in->binary_sensor_states));
  memcpy(g_tab0_weather, in->weather, sizeof(in->weather));
  memcpy(g_tab0_media, in->media, sizeof(in->media));
}

/* === Thread-safe update queue (MQTT -> main loop) === */
struct SensorUpdate {
  GridType grid_type;
  uint8_t grid_index;
  String value;
  String unit;
  bool valid;
};

static const uint8_t QUEUE_SIZE = 32;
static SensorUpdate g_update_queue[QUEUE_SIZE];

// Keep byte-sized queue cursors together to avoid padding before each aligned
// queue array. Array declarations and construction order remain unchanged.
static volatile uint8_t g_queue_head = 0;
static volatile uint8_t g_queue_tail = 0;
static volatile uint8_t g_switch_head = 0;
static volatile uint8_t g_switch_tail = 0;
static volatile uint8_t g_climate_head = 0;
static volatile uint8_t g_climate_tail = 0;
static volatile uint8_t g_weather_head = 0;
static volatile uint8_t g_weather_tail = 0;
static volatile uint8_t g_media_head = 0;
static volatile uint8_t g_media_tail = 0;

static uint32_t g_queue_overflow_count = 0;

static uint8_t get_sensor_decimals(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return 0xFF;
  const Tile* tile = tile_renderer_get_tile_config(grid_type, grid_index);
  return tile ? tile->sensor_decimals : 0xFF;
}

static uint32_t fnv1a_hash(const char* data) {
  if (!data) return 0;
  uint32_t hash = 2166136261u;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data);
  while (*ptr) {
    hash ^= static_cast<uint32_t>(*ptr++);
    hash *= 16777619u;
  }
  return hash;
}

static const lv_font_t* get_sensor_value_font(const Tile& tile) {
  switch (tile.sensor_value_font) {
    case 1: return tile_layout::content_font_20();
    case 2: return tile_layout::content_font_24();
    case 3: return tile_layout::content_font_32();
    case 4: return tile_layout::content_font_40();
    default: return FONT_VALUE;
  }
}

static bool apply_decimals(String& value, uint8_t decimals) {
  const char* language = configManager.getConfig().language;
  if (decimals == 0xFF) {
    String localized = i18n::localize_numeric_text(language, value);
    const bool changed = localized != value;
    value = localized;
    return changed;
  }
  String normalized = value;
  normalized.replace(",", ".");
  char* end = nullptr;
  float f = strtof(normalized.c_str(), &end);
  if (!end || end == normalized.c_str()) return false;  // Not a number.
  if (isnan(f) || isinf(f)) return false;
  uint8_t d = decimals > 6 ? 6 : decimals;
  value = i18n::format_number(language, f, d);
  return true;
}

static bool parse_sensor_number(const String& value, float& out) {
  String normalized = value;
  normalized.trim();
  if (!normalized.length()) return false;
  normalized.replace(",", ".");
  char* end = nullptr;
  float f = strtof(normalized.c_str(), &end);
  if (!end || end == normalized.c_str()) return false;
  if (isnan(f) || isinf(f)) return false;
  out = f;
  return true;
}

bool is_light_entity_id(const String& entity_id) {
  return entity_id.length() >= 6 && entity_id.startsWith("light.");
}

static int32_t map_gauge_arc_value(float numeric, int32_t min_value, int32_t max_value) {
  if (max_value <= min_value) {
    min_value = 0;
    max_value = 100;
  }
  const float span = static_cast<float>(max_value - min_value);
  float ratio = (numeric - static_cast<float>(min_value)) / span;
  if (ratio < 0.0f) ratio = 0.0f;
  if (ratio > 1.0f) ratio = 1.0f;
  return static_cast<int32_t>(roundf(ratio * static_cast<float>(GAUGE_ARC_STEPS)));
}

// Called by the MQTT callback; thread-safe.
void queue_sensor_tile_update(GridType grid_type, uint8_t grid_index, const char* value, const char* unit) {
  if (grid_index >= TILES_PER_GRID || !value) {
    return;
  }

  // Replace an existing unprocessed update for the same tile.
  uint8_t idx = g_queue_tail;
  while (idx != g_queue_head) {
    SensorUpdate& pending = g_update_queue[idx];
    if (pending.valid &&
        pending.grid_type == grid_type &&
        pending.grid_index == grid_index) {
      pending.value = String(value);
      pending.unit = unit ? String(unit) : "";
      return;
    }
    idx = (idx + 1) % QUEUE_SIZE;
  }

  uint8_t next_head = (g_queue_head + 1) % QUEUE_SIZE;

  if (next_head == g_queue_tail) {
    // Queue full: discard and overwrite the oldest entry.
    if ((g_queue_overflow_count++ % 10) == 0) {
      Serial.println("[Queue] FULL! Oldest sensor update will be overwritten");
    }
    g_queue_tail = (g_queue_tail + 1) % QUEUE_SIZE;
  }

  g_update_queue[g_queue_head].grid_type = grid_type;
  g_update_queue[g_queue_head].grid_index = grid_index;
  g_update_queue[g_queue_head].value = String(value);
  g_update_queue[g_queue_head].unit = unit ? String(unit) : "";
  g_update_queue[g_queue_head].valid = true;

  g_queue_head = next_head;
}

// The main loop calls this BEFORE lv_timer_handler().
void process_sensor_update_queue(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_queue_tail != g_queue_head && (max_updates == 0 || processed < max_updates)) {
    SensorUpdate& upd = g_update_queue[g_queue_tail];

    if (upd.valid) {
      update_sensor_tile_value(upd.grid_type, upd.grid_index, upd.value.c_str(),
                              upd.unit.length() > 0 ? upd.unit.c_str() : nullptr);
      upd.valid = false;
      ++processed;
    }

    g_queue_tail = (g_queue_tail + 1) % QUEUE_SIZE;
  }
}

/* === Thread-safe switch update queue (MQTT -> main loop) === */
struct SwitchUpdate {
  GridType grid_type;
  uint64_t grid_indices;
  String entity_id;
  String payload;
  SwitchState parsed_state;
  uint32_t layout_generation = 0;
  bool require_entity_match = false;
  bool parsed = false;
  bool valid = false;
};

static_assert(TILES_PER_GRID <= 64,
              "Switch update masks require at most 64 tile slots");

static const uint8_t SWITCH_QUEUE_SIZE = 32;
static SwitchUpdate g_switch_queue[SWITCH_QUEUE_SIZE];
static uint32_t g_switch_overflow_count = 0;

static void invalidate_queued_switch_slot(GridType grid_type,
                                          uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return;
  const uint64_t bit = uint64_t{1} << grid_index;
  uint8_t index = g_switch_tail;
  while (index != g_switch_head) {
    SwitchUpdate& pending = g_switch_queue[index];
    if (pending.valid && pending.grid_type == grid_type) {
      pending.grid_indices &= ~bit;
    }
    index = (index + 1) % SWITCH_QUEUE_SIZE;
  }
}

static uint32_t clamp_rgb(int r, int g, int b) {
  auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
  return (static_cast<uint32_t>(clamp(r)) << 16) |
         (static_cast<uint32_t>(clamp(g)) << 8) |
         static_cast<uint32_t>(clamp(b));
}

static bool parse_on_off(const String& text, bool& is_on) {
  String lower = text;
  lower.trim();
  lower.toLowerCase();
  if (lower == "on" || lower == "true" || lower == "1" || lower == "yes") {
    is_on = true;
    return true;
  }
  if (lower == "off" || lower == "false" || lower == "0" || lower == "no") {
    is_on = false;
    return true;
  }
  return false;
}

static bool parse_hex_color(const String& text, uint32_t& color) {
  String t = text;
  t.trim();
  if (t.startsWith("#")) t.remove(0, 1);
  if (t.startsWith("0x") || t.startsWith("0X")) t.remove(0, 2);
  if (t.length() != 6) return false;
  char* end = nullptr;
  long val = strtol(t.c_str(), &end, 16);
  if (!end || end == t.c_str() || *end != '\0') return false;
  color = static_cast<uint32_t>(val) & 0xFFFFFF;
  return true;
}

static bool parse_rgb_list(const String& list, int& r, int& g, int& b) {
  const char* ptr = list.c_str();
  char* end = nullptr;
  long vals[3];
  for (int i = 0; i < 3; ++i) {
    while (*ptr && (*ptr == ' ' || *ptr == ',')) ++ptr;
    if (!*ptr) return false;
    vals[i] = strtol(ptr, &end, 10);
    if (!end || end == ptr) return false;
    ptr = end;
  }
  r = static_cast<int>(vals[0]);
  g = static_cast<int>(vals[1]);
  b = static_cast<int>(vals[2]);
  return true;
}

static bool parse_hs_list(const String& list, float& h, float& s) {
  const char* ptr = list.c_str();
  char* end = nullptr;
  float vals[2];
  for (int i = 0; i < 2; ++i) {
    while (*ptr && (*ptr == ' ' || *ptr == ',')) ++ptr;
    if (!*ptr) return false;
    vals[i] = strtof(ptr, &end);
    if (!end || end == ptr) return false;
    ptr = end;
  }
  h = vals[0];
  s = vals[1];
  return true;
}

static uint32_t hs_to_rgb(float h, float s) {
  float hh = fmodf(h, 360.0f);
  if (hh < 0) hh += 360.0f;
  float sat = s / 100.0f;
  float c = sat;
  float x = c * (1.0f - fabsf(fmodf(hh / 60.0f, 2.0f) - 1.0f));
  float m = 1.0f - c;
  float r1 = 0, g1 = 0, b1 = 0;
  if (hh < 60.0f) { r1 = c; g1 = x; b1 = 0; }
  else if (hh < 120.0f) { r1 = x; g1 = c; b1 = 0; }
  else if (hh < 180.0f) { r1 = 0; g1 = c; b1 = x; }
  else if (hh < 240.0f) { r1 = 0; g1 = x; b1 = c; }
  else if (hh < 300.0f) { r1 = x; g1 = 0; b1 = c; }
  else { r1 = c; g1 = 0; b1 = x; }
  int r = static_cast<int>((r1 + m) * 255.0f);
  int g = static_cast<int>((g1 + m) * 255.0f);
  int b = static_cast<int>((b1 + m) * 255.0f);
  return clamp_rgb(r, g, b);
}

// Lenient variants kept on purpose: unlike hometiles_json::stringSpan these
// accept the first quoted run after the colon and treat an empty result as a
// miss. Several callers rely on that for payloads that mix quoted and bare
// values. Do not merge them into the shared header without checking every
// caller.
static bool extract_json_string_field(const String& src, const char* key, String& out) {
  if (!key || !*key) return false;
  String pattern = String("\"") + key + "\"";
  int idx = src.indexOf(pattern);
  if (idx < 0) return false;
  int colon = src.indexOf(':', idx);
  if (colon < 0) return false;
  int q1 = src.indexOf('"', colon);
  if (q1 < 0) return false;
  int q2 = src.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  out = src.substring(q1 + 1, q2);
  out.trim();
  return out.length() > 0;
}

static bool extract_json_string_field_cstr(const char* src, const char* key, String& out) {
  if (!src || !key || !*key) return false;
  String pattern = String("\"") + key + "\"";
  const char* idx = strstr(src, pattern.c_str());
  if (!idx) return false;
  const char* colon = strchr(idx + pattern.length(), ':');
  if (!colon) return false;
  const char* q1 = strchr(colon, '"');
  if (!q1) return false;
  const char* q2 = q1 + 1;
  bool escaped = false;
  while (*q2) {
    if (*q2 == '"' && !escaped) break;
    escaped = (*q2 == '\\' && !escaped);
    if (*q2 != '\\') escaped = false;
    ++q2;
  }
  if (*q2 != '"') return false;

  out = "";
  out.reserve(q2 - q1 - 1);
  for (const char* p = q1 + 1; p < q2; ++p) {
    out += *p;
  }
  out.trim();
  return out.length() > 0;
}

static bool extract_json_array_field(const String& src, const char* key, String& out) {
  if (!key || !*key) return false;
  String pattern = String("\"") + key + "\"";
  int idx = src.indexOf(pattern);
  if (idx < 0) return false;
  int start = src.indexOf('[', idx);
  int end = src.indexOf(']', start);
  if (start < 0 || end < start) return false;
  out = src.substring(start + 1, end);
  out.trim();
  return out.length() > 0;
}

static bool extract_json_number_field(const String& src, const char* key, float& out) {
  return hometiles_json::number(src.c_str(), src.length(), key, &out);
}

static bool extract_json_number_field_cstr(const char* src, const char* key, float& out) {
  if (!src || !key || !*key) return false;
  String pattern = String("\"") + key + "\"";
  const char* idx = strstr(src, pattern.c_str());
  if (!idx) return false;
  const char* colon = strchr(idx + pattern.length(), ':');
  if (!colon) return false;
  const char* start = colon + 1;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    ++start;
  }
  bool quoted = *start == '"';
  if (quoted) ++start;
  char* end = nullptr;
  float val = strtof(start, &end);
  if (!end || end == start) return false;
  if (quoted && *end != '"') return false;
  out = val;
  return true;
}

static bool extract_json_bool_field_cstr(const char* src, const char* key, bool& out) {
  if (!src || !key || !*key) return false;
  String pattern = String("\"") + key + "\"";
  const char* idx = strstr(src, pattern.c_str());
  if (!idx) return false;
  const char* colon = strchr(idx + pattern.length(), ':');
  if (!colon) return false;
  const char* start = colon + 1;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    ++start;
  }
  if (strncmp(start, "true", 4) == 0) {
    out = true;
    return true;
  }
  if (strncmp(start, "false", 5) == 0) {
    out = false;
    return true;
  }
  if (*start == '"' && strncmp(start + 1, "true", 4) == 0) {
    out = true;
    return true;
  }
  if (*start == '"' && strncmp(start + 1, "false", 5) == 0) {
    out = false;
    return true;
  }
  return false;
}

static bool extract_json_object_field(const String& src, const char* key, String& out) {
  int begin = 0;
  int end = 0;
  if (!hometiles_json::objectSpan(src.c_str(), src.length(), key, &begin, &end)) {
    return false;
  }
  out = src.substring(begin, end);
  return true;
}

static bool extract_json_number_or_string_field(const String& src, const char* key, float& out) {
  if (extract_json_number_field(src, key, out)) return true;
  String text;
  if (!extract_json_string_field(src, key, text)) return false;
  text.trim();
  if (!text.length()) return false;
  text.replace(",", ".");
  char* end = nullptr;
  float val = strtof(text.c_str(), &end);
  if (!end || end == text.c_str()) return false;
  out = val;
  return true;
}

static String format_weather_temp(float temp, const String& unit) {
  String text = i18n::format_number(
      configManager.getConfig().language, temp, 1, true);
  if (unit.length()) {
    text += " ";
    text += unit;
  }
  return text;
}

static String format_weather_temp_value(float temp) {
  return i18n::format_number(
      configManager.getConfig().language, temp, 1, true);
}

static const char* weather_unit_gap() {
#if defined(DEVICE_LAYOUT_1024X600)
  return " ";
#else
  return "\xE2\x80\x89";
#endif
}

static String format_weather_temp_unit(const String& unit) {
  return String(weather_unit_gap()) + (unit.length() ? unit : String("\xC2\xB0\x43"));
}

static const lv_font_t* weather_unit_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return LV_FONT_DEFAULT;
#else
  return FONT_SMALL;
#endif
}

static void position_tile_value_unit_centered(
    lv_obj_t* val_label, lv_obj_t* unit_label,
    lv_coord_t center_x, lv_coord_t y, lv_coord_t wrap_w) {
  lv_obj_update_layout(val_label);
  lv_obj_update_layout(unit_label);
  lv_coord_t val_w = lv_obj_get_width(val_label);
  lv_coord_t unit_w = lv_obj_get_width(unit_label);
  lv_coord_t total_w = val_w + unit_w;
  lv_coord_t x = center_x - (total_w / 2);
  if (x < 0) x = 0;
  if (x + total_w > wrap_w) x = wrap_w - total_w;
  lv_obj_set_pos(val_label, x, y);
#if defined(DEVICE_LAYOUT_1024X600)
  const lv_coord_t unit_y_offset = 0;
#elif defined(DEVICE_LAYOUT_480X480)
  // The compact unit font must share the value's top edge. A baseline-style
  // offset makes the value/unit pair look low and no longer centered below
  // the day icon.
  const lv_coord_t unit_y_offset = 0;
#else
  const lv_coord_t unit_y_offset = 5;
#endif
  lv_obj_set_pos(unit_label, x + val_w, y + unit_y_offset);
}

static void decode_basic_json_escapes(String& text) {
  if (!text.length()) return;
  auto is_hex_digit = [](char c) -> bool {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  };
  auto hex_value = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + (c - 'A'));
    return 0;
  };
  auto append_utf8 = [](String& out, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
      out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
      out += static_cast<char>(0xC0 | (codepoint >> 6));
      out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
      out += static_cast<char>(0xE0 | (codepoint >> 12));
      out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (codepoint >> 18));
      out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
  };

  String out;
  out.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text.charAt(i);
    if (c != '\\' || i + 1 >= text.length()) {
      out += c;
      continue;
    }

    char esc = text.charAt(i + 1);
    if (esc == 'u' && i + 5 < text.length() &&
        is_hex_digit(text.charAt(i + 2)) &&
        is_hex_digit(text.charAt(i + 3)) &&
        is_hex_digit(text.charAt(i + 4)) &&
        is_hex_digit(text.charAt(i + 5))) {
      uint16_t code = (static_cast<uint16_t>(hex_value(text.charAt(i + 2))) << 12) |
                      (static_cast<uint16_t>(hex_value(text.charAt(i + 3))) << 8) |
                      (static_cast<uint16_t>(hex_value(text.charAt(i + 4))) << 4) |
                      static_cast<uint16_t>(hex_value(text.charAt(i + 5)));
      i += 5;

      if (code >= 0xD800 && code <= 0xDBFF &&
          i + 6 < text.length() &&
          text.charAt(i + 1) == '\\' &&
          text.charAt(i + 2) == 'u' &&
          is_hex_digit(text.charAt(i + 3)) &&
          is_hex_digit(text.charAt(i + 4)) &&
          is_hex_digit(text.charAt(i + 5)) &&
          is_hex_digit(text.charAt(i + 6))) {
        uint16_t low = (static_cast<uint16_t>(hex_value(text.charAt(i + 3))) << 12) |
                       (static_cast<uint16_t>(hex_value(text.charAt(i + 4))) << 8) |
                       (static_cast<uint16_t>(hex_value(text.charAt(i + 5))) << 4) |
                       static_cast<uint16_t>(hex_value(text.charAt(i + 6)));
        if (low >= 0xDC00 && low <= 0xDFFF) {
          uint32_t codepoint = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
          append_utf8(out, codepoint);
          i += 6;
          continue;
        }
      }

      append_utf8(out, code);
      continue;
    }

    switch (esc) {
      case '"': out += '"'; break;
      case '\\': out += '\\'; break;
      case '/': out += '/'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      default: out += esc; break;
    }
    ++i;
  }
  text = out;
}

static String weather_icon_from_condition(const String& condition) {
  String key = condition;
  key.trim();
  key.toLowerCase();
  if (!key.length()) return "";
  if (key == "clear-night") return "mdi:weather-night";
  if (key == "cloudy") return "mdi:weather-cloudy";
  if (key == "exceptional") return "mdi:alert-circle-outline";
  if (key == "fog") return "mdi:weather-fog";
  if (key == "hail") return "mdi:weather-hail";
  if (key == "lightning") return "mdi:weather-lightning";
  if (key == "lightning-rainy") return "mdi:weather-lightning-rainy";
  if (key == "partlycloudy") return "mdi:weather-partly-cloudy";
  if (key == "pouring") return "mdi:weather-pouring";
  if (key == "rainy") return "mdi:weather-rainy";
  if (key == "snowy") return "mdi:weather-snowy";
  if (key == "snowy-rainy") return "mdi:weather-snowy-rainy";
  if (key == "sunny") return "mdi:weather-sunny";
  if (key == "windy") return "mdi:weather-windy";
  if (key == "windy-variant") return "mdi:weather-windy-variant";
  return "";
}

static String weather_condition_display_label(const String& condition) {
  return i18n::weather_condition_label(configManager.getConfig().language, condition);
}

static String short_date_from_iso(const String& iso) {
  int dash1 = iso.indexOf('-');
  if (dash1 < 0) return "";
  int dash2 = iso.indexOf('-', dash1 + 1);
  if (dash2 < 0) return "";
  String mm = iso.substring(dash1 + 1, dash2);
  if (dash2 + 2 > iso.length()) return "";
  String dd = iso.substring(dash2 + 1, dash2 + 3);
  if (!dd.length() || !mm.length()) return "";
  return dd + "." + mm;
}

static bool parse_iso_date(const String& iso, int& y, int& m, int& d) {
  if (iso.length() < 10) return false;
  if (iso.charAt(4) != '-' || iso.charAt(7) != '-') return false;
  y = iso.substring(0, 4).toInt();
  m = iso.substring(5, 7).toInt();
  d = iso.substring(8, 10).toInt();
  return (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31);
}

static String weekday_from_iso(const String& iso) {
  return i18n::weather_weekday_short(configManager.getConfig().language, iso);
  int y = 0, m = 0, d = 0;
  if (!parse_iso_date(iso, y, m, d)) return "";
  int mm = m;
  int yy = y;
  if (mm < 3) {
    mm += 12;
    yy -= 1;
  }
  int K = yy % 100;
  int J = yy / 100;
  int h = (d + (13 * (mm + 1)) / 5 + K + (K / 4) + (J / 4) + (5 * J)) % 7;
  // h: 0=Saturday, 1=Sunday, 2=Monday, ...
  int dow = (h + 6) % 7;  // 0=Sunday, 1=Monday, ...
  static const char* kDaysDe[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  if (dow < 0 || dow > 6) return "";
  return String(kDaysDe[dow]);
}

static const char* weather_today_tile_label() {
  return i18n::weather_today_label(configManager.getConfig().language);
}

static bool get_local_today_date(String& date_out) {
  time_t now = time(nullptr);
  if (now < 1704067200) return false;

  struct tm local_tm;
  if (!localtime_r(&now, &local_tm)) return false;

  char date_buf[11];
  if (strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local_tm) == 0) return false;
  date_out = date_buf;
  return true;
}

static String iso_date_add_days(const String& iso, int day_offset) {
  int y = 0, m = 0, d = 0;
  if (!parse_iso_date(iso, y, m, d)) return "";

  struct tm date_tm = {};
  date_tm.tm_year = y - 1900;
  date_tm.tm_mon = m - 1;
  date_tm.tm_mday = d + day_offset;
  date_tm.tm_hour = 12;

  time_t date_value = mktime(&date_tm);
  if (date_value < 0) return "";

  struct tm normalized_tm;
  if (!localtime_r(&date_value, &normalized_tm)) return "";

  char date_buf[11];
  if (strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &normalized_tm) == 0) return "";
  return date_buf;
}

static int iso_date_day_offset(const String& base_iso, const String& target_iso) {
  int base_y = 0, base_m = 0, base_d = 0;
  int target_y = 0, target_m = 0, target_d = 0;
  if (!parse_iso_date(base_iso, base_y, base_m, base_d) ||
      !parse_iso_date(target_iso, target_y, target_m, target_d)) {
    return -32768;
  }

  struct tm base_tm = {};
  base_tm.tm_year = base_y - 1900;
  base_tm.tm_mon = base_m - 1;
  base_tm.tm_mday = base_d;
  base_tm.tm_hour = 12;

  struct tm target_tm = {};
  target_tm.tm_year = target_y - 1900;
  target_tm.tm_mon = target_m - 1;
  target_tm.tm_mday = target_d;
  target_tm.tm_hour = 12;

  time_t base_value = mktime(&base_tm);
  time_t target_value = mktime(&target_tm);
  if (base_value < 0 || target_value < 0) return -32768;

  return static_cast<int>((target_value - base_value) / 86400);
}

static bool list_contains_mode(String list, const char* mode) {
  if (!mode || !*mode) return false;
  list.toLowerCase();
  list.replace("\"", "");
  int start = 0;
  while (start < list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String token = list.substring(start, comma);
    token.trim();
    if (token == mode) return true;
    start = comma + 1;
  }
  return false;
}

static bool is_color_mode(const String& mode) {
  return mode == "hs" ||
         mode == "rgb" ||
         mode == "xy" ||
         mode == "rgbw" ||
         mode == "rgbww";
}

static bool is_switch_widget_style(const Tile& tile) {
  return tile.sensor_decimals == 1;
}

static LightPopupInit build_popup_init_from_state(
    GridType grid_type,
    uint8_t tile_index,
    const Tile& tile,
    const SwitchState& state) {
  LightPopupInit init;
  init.entity_id = tile.sensor_entity;
  init.title = tile.title;
  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  init.icon_name = icon_name;
  init.is_light = is_light_entity_id(tile.sensor_entity);
  init.available = state.available;
  init.keep_icon_white = is_switch_widget_style(tile);
  init.has_tile_ref = true;
  init.tile_grid = static_cast<uint8_t>(grid_type);
  init.tile_index = tile_index;
  init.has_state = state.has_state;
  init.has_color = state.has_color;
  init.has_brightness = state.has_brightness;
  init.has_color_temp = state.has_color_temp;
  init.has_hs = state.has_hs;
  init.hs_h = state.hs_h;
  init.hs_s = state.hs_s;
  init.color_temp_kelvin = state.color_temp_kelvin;
  init.min_color_temp_kelvin = state.min_color_temp_kelvin;
  init.max_color_temp_kelvin = state.max_color_temp_kelvin;

  if (state.has_state) {
    init.is_on = state.is_on;
  } else if (state.has_brightness) {
    init.is_on = state.brightness_pct > 0;
  } else {
    init.is_on = true;
  }

  if (init.is_light) {
    init.supports_color = state.supports_color;
    init.supports_brightness = state.supports_brightness || state.supports_color;
    init.supports_temperature = state.supports_temperature;
  } else {
    init.supports_color = false;
    init.supports_brightness = false;
    init.supports_temperature = false;
  }

  if (state.has_color) {
    init.color = state.color;
  }
  if (state.has_brightness) {
    init.brightness_pct = state.brightness_pct;
  } else if (state.has_state && !state.is_on) {
    init.brightness_pct = 0;
  } else {
    init.brightness_pct = 100;
  }

  return init;
}

static SwitchState parse_switch_payload(const char* payload) {
  SwitchState out;
  if (!payload) return out;
  String text = payload;
  text.trim();
  if (!text.length()) return out;

  auto set_color_temp_kelvin = [&out](float raw_kelvin) {
    if (!isfinite(raw_kelvin) || raw_kelvin <= 0.0f) return;
    int kelvin = static_cast<int>(roundf(raw_kelvin));
    if (kelvin < 1) kelvin = 1;
    if (kelvin > 65535) kelvin = 65535;
    out.has_color_temp = true;
    out.color_temp_kelvin = static_cast<uint16_t>(kelvin);
    if (!out.supported_modes_known) out.supports_temperature = true;
  };

  auto set_color_temp_range = [&out](float raw_min_kelvin, float raw_max_kelvin) {
    if (!isfinite(raw_min_kelvin) || raw_min_kelvin <= 0.0f) {
      raw_min_kelvin = 2000.0f;
    }
    if (!isfinite(raw_max_kelvin) || raw_max_kelvin <= 0.0f) {
      raw_max_kelvin = 6535.0f;
    }
    int min_kelvin = static_cast<int>(roundf(raw_min_kelvin));
    int max_kelvin = static_cast<int>(roundf(raw_max_kelvin));
    if (min_kelvin < 1) min_kelvin = 1;
    if (max_kelvin < 1) max_kelvin = 1;
    if (min_kelvin > 65535) min_kelvin = 65535;
    if (max_kelvin > 65535) max_kelvin = 65535;
    if (min_kelvin > max_kelvin) {
      const int tmp = min_kelvin;
      min_kelvin = max_kelvin;
      max_kelvin = tmp;
    }
    out.min_color_temp_kelvin = static_cast<uint16_t>(min_kelvin);
    out.max_color_temp_kelvin = static_cast<uint16_t>(max_kelvin);
    if (!out.supported_modes_known) out.supports_temperature = true;
  };

  auto try_extract_color_temp = [&set_color_temp_kelvin](const String& source) {
    float color_temp_kelvin = 0.0f;
    if (extract_json_number_field(source, "color_temp_kelvin", color_temp_kelvin) ||
        extract_json_number_or_string_field(source, "color_temp_kelvin", color_temp_kelvin) ||
        extract_json_number_field(source, "kelvin", color_temp_kelvin) ||
        extract_json_number_or_string_field(source, "kelvin", color_temp_kelvin)) {
      set_color_temp_kelvin(color_temp_kelvin);
      return true;
    }

    float color_temp_mired = 0.0f;
    if (extract_json_number_field(source, "color_temp", color_temp_mired) ||
        extract_json_number_or_string_field(source, "color_temp", color_temp_mired)) {
      if (color_temp_mired > 0.0f) {
        set_color_temp_kelvin(floorf(1000000.0f / color_temp_mired));
        return true;
      }
    }

    return false;
  };

  auto try_extract_color_temp_range = [&set_color_temp_range](const String& source) {
    float min_kelvin = 2000.0f;
    float max_kelvin = 6535.0f;
    bool has_min_kelvin =
        extract_json_number_field(source, "min_color_temp_kelvin", min_kelvin) ||
        extract_json_number_or_string_field(source, "min_color_temp_kelvin", min_kelvin);
    bool has_max_kelvin =
        extract_json_number_field(source, "max_color_temp_kelvin", max_kelvin) ||
        extract_json_number_or_string_field(source, "max_color_temp_kelvin", max_kelvin);

    float min_mired = 0.0f;
    float max_mired = 0.0f;
    const bool has_min_mired =
        extract_json_number_field(source, "min_mireds", min_mired) ||
        extract_json_number_or_string_field(source, "min_mireds", min_mired);
    const bool has_max_mired =
        extract_json_number_field(source, "max_mireds", max_mired) ||
        extract_json_number_or_string_field(source, "max_mireds", max_mired);
    if (!has_min_kelvin && has_max_mired && max_mired > 0.0f) {
      min_kelvin = floorf(1000000.0f / max_mired);
      has_min_kelvin = true;
    }
    if (!has_max_kelvin && has_min_mired && min_mired > 0.0f) {
      max_kelvin = floorf(1000000.0f / min_mired);
      has_max_kelvin = true;
    }
    if (has_min_kelvin || has_max_kelvin) {
      set_color_temp_range(min_kelvin, max_kelvin);
      return true;
    }

    return false;
  };

  if (text.startsWith("{")) {
    bool state_unavailable = false;
    String state;
    if (extract_json_string_field(text, "state", state)) {
      String normalized_state = state;
      normalized_state.trim();
      normalized_state.toLowerCase();
      if (normalized_state == "unavailable") {
        state_unavailable = true;
        out.available = false;
      } else {
        out.has_state = parse_on_off(state, out.is_on);
      }
    }
    bool available = true;
    if (extract_json_bool_field_cstr(
            text.c_str(), "available", available)) {
      out.available = available;
    }
    if (state_unavailable) out.available = false;

    String supported_modes;
    if (extract_json_array_field(text, "supported_color_modes", supported_modes)) {
      out.supported_modes_known = true;
      bool has_brightness = list_contains_mode(supported_modes, "brightness");
      bool has_color = list_contains_mode(supported_modes, "hs") ||
                       list_contains_mode(supported_modes, "rgb") ||
                       list_contains_mode(supported_modes, "xy") ||
                       list_contains_mode(supported_modes, "rgbw") ||
                       list_contains_mode(supported_modes, "rgbww");
      bool has_temperature = list_contains_mode(supported_modes, "color_temp");
      bool has_onoff = list_contains_mode(supported_modes, "onoff");
      // In Home Assistant, every color and color-temperature mode is also
      // dimmable. The explicit capability list is authoritative; converted
      // state attributes such as rgb_color/hs_color are only current values.
      out.supports_brightness = has_brightness || has_color || has_temperature;
      out.supports_color = has_color;
      out.supports_temperature = has_temperature;
      out.supported_onoff_only =
          has_onoff && !has_brightness && !has_color && !has_temperature;
    }

    String color_mode;
    if (extract_json_string_field(text, "color_mode", color_mode)) {
      String mode = color_mode;
      mode.toLowerCase();
      // color_mode describes the currently active mode, not the complete
      // feature set. Use it only as a legacy fallback when HA did not provide
      // supported_color_modes.
      if (!out.supported_modes_known) {
        if (mode == "brightness") {
          out.supports_brightness = true;
          out.supported_onoff_only = false;
        }
        if (mode == "color_temp") {
          out.supports_temperature = true;
          out.supports_brightness = true;
          out.supported_onoff_only = false;
        }
        if (is_color_mode(mode)) {
          out.supports_color = true;
          out.supports_brightness = true;
          out.supported_onoff_only = false;
        }
        if (mode == "onoff") {
          out.supported_onoff_only = true;
        }
      }
    }

    float bright_pct = -1.0f;
    float bright_raw = -1.0f;
    if (extract_json_number_field(text, "brightness_pct", bright_pct)) {
      int pct = static_cast<int>(roundf(bright_pct));
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      out.has_brightness = true;
      out.brightness_pct = static_cast<uint8_t>(pct);
    } else if (extract_json_number_field(text, "brightness", bright_raw)) {
      int pct = static_cast<int>(roundf((bright_raw / 255.0f) * 100.0f));
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      out.has_brightness = true;
      out.brightness_pct = static_cast<uint8_t>(pct);
    }

    String color_text;
    if (extract_json_string_field(text, "color", color_text)) {
      uint32_t color = 0;
      if (parse_hex_color(color_text, color)) {
        out.has_color = true;
        out.color = color;
      }
    }

    if (!out.has_color) {
      String rgb_list;
      if (extract_json_array_field(text, "rgb_color", rgb_list)) {
        int r = 0, g = 0, b = 0;
        if (parse_rgb_list(rgb_list, r, g, b)) {
          out.has_color = true;
          out.color = clamp_rgb(r, g, b);
        }
      }
    }

    {
      String hs_list;
      if (extract_json_array_field(text, "hs_color", hs_list)) {
        float h = 0.0f, s = 0.0f;
        if (parse_hs_list(hs_list, h, s)) {
          out.has_hs = true;
          out.hs_h = h;
          out.hs_s = s;
          out.has_color = true;
          out.color = hs_to_rgb(h, s);
        }
      }
    }

    try_extract_color_temp(text);
    try_extract_color_temp_range(text);

    String attributes;
    if (extract_json_object_field(text, "attributes", attributes)) {
      if (extract_json_array_field(attributes, "supported_color_modes", supported_modes)) {
        out.supported_modes_known = true;
        bool has_brightness = list_contains_mode(supported_modes, "brightness");
        bool has_color = list_contains_mode(supported_modes, "hs") ||
                         list_contains_mode(supported_modes, "rgb") ||
                         list_contains_mode(supported_modes, "xy") ||
                         list_contains_mode(supported_modes, "rgbw") ||
                         list_contains_mode(supported_modes, "rgbww");
        bool has_temperature = list_contains_mode(supported_modes, "color_temp");
        bool has_onoff = list_contains_mode(supported_modes, "onoff");
        out.supports_brightness = has_brightness || has_color || has_temperature;
        out.supports_color = has_color;
        out.supports_temperature = has_temperature;
        out.supported_onoff_only =
            has_onoff && !has_brightness && !has_color && !has_temperature;
      }

      if (extract_json_string_field(attributes, "color_mode", color_mode)) {
        String mode = color_mode;
        mode.toLowerCase();
        if (!out.supported_modes_known) {
          if (mode == "brightness") {
            out.supports_brightness = true;
            out.supported_onoff_only = false;
          }
          if (mode == "color_temp") {
            out.supports_temperature = true;
            out.supports_brightness = true;
            out.supported_onoff_only = false;
          }
          if (is_color_mode(mode)) {
            out.supports_color = true;
            out.supports_brightness = true;
            out.supported_onoff_only = false;
          }
          if (mode == "onoff") {
            out.supported_onoff_only = true;
          }
        }
      }

      if (!out.has_brightness && extract_json_number_field(attributes, "brightness_pct", bright_pct)) {
        int pct = static_cast<int>(roundf(bright_pct));
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        out.has_brightness = true;
        out.brightness_pct = static_cast<uint8_t>(pct);
      } else if (!out.has_brightness && extract_json_number_field(attributes, "brightness", bright_raw)) {
        int pct = static_cast<int>(roundf((bright_raw / 255.0f) * 100.0f));
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        out.has_brightness = true;
        out.brightness_pct = static_cast<uint8_t>(pct);
      }

      if (!out.has_color) {
        if (extract_json_string_field(attributes, "color", color_text)) {
          uint32_t color = 0;
          if (parse_hex_color(color_text, color)) {
            out.has_color = true;
            out.color = color;
          }
        }
      }

      if (!out.has_color) {
        String rgb_list;
        if (extract_json_array_field(attributes, "rgb_color", rgb_list)) {
          int r = 0, g = 0, b = 0;
          if (parse_rgb_list(rgb_list, r, g, b)) {
            out.has_color = true;
            out.color = clamp_rgb(r, g, b);
          }
        }
      }

      {
        String hs_list;
        if (extract_json_array_field(attributes, "hs_color", hs_list)) {
          float h = 0.0f, s = 0.0f;
          if (parse_hs_list(hs_list, h, s)) {
            out.has_hs = true;
            out.hs_h = h;
            out.hs_s = s;
            out.has_color = true;
            out.color = hs_to_rgb(h, s);
          }
        }
      }

      try_extract_color_temp(attributes);
      try_extract_color_temp_range(attributes);
    }
  }

  if (text.equalsIgnoreCase("unavailable")) {
    out.available = false;
  }
  if (!out.has_state) {
    out.has_state = parse_on_off(text, out.is_on);
  }

  if (!out.has_color) {
    uint32_t color = 0;
    if (parse_hex_color(text, color)) {
      out.has_color = true;
      out.color = color;
    } else if (text.startsWith("rgb(") && text.endsWith(")")) {
      String list = text.substring(4, text.length() - 1);
      int r = 0, g = 0, b = 0;
      if (parse_rgb_list(list, r, g, b)) {
        out.has_color = true;
        out.color = clamp_rgb(r, g, b);
      }
    }
  }

  if (!out.has_state && out.has_color) {
    out.has_state = true;
    out.is_on = true;
  }
  if (!out.has_state && out.has_brightness) {
    out.has_state = true;
    out.is_on = out.brightness_pct > 0;
  }
  if (!out.has_state && out.has_color_temp) {
    out.has_state = true;
    out.is_on = true;
  }

  // Legacy payloads did not always include supported_color_modes. Keep the
  // old value-based detection only for those payloads. Modern HA payloads can
  // contain converted rgb_color/hs_color values even for CCT-only lights.
  if (!out.supported_modes_known) {
    if (out.has_color || out.has_hs) {
      out.supports_color = true;
    }
    if (out.has_color_temp) {
      out.supports_temperature = true;
    }
    if (out.has_brightness || out.supports_color || out.supports_temperature) {
      out.supports_brightness = true;
      out.supported_onoff_only = false;
    }
  }
  if (out.supports_color) {
    out.supports_brightness = true;
    out.supported_onoff_only = false;
  }

  return out;
}

static bool switch_tile_visual_state_equal(const SwitchState& left,
                                           const SwitchState& right) {
  return left.available == right.available &&
         left.has_state == right.has_state &&
         left.is_on == right.is_on &&
         left.has_color == right.has_color &&
         (!left.has_color || left.color == right.color) &&
         left.has_color_temp == right.has_color_temp &&
         (!left.has_color_temp ||
          left.color_temp_kelvin == right.color_temp_kelvin) &&
         left.supports_color == right.supports_color &&
         left.supports_brightness == right.supports_brightness &&
         left.supports_temperature == right.supports_temperature &&
         left.supported_modes_known == right.supported_modes_known &&
         left.supported_onoff_only == right.supported_onoff_only;
}

static bool switch_state_has_update(const SwitchState& state) {
  return !state.available || state.has_state || state.has_color ||
         state.has_brightness || state.has_color_temp || state.supports_color ||
         state.supports_brightness || state.supports_temperature;
}

static void apply_switch_tile_state(GridType grid_type, uint8_t grid_index,
                                    SwitchState state) {
  if (grid_index >= TILES_PER_GRID) return;
  SwitchTileWidgets* target = g_tab0_switches;
  SwitchState* state_target = g_tab0_switch_states;
  if (grid_type == GridType::SCREENSAVER) {
    target = g_screensaver_switches;
    state_target = g_screensaver_switch_states;
  } else if (grid_type == GridType::TAB1) {
    target = g_tab1_switches;
    state_target = g_tab1_switch_states;
  } else if (grid_type == GridType::TAB2) {
    target = g_tab2_switches;
    state_target = g_tab2_switch_states;
  }

  SwitchState prev = state_target[grid_index];
  if (!state.supported_modes_known && prev.supported_modes_known) {
    state.supported_modes_known = true;
    state.supported_onoff_only = prev.supported_onoff_only;
    state.supports_color = prev.supports_color;
    state.supports_brightness = prev.supports_brightness;
    state.supports_temperature = prev.supports_temperature;
  }
  if (!state.supported_modes_known) {
    if (!state.supports_color && prev.supports_color) {
      state.supports_color = true;
    }
    if (!state.supports_brightness && prev.supports_brightness) {
      state.supports_brightness = true;
    }
    if (!state.supports_temperature && prev.supports_temperature) {
      state.supports_temperature = true;
    }
  }
  if (state.supports_color) {
    state.supports_brightness = true;
  }

  if (!state.has_color && prev.has_color && !state.supported_onoff_only) {
    state.has_color = true;
    state.color = prev.color;
  }

  const Tile* tile_ptr = tile_renderer_get_tile_config(grid_type, grid_index);
  if (!tile_ptr) return;
  const Tile& tile = *tile_ptr;
  const String& entity_id = tile.sensor_entity;
  const bool is_light_entity = is_light_entity_id(entity_id);
  const bool use_switch_widget = is_switch_widget_style(tile);
  if (is_light_entity) {
    if (state.supported_modes_known && state.supported_onoff_only) {
      state.supports_color = false;
      state.supports_brightness = false;
      state.supports_temperature = false;
    }
  }

  // Brightness-only state echoes do not alter the visible tile. This matters
  // when the same entity is present several times: keep every state cache and
  // the bound popup current, but do not invalidate every duplicate tile.
  const bool tile_visual_unchanged =
      switch_tile_visual_state_equal(prev, state);
  state_target[grid_index] = state;

  if (tile.sensor_entity.length()) {
    LightPopupInit init = build_popup_init_from_state(grid_type, grid_index, tile, state);
    update_light_popup(init);
  }
  if (tile_visual_unchanged) return;

  SwitchTileWidgets& widgets = target[grid_index];
  if (!widgets.icon_label && !widgets.title_label && !widgets.switch_obj) return;

  static const uint32_t kIconOn = 0xFFD54F;
  static const uint32_t kIconOff = 0xB0B0B0;
  static const uint32_t kIconNeutral = 0xFFFFFF;
  static const uint32_t kSwitchOff = 0xFFFFFF;
  static const uint32_t kSwitchOn = 0x3B82F6;

  const bool control_unavailable = !state.available;
  uint32_t icon_color = kIconOff;
  if (!control_unavailable && (!state.has_state || state.is_on)) {
    if (state.supports_temperature && !state.supports_color && state.has_color_temp) {
      // CCT-only lights have no true RGB capability. Their tiles use the same
      // Kelvin color as the popup instead of the fixed yellow default used
      // for simple on/off lights.
      icon_color = lv_color_to_u32(
                       light_color_from_temperature_kelvin(state.color_temp_kelvin)) &
                   0xFFFFFF;
    } else {
      icon_color = state.has_color ? state.color : kIconOn;
    }
  }

  uint32_t label_color =
      control_unavailable
          ? kIconOff
          : (use_switch_widget ? kIconNeutral : icon_color);
  lv_color_t lv_color = lv_color_hex(label_color);
  if (widgets.icon_label) {
    lv_obj_set_style_text_color(widgets.icon_label, lv_color, 0);
  } else if (widgets.title_label) {
    lv_obj_set_style_text_color(widgets.title_label, lv_color, 0);
  }

  if (widgets.switch_obj) {
    if (control_unavailable) {
      lv_obj_remove_state(widgets.switch_obj, LV_STATE_CHECKED);
      lv_obj_add_state(widgets.switch_obj, LV_STATE_DISABLED);
    } else if (!state.has_state || state.is_on) {
      lv_obj_add_state(widgets.switch_obj, LV_STATE_CHECKED);
      lv_obj_clear_state(widgets.switch_obj, LV_STATE_DISABLED);
    } else {
      lv_obj_remove_state(widgets.switch_obj, LV_STATE_CHECKED);
      lv_obj_clear_state(widgets.switch_obj, LV_STATE_DISABLED);
    }
    uint32_t tile_color = tileBgColorOrDefault(tile, 0x353535);
    lv_obj_set_style_bg_color(widgets.switch_obj, lv_color_hex(tile_color), LV_PART_KNOB);
    lv_obj_set_style_bg_color(
        widgets.switch_obj,
        lv_color_hex(use_switch_widget ? kSwitchOff : kIconOff),
        LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(
        widgets.switch_obj,
        lv_color_hex(use_switch_widget ? kSwitchOn : icon_color),
        LV_PART_INDICATOR | LV_STATE_CHECKED);
  }
}

void update_switch_tile_state(GridType grid_type, uint8_t grid_index,
                              const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  SwitchState state = parse_switch_payload(payload);
  if (!switch_state_has_update(state)) return;
  apply_switch_tile_state(grid_type, grid_index, state);
}

static uint64_t capture_switch_update_targets(GridType grid_type,
                                              uint64_t grid_indices,
                                              String& entity_id) {
  entity_id.remove(0);
  uint64_t verified_indices = 0;
  for (uint8_t grid_index = 0; grid_index < TILES_PER_GRID; ++grid_index) {
    const uint64_t bit = uint64_t{1} << grid_index;
    if ((grid_indices & bit) == 0) continue;

    const Tile* tile = tile_renderer_get_tile_config(grid_type, grid_index);
    if (!tile || tile->type != TILE_SWITCH || !tile->sensor_entity.length()) {
      continue;
    }
    if (!entity_id.length()) {
      entity_id = tile->sensor_entity;
    }
    if (!tile->sensor_entity.equalsIgnoreCase(entity_id)) {
      continue;
    }
    verified_indices |= bit;
  }
  return verified_indices;
}

static void enqueue_switch_update(GridType grid_type, uint64_t grid_indices,
                                  const String& entity_id,
                                  bool require_entity_match,
                                  const char* payload) {
  if (grid_indices == 0 || !payload) return;
  const uint32_t layout_generation = switch_layout_generation(grid_type);
  if (layout_generation == 0) return;

  uint8_t idx = g_switch_tail;
  while (idx != g_switch_head) {
    SwitchUpdate& pending = g_switch_queue[idx];
    const bool same_target =
        pending.require_entity_match == require_entity_match &&
        (require_entity_match
             ? pending.entity_id.equalsIgnoreCase(entity_id)
             : pending.grid_indices == grid_indices);
    if (pending.valid && pending.grid_type == grid_type && same_target) {
      // Replacing the complete target set also handles a partly applied
      // batch. Every currently bound duplicate receives only the newest
      // payload, parsed once when processing resumes.
      pending.grid_indices = grid_indices;
      pending.entity_id = entity_id;
      pending.payload = String(payload);
      pending.layout_generation = layout_generation;
      pending.parsed = false;
      return;
    }
    idx = (idx + 1) % SWITCH_QUEUE_SIZE;
  }

  uint8_t next_head = (g_switch_head + 1) % SWITCH_QUEUE_SIZE;
  if (next_head == g_switch_tail) {
    if ((g_switch_overflow_count++ % 10) == 0) {
      Serial.println("[Queue] Full; replacing the oldest switch update");
    }
    g_switch_tail = (g_switch_tail + 1) % SWITCH_QUEUE_SIZE;
  }

  SwitchUpdate& update = g_switch_queue[g_switch_head];
  update.grid_type = grid_type;
  update.grid_indices = grid_indices;
  update.entity_id = entity_id;
  update.payload = String(payload);
  update.layout_generation = layout_generation;
  update.require_entity_match = require_entity_match;
  update.parsed = false;
  update.valid = true;
  g_switch_head = next_head;
}

void queue_switch_tile_updates(GridType grid_type, uint64_t grid_indices,
                               const char* payload) {
  if (grid_indices == 0 || !payload) return;

  String entity_id;
  grid_indices =
      capture_switch_update_targets(grid_type, grid_indices, entity_id);
  if (grid_indices == 0 || !entity_id.length()) return;
  enqueue_switch_update(grid_type, grid_indices, entity_id, true, payload);
}

void queue_switch_tile_update(GridType grid_type, uint8_t grid_index,
                              const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  // Cache preloading can render a hidden folder while another folder remains
  // the active TileConfig. Preserve that established single-slot path instead
  // of resolving the entity through the active config. The captured layout
  // generation still prevents delivery after the widget array is replaced.
  enqueue_switch_update(grid_type, uint64_t{1} << grid_index, String(), false,
                        payload);
}

void process_switch_update_queue(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_switch_tail != g_switch_head) {
    if (max_updates != 0 && processed >= max_updates) return;

    SwitchUpdate& upd = g_switch_queue[g_switch_tail];
    if (!upd.valid) {
      g_switch_tail = (g_switch_tail + 1) % SWITCH_QUEUE_SIZE;
      continue;
    }

    if (upd.layout_generation != switch_layout_generation(upd.grid_type)) {
      // A full rebuild or cached-grid restore replaced the widget bindings
      // while this entry waited. Drop it before parsing or applying.
      upd.grid_indices = 0;
    }

    if (upd.grid_indices != 0 && !upd.parsed) {
      upd.parsed_state = parse_switch_payload(upd.payload.c_str());
      upd.parsed = true;
      if (!switch_state_has_update(upd.parsed_state)) {
        upd.grid_indices = 0;
      }
    }

    for (uint8_t grid_index = 0;
         grid_index < TILES_PER_GRID && upd.grid_indices != 0;
         ++grid_index) {
      const uint64_t bit = uint64_t{1} << grid_index;
      if ((upd.grid_indices & bit) == 0) continue;

      // Consume the bit even if it became stale. Layout/folder replacement
      // can happen while an entry is waiting; never apply the old entity's
      // state to the tile that now occupies the same slot.
      upd.grid_indices &= ~bit;
      if (upd.require_entity_match) {
        const Tile* tile =
            tile_renderer_get_tile_config(upd.grid_type, grid_index);
        if (!tile || tile->type != TILE_SWITCH ||
            !tile->sensor_entity.equalsIgnoreCase(upd.entity_id)) {
          continue;
        }
      }

      apply_switch_tile_state(upd.grid_type, grid_index, upd.parsed_state);
      ++processed;
      if (max_updates != 0 && processed >= max_updates &&
          upd.grid_indices != 0) {
        // Keep this parsed entry at the queue tail. The next call resumes its
        // remaining slots without parsing or allocating the payload again.
        return;
      }
    }

    if (upd.grid_indices != 0) continue;
    upd.valid = false;
    upd.parsed = false;
    upd.require_entity_match = false;
    upd.layout_generation = 0;
    upd.entity_id.remove(0);
    upd.payload.remove(0);
    g_switch_tail = (g_switch_tail + 1) % SWITCH_QUEUE_SIZE;
  }
}

static void climate_copy_text(char* dest, size_t dest_size, const String& value) {
  if (!dest || dest_size == 0) return;
  size_t len = value.length();
  if (len >= dest_size) len = dest_size - 1;
  memcpy(dest, value.c_str(), len);
  dest[len] = '\0';
}

static void climate_normalize_modes(String& modes) {
  modes.replace("[", "");
  modes.replace("]", "");
  modes.replace("\"", "");
  modes.replace("'", "");
  modes.replace(" ", "");
  modes.trim();
  modes.toLowerCase();
}

static uint8_t climate_modes_mask(const String& normalized_modes) {
  uint8_t mask = 0;
  int start = 0;
  while (start <= normalized_modes.length()) {
    int comma = normalized_modes.indexOf(',', start);
    if (comma < 0) comma = normalized_modes.length();
    const String mode = normalized_modes.substring(start, comma);
    if (mode == "off") mask |= CLIMATE_MODE_OFF;
    else if (mode == "heat") mask |= CLIMATE_MODE_HEAT;
    else if (mode == "cool") mask |= CLIMATE_MODE_COOL;
    else if (mode == "heat_cool") mask |= CLIMATE_MODE_HEAT_COOL;
    else if (mode == "auto") mask |= CLIMATE_MODE_AUTO;
    else if (mode == "dry") mask |= CLIMATE_MODE_DRY;
    else if (mode == "fan_only") mask |= CLIMATE_MODE_FAN_ONLY;
    if (comma >= normalized_modes.length()) break;
    start = comma + 1;
  }
  return mask;
}

static uint8_t climate_preset_id(String preset) {
  preset.trim();
  preset.toLowerCase();
  for (uint8_t id = 0; id < 8; ++id) {
    if (preset == climatePresetName(id)) return id;
  }
  return 0xFF;
}

static uint8_t climate_preset_modes_mask(const String& normalized_modes) {
  uint8_t mask = 0;
  int start = 0;
  while (start <= normalized_modes.length()) {
    int comma = normalized_modes.indexOf(',', start);
    if (comma < 0) comma = normalized_modes.length();
    const uint8_t id =
        climate_preset_id(normalized_modes.substring(start, comma));
    if (id < 8) mask |= (1U << id);
    if (comma >= normalized_modes.length()) break;
    start = comma + 1;
  }
  return mask;
}

static uint16_t climate_fan_modes_mask(const String& normalized_modes) {
  static const char* const names[] = {
      "auto", "low", "medium", "high", "on",
      "off", "top", "middle", "focus", "diffuse"};
  uint16_t mask = 0;
  int start = 0;
  while (start <= normalized_modes.length()) {
    int comma = normalized_modes.indexOf(',', start);
    if (comma < 0) comma = normalized_modes.length();
    const String mode = normalized_modes.substring(start, comma);
    for (uint8_t id = 0; id < 10; ++id) {
      if (mode == names[id]) {
        mask |= static_cast<uint16_t>(1U << id);
        break;
      }
    }
    if (comma >= normalized_modes.length()) break;
    start = comma + 1;
  }
  return mask;
}

static uint8_t climate_swing_modes_mask(const String& normalized_modes) {
  static const char* const names[] = {
      "off", "on", "vertical", "horizontal", "both"};
  uint8_t mask = 0;
  int start = 0;
  while (start <= normalized_modes.length()) {
    int comma = normalized_modes.indexOf(',', start);
    if (comma < 0) comma = normalized_modes.length();
    const String mode = normalized_modes.substring(start, comma);
    for (uint8_t id = 0; id < 5; ++id) {
      if (mode == names[id]) {
        mask |= static_cast<uint8_t>(1U << id);
        break;
      }
    }
    if (comma >= normalized_modes.length()) break;
    start = comma + 1;
  }
  return mask;
}

static uint8_t climate_horizontal_swing_modes_mask(
    const String& normalized_modes) {
  static const char* const names[] = {
      "off", "on", "left", "center", "right", "swing", "wide"};
  uint8_t mask = 0;
  int start = 0;
  while (start <= normalized_modes.length()) {
    int comma = normalized_modes.indexOf(',', start);
    if (comma < 0) comma = normalized_modes.length();
    const String mode = normalized_modes.substring(start, comma);
    for (uint8_t id = 0; id < 7; ++id) {
      if (mode == names[id]) {
        mask |= static_cast<uint8_t>(1U << id);
        break;
      }
    }
    if (comma >= normalized_modes.length()) break;
    start = comma + 1;
  }
  return mask;
}

static ClimateState parse_climate_payload(const char* payload) {
  ClimateState out;
  if (!payload) return out;
  String json(payload);
  json.trim();
  if (!json.startsWith("{")) return out;

  auto parse_source = [&](const String& source) {
    String text;
    if (extract_json_string_field(source, "hvac_mode", text) ||
        extract_json_string_field(source, "state", text)) {
      text.trim();
      text.toLowerCase();
      climate_copy_text(out.hvac_mode, sizeof(out.hvac_mode), text);
    }
    if (extract_json_string_field(source, "hvac_action", text)) {
      text.trim();
      text.toLowerCase();
      climate_copy_text(out.hvac_action, sizeof(out.hvac_action), text);
    }
    if (extract_json_string_field(source, "preset_mode", text)) {
      out.preset_mode_id = climate_preset_id(text);
    }
    if (extract_json_string_field(source, "fan_mode", text)) {
      text.trim();
      text.toLowerCase();
      climate_copy_text(out.fan_mode, sizeof(out.fan_mode), text);
    }
    if (extract_json_string_field(source, "swing_mode", text)) {
      text.trim();
      text.toLowerCase();
      climate_copy_text(out.swing_mode, sizeof(out.swing_mode), text);
    }
    if (extract_json_string_field(source, "swing_horizontal_mode", text)) {
      text.trim();
      text.toLowerCase();
      climate_copy_text(
          out.swing_horizontal_mode, sizeof(out.swing_horizontal_mode), text);
    }
    if (extract_json_string_field(source, "temperature_unit", text) ||
        extract_json_string_field(source, "unit_of_measurement", text)) {
      text.trim();
      decode_basic_json_escapes(text);
      climate_copy_text(out.temperature_unit, sizeof(out.temperature_unit), text);
    }

    bool available = true;
    if (extract_json_bool_field_cstr(
            source.c_str(), "available", available)) {
      out.available = available;
    }

    String modes;
    if (extract_json_array_field(source, "hvac_modes", modes)) {
      climate_normalize_modes(modes);
      out.hvac_modes_mask = climate_modes_mask(modes);
    }
    if (extract_json_array_field(source, "preset_modes", modes)) {
      climate_normalize_modes(modes);
      out.preset_modes_mask = climate_preset_modes_mask(modes);
    }
    if (extract_json_array_field(source, "fan_modes", modes)) {
      climate_normalize_modes(modes);
      out.fan_modes_mask = climate_fan_modes_mask(modes);
    }
    if (extract_json_array_field(source, "swing_modes", modes)) {
      climate_normalize_modes(modes);
      out.swing_modes_mask = climate_swing_modes_mask(modes);
    }
    if (extract_json_array_field(source, "swing_horizontal_modes", modes)) {
      climate_normalize_modes(modes);
      out.swing_horizontal_modes_mask =
          climate_horizontal_swing_modes_mask(modes);
    }

    float number = 0.0f;
    if (extract_json_number_or_string_field(
            source, "supported_features", number) &&
        isfinite(number) && number >= 0.0f) {
      out.has_supported_features = true;
      out.supported_features = static_cast<uint16_t>(
          number > 65535.0f ? 65535U : lroundf(number));
    }
    if (extract_json_number_or_string_field(source, "current_temperature", number)) {
      out.has_current_temperature = true;
      out.current_temperature = number;
    }
    if (extract_json_number_or_string_field(source, "current_humidity", number)) {
      out.has_current_humidity = true;
      out.current_humidity = number;
    }
    if (extract_json_number_or_string_field(source, "target_humidity", number) ||
        extract_json_number_or_string_field(source, "humidity", number)) {
      out.has_target_humidity = true;
      out.target_humidity = number;
    }
    if (extract_json_number_or_string_field(source, "temperature", number)) {
      out.has_target_temperature = true;
      out.target_temperature = number;
    }
    if (extract_json_number_or_string_field(source, "target_temp_low", number)) {
      out.has_target_range = true;
      out.target_temp_low = number;
    }
    if (extract_json_number_or_string_field(source, "target_temp_high", number)) {
      out.has_target_range = true;
      out.target_temp_high = number;
    }
    if (extract_json_number_or_string_field(source, "min_temp", number)) {
      out.min_temp = number;
    }
    if (extract_json_number_or_string_field(source, "max_temp", number)) {
      out.max_temp = number;
    }
    if (extract_json_number_or_string_field(source, "min_humidity", number)) {
      out.min_humidity = number;
    }
    if (extract_json_number_or_string_field(source, "max_humidity", number)) {
      out.max_humidity = number;
    }
    if (extract_json_number_or_string_field(source, "target_temp_step", number) ||
        extract_json_number_or_string_field(source, "precision", number)) {
      out.target_temp_step = number;
    }
    if (extract_json_number_or_string_field(
            source, "target_humidity_step", number)) {
      out.target_humidity_step = number;
    }
  };

  parse_source(json);
  String attributes;
  if (extract_json_object_field(json, "attributes", attributes)) {
    parse_source(attributes);
  }

  if (out.max_temp <= out.min_temp) {
    out.min_temp = 7.0f;
    out.max_temp = 35.0f;
  }
  if (out.target_temp_step <= 0.0f || out.target_temp_step > 10.0f) {
    out.target_temp_step = 0.5f;
  }
  if (out.target_humidity_step <= 0.0f ||
      out.target_humidity_step > 100.0f) {
    out.target_humidity_step = 1.0f;
  }
  if (out.max_humidity <= out.min_humidity) {
    out.min_humidity = 30.0f;
    out.max_humidity = 99.0f;
  }
  if (!out.temperature_unit[0]) {
    climate_copy_text(
        out.temperature_unit, sizeof(out.temperature_unit),
        String("\xC2\xB0") + "C");
  }
  if (strcmp(out.hvac_mode, "unavailable") == 0) {
    out.available = false;
  }
  out.valid = !out.available || out.hvac_mode[0] || out.hvac_action[0] ||
              out.has_current_temperature || out.has_target_temperature ||
              out.has_current_humidity || out.has_target_humidity ||
              out.has_target_range;
  return out;
}

String climate_tile_base_icon(const Tile& tile) {
  if (isMdiIconDisabled(tile.icon_name)) return "";
  String icon = normalizeMdiIconName(tile.icon_name);
  if (!icon.length() && tile.sensor_entity.length()) {
    icon = normalizeMdiIconName(
        haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (!icon.length()) icon = "thermostat";
  return icon;
}

String climate_visual_icon(
    const ClimateState& state, const String& base_icon) {
  const char* action = state.hvac_action;
  if (strcmp(action, "heating") == 0 ||
      strcmp(action, "preheating") == 0) return "fire";
  if (strcmp(action, "cooling") == 0) return "snowflake";
  if (strcmp(action, "drying") == 0) return "water-percent";
  if (strcmp(action, "fan") == 0) return "fan";
  if (strcmp(action, "defrosting") == 0) return "snowflake-melt";

  String fallback = base_icon.length() ? base_icon : String("thermostat");
  if (strcmp(action, "idle") == 0) {
    if (strcmp(state.hvac_mode, "heat") == 0) return "fire";
    if (strcmp(state.hvac_mode, "cool") == 0) return "snowflake";
    if (strcmp(state.hvac_mode, "dry") == 0) return "water-percent";
    if (strcmp(state.hvac_mode, "fan_only") == 0) return "fan";
    if (strcmp(state.hvac_mode, "heat_cool") == 0) {
      return "sun-snowflake-variant";
    }
    if (strcmp(state.hvac_mode, "auto") == 0) return "thermostat-auto";
  }
  if (strcmp(action, "off") == 0 ||
      strcmp(state.hvac_mode, "off") == 0 || action[0]) {
    return fallback;
  }
  if (strcmp(state.hvac_mode, "heat") == 0) return "fire";
  if (strcmp(state.hvac_mode, "cool") == 0) return "snowflake";
  if (strcmp(state.hvac_mode, "dry") == 0) return "water-percent";
  if (strcmp(state.hvac_mode, "fan_only") == 0) return "fan";
  if (strcmp(state.hvac_mode, "heat_cool") == 0) {
    return "sun-snowflake-variant";
  }
  if (strcmp(state.hvac_mode, "auto") == 0) return "thermostat-auto";
  return fallback;
}

uint32_t climate_visual_color(const ClimateState& state) {
  if (!state.available) return 0x9E9E9E;
  return climate_visuals::state_foreground_color(
      state.hvac_mode, state.hvac_action);
}

static ClimatePopupInit build_climate_popup_init(
    GridType grid_type, uint8_t grid_index, const ClimateState& state) {
  ClimatePopupInit init;
  const Tile* tile = tile_renderer_get_tile_config(grid_type, grid_index);
  if (!tile) return init;
  init.entity_id = tile->sensor_entity;
  init.title = tile->title.length()
                   ? tile->title
                   : haBridgeConfig.findSensorName(tile->sensor_entity);
  if (!init.title.length()) init.title = tile->sensor_entity;
  String configured_icon = normalizeMdiIconName(tile->icon_name);
  init.icon_visible = !isMdiIconDisabled(tile->icon_name);
  init.dynamic_icon = init.icon_visible && !configured_icon.length();
  init.available = state.available;
  init.has_supported_features = state.has_supported_features;
  init.supported_features = state.supported_features;
  init.icon_name = climate_tile_base_icon(*tile);
  init.hvac_mode = state.hvac_mode;
  init.hvac_action = state.hvac_action;
  init.hvac_modes = climateHvacModesCsv(state.hvac_modes_mask);
  init.preset_mode = climatePresetName(state.preset_mode_id);
  init.preset_modes = climatePresetModesCsv(state.preset_modes_mask);
  init.fan_mode = state.fan_mode;
  init.fan_modes = climateFanModesCsv(state.fan_modes_mask);
  init.swing_mode = state.swing_mode;
  init.swing_modes = climateSwingModesCsv(state.swing_modes_mask);
  init.swing_horizontal_mode = state.swing_horizontal_mode;
  init.swing_horizontal_modes =
      climateHorizontalSwingModesCsv(state.swing_horizontal_modes_mask);
  init.temperature_unit = state.temperature_unit;
  init.current_temperature = state.current_temperature;
  init.current_humidity = state.current_humidity;
  init.target_temperature = state.target_temperature;
  init.target_humidity = state.target_humidity;
  init.target_temp_low = state.target_temp_low;
  init.target_temp_high = state.target_temp_high;
  init.min_temp = state.min_temp;
  init.max_temp = state.max_temp;
  init.min_humidity = state.min_humidity;
  init.max_humidity = state.max_humidity;
  init.target_temp_step = state.target_temp_step;
  init.target_humidity_step = state.target_humidity_step;
  init.has_current_temperature = state.has_current_temperature;
  init.has_current_humidity = state.has_current_humidity;
  init.has_target_temperature = state.has_target_temperature;
  init.has_target_humidity = state.has_target_humidity;
  init.has_target_range = state.has_target_range;
  return init;
}

static void update_climate_tile_state(
    GridType grid_type, uint8_t grid_index, const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  ClimateTileWidgets* widgets = tile_renderer_get_climate_widgets(grid_type);
  ClimateState* states = tile_renderer_get_climate_states(grid_type);
  ClimateTileWidgets& widget = widgets[grid_index];

  const uint32_t payload_hash = fnv1a_hash(payload);
  if (widget.last_payload_hash == payload_hash) {
    refresh_climate_tile_content(
        grid_type, grid_index, states[grid_index]);
    return;
  }

  ClimateState state = parse_climate_payload(payload);
  if (!state.valid) return;
  const bool kept_optimistic_target =
      reconcile_climate_mini_target_state(
          grid_type, grid_index, state);
  states[grid_index] = state;
  // Do not cache a stale payload whose target fields were replaced by the
  // local optimistic value. It must be parsed again after the protection
  // window if Home Assistant did not acknowledge the command.
  widget.last_payload_hash =
      kept_optimistic_target ? 0 : payload_hash;

  if (widget.value_label) {
    String value = state.has_current_temperature
                       ? i18n::format_number(
                             configManager.getConfig().language,
                             state.current_temperature,
                             1)
                       : String("--");
    value += " ";
    value += state.temperature_unit[0]
                 ? state.temperature_unit
                 : "\xC2\xB0"
                   "C";
    lv_label_set_text(widget.value_label, value.c_str());
  }
  refresh_climate_tile_content(grid_type, grid_index, state);
  if (widget.icon_label) {
    if (widget.dynamic_icon) {
      const Tile* tile =
          tile_renderer_get_tile_config(grid_type, grid_index);
      const String base_icon =
          tile ? climate_tile_base_icon(*tile) : String("thermostat");
      String icon = getMdiChar(climate_visual_icon(state, base_icon));
      if (!icon.length()) icon = getMdiChar("thermostat");
      lv_label_set_text(widget.icon_label, icon.c_str());
    }
    lv_obj_set_style_text_color(
        widget.icon_label, lv_color_hex(climate_visual_color(state)), 0);
  }

  ClimatePopupInit init = build_climate_popup_init(grid_type, grid_index, state);
  update_climate_popup(init);
}

struct ClimateUpdate {
  GridType grid_type;
  uint8_t grid_index = 0;
  String payload;
  bool valid = false;
};

static constexpr uint8_t CLIMATE_QUEUE_SIZE = 16;
static ClimateUpdate g_climate_queue[CLIMATE_QUEUE_SIZE];

void queue_climate_tile_update(
    GridType grid_type, uint8_t grid_index, const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  uint8_t idx = g_climate_tail;
  while (idx != g_climate_head) {
    ClimateUpdate& pending = g_climate_queue[idx];
    if (pending.valid && pending.grid_type == grid_type &&
        pending.grid_index == grid_index) {
      pending.payload = payload;
      return;
    }
    idx = (idx + 1) % CLIMATE_QUEUE_SIZE;
  }

  const uint8_t next = (g_climate_head + 1) % CLIMATE_QUEUE_SIZE;
  if (next == g_climate_tail) {
    g_climate_tail = (g_climate_tail + 1) % CLIMATE_QUEUE_SIZE;
    Serial.println("[Queue] Climate queue full, oldest update replaced");
  }
  ClimateUpdate& update = g_climate_queue[g_climate_head];
  update.grid_type = grid_type;
  update.grid_index = grid_index;
  update.payload = payload;
  update.valid = true;
  g_climate_head = next;
}

void process_climate_update_queue(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_climate_tail != g_climate_head &&
         (max_updates == 0 || processed < max_updates)) {
    ClimateUpdate& update = g_climate_queue[g_climate_tail];
    if (update.valid) {
      update_climate_tile_state(
          update.grid_type, update.grid_index, update.payload.c_str());
      update.valid = false;
      ++processed;
    }
    g_climate_tail = (g_climate_tail + 1) % CLIMATE_QUEUE_SIZE;
  }
}

/* === Thread-safe weather update queue (MQTT -> main loop) === */
struct WeatherUpdate {
  GridType grid_type;
  uint8_t grid_index;
  String payload;
  bool valid = false;
};

static const uint8_t WEATHER_QUEUE_SIZE = 16;
static WeatherUpdate g_weather_queue[WEATHER_QUEUE_SIZE];
static uint32_t g_weather_overflow_count = 0;

static void update_weather_tile_state(GridType grid_type, uint8_t grid_index, const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  WeatherTileWidgets* target = g_tab0_weather;
  if (grid_type == GridType::TAB1) target = g_tab1_weather;
  else if (grid_type == GridType::TAB2) target = g_tab2_weather;

  WeatherTileWidgets& widgets = target[grid_index];
  if (!widgets.icon_label && !widgets.temp_label && !widgets.condition_label && !widgets.location_label) {
    return;
  }

  uint32_t payload_hash = fnv1a_hash(payload);
  if (widgets.last_payload_hash == payload_hash) {
    return;
  }
  widgets.last_payload_hash = payload_hash;

  String json = payload;
  json.trim();
  if (!json.length()) return;

  String condition;
  if (!extract_json_string_field(json, "state", condition)) {
    extract_json_string_field(json, "condition", condition);
  }

  String icon_name;
  extract_json_string_field(json, "icon", icon_name);
  if (!icon_name.length() && condition.length()) {
    icon_name = weather_icon_from_condition(condition);
  }

  float temperature = 0.0f;
  bool has_temp = extract_json_number_or_string_field(json, "temperature", temperature);

  String unit;
  String units_obj;
  if (extract_json_object_field(json, "units", units_obj)) {
    extract_json_string_field(units_obj, "temperature", unit);
  } else {
    extract_json_string_field(json, "temperature_unit", unit);
  }
  decode_basic_json_escapes(unit);

  if (widgets.icon_label) {
    if (icon_name.length()) {
      String iconChar = getMdiChar(icon_name);
      if (iconChar.length()) {
        lv_label_set_text(widgets.icon_label, iconChar.c_str());
        lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_obj_add_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  String condition_text = weather_condition_display_label(condition);

  const TileGridConfig& grid = tileConfig.getActiveGrid();
  const Tile& tile = grid.tiles[grid_index];
  const bool has_condition_text = condition_text.length() && condition_text != "--";
  const bool show_condition = (tile.span_w > 1) && has_condition_text;
  const uint8_t forecast_limit =
      (tile.span_h >= 2) ? weather_forecast_count(tile.span_w) : 0;
  String today_date;
  const bool has_today = get_local_today_date(today_date);

  if (widgets.temp_label) {
    String temp_text = has_temp ? format_weather_temp(temperature, unit) : String("--");
    if (!show_condition) {
      lv_label_set_text(widgets.temp_label, temp_text.c_str());
    } else if (!widgets.condition_label) {
      String combined = temp_text;
      if (condition_text.length() && condition_text != "--") {
        combined += " ";
        combined += condition_text;
      }
      lv_label_set_text(widgets.temp_label, combined.c_str());
    } else {
      lv_label_set_text(widgets.temp_label, temp_text.c_str());
    }
    lv_obj_clear_flag(widgets.temp_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (widgets.condition_label) {
    if (show_condition) {
      lv_label_set_text(widgets.condition_label, condition_text.c_str());
      lv_obj_clear_flag(widgets.condition_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(widgets.condition_label, "");
      lv_obj_add_flag(widgets.condition_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (widgets.condition_sep_label) {
    if (show_condition && has_temp) {
      lv_label_set_text(widgets.condition_sep_label, "|");
      lv_obj_clear_flag(widgets.condition_sep_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(widgets.condition_sep_label, "");
      lv_obj_add_flag(widgets.condition_sep_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (widgets.location_label) {
    String name = tile.title;
    name.trim();
    if (!name.length()) {
      String payload_name;
      if (extract_json_string_field(json, "name", payload_name)) {
        decode_basic_json_escapes(payload_name);
        name = payload_name;
      }
    }
    if (!name.length()) name = "--";
    lv_label_set_text(widgets.location_label, name.c_str());
    lv_obj_clear_flag(widgets.location_label, LV_OBJ_FLAG_HIDDEN);
  }

  // Forecast handling (up to WEATHER_FORECAST_MAX entries)
  struct TileForecastSlot {
    bool has_data = false;
    String date_local;
    String day_text;
    String icon_name;
    bool has_temp = false;
    float temp = 0.0f;
    bool has_low = false;
    float low = 0.0f;
  };

  TileForecastSlot forecast_slots[WEATHER_FORECAST_MAX];
  String forecast_raw;
  bool has_forecast = false;
  String base_forecast_date = has_today ? today_date : "";
  if (extract_json_array_field(json, "forecast", forecast_raw) && forecast_raw.indexOf('{') >= 0) {
    has_forecast = true;
  }

  if (has_forecast && forecast_limit > 0) {
    int cursor = 0;
    int obj_begin = 0;
    int obj_end = 0;
    uint8_t fallback_slot = 0;
    while (hometiles_json::nextObjectInArray(
        forecast_raw.c_str(), forecast_raw.length(), &cursor,
        &obj_begin, &obj_end)) {
      String obj = forecast_raw.substring(obj_begin, obj_end);
      String f_condition;
      String f_icon;
      String f_day;
      String f_date_local;
      float f_temp = 0.0f;
      float f_low = 0.0f;
      const bool f_has_temp = extract_json_number_or_string_field(obj, "temperature", f_temp);
      bool f_has_low = false;
      if (extract_json_number_or_string_field(obj, "templow", f_low)) f_has_low = true;
      if (!f_has_low && extract_json_number_or_string_field(obj, "temperature_low", f_low)) f_has_low = true;
      if (!f_has_low && extract_json_number_or_string_field(obj, "temp_low", f_low)) f_has_low = true;
      if (!f_has_low && extract_json_number_or_string_field(obj, "low", f_low)) f_has_low = true;
      extract_json_string_field(obj, "condition", f_condition);
      extract_json_string_field(obj, "icon", f_icon);
      String datetime;
      extract_json_string_field(obj, "date_local", f_date_local);
      if (extract_json_string_field(obj, "datetime", datetime)) {
        f_day = weekday_from_iso(datetime);
        if (!f_date_local.length() && datetime.length() >= 10) {
          f_date_local = datetime.substring(0, 10);
        }
      }
      if (!f_icon.length() && f_condition.length()) {
        f_icon = weather_icon_from_condition(f_condition);
      }
      if (!base_forecast_date.length() && f_date_local.length()) {
        base_forecast_date = f_date_local;
      }

      int slot_index = -1;
      if (base_forecast_date.length() && f_date_local.length()) {
        const int offset = iso_date_day_offset(base_forecast_date, f_date_local);
        if (offset >= 0 && offset < forecast_limit) {
          slot_index = offset;
        }
      }
      if (slot_index < 0) {
        while (fallback_slot < forecast_limit && forecast_slots[fallback_slot].has_data) ++fallback_slot;
        if (fallback_slot < forecast_limit) {
          slot_index = fallback_slot++;
        }
      }
      if (slot_index >= 0 && slot_index < forecast_limit) {
        TileForecastSlot& slot = forecast_slots[slot_index];
        slot.has_data = true;
        slot.date_local = f_date_local;
        slot.day_text = f_day;
        slot.icon_name = f_icon;
        slot.has_temp = f_has_temp;
        slot.temp = f_temp;
        slot.has_low = f_has_low;
        slot.low = f_low;
      }
    }
  }

  const lv_color_t forecast_active_color = lv_color_white();
  const lv_color_t forecast_inactive_color = lv_color_hex(0x7F8BAA);
  for (uint8_t i = 0; i < forecast_limit; ++i) {
    WeatherForecastWidgets& fw = widgets.forecast[i];
    TileForecastSlot& slot = forecast_slots[i];
    String display_date = slot.date_local;
    if (!display_date.length() && base_forecast_date.length()) {
      display_date = iso_date_add_days(base_forecast_date, i);
    }

    String day_text = slot.day_text;
    if (i == 0 && (slot.has_data || display_date.length())) {
      day_text = weather_today_tile_label();
    } else if (has_today && display_date.length() && display_date == today_date) {
      day_text = weather_today_tile_label();
    } else if (!day_text.length() && display_date.length()) {
      day_text = weekday_from_iso(display_date);
    }
    if (!day_text.length()) day_text = "--";

    if (fw.day_label) {
      lv_label_set_text(fw.day_label, day_text.c_str());
      lv_obj_set_style_text_color(fw.day_label,
                                  slot.has_data ? forecast_active_color : forecast_inactive_color,
                                  0);
      lv_obj_clear_flag(fw.day_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.sep_label) {
      lv_obj_add_flag(fw.sep_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.icon_label) {
      if (slot.has_data && slot.icon_name.length()) {
        String icon_char = getMdiChar(slot.icon_name);
        if (icon_char.length()) {
          lv_label_set_text(fw.icon_label, icon_char.c_str());
          lv_obj_set_style_text_color(fw.icon_label, forecast_active_color, 0);
          lv_obj_clear_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_label_set_text(fw.icon_label, "");
          lv_obj_add_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
        }
      } else {
        lv_label_set_text(fw.icon_label, "");
        lv_obj_add_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (fw.temp_high_label) {
      constexpr lv_coord_t kTileForecastTempTop =
          tile_layout::scale(52 + 54);
      constexpr lv_coord_t kTileForecastLowTop =
          kTileForecastTempTop + tile_layout::scale(30);
      constexpr lv_coord_t kTileColContentW =
          WEATHER_FORECAST_COL_W -
          (2 * tile_layout::scale_480(20));
      constexpr lv_coord_t kTileColCenterX = kTileColContentW / 2;
      String unit_text = format_weather_temp_unit(unit);
      if (slot.has_data && slot.has_temp) {
        lv_label_set_text(fw.temp_high_label, format_weather_temp_value(slot.temp).c_str());
        lv_label_set_text(fw.temp_high_unit_label, unit_text.c_str());
        lv_obj_set_style_text_color(fw.temp_high_label, forecast_active_color, 0);
        lv_obj_set_style_text_color(fw.temp_high_unit_label, forecast_active_color, 0);
        position_tile_value_unit_centered(fw.temp_high_label, fw.temp_high_unit_label,
                                          kTileColCenterX, kTileForecastTempTop, kTileColContentW);
        lv_obj_clear_flag(fw.temp_high_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(fw.temp_high_unit_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(fw.temp_high_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fw.temp_high_unit_label, LV_OBJ_FLAG_HIDDEN);
      }
      if (slot.has_data && slot.has_low) {
        lv_label_set_text(fw.temp_low_label, format_weather_temp_value(slot.low).c_str());
        lv_label_set_text(fw.temp_low_unit_label, unit_text.c_str());
        lv_obj_set_style_text_color(fw.temp_low_label, forecast_active_color, 0);
        lv_obj_set_style_text_color(fw.temp_low_unit_label, forecast_active_color, 0);
        position_tile_value_unit_centered(fw.temp_low_label, fw.temp_low_unit_label,
                                          kTileColCenterX, kTileForecastLowTop, kTileColContentW);
        lv_obj_clear_flag(fw.temp_low_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(fw.temp_low_unit_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(fw.temp_low_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fw.temp_low_unit_label, LV_OBJ_FLAG_HIDDEN);
      }
    } else if (fw.temp_label) {
      if (slot.has_data) {
        String hi_text = slot.has_temp ? format_weather_temp(slot.temp, unit) : String("--");
        String lo_text = slot.has_low ? format_weather_temp(slot.low, unit) : String("--");
        lv_label_set_text(fw.temp_label, (hi_text + "\n" + lo_text).c_str());
        lv_obj_set_style_text_color(fw.temp_label, forecast_active_color, 0);
        lv_obj_clear_flag(fw.temp_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(fw.temp_label, "");
        lv_obj_add_flag(fw.temp_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

void queue_weather_tile_update(GridType grid_type, uint8_t grid_index, const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;

  uint8_t idx = g_weather_tail;
  while (idx != g_weather_head) {
    WeatherUpdate& pending = g_weather_queue[idx];
    if (pending.valid &&
        pending.grid_type == grid_type &&
        pending.grid_index == grid_index) {
      pending.payload = String(payload);
      return;
    }
    idx = (idx + 1) % WEATHER_QUEUE_SIZE;
  }

  uint8_t next_head = (g_weather_head + 1) % WEATHER_QUEUE_SIZE;
  if (next_head == g_weather_tail) {
    if ((g_weather_overflow_count++ % 10) == 0) {
      Serial.println("[Queue] FULL! Oldest weather update will be overwritten");
    }
    g_weather_tail = (g_weather_tail + 1) % WEATHER_QUEUE_SIZE;
  }

  g_weather_queue[g_weather_head].grid_type = grid_type;
  g_weather_queue[g_weather_head].grid_index = grid_index;
  g_weather_queue[g_weather_head].payload = String(payload);
  g_weather_queue[g_weather_head].valid = true;
  g_weather_head = next_head;
}

void process_weather_update_queue(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_weather_tail != g_weather_head && (max_updates == 0 || processed < max_updates)) {
    WeatherUpdate& upd = g_weather_queue[g_weather_tail];
    if (upd.valid) {
      update_weather_tile_state(upd.grid_type, upd.grid_index, upd.payload.c_str());
      upd.valid = false;
      ++processed;
    }
    g_weather_tail = (g_weather_tail + 1) % WEATHER_QUEUE_SIZE;
  }
}

/* === Thread-safe media update queue (MQTT -> main loop) === */
struct MediaUpdate {
  GridType grid_type;
  uint8_t grid_index;
  String payload;
  bool valid = false;
};

static const uint8_t MEDIA_QUEUE_SIZE = 24;
static MediaUpdate g_media_queue[MEDIA_QUEUE_SIZE];
static uint32_t g_media_overflow_count = 0;

static String media_first_non_empty(const String& a,
                                    const String& b,
                                    const String& c = String(),
                                    const String& d = String()) {
  if (a.length()) return a;
  if (b.length()) return b;
  if (c.length()) return c;
  return d;
}

static bool media_text_same(String a, String b) {
  a.trim();
  b.trim();
  return a.length() && b.length() && a.equalsIgnoreCase(b);
}

static String media_empty_title_label(String state) {
  state.trim();
  state.toLowerCase();
  const auto& tr = i18n::strings(configManager.getConfig().language);
  if (state == "playing") return tr.media_state_playing;
  if (state == "paused") return tr.media_state_paused;
  if (state == "idle") return tr.media_state_idle;
  if (state == "standby") return tr.media_state_standby;
  if (state == "off") return tr.media_state_off;
  return tr.media_no_playback;
}

static void sanitize_media_display_text(String& text) {
  text.replace("`", "'");
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.trim();
}

static bool set_label_text_if_changed(lv_obj_t* label, const char* text) {
  if (!label) return false;
  if (!text) text = "";
  const char* current = lv_label_get_text(label);
  if (!current || strcmp(current, text) != 0) {
    lv_label_set_text(label, text);
    return true;
  }
  return false;
}

static bool set_label_text_if_changed(lv_obj_t* label, const String& text) {
  return set_label_text_if_changed(label, text.c_str());
}

static bool set_label_long_mode_if_changed(lv_obj_t* label, lv_label_long_mode_t mode) {
  if (!label) return false;
  if (lv_label_get_long_mode(label) != mode) {
    lv_label_set_long_mode(label, mode);
    return true;
  }
  return false;
}

static void restart_visible_media_text_scroll(lv_obj_t* label) {
  if (!label || lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_label_get_long_mode(label) != LV_LABEL_LONG_SCROLL) return;
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
}

static String media_icon_for_state(const String& state) {
  String lower = state;
  lower.trim();
  lower.toLowerCase();
  if (lower == "playing") return "pause";
  return "play";
}

static bool media_is_playing_state(const String& state) {
  String lower = state;
  lower.trim();
  lower.toLowerCase();
  return lower == "playing";
}

static String media_label_text(lv_obj_t* label) {
  if (!label || lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) return "";
  const char* text = lv_label_get_text(label);
  return text ? String(text) : String();
}

static bool media_widget_is_playing(const MediaTileWidgets& widgets) {
  if (!widgets.play_pause_label) return false;
  const char* current = lv_label_get_text(widgets.play_pause_label);
  String pause_icon = getMdiChar("pause");
  return current && pause_icon.length() && strcmp(current, pause_icon.c_str()) == 0;
}

static String media_friendly_name_from_entity_id(const String& entity_id) {
  int dot = entity_id.indexOf('.');
  String name = dot >= 0 ? entity_id.substring(dot + 1) : entity_id;
  name.replace('_', ' ');
  name.trim();
  bool capitalize_next = true;
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name.charAt(i);
    if (capitalize_next && c >= 'a' && c <= 'z') {
      name.setCharAt(i, static_cast<char>(c - 32));
    }
    capitalize_next = (c == ' ');
  }
  return name;
}

static String media_popup_title_for_tile(const Tile& tile) {
  String title = tile.title;
  title.trim();
  if (!title.length() && tile.sensor_entity.length()) {
    title = haBridgeConfig.findSensorName(tile.sensor_entity);
  }
  if (!title.length() && tile.sensor_entity.length()) {
    title = media_friendly_name_from_entity_id(tile.sensor_entity);
  }
  return title;
}

static String media_popup_icon_for_tile(const Tile& tile) {
  String icon_name = normalizeMdiIconName(tile.icon_name);
  if (!icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (!icon_name.length()) icon_name = "television";
  return icon_name;
}

static String media_entity_for_grid_index(GridType grid_type, uint8_t grid_index) {
  if (grid_index >= TILES_PER_GRID) return "";
  const Tile* tile = tile_renderer_get_tile_config(grid_type, grid_index);
  if (!tile || tile->type != TILE_MEDIA) return "";
  return tile->sensor_entity;
}

const lv_image_dsc_t* tile_renderer_find_media_cover(const String& entity_id, uint32_t& hash) {
  const GridType types[] = {GridType::TAB0, GridType::TAB1, GridType::TAB2};
  MediaTileWidgets* const grids[] = {g_tab0_media, g_tab1_media, g_tab2_media};
  for (size_t grid = 0; grid < 3; ++grid) {
    if (!grids[grid]) continue;
    for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
      const auto& widgets = grids[grid][i];
      if (!widgets.cover_ref || !widgets.cover_ref->dsc || !widgets.cover_clip ||
          lv_obj_has_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN)) continue;
      if (!media_entity_for_grid_index(types[grid], i).equalsIgnoreCase(entity_id)) continue;
      hash = widgets.cover_ref->url_hash;
      return widgets.cover_ref->popup_dsc ? widgets.cover_ref->popup_dsc : widgets.cover_ref->dsc;
    }
  }
  hash = 0;
  return nullptr;
}

static void update_media_popup_from_widgets(GridType grid_type,
                                            uint8_t grid_index,
                                            MediaTileWidgets& widgets,
                                            const String& state_override = String()) {
  // Screensaver media tiles deliberately have no popup.
  if (grid_type == GridType::SCREENSAVER) return;
  if (grid_index >= TILES_PER_GRID) return;
  const Tile* tile_ptr = tile_renderer_get_tile_config(grid_type, grid_index);
  if (!tile_ptr || tile_ptr->type != TILE_MEDIA ||
      !tile_ptr->sensor_entity.length()) return;
  const Tile& tile = *tile_ptr;

  MediaPopupInit init;
  init.entity_id = tile.sensor_entity;
  init.title = media_popup_title_for_tile(tile);
  init.icon_name = media_popup_icon_for_tile(tile);
  init.icon_char = media_label_text(widgets.icon_label);
  init.bg_color = tileBgColorOrDefault(tile, 0x2A2A2A);
  init.media_title = media_label_text(widgets.media_title_label);
  init.media_subtitle = media_label_text(widgets.media_subtitle_label);
  init.is_playing = state_override.length() ? media_is_playing_state(state_override)
                                            : media_widget_is_playing(widgets);
  init.has_media_position = widgets.has_media_position;
  init.media_position = widgets.media_position;
  init.media_duration = widgets.media_duration;
  init.media_position_received_ms = widgets.media_position_received_ms;
  init.has_volume = widgets.has_media_volume;
  init.volume_level = widgets.media_volume_level;
  init.is_muted = widgets.media_is_muted;
  const bool cover_visible = widgets.cover_ref &&
                             widgets.cover_ref->dsc &&
                             widgets.cover_clip &&
                             !lv_obj_has_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN);
  if (cover_visible) {
    init.cover_dsc = widgets.cover_ref->popup_dsc
                         ? widgets.cover_ref->popup_dsc
                         : widgets.cover_ref->dsc;
    init.cover_hash = widgets.cover_ref->url_hash;
  }
  update_media_popup(init);
}

struct MediaCoverDecodeCtx {
  const uint8_t* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
  uint16_t* pixels = nullptr;
  uint16_t w = 0;
  uint16_t h = 0;
};

static size_t media_cover_jpeg_input(JDEC* jd, uint8_t* buff, size_t ndata) {
  MediaCoverDecodeCtx* ctx = static_cast<MediaCoverDecodeCtx*>(jd->device);
  if (!ctx || !ctx->data || ctx->pos >= ctx->len) return 0;
  size_t remain = ctx->len - ctx->pos;
  size_t take = (ndata < remain) ? ndata : remain;
  if (buff && take) {
    memcpy(buff, ctx->data + ctx->pos, take);
  }
  ctx->pos += take;
  return take;
}

static int media_cover_jpeg_output(JDEC* jd, void* bitmap, JRECT* rect) {
  MediaCoverDecodeCtx* ctx = static_cast<MediaCoverDecodeCtx*>(jd->device);
  if (!ctx || !ctx->pixels || !bitmap) return 0;

  const uint8_t* src = static_cast<const uint8_t*>(bitmap);
  const uint16_t rw = rect->right - rect->left + 1;
  for (uint16_t y = rect->top; y <= rect->bottom && y < ctx->h; ++y) {
    for (uint16_t x = rect->left; x <= rect->right && x < ctx->w; ++x) {
      const size_t si = ((y - rect->top) * rw + (x - rect->left)) * 3;
      const uint8_t b = src[si];
      const uint8_t g = src[si + 1];
      const uint8_t r = src[si + 2];
      const uint16_t c = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      ctx->pixels[static_cast<size_t>(y) * ctx->w + x] =
          static_cast<uint16_t>((c >> 8) | (c << 8));
    }
  }
  return 1;
}

static void* alloc_media_cover_memory(size_t bytes, bool prefer_psram = false) {
  if (!bytes) return nullptr;
  void* data = nullptr;
  if (prefer_psram) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!data) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  return data;
}

static void free_media_cover_dsc(lv_image_dsc_t*& dsc) {
  if (!dsc) return;
  // LVGL caches decoded data and headers BY POINTER ADDRESS when
  // LV_CACHE_DEF_SIZE is enabled. Without dropping the entry, a later malloc
  // at the same address can return the OLD image from the cache, causing
  // occasional stale cover artifacts when tracks change.
  // Call only from the LVGL task; the cover worker frees its data inline.
  lv_image_cache_drop(dsc);
  if (dsc->data) {
    free(const_cast<uint8_t*>(dsc->data));
  }
  free(dsc);
  dsc = nullptr;
}

static bool is_media_cover_jpeg(const uint8_t* data, size_t len) {
  return data && len >= 2 && data[0] == 0xFF && data[1] == 0xD8;
}

static bool is_media_cover_png(const uint8_t* data, size_t len) {
  return data && len >= 8 &&
         data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
         data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A;
}

static bool read_media_cover_png_size(const uint8_t* data, size_t len, uint16_t& w, uint16_t& h) {
  if (!is_media_cover_png(data, len) || len < 24) return false;
  uint32_t png_w = (static_cast<uint32_t>(data[16]) << 24) |
                   (static_cast<uint32_t>(data[17]) << 16) |
                   (static_cast<uint32_t>(data[18]) << 8) |
                   static_cast<uint32_t>(data[19]);
  uint32_t png_h = (static_cast<uint32_t>(data[20]) << 24) |
                   (static_cast<uint32_t>(data[21]) << 16) |
                   (static_cast<uint32_t>(data[22]) << 8) |
                   static_cast<uint32_t>(data[23]);
  if (png_w == 0 || png_h == 0 || png_w > UINT16_MAX || png_h > UINT16_MAX) return false;
  w = static_cast<uint16_t>(png_w);
  h = static_cast<uint16_t>(png_h);
  return true;
}

static bool read_media_cover_jpeg_size(const uint8_t* data, size_t len, uint16_t& w, uint16_t& h) {
  if (!is_media_cover_jpeg(data, len) || len < 32) return false;

  uint8_t* work = static_cast<uint8_t*>(alloc_media_cover_memory(4096));
  if (!work) return false;

  MediaCoverDecodeCtx ctx{};
  ctx.data = data;
  ctx.len = len;

  JDEC jd;
  JRESULT rc = jd_prepare(&jd, media_cover_jpeg_input, work, 4096, &ctx);
  free(work);
  if (rc != JDR_OK || jd.width == 0 || jd.height == 0 ||
      jd.width > UINT16_MAX || jd.height > UINT16_MAX) {
    Serial.printf("[MediaCover] JPEG info failed: %d\n", static_cast<int>(rc));
    return false;
  }

  w = static_cast<uint16_t>(jd.width);
  h = static_cast<uint16_t>(jd.height);
  return true;
}

#if defined(CONFIG_IDF_TARGET_ESP32P4) && SOC_JPEG_DECODE_SUPPORTED
static uint32_t media_cover_align16(uint32_t value) {
  return (value + 15U) & ~15U;
}

// ESP32-P4 has a dedicated JPEG block. The old path below uses TJpgDec in
// software and writes every decoded pixel individually to PSRAM. Decode RGB565
// directly with the hardware instead; the driver allocator returns the
// DMA/cache-aligned PSRAM buffer required by the JPEG peripheral. A bounded
// timeout plus the unchanged software path below keeps malformed/unsupported
// JPEGs from breaking cover updates.
static lv_image_dsc_t* make_media_cover_decoded_jpeg_hw_dsc(const uint8_t* data,
                                                             size_t len) {
  if (!is_media_cover_jpeg(data, len) || len < 32 || len > UINT32_MAX) return nullptr;

  const uint32_t started_ms = millis();
  jpeg_decode_picture_info_t info{};
  esp_err_t err = jpeg_decoder_get_info(data, static_cast<uint32_t>(len), &info);
  if (err != ESP_OK || info.width == 0 || info.height == 0 ||
      info.width > 512U || info.height > 512U) {
    Serial.printf("[MediaCover] HW JPEG info failed: %s (%ux%u)\n",
                  esp_err_to_name(err),
                  static_cast<unsigned>(info.width),
                  static_cast<unsigned>(info.height));
    return nullptr;
  }

  // The common bridge artwork is 240x240 and therefore naturally aligned.
  // For unusual non-16-aligned radio logos keep using TJpgDec: IDF revisions
  // differ in whether decoded_size reports packed or padded rows, and guessing
  // that stride would risk a distorted image.
  if ((info.width & 15U) != 0 || (info.height & 15U) != 0) return nullptr;

  const uint32_t padded_w = media_cover_align16(info.width);
  const uint32_t padded_h = media_cover_align16(info.height);
  const size_t requested_bytes =
      static_cast<size_t>(padded_w) * padded_h * sizeof(uint16_t);
  jpeg_decode_memory_alloc_cfg_t mem_cfg{};
  mem_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
  size_t allocated_bytes = 0;
  uint16_t* decoded = static_cast<uint16_t*>(
      jpeg_alloc_decoder_mem(requested_bytes, &mem_cfg, &allocated_bytes));
  if (!decoded || allocated_bytes < requested_bytes) {
    free(decoded);
    Serial.printf("[MediaCover] HW JPEG PSRAM buffer missing: %u bytes\n",
                  static_cast<unsigned>(requested_bytes));
    return nullptr;
  }

  // Create the decode engine ONCE and retain it. Creating and deleting it
  // acquires and releases the 2D-DMA pool shared by JPEG and PPA display
  // rotation. Doing this for every cover was suspected of occasionally losing
  // a PPA transaction on Tab5, leaving it wedged until reboot. A persistent
  // engine keeps the pool referenced and avoids creation overhead per cover.
  static jpeg_decoder_handle_t s_media_jpeg_decoder = nullptr;
  jpeg_decode_cfg_t decode_cfg{};
  decode_cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  // RGB order yields big-endian RGB565 bytes. That is exactly the byte layout
  // represented by LV_COLOR_FORMAT_RGB565_SWAPPED on the little-endian P4.
  decode_cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
  decode_cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t decoded_bytes = 0;
  {
    // Keep engine lifecycle and decoding under the same arbiter. Never use
    // the shared JPEG/PPA DMA pool without the lock.
    Dma2dArbiterGuard dma2d_guard(2000);
    if (!dma2d_guard.locked()) {
      free(decoded);
      Serial.println("[MediaCover] 2D DMA arbiter timeout, using software fallback");
      return nullptr;
    }
    if (!s_media_jpeg_decoder) {
      jpeg_decode_engine_cfg_t engine_cfg{};
      engine_cfg.intr_priority = 0;
      engine_cfg.timeout_ms = 100;
      err = jpeg_new_decoder_engine(&engine_cfg, &s_media_jpeg_decoder);
      if (err != ESP_OK || !s_media_jpeg_decoder) {
        s_media_jpeg_decoder = nullptr;
        free(decoded);
        Serial.printf("[MediaCover] HW JPEG engine unavailable: %s\n",
                      esp_err_to_name(err));
        return nullptr;
      }
    }
    err = jpeg_decoder_process(s_media_jpeg_decoder,
                               &decode_cfg,
                               data,
                               static_cast<uint32_t>(len),
                               reinterpret_cast<uint8_t*>(decoded),
                               static_cast<uint32_t>(allocated_bytes),
                               &decoded_bytes);
    if (err != ESP_OK || decoded_bytes < requested_bytes) {
      free(decoded);
      Serial.printf("[MediaCover] HW JPEG decode failed: decode=%s bytes=%u/%u\n",
                    esp_err_to_name(err),
                    static_cast<unsigned>(decoded_bytes),
                    static_cast<unsigned>(requested_bytes));
      // After a failure, such as a timeout during transfer, discard the engine
      // and recreate it for the next cover instead of reusing possibly stuck state.
      jpeg_del_decoder_engine(s_media_jpeg_decoder);
      s_media_jpeg_decoder = nullptr;
      return nullptr;
    }
  }

  constexpr uint16_t kMaxCoverSide = 240;
  uint16_t dst_w = static_cast<uint16_t>(info.width);
  uint16_t dst_h = static_cast<uint16_t>(info.height);
  if (dst_w > kMaxCoverSide || dst_h > kMaxCoverSide) {
    if (dst_w >= dst_h) {
      dst_w = kMaxCoverSide;
      dst_h = static_cast<uint16_t>((info.height * dst_w) / info.width);
    } else {
      dst_h = kMaxCoverSide;
      dst_w = static_cast<uint16_t>((info.width * dst_h) / info.height);
    }
    if (dst_w == 0) dst_w = 1;
    if (dst_h == 0) dst_h = 1;
  }

  uint16_t* final_pixels = decoded;
  size_t final_bytes = static_cast<size_t>(dst_w) * dst_h * sizeof(uint16_t);
  const bool needs_copy = padded_w != info.width || padded_h != info.height ||
                          dst_w != info.width || dst_h != info.height;
  if (needs_copy) {
    final_pixels = static_cast<uint16_t*>(alloc_media_cover_memory(final_bytes, true));
    if (!final_pixels) {
      free(decoded);
      return nullptr;
    }
    for (uint16_t y = 0; y < dst_h; ++y) {
      const uint32_t sy = (static_cast<uint32_t>(y) * info.height) / dst_h;
      for (uint16_t x = 0; x < dst_w; ++x) {
        const uint32_t sx = (static_cast<uint32_t>(x) * info.width) / dst_w;
        final_pixels[static_cast<size_t>(y) * dst_w + x] =
            decoded[static_cast<size_t>(sy) * padded_w + sx];
      }
    }
    free(decoded);
  }

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(
      alloc_media_cover_memory(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(final_pixels);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  dsc->header.w = dst_w;
  dsc->header.h = dst_h;
  dsc->header.stride = dst_w * 2;
  dsc->data_size = final_bytes;
  dsc->data = reinterpret_cast<const uint8_t*>(final_pixels);

  Serial.printf("[MediaCover] HW JPEG: %ux%u -> %ux%u, %u bytes in %u ms\n",
                static_cast<unsigned>(info.width),
                static_cast<unsigned>(info.height),
                static_cast<unsigned>(dst_w),
                static_cast<unsigned>(dst_h),
                static_cast<unsigned>(final_bytes),
                static_cast<unsigned>(millis() - started_ms));
  return dsc;
}
#endif

static lv_image_dsc_t* make_media_cover_decoded_jpeg_dsc(const uint8_t* data, size_t len) {
  if (!is_media_cover_jpeg(data, len) || len < 32) return nullptr;

  uint8_t* work = static_cast<uint8_t*>(alloc_media_cover_memory(4096));
  if (!work) return nullptr;

  MediaCoverDecodeCtx ctx{};
  ctx.data = data;
  ctx.len = len;

  JDEC jd;
  JRESULT rc = jd_prepare(&jd, media_cover_jpeg_input, work, 4096, &ctx);
  if (rc != JDR_OK || jd.width == 0 || jd.height == 0 ||
      jd.width > UINT16_MAX || jd.height > UINT16_MAX) {
    Serial.printf("[MediaCover] MQTT JPEG prepare failed: %d\n", static_cast<int>(rc));
    free(work);
    return nullptr;
  }

  const size_t full_pixels = static_cast<size_t>(jd.width) * jd.height;
  if (full_pixels == 0 || full_pixels > (512U * 512U)) {
    Serial.printf("[MediaCover] MQTT JPEG too large: %ux%u\n",
                  static_cast<unsigned>(jd.width),
                  static_cast<unsigned>(jd.height));
    free(work);
    return nullptr;
  }

  const size_t full_bytes = full_pixels * sizeof(uint16_t);
  uint16_t* full = static_cast<uint16_t*>(alloc_media_cover_memory(full_bytes, true));
  if (!full) {
    free(work);
    return nullptr;
  }
  memset(full, 0, full_bytes);

  ctx.pixels = full;
  ctx.w = static_cast<uint16_t>(jd.width);
  ctx.h = static_cast<uint16_t>(jd.height);
  rc = jd_decomp(&jd, media_cover_jpeg_output, 0);
  free(work);
  if (rc != JDR_OK) {
    Serial.printf("[MediaCover] MQTT JPEG decode failed: %d\n", static_cast<int>(rc));
    free(full);
    return nullptr;
  }

  // Keep enough resolution for the 240x240 media popup. The tile itself still
  // renders the same descriptor at its smaller size. Older firmware keeps its
  // previous 160px limit, so the bridge can publish the larger JPEG without a
  // protocol/version split.
  constexpr uint16_t kMaxCoverSide = 240;
  uint16_t dst_w = ctx.w;
  uint16_t dst_h = ctx.h;
  if (dst_w > kMaxCoverSide || dst_h > kMaxCoverSide) {
    if (dst_w >= dst_h) {
      dst_w = kMaxCoverSide;
      dst_h = static_cast<uint16_t>((static_cast<uint32_t>(ctx.h) * dst_w) / ctx.w);
    } else {
      dst_h = kMaxCoverSide;
      dst_w = static_cast<uint16_t>((static_cast<uint32_t>(ctx.w) * dst_h) / ctx.h);
    }
    if (dst_w == 0) dst_w = 1;
    if (dst_h == 0) dst_h = 1;
  }

  uint16_t* final_pixels = full;
  size_t final_bytes = full_bytes;
  if (dst_w != ctx.w || dst_h != ctx.h) {
    final_bytes = static_cast<size_t>(dst_w) * dst_h * sizeof(uint16_t);
    final_pixels = static_cast<uint16_t*>(alloc_media_cover_memory(final_bytes, true));
    if (!final_pixels) {
      free(full);
      return nullptr;
    }
    for (uint16_t y = 0; y < dst_h; ++y) {
      const uint16_t sy = static_cast<uint16_t>((static_cast<uint32_t>(y) * ctx.h) / dst_h);
      for (uint16_t x = 0; x < dst_w; ++x) {
        const uint16_t sx = static_cast<uint16_t>((static_cast<uint32_t>(x) * ctx.w) / dst_w);
        final_pixels[static_cast<size_t>(y) * dst_w + x] =
            full[static_cast<size_t>(sy) * ctx.w + sx];
      }
    }
    free(full);
  }

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(alloc_media_cover_memory(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(final_pixels);
    return nullptr;
  }

  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  dsc->header.w = dst_w;
  dsc->header.h = dst_h;
  dsc->header.stride = dst_w * 2;
  dsc->data_size = final_bytes;
  dsc->data = reinterpret_cast<const uint8_t*>(final_pixels);

  Serial.printf("[MediaCover] MQTT JPEG decoded: %ux%u -> %ux%u, %u bytes\n",
                static_cast<unsigned>(ctx.w),
                static_cast<unsigned>(ctx.h),
                static_cast<unsigned>(dst_w),
                static_cast<unsigned>(dst_h),
                static_cast<unsigned>(final_bytes));
  return dsc;
}

// Copy pixels from a completed cover descriptor. Each tile still owns its
// buffers, so existing cleanup paths work without reference counting.
static lv_image_dsc_t* clone_media_cover_dsc(const lv_image_dsc_t* source) {
  if (!source || !source->data || !source->data_size) return nullptr;
  uint8_t* pixels = static_cast<uint8_t*>(
      heap_caps_malloc(source->data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) return nullptr;
  memcpy(pixels, source->data, source->data_size);
  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(
      alloc_media_cover_memory(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(pixels);
    return nullptr;
  }
  *dsc = *source;
  dsc->data = pixels;
  return dsc;
}

// For media tiles sharing a source, return another tile's fully decoded cover
// reference if its hash matches. The caller clones the pixels instead of
// repeating base64 decoding, JPEG decoding and scaling.
static const MediaCoverRef* find_decoded_media_cover_sibling(const MediaCoverRef* self,
                                                             uint32_t hash) {
  if (!hash) return nullptr;
  MediaTileWidgets* const grids[] = {
      g_tab0_media, g_tab1_media, g_tab2_media, g_screensaver_media};
  for (MediaTileWidgets* grid : grids) {
    for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
      const MediaCoverRef* ref = grid[i].cover_ref;
      if (!ref || ref == self) continue;
      if (ref->url_hash == hash && ref->dsc) return ref;
    }
  }
  return nullptr;
}

static lv_image_dsc_t* make_media_cover_raw_dsc(const uint8_t* data, size_t len) {
  if (!data || len < 32) return nullptr;

  uint16_t img_w = 0;
  uint16_t img_h = 0;
  const bool is_png = read_media_cover_png_size(data, len, img_w, img_h);
  const bool is_jpeg = !is_png && read_media_cover_jpeg_size(data, len, img_w, img_h);
  if (!is_png && !is_jpeg) return nullptr;

  uint8_t* copy = static_cast<uint8_t*>(alloc_media_cover_memory(len, true));
  if (!copy) return nullptr;
  memcpy(copy, data, len);

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(alloc_media_cover_memory(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(copy);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = is_png ? LV_COLOR_FORMAT_RAW_ALPHA : LV_COLOR_FORMAT_RAW;
  dsc->header.w = img_w;
  dsc->header.h = img_h;
  dsc->header.stride = 0;
  dsc->data_size = len;
  dsc->data = copy;
  return dsc;
}

static lv_image_dsc_t* make_media_cover_dsc_from_bytes(const uint8_t* data, size_t len) {
  if (!data || len < 32) return nullptr;
  if (is_media_cover_jpeg(data, len)) {
#if defined(CONFIG_IDF_TARGET_ESP32P4) && SOC_JPEG_DECODE_SUPPORTED
    if (lv_image_dsc_t* hardware = make_media_cover_decoded_jpeg_hw_dsc(data, len)) {
      return hardware;
    }
    Serial.println("[MediaCover] HW JPEG unavailable, using software fallback");
#endif
    return make_media_cover_decoded_jpeg_dsc(data, len);
  }
  if (is_media_cover_png(data, len)) {
    lv_image_dsc_t* dsc = make_media_cover_raw_dsc(data, len);
    if (dsc) {
      Serial.printf("[MediaCover] MQTT PNG passed to LVGL decoder: %ux%u\n",
                    static_cast<unsigned>(dsc->header.w),
                    static_cast<unsigned>(dsc->header.h));
    }
    return dsc;
  }
  Serial.printf("[MediaCover] Unknown MQTT image format: %02X %02X %02X %02X\n",
                data[0], data[1], data[2], data[3]);
  return nullptr;
}

static lv_image_dsc_t* make_media_cover_dsc_from_base64(const String& encoded) {
  if (!encoded.length()) return nullptr;
  const uint32_t started_ms = millis();
  const size_t cap = base64_decode_expected_len(encoded.length()) + 4;
  if (cap < 32 || cap > (96U * 1024U)) return nullptr;

  uint8_t* decoded = static_cast<uint8_t*>(alloc_media_cover_memory(cap, true));
  if (!decoded) return nullptr;

  base64_decodestate state;
  base64_init_decodestate(&state);
  const int decoded_len = base64_decode_block(
      encoded.c_str(),
      static_cast<int>(encoded.length()),
      reinterpret_cast<char*>(decoded),
      &state);

  if (decoded_len < 32) {
    free(decoded);
    return nullptr;
  }

  Serial.printf("[MediaCover] MQTT Cover Base64: encoded=%u decoded=%d bytes magic=%02X %02X %02X %02X\n",
                static_cast<unsigned>(encoded.length()),
                decoded_len,
                decoded[0], decoded[1], decoded[2], decoded[3]);

  const uint32_t base64_done_ms = millis();
  lv_image_dsc_t* dsc = make_media_cover_dsc_from_bytes(decoded, static_cast<size_t>(decoded_len));
  free(decoded);
  Serial.printf("[MediaCover] Processing: base64=%u ms image=%u ms total=%u ms\n",
                static_cast<unsigned>(base64_done_ms - started_ms),
                static_cast<unsigned>(millis() - base64_done_ms),
                static_cast<unsigned>(millis() - started_ms));
  return dsc;
}

static uint8_t* alloc_media_cover_download_buffer(size_t bytes) {
  return static_cast<uint8_t*>(alloc_media_cover_memory(bytes, true));
}

static bool media_cover_network_budget_ok() {
  constexpr size_t kMinInternalFree = 96U * 1024U;
  constexpr size_t kMinInternalLargest = 24U * 1024U;
  const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return free_internal >= kMinInternalFree && largest_internal >= kMinInternalLargest;
}

static bool media_cover_download_allowed(const String& url) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (url.startsWith("https://")) return false;
#endif
  return true;
}

static void log_media_cover_download_blocked() {
  static uint32_t last_log_ms = 0;
  const uint32_t now = millis();
  if (last_log_ms != 0 && static_cast<uint32_t>(now - last_log_ms) < 30000) return;
  last_log_ms = now;
  Serial.println("[MediaCover] HTTPS cover download skipped on ESP32-P4 (Wi-Fi SDIO stability)");
}

static lv_image_dsc_t* download_media_cover_jpeg(const String& url, bool* deferred) {
  if (deferred) *deferred = false;
  if (!url.startsWith("http://") && !url.startsWith("https://")) return nullptr;
  if (!media_cover_download_allowed(url)) {
    log_media_cover_download_blocked();
    return nullptr;
  }
  if (webAdminRecentlyActive(3500) || !media_cover_network_budget_ok()) {
    if (deferred) *deferred = true;
    return nullptr;
  }

  constexpr size_t kMaxDownloadBytes = 96U * 1024U;
  constexpr size_t kReadChunkBytes = 512U;
  Serial.printf("[MediaCover] Downloading: %s\n", url.c_str());
  HTTPClient http;
  http.setTimeout(1200);
  http.setReuse(false);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  NetworkClient plain_client;
  NetworkClientSecure secure_client;
  bool ok_begin = false;
  if (url.startsWith("https://")) {
    secure_client.setInsecure();
    ok_begin = http.begin(secure_client, url);
  } else {
    ok_begin = http.begin(plain_client, url);
  }
  if (!ok_begin) return nullptr;
  http.addHeader("Connection", "close");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[MediaCover] HTTP %d for cover\n", code);
    http.end();
    plain_client.stop();
    secure_client.stop();
    return nullptr;
  }

  int content_len = http.getSize();
  if (content_len > 0 && static_cast<size_t>(content_len) > kMaxDownloadBytes) {
    Serial.printf("[MediaCover] Cover too large: %d bytes\n", content_len);
    http.end();
    plain_client.stop();
    secure_client.stop();
    return nullptr;
  }

  size_t buffer_cap = (content_len > 0) ? static_cast<size_t>(content_len) : kMaxDownloadBytes;
  if (buffer_cap < 1024) buffer_cap = 1024;
  uint8_t* data = alloc_media_cover_download_buffer(buffer_cap);
  if (!data) {
    Serial.println("[MediaCover] Download buffer missing");
    http.end();
    plain_client.stop();
    secure_client.stop();
    return nullptr;
  }

  NetworkClient* stream = http.getStreamPtr();
  size_t total = 0;
  uint32_t deadline = millis() + 2200;
  while (http.connected() && millis() < deadline) {
    if (webAdminRecentlyActive(1000)) {
      if (deferred) *deferred = true;
      free(data);
      http.end();
      plain_client.stop();
      secure_client.stop();
      delay(20);
      return nullptr;
    }
    int available = stream ? stream->available() : 0;
    if (available > 0) {
      size_t room = buffer_cap - total;
      if (!room) {
        Serial.println("[MediaCover] Cover download limit reached");
        free(data);
        http.end();
        plain_client.stop();
        secure_client.stop();
        return nullptr;
      }
      size_t to_read = static_cast<size_t>(available);
      if (to_read > room) to_read = room;
      if (to_read > kReadChunkBytes) to_read = kReadChunkBytes;
      int read_bytes = stream->readBytes(data + total, to_read);
      if (read_bytes > 0) {
        total += static_cast<size_t>(read_bytes);
        deadline = millis() + 900;
        delay(2);
        yield();
      }
    } else {
      if (content_len >= 0 && total >= static_cast<size_t>(content_len)) break;
      delay(5);
      yield();
    }
  }
  http.end();
  plain_client.stop();
  secure_client.stop();
  delay(20);

  if (total < 32) {
    Serial.printf("[MediaCover] Insufficient data: %u bytes\n", static_cast<unsigned>(total));
    free(data);
    return nullptr;
  }

  lv_image_dsc_t* dsc = nullptr;
  if (is_media_cover_jpeg(data, total)) {
    dsc = make_media_cover_raw_dsc(data, total);
  } else if (is_media_cover_png(data, total)) {
    dsc = make_media_cover_raw_dsc(data, total);
    if (dsc) {
      Serial.printf("[MediaCover] PNG passed to LVGL decoder: %ux%u\n",
                    static_cast<unsigned>(dsc->header.w),
                    static_cast<unsigned>(dsc->header.h));
    }
  } else {
    Serial.printf("[MediaCover] Unknown image format: %02X %02X %02X %02X\n",
                  data[0], data[1], data[2], data[3]);
  }
  if (dsc) {
    if (dsc->header.w && dsc->header.h) {
      Serial.printf("[MediaCover] OK: %ux%u, %u bytes\n",
                    static_cast<unsigned>(dsc->header.w),
                    static_cast<unsigned>(dsc->header.h),
                    static_cast<unsigned>(total));
    } else {
      Serial.printf("[MediaCover] OK: RAW, %u bytes\n", static_cast<unsigned>(total));
    }
  }
  free(data);
  return dsc;
}

static bool media_cover_has_hidden_ancestor(lv_obj_t* obj) {
  obj = obj ? lv_obj_get_parent(obj) : nullptr;
  while (obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return true;
    obj = lv_obj_get_parent(obj);
  }
  return false;
}

static bool media_obj_is_visible(lv_obj_t* obj) {
  return obj &&
         !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) &&
         !media_cover_has_hidden_ancestor(obj);
}

static bool media_widgets_are_visible(const MediaTileWidgets& widgets) {
  return media_obj_is_visible(widgets.cover_clip) ||
         media_obj_is_visible(widgets.cover_image) ||
         media_obj_is_visible(widgets.media_title_label) ||
         media_obj_is_visible(widgets.media_subtitle_label) ||
         media_obj_is_visible(widgets.play_pause_label);
}

static bool set_media_cover_visible(MediaTileWidgets& widgets, bool visible) {
  bool changed = false;
  if (widgets.cover_clip) {
    const bool hidden = lv_obj_has_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN);
    if (visible && hidden) {
      lv_obj_clear_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN);
      changed = true;
    } else if (!visible && !hidden) {
      lv_obj_add_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN);
      changed = true;
    }
  }
  if (widgets.cover_image) {
    const bool hidden = lv_obj_has_flag(widgets.cover_image, LV_OBJ_FLAG_HIDDEN);
    if (visible && hidden) {
      lv_obj_clear_flag(widgets.cover_image, LV_OBJ_FLAG_HIDDEN);
      changed = true;
    } else if (!visible && !hidden) {
      lv_obj_add_flag(widgets.cover_image, LV_OBJ_FLAG_HIDDEN);
      changed = true;
    }
  }
  return changed;
}

struct MediaCoverRequest {
  GridType grid_type;
  uint8_t grid_index = 0;
  uint32_t url_hash = 0;
  char url[512] = {};
};

struct MediaCoverResult {
  GridType grid_type;
  uint8_t grid_index = 0;
  uint32_t url_hash = 0;
  char url[512] = {};
  lv_image_dsc_t* dsc = nullptr;
  bool ok = false;
  bool deferred = false;
};

static QueueHandle_t g_media_cover_request_queue = nullptr;
static QueueHandle_t g_media_cover_result_queue = nullptr;
static TaskHandle_t g_media_cover_task = nullptr;
static uint32_t g_media_cover_request_full_count = 0;
static uint32_t g_media_cover_result_full_count = 0;
static uint32_t g_media_cover_last_download_ms = 0;
static constexpr bool kMediaCoverDownloadsEnabled = true;
static constexpr uint32_t kMediaCoverRetryCooldownMs = 10000;

static void media_cover_worker_task(void*) {
  for (;;) {
    MediaCoverRequest req{};
    if (xQueueReceive(g_media_cover_request_queue, &req, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    MediaCoverResult result{};
    result.grid_type = req.grid_type;
    result.grid_index = req.grid_index;
    result.url_hash = req.url_hash;
    strncpy(result.url, req.url, sizeof(result.url) - 1);
    result.url[sizeof(result.url) - 1] = '\0';
    while (webAdminRecentlyActive(3500)) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    const uint32_t now = millis();
    if (g_media_cover_last_download_ms != 0 &&
        static_cast<uint32_t>(now - g_media_cover_last_download_ms) < 1500) {
      vTaskDelay(pdMS_TO_TICKS(1500 - static_cast<uint32_t>(now - g_media_cover_last_download_ms)));
    }
    bool deferred = false;
    result.dsc = download_media_cover_jpeg(String(req.url), &deferred);
    result.ok = result.dsc != nullptr;
    result.deferred = deferred;
    g_media_cover_last_download_ms = millis();

    if (!g_media_cover_result_queue ||
        xQueueSend(g_media_cover_result_queue, &result, 0) != pdTRUE) {
      if ((g_media_cover_result_full_count++ % 5) == 0) {
        Serial.println("[MediaCover] Result queue full, cover dropped");
      }
      // Free inline: free_media_cover_dsc() calls lv_image_cache_drop(), an
      // LVGL API that must not run on the worker task. This descriptor was
      // never an image source and therefore has no cache entry.
      if (result.dsc) {
        if (result.dsc->data) free(const_cast<uint8_t*>(result.dsc->data));
        free(result.dsc);
        result.dsc = nullptr;
      }
    }
  }
}

static bool ensure_media_cover_worker() {
  if (!g_media_cover_request_queue) {
    g_media_cover_request_queue = xQueueCreate(1, sizeof(MediaCoverRequest));
  }
  if (!g_media_cover_result_queue) {
    g_media_cover_result_queue = xQueueCreate(1, sizeof(MediaCoverResult));
  }
  if (!g_media_cover_request_queue || !g_media_cover_result_queue) {
    return false;
  }
  if (!g_media_cover_task) {
    BaseType_t ok = xTaskCreateWithCaps(
        media_cover_worker_task,
        "media_cover",
        16384,
        nullptr,
        1,
        &g_media_cover_task,
        MALLOC_CAP_SPIRAM);
    if (ok != pdPASS) {
      g_media_cover_task = nullptr;
      return false;
    }
  }
  return true;
}

static void queue_media_cover_request(GridType grid_type,
                                      uint8_t grid_index,
                                      MediaCoverRef* ref,
                                      const String& url,
                                      uint32_t hash) {
  if (!ref || !url.length()) return;
  if (ref->requested_url_hash == hash) return;
  if (!media_cover_download_allowed(url)) {
    ref->requested_url_hash = 0;
    log_media_cover_download_blocked();
    return;
  }
  if (!ensure_media_cover_worker()) {
    if ((g_media_cover_request_full_count++ % 5) == 0) {
      Serial.println("[MediaCover] Worker could not be started");
    }
    return;
  }

  MediaCoverRequest req{};
  req.grid_type = grid_type;
  req.grid_index = grid_index;
  req.url_hash = hash;
  strncpy(req.url, url.c_str(), sizeof(req.url) - 1);
  req.url[sizeof(req.url) - 1] = '\0';

  if (xQueueSend(g_media_cover_request_queue, &req, 0) != pdTRUE) {
    if ((g_media_cover_request_full_count++ % 5) == 0) {
      Serial.println("[MediaCover] Request queue full, cover will be retried later");
    }
    return;
  }

  ref->requested_url_hash = hash;
}

static void process_media_cover_results() {
  if (!g_media_cover_result_queue) return;

  MediaCoverResult result{};
  for (uint8_t processed = 0; processed < 1 &&
       xQueueReceive(g_media_cover_result_queue, &result, 0) == pdTRUE; ++processed) {
    MediaTileWidgets* target = tile_renderer_get_media_widgets(result.grid_type);
    if (!target || result.grid_index >= TILES_PER_GRID) {
      free_media_cover_dsc(result.dsc);
      continue;
    }

    MediaTileWidgets& widgets = target[result.grid_index];
    MediaCoverRef* ref = widgets.cover_ref;
    if (!widgets.cover_image || !ref || ref->requested_url_hash != result.url_hash) {
      free_media_cover_dsc(result.dsc);
      continue;
    }

    ref->requested_url_hash = 0;
    if (result.deferred) {
      if (result.url[0]) {
        queue_media_cover_request(result.grid_type, result.grid_index, ref, String(result.url), result.url_hash);
      }
      free_media_cover_dsc(result.dsc);
      continue;
    }
    if (!result.ok || !result.dsc) {
      ref->failed_url_hash = result.url_hash;
      ref->failed_at_ms = millis();
      // A failed replacement does not invalidate the displayed artwork.
      // Keep the tile and popup stable while the existing retry path runs.
      free_media_cover_dsc(result.dsc);
      continue;
    }

    lv_image_dsc_t* old = ref->dsc;
    lv_image_dsc_t* old_popup = ref->popup_dsc;
    lv_image_dsc_t* tile_dsc = make_media_tile_cover_dsc(result.dsc, lv_obj_get_style_width(widgets.cover_clip, LV_PART_MAIN));
    lv_image_set_src(widgets.cover_image, tile_dsc ? tile_dsc : result.dsc);
    const bool cover_visibility_changed = set_media_cover_visible(widgets, true);
    if (widgets.icon_label) lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
    if (cover_visibility_changed) {
      set_media_cover_text_layout(widgets, true);
    }

    ref->dsc = tile_dsc ? tile_dsc : result.dsc;
    ref->popup_dsc = tile_dsc ? result.dsc : nullptr;
    ref->url_hash = result.url_hash;
    ref->embedded_url_hash = 0;
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
    {
      String entity_id = media_entity_for_grid_index(result.grid_type, result.grid_index);
      if (entity_id.length()) {
        update_media_popup_cover(entity_id.c_str(), ref->popup_dsc ? ref->popup_dsc : ref->dsc, ref->url_hash);
      }
    }
    result.dsc = nullptr;
    free_media_cover_dsc(old);
    free_media_cover_dsc(old_popup);
  }
}

static void update_media_cover(GridType grid_type,
                               uint8_t grid_index,
                               MediaTileWidgets& widgets,
                               const String& raw_url) {
  String url = raw_url;
  url.trim();
  if (!widgets.cover_image || !widgets.cover_ref) return;
  MediaCoverRef* ref = widgets.cover_ref;

  if (!url.length()) {
    ref->source_url = "";
    ref->embedded_url_hash = 0;
    ref->requested_url_hash = 0;
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
    const bool cover_visibility_changed = set_media_cover_visible(widgets, false);
    if (widgets.icon_label) lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
    if (cover_visibility_changed) {
      set_media_cover_text_layout(widgets, false);
    }
    return;
  }

  uint32_t hash = fnv1a_hash(url.c_str());
  if (ref->source_url != url) {
    ref->source_url = url;
    // Reject an older result even if the new request cannot be queued yet.
    ref->requested_url_hash = 0;
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
  }

  if (!kMediaCoverDownloadsEnabled) {
    ref->requested_url_hash = 0;
    return;
  }

  if (ref->dsc && (ref->url_hash == hash || ref->embedded_url_hash == hash)) {
    ref->requested_url_hash = 0;
    const bool cover_visibility_changed = set_media_cover_visible(widgets, true);
    if (widgets.icon_label) lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
    if (cover_visibility_changed) {
      set_media_cover_text_layout(widgets, true);
    }
    return;
  }
  if (!media_cover_download_allowed(url)) {
    ref->requested_url_hash = 0;
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
    // state_fast carries URLs before the Bridge sends MQTT pixels. P4's
    // HTTPS restriction must never hide an already displayed MQTT cover.
    log_media_cover_download_blocked();
    return;
  }
  if (ref->failed_url_hash == hash) {
    const uint32_t failed_age = static_cast<uint32_t>(millis() - ref->failed_at_ms);
    if (ref->failed_at_ms != 0 && failed_age < kMediaCoverRetryCooldownMs) return;
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
  }

  // If another tile has already downloaded and decoded this URL, clone its
  // pixels instead of starting another HTTP download.
  if (const MediaCoverRef* sibling = find_decoded_media_cover_sibling(ref, hash)) {
    lv_image_dsc_t* cloned_popup = clone_media_cover_dsc(sibling->popup_dsc ? sibling->popup_dsc : sibling->dsc);
    lv_image_dsc_t* cloned = cloned_popup ? make_media_tile_cover_dsc(cloned_popup, lv_obj_get_style_width(widgets.cover_clip, LV_PART_MAIN)) : nullptr;
    if (cloned_popup && !cloned) { cloned = cloned_popup; cloned_popup = nullptr; }
    if (cloned) {
      lv_image_dsc_t* old = ref->dsc;
      lv_image_dsc_t* old_popup = ref->popup_dsc;
      lv_image_set_src(widgets.cover_image, cloned);
      const bool cover_visibility_changed = set_media_cover_visible(widgets, true);
      if (widgets.icon_label) lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
      if (cover_visibility_changed) {
        set_media_cover_text_layout(widgets, true);
      }
      ref->dsc = cloned;
      ref->popup_dsc = cloned_popup;
      ref->source_url = url;
      ref->url_hash = hash;
      ref->embedded_url_hash = 0;
      ref->requested_url_hash = 0;
      ref->failed_url_hash = 0;
      ref->failed_at_ms = 0;
      free_media_cover_dsc(old);
      free_media_cover_dsc(old_popup);
      Serial.println("[MediaCover] Cover adopted from neighboring tile (no download)");
      return;
    }
    free_media_cover_dsc(cloned);
    free_media_cover_dsc(cloned_popup);
  }

  if (media_cover_has_hidden_ancestor(widgets.cover_clip ? widgets.cover_clip : widgets.cover_image)) return;

  queue_media_cover_request(grid_type, grid_index, ref, url, hash);
}

static bool update_media_cover_from_base64(MediaTileWidgets& widgets, const String& raw_data,
                                         const String& source_url = String()) {
  String encoded = raw_data;
  encoded.trim();
  if (!widgets.cover_image || !widgets.cover_ref || !encoded.length()) return false;

  MediaCoverRef* ref = widgets.cover_ref;
  const uint32_t hash = fnv1a_hash(encoded.c_str());
  if (ref->url_hash == hash && ref->dsc) {
    ref->source_url = "mqtt";
    ref->embedded_url_hash = source_url.length() ? fnv1a_hash(source_url.c_str()) : 0;
    ref->requested_url_hash = 0;
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
    const bool cover_visibility_changed = set_media_cover_visible(widgets, true);
    if (widgets.icon_label) lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
    if (cover_visibility_changed) {
      set_media_cover_text_layout(widgets, true);
    }
    return true;
  }

  // If another media tile shows this source, clone its already decoded and
  // scaled cover pixels instead of repeating the work.
  lv_image_dsc_t* dsc = nullptr;
  lv_image_dsc_t* popup_dsc = nullptr;
  uint32_t tile_scale_ms = 0;
  bool adopted_from_sibling = false;
  if (const MediaCoverRef* sibling = find_decoded_media_cover_sibling(ref, hash)) {
    const auto* full = sibling->popup_dsc ? sibling->popup_dsc : sibling->dsc;
    popup_dsc = clone_media_cover_dsc(full);
    if (popup_dsc) {
      dsc = make_media_tile_cover_dsc(popup_dsc, lv_obj_get_style_width(widgets.cover_clip, LV_PART_MAIN));
      if (!dsc) { dsc = popup_dsc; popup_dsc = nullptr; }
    }
    if (dsc) {
      adopted_from_sibling = true;
    } else {
      free_media_cover_dsc(dsc);
      free_media_cover_dsc(popup_dsc);
    }
  }

  if (!adopted_from_sibling) {
    lv_image_dsc_t* decoded_dsc = make_media_cover_dsc_from_base64(encoded);
    if (!decoded_dsc) {
      Serial.println("[MediaCover] MQTT cover could not be decoded");
      return false;
    }

    // Prepare the actual tile size once. Larger tiles use the existing 240px
    // source; smaller tiles retain their cheap, pre-scaled redraw path.
    const uint32_t tile_scale_started_ms = millis();
    lv_image_dsc_t* tile_dsc = make_media_tile_cover_dsc(decoded_dsc, lv_obj_get_style_width(widgets.cover_clip, LV_PART_MAIN));
    tile_scale_ms = millis() - tile_scale_started_ms;
    dsc = decoded_dsc;
    if (tile_dsc) {
      dsc = tile_dsc;
      popup_dsc = decoded_dsc;
    }
  }

  lv_image_dsc_t* old = ref->dsc;
  lv_image_dsc_t* old_popup = ref->popup_dsc;
  lv_image_set_src(widgets.cover_image, dsc);
  const bool cover_visibility_changed = set_media_cover_visible(widgets, true);
  if (widgets.icon_label) lv_obj_clear_flag(widgets.icon_label, LV_OBJ_FLAG_HIDDEN);
  if (cover_visibility_changed) {
    set_media_cover_text_layout(widgets, true);
  }

  ref->dsc = dsc;
  ref->popup_dsc = popup_dsc;
  ref->source_url = "mqtt";
  ref->url_hash = hash;
  ref->embedded_url_hash = source_url.length() ? fnv1a_hash(source_url.c_str()) : 0;
  ref->requested_url_hash = 0;
  ref->failed_url_hash = 0;
  ref->failed_at_ms = 0;
  free_media_cover_dsc(old);
  free_media_cover_dsc(old_popup);
  if (adopted_from_sibling) {
    Serial.println("[MediaCover] MQTT cover adopted from neighboring tile (no re-decode)");
  } else {
    Serial.printf("[MediaCover] MQTT cover loaded (tile scaling=%u ms)\n",
                  static_cast<unsigned>(tile_scale_ms));
  }
  return true;
}

static void retry_failed_media_covers_for_grid(GridType grid_type, MediaTileWidgets* target) {
  if (!target) return;
  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    MediaTileWidgets& widgets = target[i];
    MediaCoverRef* ref = widgets.cover_ref;
    if (!ref || !widgets.cover_image || ref->requested_url_hash != 0) continue;
    if (ref->failed_url_hash == 0 || ref->failed_at_ms == 0 || !ref->source_url.length()) continue;

    const uint32_t failed_age = static_cast<uint32_t>(millis() - ref->failed_at_ms);
    if (failed_age < kMediaCoverRetryCooldownMs) continue;
    if (media_cover_has_hidden_ancestor(widgets.cover_clip ? widgets.cover_clip : widgets.cover_image)) continue;
    if (!media_cover_download_allowed(ref->source_url)) {
      ref->failed_url_hash = 0;
      ref->failed_at_ms = 0;
      log_media_cover_download_blocked();
      continue;
    }

    const uint32_t hash = fnv1a_hash(ref->source_url.c_str());
    ref->failed_url_hash = 0;
    ref->failed_at_ms = 0;
    queue_media_cover_request(grid_type, i, ref, ref->source_url, hash);
  }
}

static void process_pending_media_cover_retries() {
  retry_failed_media_covers_for_grid(GridType::TAB0, g_tab0_media);
  retry_failed_media_covers_for_grid(GridType::TAB1, g_tab1_media);
  retry_failed_media_covers_for_grid(GridType::TAB2, g_tab2_media);
  retry_failed_media_covers_for_grid(GridType::SCREENSAVER,
                                     g_screensaver_media);
}

void update_media_tile_state(GridType grid_type, uint8_t grid_index, const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;
  MediaTileWidgets* target = tile_renderer_get_media_widgets(grid_type);

  MediaTileWidgets& widgets = target[grid_index];
  if (!widgets.icon_label && !widgets.media_title_label &&
      !widgets.media_subtitle_label && !widgets.state_label) {
    return;
  }

  const char* payload_start = payload;
  while (*payload_start == ' ' || *payload_start == '\t' ||
         *payload_start == '\r' || *payload_start == '\n') {
    ++payload_start;
  }
  if (!*payload_start) return;

  const uint32_t payload_hash = fnv1a_hash(payload_start);
  if (widgets.last_payload_hash == payload_hash) return;
  widgets.last_payload_hash = payload_hash;

  String state;
  String title;
  String artist;
  String album;
  String app;
  String source;
  String channel;
  float volume_level = 0.0f;
  bool has_volume_level = false;
  float media_position = 0.0f;
  float media_duration = 0.0f;
  bool has_media_position = false;
  bool has_media_duration = false;
  bool is_muted = false;
  bool has_muted = false;

  const bool is_json_payload = *payload_start == '{';
  if (is_json_payload) {
    extract_json_string_field_cstr(payload_start, "state", state);
    extract_json_string_field_cstr(payload_start, "media_title", title);
    extract_json_string_field_cstr(payload_start, "media_artist", artist);
    extract_json_string_field_cstr(payload_start, "media_album_name", album);
    extract_json_string_field_cstr(payload_start, "app_name", app);
    extract_json_string_field_cstr(payload_start, "source", source);
    extract_json_string_field_cstr(payload_start, "media_channel", channel);
    has_volume_level = extract_json_number_field_cstr(payload_start, "volume_level", volume_level);
    if (!has_volume_level) {
      has_volume_level = extract_json_number_field_cstr(payload_start, "volume", volume_level);
    }
    has_media_position = extract_json_number_field_cstr(payload_start, "media_position", media_position);
    has_media_duration = extract_json_number_field_cstr(payload_start, "media_duration", media_duration);
    has_muted = extract_json_bool_field_cstr(payload_start, "is_volume_muted", is_muted);
    if (!has_muted) {
      has_muted = extract_json_bool_field_cstr(payload_start, "muted", is_muted);
    }
  } else {
    state = payload_start;
    state.trim();
  }

  decode_basic_json_escapes(state);
  decode_basic_json_escapes(title);
  decode_basic_json_escapes(artist);
  decode_basic_json_escapes(album);
  decode_basic_json_escapes(app);
  decode_basic_json_escapes(source);
  decode_basic_json_escapes(channel);
  sanitize_media_display_text(title);
  sanitize_media_display_text(artist);
  sanitize_media_display_text(album);
  sanitize_media_display_text(app);
  sanitize_media_display_text(source);
  sanitize_media_display_text(channel);

  if (has_volume_level) {
    if (volume_level > 1.0f && volume_level <= 100.0f) {
      volume_level /= 100.0f;
    }
    if (volume_level < 0.0f) volume_level = 0.0f;
    if (volume_level > 1.0f) volume_level = 1.0f;
  }
  widgets.has_media_volume = has_volume_level;
  widgets.media_volume_level = has_volume_level ? volume_level : 0.0f;
  if (has_muted) widgets.media_is_muted = is_muted;
  widgets.has_media_position = has_media_position && has_media_duration && media_duration > 0.0f;
  widgets.media_position = widgets.has_media_position ? media_position : 0.0f;
  widgets.media_duration = widgets.has_media_position ? media_duration : 0.0f;
  widgets.media_position_received_ms = widgets.has_media_position ? millis() : 0;

  String main_text = media_first_non_empty(title, channel);
  const bool has_real_media_title = main_text.length() > 0;
  if (!main_text.length()) {
    main_text = media_empty_title_label(state);
  }
  String subtitle = media_first_non_empty(artist, album, app, source);
  if (media_text_same(subtitle, main_text)) {
    subtitle = "";
  }
  String media_text_key = main_text;
  media_text_key += '\x1f';
  media_text_key += subtitle;
  const uint32_t media_text_hash = fnv1a_hash(media_text_key.c_str());
  widgets.last_media_text_hash = media_text_hash;

  // Artwork is independent of title/state. Lightweight payloads omit it;
  // explicit empty/null fields clear it, and URL changes must always apply.
  const bool should_update_cover = is_json_payload && media_artwork::has_fields(payload_start);
  if (widgets.play_pause_label) {
    String icon = getMdiChar(media_icon_for_state(state));
    if (icon.length()) {
      set_label_text_if_changed(widgets.play_pause_label, icon);
      lv_obj_clear_flag(widgets.play_pause_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  bool text_layout_dirty = false;
  bool title_text_changed = false;
  bool subtitle_text_changed = false;
  if (widgets.media_title_label) {
    text_layout_dirty |= set_label_long_mode_if_changed(widgets.media_title_label,
                                                        has_real_media_title ? LV_LABEL_LONG_SCROLL : LV_LABEL_LONG_DOT);
    title_text_changed = set_label_text_if_changed(widgets.media_title_label, main_text);
    text_layout_dirty |= title_text_changed;
  }
  if (widgets.media_subtitle_label) {
    const bool was_hidden = lv_obj_has_flag(widgets.media_subtitle_label, LV_OBJ_FLAG_HIDDEN);
    if (subtitle.length()) {
      text_layout_dirty |= set_label_long_mode_if_changed(widgets.media_subtitle_label, LV_LABEL_LONG_SCROLL);
      subtitle_text_changed = set_label_text_if_changed(widgets.media_subtitle_label, subtitle);
      text_layout_dirty |= subtitle_text_changed;
      lv_obj_clear_flag(widgets.media_subtitle_label, LV_OBJ_FLAG_HIDDEN);
      if (was_hidden) text_layout_dirty = true;
    } else {
      text_layout_dirty |= set_label_text_if_changed(widgets.media_subtitle_label, "");
      lv_obj_add_flag(widgets.media_subtitle_label, LV_OBJ_FLAG_HIDDEN);
      if (!was_hidden) text_layout_dirty = true;
    }
    if (text_layout_dirty) {
      set_media_cover_text_layout(widgets, widgets.cover_clip && !lv_obj_has_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN));
    }
  }
  if (title_text_changed) restart_visible_media_text_scroll(widgets.media_title_label);
  if (subtitle_text_changed) restart_visible_media_text_scroll(widgets.media_subtitle_label);
  if (widgets.state_label) {
    set_label_text_if_changed(widgets.state_label, "");
    lv_obj_add_flag(widgets.state_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (should_update_cover) {
    String cover_url;
    String cover_data;
    if (!media_artwork::read_string(payload_start, "entity_picture", cover_url)) {
      media_artwork::read_string(payload_start, "media_image_url", cover_url);
    }
    media_artwork::read_string(payload_start, "entity_picture_data", cover_data);
    decode_basic_json_escapes(cover_url);
    decode_basic_json_escapes(cover_data);

    if (cover_data.length()) {
      update_media_cover_from_base64(widgets, cover_data, cover_url);
    } else {
      update_media_cover(grid_type, grid_index, widgets, cover_url);
    }
  }

  update_media_popup_from_widgets(grid_type, grid_index, widgets, state);
}

void queue_media_tile_update(GridType grid_type, uint8_t grid_index, const char* payload) {
  if (grid_index >= TILES_PER_GRID || !payload) return;

  uint8_t idx = g_media_tail;
  while (idx != g_media_head) {
    MediaUpdate& pending = g_media_queue[idx];
    if (pending.valid &&
        pending.grid_type == grid_type &&
        pending.grid_index == grid_index) {
      if (pending.payload.equals(payload)) return;
      pending.payload = String(payload);
      return;
    }
    idx = (idx + 1) % MEDIA_QUEUE_SIZE;
  }

  uint8_t next_head = (g_media_head + 1) % MEDIA_QUEUE_SIZE;
  if (next_head == g_media_tail) {
    if ((g_media_overflow_count++ % 10) == 0) {
      Serial.println("[Queue] FULL! Oldest media update will be overwritten");
    }
    g_media_tail = (g_media_tail + 1) % MEDIA_QUEUE_SIZE;
  }

  g_media_queue[g_media_head].grid_type = grid_type;
  g_media_queue[g_media_head].grid_index = grid_index;
  g_media_queue[g_media_head].payload = String(payload);
  g_media_queue[g_media_head].valid = true;
  g_media_head = next_head;
}

static void process_media_state_updates(uint8_t max_updates) {
  uint8_t processed = 0;
  while (g_media_tail != g_media_head && (max_updates == 0 || processed < max_updates)) {
    MediaUpdate& upd = g_media_queue[g_media_tail];
    if (upd.valid) {
      update_media_tile_state(upd.grid_type, upd.grid_index, upd.payload.c_str());
      upd.valid = false;
      ++processed;
    }
    g_media_tail = (g_media_tail + 1) % MEDIA_QUEUE_SIZE;
  }
}

void process_media_update_queue(uint8_t max_updates) {
  process_media_state_updates(max_updates);
  process_media_cover_results();
  process_pending_media_cover_retries();
}

void process_idle_media_updates() {
  static uint32_t last_batch_ms = 0;
  const bool pending = g_media_tail != g_media_head ||
      (g_media_cover_result_queue && uxQueueMessagesWaiting(g_media_cover_result_queue) != 0);
  if (media_updates::idle_batch_due(pending, millis(), last_batch_ms)) {
    // The result queue has capacity one. Prioritize it, otherwise consume one
    // state. Retry scans keep their existing cadence in the normal batches.
    if (g_media_cover_result_queue && uxQueueMessagesWaiting(g_media_cover_result_queue) != 0)
      process_media_cover_results();
    else process_media_state_updates(1);
  }
}

/* === Thread-safe tile graph history queue (MQTT -> main loop) === */
struct TileGraphHistoryUpdate {
  String entity_id;
  String payload;
  bool valid = false;
};

static TileGraphHistoryUpdate g_tile_graph_history;

static bool extract_numeric_for_chart(const String& text, float& out) {
  String normalized = text;
  normalized.trim();
  if (!normalized.length()) return false;
  normalized.replace(",", ".");
  char* end = nullptr;
  float f = strtof(normalized.c_str(), &end);
  if (!end || end == normalized.c_str()) return false;
  if (isnan(f) || isinf(f)) return false;
  out = f;
  return true;
}

static void apply_tile_graph_history(const char* target_entity, const char* payload) {
  if (!payload || !*payload) return;

  // Parse JSON to get entity_id and values
  String json = payload;
  String entity_id;
  if (!extract_json_string_field(json, "entity_id", entity_id)) return;

  // Find matching tile with graph in active grid
  const TileGridConfig& grid = tileConfig.getActiveGrid();
  SensorTileWidgets* sensors = g_tab0_sensors;  // Active grid is always TAB0

  for (uint8_t i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = grid.tiles[i];
    if (tile.type != TILE_SENSOR) continue;
    if (tile.sensor_display_mode != 2) continue;  // Only graph mode
    if (!tile.sensor_entity.equalsIgnoreCase(entity_id)) continue;

    SensorTileWidgets& w = sensors[i];
    if (!w.chart || !w.series) continue;

    // Extract values array
    String values_str;
    int start = json.indexOf("\"values\"");
    if (start < 0) continue;
    int arr_start = json.indexOf('[', start);
    int arr_end = json.indexOf(']', arr_start);
    if (arr_start < 0 || arr_end < arr_start) continue;
    values_str = json.substring(arr_start + 1, arr_end);

    // Count and parse values
    std::vector<float> values;
    values.reserve(100);
    const char* ptr = values_str.c_str();
    while (*ptr) {
      while (*ptr && (*ptr == ' ' || *ptr == ',')) ++ptr;
      if (!*ptr) break;

      // Handle "null" or "unavailable"
      if (strncmp(ptr, "null", 4) == 0) {
        values.push_back(NAN);
        ptr += 4;
        continue;
      }
      if (*ptr == '"') {
        // Skip string values like "unavailable"
        ++ptr;
        while (*ptr && *ptr != '"') ++ptr;
        if (*ptr == '"') ++ptr;
        values.push_back(NAN);
        continue;
      }

      char* end = nullptr;
      float val = strtof(ptr, &end);
      if (end == ptr) {
        ++ptr;
        continue;
      }
      ptr = end;
      if (isfinite(val)) {
        values.push_back(val);
      } else {
        values.push_back(NAN);
      }
    }

    if (values.empty()) {
      lv_chart_set_point_count(w.chart, 1);
      lv_chart_set_value_by_id(w.chart, w.series, 0, LV_CHART_POINT_NONE);
      lv_chart_refresh(w.chart);
      Serial.printf("[TileGraph] History cleared for %s (0 points)\n", entity_id.c_str());
      continue;
    }

    // Fill missing buckets as HA does: carry the last valid value forward,
    // and fill the leading gap with the first valid value.
    size_t first_valid = values.size();
    for (size_t j = 0; j < values.size(); ++j) {
      if (isfinite(values[j])) {
        first_valid = j;
        break;
      }
    }
    if (first_valid < values.size()) {
      float last = values[first_valid];
      for (size_t j = 0; j < first_valid; ++j) {
        values[j] = last;
      }
      for (size_t j = first_valid + 1; j < values.size(); ++j) {
        if (!isfinite(values[j])) {
          values[j] = last;
        } else {
          last = values[j];
        }
      }
    }

    // Use all points (same as popup - no downsampling)
    // Calculate min/max for range
    float min_v = 0.0f, max_v = 0.0f;
    bool has_range = false;
    for (float v : values) {
      if (!isfinite(v)) continue;
      if (!has_range) {
        min_v = max_v = v;
        has_range = true;
      } else {
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
      }
    }

    // Determine scale factor
    int scale = 1;
    if (has_range) {
      float span = max_v - min_v;
      if (span <= 10.0f) {
        scale = 100;
      } else if (span <= 100.0f) {
        scale = 10;
      }
      float max_abs = fmaxf(fabsf(min_v), fabsf(max_v));
      while (scale > 1 && (max_abs * scale) > 30000.0f) {
        scale /= 10;
      }
    }

    // Update chart
    lv_chart_set_point_count(w.chart, static_cast<uint16_t>(values.size()));
    for (size_t j = 0; j < values.size(); ++j) {
      float v = values[j];
      if (!isfinite(v)) {
        lv_chart_set_value_by_id(w.chart, w.series, static_cast<uint16_t>(j), LV_CHART_POINT_NONE);
      } else {
        lv_chart_set_value_by_id(w.chart, w.series, static_cast<uint16_t>(j),
                                 static_cast<lv_coord_t>(lroundf(v * scale)));
      }
    }

    // Set range
    if (has_range) {
      if (min_v == max_v) {
        min_v -= 1.0f;
        max_v += 1.0f;
      }
      lv_chart_set_range(w.chart, LV_CHART_AXIS_PRIMARY_Y,
                         static_cast<lv_coord_t>(floorf(min_v * scale)),
                         static_cast<lv_coord_t>(ceilf(max_v * scale)));
    }

    lv_chart_refresh(w.chart);
    Serial.printf("[TileGraph] History applied for %s (%zu points)\n", entity_id.c_str(), values.size());
  }
}

void queue_tile_graph_history(const char* entity_id, const char* payload, size_t len) {
  if (!payload || len == 0) return;
  g_tile_graph_history.entity_id = entity_id ? entity_id : "";
  g_tile_graph_history.payload = String(payload).substring(0, len);
  g_tile_graph_history.valid = true;
}

void process_tile_graph_queue() {
  if (!g_tile_graph_history.valid) return;

  apply_tile_graph_history(g_tile_graph_history.entity_id.c_str(),
                           g_tile_graph_history.payload.c_str());
  g_tile_graph_history.valid = false;
}

void request_tile_graph_history(const char* entity_id) {
  if (!entity_id || !*entity_id) return;
  if (String(entity_id).startsWith("__")) return;  // Skip preload entities
  if (hardwareIo.isLocalEntityId(entity_id)) return;
  mqttPublishHistoryRequest(entity_id);
  Serial.printf("[TileGraph] History requested for %s\n", entity_id);
}

/* === Helpers === */
void set_label_style(lv_obj_t* lbl, lv_color_t c, const lv_font_t* f) {
  lv_obj_set_style_text_color(lbl, c, 0);
  lv_obj_set_style_text_font(lbl, f, 0);
  if (f == FONT_MDI_ICONS) {
    tile_layout::apply_mdi_icon_scale(lbl);
  }
}

void set_tile_grid_cell(lv_obj_t* obj, uint8_t col, uint8_t row, uint8_t span_w, uint8_t span_h) {
  if (!obj) return;
  uint8_t w = span_w < 1 ? 1 : span_w;
  uint8_t h = span_h < 1 ? 1 : span_h;
  if (w > GRID_COLS - col) w = GRID_COLS - col;
  if (h > GRID_ROWS - row) h = GRID_ROWS - row;
  lv_obj_set_grid_cell(obj,
      LV_GRID_ALIGN_STRETCH, col, w,
      LV_GRID_ALIGN_STRETCH, row, h);
}

static bool get_tile_layout(const Tile& tile, uint8_t& col, uint8_t& row, uint8_t& span_w, uint8_t& span_h) {
  if (tile.col >= GRID_COLS || tile.row >= GRID_ROWS) return false;
  col = tile.col;
  row = tile.row;
  span_w = tile.span_w < 1 ? 1 : tile.span_w;
  span_h = tile.span_h < 1 ? 1 : tile.span_h;
  clamp_media_tile_layout(tile.type, col, row, span_w, span_h);
  if (span_w > GRID_COLS - col) span_w = GRID_COLS - col;
  if (span_h > GRID_ROWS - row) span_h = GRID_ROWS - row;
  return true;
}

static void mark_occupied(bool occupied[GRID_ROWS][GRID_COLS], uint8_t col, uint8_t row, uint8_t span_w, uint8_t span_h) {
  for (uint8_t r = row; r < row + span_h; ++r) {
    for (uint8_t c = col; c < col + span_w; ++c) {
      if (r < GRID_ROWS && c < GRID_COLS) {
        occupied[r][c] = true;
      }
    }
  }
}

void render_tile_grid(lv_obj_t* parent, const TileGridConfig& config, GridType grid_type,
                      scene_publish_cb_t scene_cb, lv_obj_t** out_tile_objs) {
  // Memory monitoring before reset.
  uint32_t heap_before = ESP.getFreeHeap();
  uint32_t psram_before = ESP.getFreePsram();
  Serial.printf("[TileRenderer] Loading %d tiles... | Heap: %u KB | PSRAM: %u KB\n",
                TILES_PER_GRID, heap_before / 1024, psram_before / 1024);

  // Reset sensor widget pointers for this grid to avoid stale references
  clear_sensor_widgets(grid_type);
  clear_switch_widgets(grid_type);
  reset_binary_sensor_widgets(grid_type);
  clear_weather_widgets(grid_type);
  clear_media_widgets(grid_type);

  if (parent == nullptr) {
    Serial.println("[TileRenderer] ERROR: Parent is NULL!");
    return;
  }

  if (out_tile_objs) {
    for (size_t i = 0; i < TILES_PER_GRID; ++i) {
      out_tile_objs[i] = nullptr;
    }
  }

  bool occupied[GRID_ROWS][GRID_COLS] = {};
  struct TileLayout {
    uint8_t col = 0;
    uint8_t row = 0;
    uint8_t span_w = 1;
    uint8_t span_h = 1;
    bool valid = false;
  };
  TileLayout layouts[TILES_PER_GRID]{};

  for (int i = 0; i < TILES_PER_GRID; ++i) {
    const Tile& tile = config.tiles[i];
    if (tile.type == TILE_EMPTY) continue;
    uint8_t col = 0;
    uint8_t row = 0;
    uint8_t span_w = 1;
    uint8_t span_h = 1;
    if (!get_tile_layout(tile, col, row, span_w, span_h)) continue;
    layouts[i] = {col, row, span_w, span_h, true};
    mark_occupied(occupied, col, row, span_w, span_h);
  }

  for (uint8_t r = 0; r < GRID_ROWS; ++r) {
    for (uint8_t c = 0; c < GRID_COLS; ++c) {
      if (!occupied[r][c]) {
        render_empty_tile(parent, c, r);
      }
    }
    yield();
  }

  size_t render_count = 0;
  for (int i = 0; i < TILES_PER_GRID; ++i) {
    if (!layouts[i].valid) continue;
    Serial.printf("[TileRenderer] Creating tile %d/%d...\n", i + 1, TILES_PER_GRID);

    Tile layout_tile = config.tiles[i];
    layout_tile.col = layouts[i].col;
    layout_tile.row = layouts[i].row;
    layout_tile.span_w = layouts[i].span_w;
    layout_tile.span_h = layouts[i].span_h;
    lv_obj_t* tile_obj = render_tile(parent, layouts[i].col, layouts[i].row, layout_tile, i, grid_type, scene_cb);
    if (out_tile_objs) {
      out_tile_objs[i] = tile_obj;
    }

    if ((++render_count % GRID_COLS) == 0) {
      yield();
      delay(1);
      yield();
    }

    Serial.printf("[TileRenderer] Tile %d/%d complete\n", i + 1, TILES_PER_GRID);
  }
  // Memory monitoring after reset.
  uint32_t heap_after = ESP.getFreeHeap();
  uint32_t psram_after = ESP.getFreePsram();
  int32_t heap_used = heap_before - heap_after;
  int32_t psram_used = psram_before - psram_after;

  Serial.printf("[TileRenderer] All tiles loaded | Heap: %u KB (-%d KB) | PSRAM: %u KB (-%d KB)\n",
                heap_after / 1024, heap_used / 1024,
                psram_after / 1024, psram_used / 1024);
  Serial.printf("[TileRenderer] Minimum free heap since boot: %u KB\n", ESP.getMinFreeHeap() / 1024);
}

lv_obj_t* render_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type, scene_publish_cb_t scene_cb) {
  // Serial.printf("[render_tile] Index=%d, Type=%d, Title='%s'\n", index, tile.type, tile.title.c_str());

  const TileTypeDescriptor* desc = get_tile_type_descriptor(tile.type);
  if (desc && desc->render) {
    lv_obj_t* tile_obj =
        desc->render(parent, col, row, tile, index, grid_type, scene_cb);
    // Normal grids use the global display option. The screensaver has its
    // own setting and applies it after rendering in image_screensaver.cpp.
    if (tile_obj && grid_type != GridType::SCREENSAVER &&
        tile.type != TILE_EMPTY) {
      ui_surface_style::apply_global_tile_border(tile_obj);
    }
    return tile_obj;
  }
  return render_empty_tile(parent, col, row);
}

void update_sensor_tile_value(GridType grid_type, uint8_t grid_index, const char* value, const char* unit) {
  if (grid_index >= TILES_PER_GRID) {
    return;
  }

  SensorTileWidgets* target = tile_renderer_get_sensor_widgets(grid_type);
  lv_obj_t* value_label = target[grid_index].value_label;
  if (!value_label) {
    return;
  }

  if (target[grid_index].gauge && value) {
    float numeric = 0.0f;
    if (parse_sensor_number(String(value), numeric)) {
      const int32_t mapped = map_gauge_arc_value(numeric,
                                                 target[grid_index].gauge_min,
                                                 target[grid_index].gauge_max);
      lv_arc_set_value(target[grid_index].gauge, mapped);
    }
  }

  String displayValue = value ? String(value) : String();
  displayValue.trim();

  String lower = displayValue;
  lower.toLowerCase();
  if (lower == "unavailable" || lower == "unknown" || lower == "none" || lower == "null") {
    displayValue = "--";
  }

  // Format using the tile's configured decimal places.
  if (displayValue.length() > 0 &&
      displayValue != "--" &&
      !displayValue.equalsIgnoreCase("unavailable")) {
    uint8_t decimals = get_sensor_decimals(grid_type, grid_index);
    apply_decimals(displayValue, decimals);
  }

  // Show "--" for an empty or unavailable value.
  if (displayValue.length() == 0 || displayValue.equalsIgnoreCase("unavailable")) {
    displayValue = "--";
  }

  // Combine value and unit in one label at the same size.
  String combined = displayValue;
  if (unit && strlen(unit) > 0 && displayValue != "--") {
    combined += " ";
    combined += unit;
  }
  lv_label_set_text(value_label, combined.c_str());
}
