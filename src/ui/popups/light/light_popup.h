#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct LightPopupInit {
  String entity_id;
  String title;
  String icon_name;
  uint32_t color = 0;
  // Card color: the opening tile's current background (0 = default card).
  // Only an opening applies it; state updates keep the last opening's color.
  uint32_t bg_color = 0;
  uint8_t brightness_pct = 100;
  uint16_t color_temp_kelvin = 4000;
  uint16_t min_color_temp_kelvin = 2000;
  uint16_t max_color_temp_kelvin = 6535;
  bool has_color = false;
  bool has_brightness = false;
  bool has_color_temp = false;
  bool has_state = false;
  bool has_hs = false;
  float hs_h = 0.0f;
  float hs_s = 0.0f;
  bool supports_color = false;
  bool supports_brightness = false;
  bool supports_temperature = false;
  bool available = true;
  bool is_light = true;
  bool is_on = true;
  bool keep_icon_white = false;
  bool has_tile_ref = false;
  uint8_t tile_grid = 0;
  uint8_t tile_index = 0;
};

void show_light_popup(const LightPopupInit& init);
void update_light_popup(const LightPopupInit& init);
lv_color_t light_color_from_temperature_kelvin(uint16_t kelvin);
void preload_light_popup();
void hide_light_popup();
void light_popup_follow_tile_color(uint32_t color);
// True while a slider, the color wheel or the Kelvin control of the open
// Light popup is dragged. Tile recoloring waits for the release.
bool light_popup_is_dragging();
