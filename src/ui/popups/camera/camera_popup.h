#pragma once

#include <Arduino.h>

struct CameraPopupInit {
  String entity_id;
  String title;
  String icon_name;
  uint32_t bg_color = 0x2A2A2A;
};

void show_camera_popup(const CameraPopupInit& init);
void hide_camera_popup();
void preload_camera_popup();
bool camera_popup_is_visible();
bool camera_popup_is_busy();
void process_camera_popup();
void camera_popup_handle_mqtt_status(const char* payload);
void camera_popup_set_status(const char* text, bool error = false);
