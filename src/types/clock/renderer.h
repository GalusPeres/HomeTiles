#pragma once

#include "src/tiles/runtime/tile_renderer.h"

struct ClockWidgetConfig {
  bool show_time = true;
  bool show_date = false;
  // Prefix the date line with the weekday (e.g. "Tuesday, 15.07.2026").
  // Without a date, show the weekday alone on the line.
  bool show_weekday = false;
  // The short weekday ("Sa") instead of the full name (clock tile option).
  bool weekday_short = false;
  // A changed digit rolls up once (clock tile option, user 2026-10-11); not
  // with text_shadow.
  bool roll = false;
  // Language code for the weekday name; nullptr falls back to English.
  const char* weekday_language = nullptr;
  bool fill_parent = false;
  // Put time and date side by side (half-height clock tiles).
  bool horizontal = false;
  // The screensaver's one-line clock: both boxes take the width of the longer
  // line, so the gap shows and each line's alignment acts (the Web preview
  // does the same). Half-height clock tiles keep their natural widths.
  bool one_line = false;
  // LVGL has no glyph shadow. Several slightly offset dark copies behind
  // each line approximate a blur. Used only by the screensaver.
  bool text_shadow = false;
  uint8_t time_font_size = 40;
  uint8_t date_font_size = 20;
  uint8_t time_format = 0;
  uint8_t date_format = 0;
  uint8_t time_alignment = 1;  // 0=left, 1=center, 2=right
  uint8_t date_alignment = 1;
};

// Shared borderless clock content for clock tiles and the screensaver.
// The object owns its one-second timer and releases it when deleted.
lv_obj_t* create_clock_widget(lv_obj_t* parent, const ClockWidgetConfig& config);

lv_obj_t* render_clock_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index);
