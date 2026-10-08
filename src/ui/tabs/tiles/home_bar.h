#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

// The head bar above the tiles (layouts, grid_layout.h): the Settings screen's
// head on Home and in every folder. A circle with the page's icon, its title,
// the time, and the gear (Home: opens Settings) or the X (a folder: back to
// its parent). It replaces the Settings and Back tiles, which the shown grid
// then leaves out (shownTileLayout()).
namespace home_bar {

// The bar's places in screen pixels (the Settings head's): the Web Admin
// preview draws the same head from them (web_admin_styles.cpp).
struct Geometry {
  int disc;      // circle diameter
  int disc_x;    // circle left
  int center_y;  // the X's centre line
  int close;     // the X's (or gear's) box
  int close_x;   // its left
  int title_x;
  int title_w;
  int time_x;
  int time_w;
  int line;       // line height of the head font (the title)
  int time_line;  // line height of the time font
  // Home without the gear: the time's digits as far from the screen's right
  // edge as from its top, the title wider by as much.
  int time_x_alone;
  int title_w_alone;
};
Geometry geometry();
// The head font for the title, when it fits.
const lv_font_t* head_font();
// The time's font, a size larger than the head font (user 2026-10-08).
const lv_font_t* time_font();
// HH:MM in the panel's time format; dashes until the clock is set.
void format_time(char* out, size_t size);

// Builds the bar into a tile grid (one floating child at the screen's top,
// in the grid's top margin). Does nothing without the head bar layout.
void build(lv_obj_t* grid, uint16_t folder_id);

// True for the bar's object among a grid's children: it is neither a tile
// nor an empty cell placeholder.
bool is_bar(const lv_obj_t* obj);

}  // namespace home_bar
