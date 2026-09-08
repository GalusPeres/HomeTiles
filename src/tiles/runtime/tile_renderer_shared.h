#include "src/ui/navigation/view_navigation.h"
#pragma once

#include <lvgl.h>
#include "src/tiles/runtime/tile_renderer.h"

static constexpr int32_t GAUGE_ARC_STEPS = 1000;

// Extern declarations for global widget arrays
extern SensorTileWidgets g_tab0_sensors[];
extern SensorTileWidgets g_tab1_sensors[];
extern SensorTileWidgets g_tab2_sensors[];
extern SensorTileWidgets g_screensaver_sensors[];

extern SwitchTileWidgets g_tab0_switches[];
extern SwitchTileWidgets g_tab1_switches[];
extern SwitchTileWidgets g_tab2_switches[];
extern SwitchTileWidgets g_screensaver_switches[];

#if defined(CONFIG_IDF_TARGET_ESP32P4)
extern WeatherTileWidgets* g_tab0_weather;
extern WeatherTileWidgets* g_tab1_weather;
extern WeatherTileWidgets* g_tab2_weather;

extern MediaTileWidgets* g_tab0_media;
extern MediaTileWidgets* g_tab1_media;
extern MediaTileWidgets* g_tab2_media;
extern MediaTileWidgets* g_screensaver_media;
#else
extern WeatherTileWidgets g_tab0_weather[];
extern WeatherTileWidgets g_tab1_weather[];
extern WeatherTileWidgets g_tab2_weather[];

extern MediaTileWidgets g_tab0_media[];
extern MediaTileWidgets g_tab1_media[];
extern MediaTileWidgets g_tab2_media[];
extern MediaTileWidgets g_screensaver_media[];
#endif

extern SwitchState g_tab0_switch_states[];
extern SwitchState g_tab1_switch_states[];
extern SwitchState g_tab2_switch_states[];
extern SwitchState g_screensaver_switch_states[];

extern ClimateTileWidgets g_tab0_climate[];
extern ClimateTileWidgets g_tab1_climate[];
extern ClimateTileWidgets g_tab2_climate[];

void set_label_style(lv_obj_t* lbl, lv_color_t c, const lv_font_t* f);
void set_tile_grid_cell(lv_obj_t* obj, uint8_t col, uint8_t row, uint8_t span_w, uint8_t span_h);

SensorTileWidgets* tile_renderer_get_sensor_widgets(GridType grid_type);
SwitchTileWidgets* tile_renderer_get_switch_widgets(GridType grid_type);
WeatherTileWidgets* tile_renderer_get_weather_widgets(GridType grid_type);
MediaTileWidgets* tile_renderer_get_media_widgets(GridType grid_type);
SwitchState* tile_renderer_get_switch_states(GridType grid_type);
ClimateTileWidgets* tile_renderer_get_climate_widgets(GridType grid_type);
ClimateState* tile_renderer_get_climate_states(GridType grid_type);
const Tile* tile_renderer_get_tile_config(GridType grid_type, uint8_t index);

bool is_light_entity_id(const String& entity_id);
void update_switch_tile_state(GridType grid_type, uint8_t grid_index, const char* payload);
void update_media_tile_state(GridType grid_type, uint8_t grid_index, const char* payload);

// Call when a media card is destroyed (LV_EVENT_DELETE): clears every widget
// array entry that still points at the reference about to be freed. Without it a
// tile slot deleted through Web Admin keeps its entry forever and the cover
// retry reads freed memory, which crashed as "Load access fault" in
// String::startsWith (v0.4.16 core dump).
void tile_renderer_forget_media_widgets(const MediaCoverRef* ref);

static inline uint32_t brighten_rgb_color(uint32_t color, uint8_t delta) {
  const uint16_t r = ((color >> 16) & 0xFFu) + delta;
  const uint16_t g = ((color >> 8) & 0xFFu) + delta;
  const uint16_t b = (color & 0xFFu) + delta;
  return ((r > 0xFFu ? 0xFFu : r) << 16) |
         ((g > 0xFFu ? 0xFFu : g) << 8) |
         (b > 0xFFu ? 0xFFu : b);
}

static inline void disable_pressed_button_animation(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_set_style_transform_width(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
  // A state-specific translate property forces LVGL to refresh every child's
  // layout even when its value is zero. The theme does not translate buttons.
}

// Release the source and let the regular refresh draw the complete popup.
// A nested refresh here redraws the old page before the new card can appear.
static inline void finish_press_before_popup(lv_event_t* event) {
  if (!event) return;
  lv_obj_t* source = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (!source) source = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (source) {
    viewNavigationSource(source);
    lv_obj_clear_state(source, LV_STATE_PRESSED);
    lv_obj_invalidate(source);
  }
  lv_display_t* display = lv_display_get_default();
  if (display) lv_timer_ready(lv_display_get_refr_timer(display));
}
