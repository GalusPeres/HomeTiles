#pragma once

#include <stdint.h>
#include <string.h>

// The ESP32-P4 JPEG decoder writes whole MCUs: every output row is the image
// width rounded up to the MCU width, and the output has the height rounded up
// to the MCU height (esp_driver_jpeg, jpeg_parse_marker.c: process_h and
// process_v from the first component's sampling factors times 8). Images of
// any size can use the hardware this way; the padding is cut off after
// decoding.
namespace jpeg_padded_output {

inline uint32_t align_up(uint32_t value, uint32_t step) {
  return (value + step - 1) / step * step;
}

// Moves the visible width x height pixels to the front of the buffer, one row
// after the other. Destination rows never pass their source rows, so the
// buffer is reused in place.
inline void crop_in_place(uint16_t* pixels, uint32_t stride, uint32_t width, uint32_t height) {
  if (!pixels || stride == width) return;
  for (uint32_t y = 1; y < height; ++y) {
    memmove(pixels + static_cast<size_t>(y) * width, pixels + static_cast<size_t>(y) * stride,
            static_cast<size_t>(width) * sizeof(uint16_t));
  }
}

}  // namespace jpeg_padded_output
