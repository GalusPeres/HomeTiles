#pragma once

#include <Arduino.h>
#include <lvgl.h>

// full_screen (#65): frames in the panel's own size and orientation, decoded
// straight into the framebuffer (Device::displayBeginFullFrames first).
// width, height: the popup's frames as the Bridge announced them, at most
// camera_geometry::kWidth x kBoxHeight (0: 16:9, kWidth x kHeight).
bool camera_stream_start(const char* url, uint32_t corner_rgb,
                         bool full_screen = false, uint16_t width = 0, uint16_t height = 0);
// A still camera's newest JPEG for the popup's next opening: while wanted a copy
// now and then (the first frame at once), taken over by the UI (free() it).
void camera_stream_keep_jpeg(bool keep);
bool camera_stream_take_kept_jpeg(uint8_t*& jpeg, size_t& bytes);
void camera_stream_stop();
void camera_stream_process_ui(lv_obj_t* image,
                              lv_obj_t* placeholder,
                              lv_obj_t* status_label);
void camera_stream_set_external_status(const char* text, bool error = false);
// The last status was an error (the popup shows it over the picture).
bool camera_stream_status_is_error();
bool camera_stream_is_active();
// millis() of this stream's first shown frame, 0 before it (switch timing).
uint32_t camera_stream_first_frame_ms();
// True once after the stream task ended on its own in a transport error
// (DMA safety stop, receive or acknowledgement failure).
bool camera_stream_take_transport_failure();
// Test builds (/api/debug/camera-pause): the stream task reads and
// acknowledges nothing for `ms`, a Wi-Fi stall as the Bridge sees it (#116).
// False when no stream runs.
bool camera_stream_debug_pause(uint32_t ms);
