#pragma once

#include <stdint.h>
#include <Arduino.h>
#include <lvgl.h>

struct MediaCoverRef {
  lv_image_dsc_t* dsc = nullptr;
  lv_image_dsc_t* popup_dsc = nullptr;
  String source_url;
  uint32_t url_hash = 0;
  // URL paired with the displayed MQTT pixels; their content hash is separate.
  uint32_t embedded_url_hash = 0;
  uint32_t requested_url_hash = 0;
  uint32_t failed_url_hash = 0;
  uint32_t failed_at_ms = 0;
};

struct MediaTileWidgets {
  lv_obj_t* cover_clip = nullptr;
  lv_obj_t* cover_image = nullptr;
  MediaCoverRef* cover_ref = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* previous_label = nullptr;
  lv_obj_t* play_pause_label = nullptr;
  lv_obj_t* next_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* media_title_label = nullptr;
  lv_obj_t* media_subtitle_label = nullptr;
  lv_obj_t* state_label = nullptr;
  uint32_t last_payload_hash = 0;
  uint32_t last_media_text_hash = 0;
  bool has_media_position = false;
  float media_position = 0.0f;
  float media_duration = 0.0f;
  uint32_t media_position_received_ms = 0;
  bool has_media_volume = false;
  float media_volume_level = 0.0f;
  bool media_is_muted = false;
  bool dynamic_icon = true;
};
