#pragma once

#include <stdint.h>
#include <string.h>
#include <lvgl.h>

enum CoverFeature : uint8_t {
  COVER_FEATURE_OPEN = 1U << 0,
  COVER_FEATURE_CLOSE = 1U << 1,
  COVER_FEATURE_SET_POSITION = 1U << 2,
  COVER_FEATURE_STOP = 1U << 3,
  COVER_FEATURE_OPEN_TILT = 1U << 4,
  COVER_FEATURE_CLOSE_TILT = 1U << 5,
  COVER_FEATURE_STOP_TILT = 1U << 6,
  COVER_FEATURE_SET_TILT_POSITION = 1U << 7
};

struct CoverState {
  bool valid = false;
  bool available = false;
  bool assumed_state = false;
  bool has_position = false;
  bool has_tilt_position = false;
  uint8_t position = 0;
  uint8_t tilt_position = 0;
  uint8_t supported_features = 0;
  char state[12] = {};
  char device_class[12] = {};
};

// Opening or closing: Home Assistant shows the arrow icon; the tile and the
// popup header pulse it like a Lock or Alarm panel waiting for its device
// (ui_pulse.h). A tilt that turns changes no state, so it shows nothing.
inline bool cover_state_moving(const CoverState& state) {
  return state.valid && state.available &&
         (strcmp(state.state, "opening") == 0 || strcmp(state.state, "closing") == 0);
}

struct CoverTileWidgets {
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  // Header layouts (half height, or a full tile of a Cover with a position):
  // "Open · 58 %" beside the disc.
  lv_obj_t* state_label = nullptr;
  // The state's largest font, width and tall center (tile_header::Header).
  const lv_font_t* state_font = nullptr;
  int16_t state_width = 0;
  int16_t state_center = -1;
  bool compact = false;
  // The former centered layout of a Cover without a position.
  lv_obj_t* value_label = nullptr;
  // Full header tiles: the position bar below the header (level_bar.h), the
  // level it draws, its one-row height and whether the Cover is available.
  lv_obj_t* bar = nullptr;
  int16_t bar_base = 0;
  uint8_t level = 0;
  bool available = false;
  // The fill's state color (cover_icon_color): the cover color, or the
  // inactive grey for unknown and unavailable.
  uint32_t fill_color = 0x926BC7;
  uint32_t last_payload_hash = 0;
  bool dynamic_icon = true;
};
