#pragma once

#include <Arduino.h>
#include "src/ui/shared/ui_theme.h"

struct CameraPopupInit {
  String entity_id;
  String title;
  String icon_name;
  uint32_t bg_color = ui_theme::popup_card();
  // Header icon color: the tile's fixed icon color, else white.
  uint32_t icon_color = 0xFFFFFF;
};

void show_camera_popup(const CameraPopupInit& init);
void hide_camera_popup();
void preload_camera_popup();
bool camera_popup_is_visible();
// The View select's full-screen option (#65, doorbell automations): the next
// camera popup opens straight in full screen. Cleared by that opening; the
// caller clears it when no camera popup opened.
void camera_popup_open_next_in_full_screen(bool full_screen);
bool camera_popup_is_full_screen();
bool camera_popup_is_busy();
void process_camera_popup();
void camera_popup_handle_mqtt_status(const char* payload);
void camera_popup_set_status(const char* text, bool error = false);
