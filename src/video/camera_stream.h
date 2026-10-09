#pragma once

#include <Arduino.h>
#include <lvgl.h>

// full_screen (#65): frames in the panel's own size and orientation, decoded
// straight into the framebuffer (Device::displayBeginFullFrames first).
bool camera_stream_start(const char* url, uint32_t corner_rgb,
                         bool full_screen = false);
void camera_stream_stop();
void camera_stream_process_ui(lv_obj_t* image,
                              lv_obj_t* placeholder,
                              lv_obj_t* status_label);
void camera_stream_set_external_status(const char* text, bool error = false);
bool camera_stream_is_active();
