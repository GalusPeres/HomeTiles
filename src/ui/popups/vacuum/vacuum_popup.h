#pragma once

#include <Arduino.h>
#include <lvgl.h>

// The Vacuum popup (build/design-mockups/vacuum) on the Media popup's frame:
// the header with the vacuum's icon, name and state, the map left with the
// cleaned area, cleaning time and battery beside it (or the map centred with
// the values in one line below it, the tile's popup layout), the row Dock,
// the main button (Clean, or Pause while cleaning), Stop and the menu button.
// The menu (Media's menu box) holds the suction levels, the rooms with the
// spot clean and the locate action, each tab only when the vacuum supports
// it. One cached popup, refilled for the tile that opens it.
struct VacuumPopupTarget {
  String entity;
  String title;
  // The tile's configured MDI icon; "" shows the state icon.
  String icon_name;
  bool icon_visible = true;
  bool map_centered = false;
  // The map chosen in the Web Admin; "" takes the one the Bridge finds.
  String map_entity;
  // A camera map's interval in seconds (the tile's).
  uint8_t map_every = 5;
  // The opening tile card (popup color and header disc), may be null.
  lv_obj_t* source = nullptr;
};

void show_vacuum_popup(const VacuumPopupTarget& target);
void hide_vacuum_popup();
void preload_vacuum_popup();
// The opening tile's color changed while the popup shows (tile_icon_source).
void vacuum_popup_follow_tile_color(uint32_t color);
// The detail state of `entity` changed.
void vacuum_popup_refresh(const String& entity);
// The side of the popup's map square in either layout (the tiles fetch its
// picture ahead, vacuum_map::want).
int vacuum_popup_map_edge(bool centered);
