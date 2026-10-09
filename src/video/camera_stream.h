#pragma once

#include <Arduino.h>
#include <lvgl.h>

bool camera_stream_start(const char* url, uint32_t corner_rgb);
void camera_stream_stop();
void camera_stream_process_ui(lv_obj_t* image,
                              lv_obj_t* placeholder,
                              lv_obj_t* status_label);
void camera_stream_set_external_status(const char* text, bool error = false);
bool camera_stream_is_active();
// Camera full-screen test (#65, Guition V2): the stream enlarged by the PPA
// over the whole screen instead of the popup's frame.
void camera_stream_set_fullscreen_test(bool on);
bool camera_stream_fullscreen_test();
