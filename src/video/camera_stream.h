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
// The popup frame on the screen now (byte-swapped RGB565), valid until the
// stream stops: the full screen's first moment (#65). False without one.
bool camera_stream_shown_frame(const uint16_t*& pixels, int32_t& width,
                               int32_t& height, int32_t& stride, size_t& bytes);
// millis() of this stream's first shown frame, 0 before it (switch timing).
uint32_t camera_stream_first_frame_ms();
// True once after the stream task ended on its own in a transport error
// (DMA safety stop, receive or acknowledgement failure).
bool camera_stream_take_transport_failure();
