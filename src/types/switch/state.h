#pragma once

#include <stdint.h>
#include <lvgl.h>

// Live view of a Switch tile with the Sensor header (Switch, Dimmer and
// Automatic layouts, and every half-height Switch tile). Owned by the tile
// card and freed with it; the bar's draw and touch callbacks read it.
struct SwitchBarView {
  lv_obj_t* card = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* state_label = nullptr;
  // The control bar: one object that draws its fill, handle and thumb.
  lv_obj_t* bar = nullptr;
  uint8_t layout = 0;    // switch_layout::Layout
  uint8_t bar_kind = 0;  // switch_layout::Bar
  // Dimmer level drawn (0 = no fill); toggle position drawn.
  uint8_t level = 0;
  bool on = false;
  bool available = true;
  // Half-height tile: the Sensor compact layout sets the state font.
  bool compact = false;
  // Width the state line may use before its font steps down (full tiles).
  int16_t state_width = 0;
  // The chosen state size (Tile::sensor_value_font): a half-height Sensor
  // value size beside the disc, a Sensor value size in the middle of a tall
  // tile. A longer state steps down from it until it fits.
  const lv_font_t* state_font = nullptr;
  // Tall tiles: the state's vertical center in the card's content box, kept
  // when the font steps down; -1 beside the disc.
  int16_t state_center = -1;
  // Fill and thumb color when the tile has no icon to read it from.
  uint32_t fill_rgb = 0xFFD54F;
  // Off thumb color cache: one OKLCH step above the bar color.
  uint32_t thumb_base = 0;
  uint32_t thumb_off = 0;
};

struct SwitchTileWidgets {
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  SwitchBarView* view = nullptr;
};

struct SwitchState {
  bool available = true;
  bool has_state = false;
  bool is_on = false;
  // Home Assistant reported "unknown" (kept apart from a missing state).
  bool unknown = false;
  bool has_color = false;
  uint32_t color = 0;
  bool has_hs = false;
  float hs_h = 0.0f;
  float hs_s = 0.0f;
  bool has_brightness = false;
  uint8_t brightness_pct = 100;
  bool has_color_temp = false;
  uint16_t color_temp_kelvin = 4000;
  uint16_t min_color_temp_kelvin = 2000;
  uint16_t max_color_temp_kelvin = 6535;
  bool supports_color = false;
  bool supports_brightness = false;
  bool supports_temperature = false;
  bool supported_modes_known = false;
  bool supported_onoff_only = false;
};
