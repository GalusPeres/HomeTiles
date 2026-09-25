#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer.h"

struct CoverPopupInit {
  String entity_id;
  String title;
  String icon_name;
  CoverState state;
  bool icon_visible = true;
  // The opening tile's current background (own color or rules tint); 0 means
  // popup_surface::kDefaultCard. Only an opening applies it: state updates
  // keep the card color of the last opening.
  uint32_t bg_color = 0;
};

void show_cover_popup(const CoverPopupInit& init);
void update_cover_popup(const CoverPopupInit& init);
void preload_cover_popup();
void hide_cover_popup();
