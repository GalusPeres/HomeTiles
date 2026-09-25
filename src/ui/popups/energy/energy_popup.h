#pragma once

#include <Arduino.h>
#include <lvgl.h>

struct EnergyPopupInit {
  String entity_id;
  String title;
  String icon_name;
  String unit;
  uint8_t decimals = 1;
  uint32_t bg_color = 0;
  // Header icon color: the tile icon's color for the state at opening.
  uint32_t icon_color = 0xFFFFFF;
};

void show_energy_popup(const EnergyPopupInit& init);
void preload_energy_popup();
void hide_energy_popup();
void queue_energy_popup_refresh(const char* period);
void process_energy_popup_queue();
