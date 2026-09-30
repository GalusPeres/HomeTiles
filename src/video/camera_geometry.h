#pragma once

#include <stdint.h>

#include "src/ui/popups/popup_layout.h"

namespace camera_geometry {

constexpr uint16_t evenCeil(uint32_t numerator, uint32_t denominator) {
  const uint32_t rounded = (numerator + denominator - 1U) / denominator;
  return static_cast<uint16_t>((rounded + 1U) & ~1U);
}

// The camera frame always fills the popup width. A 16:9 height is rounded up
// to an even value so FFmpeg can produce yuv420 JPEG frames without padding
// the visible image differently on each display profile.
inline constexpr uint16_t kWidth =
    static_cast<uint16_t>(popup_layout::kContentWidth & ~1);
inline constexpr uint16_t kHeight = evenCeil(
    static_cast<uint32_t>(kWidth) * 9U, 16U);
inline constexpr uint16_t kCornerRadius =
    static_cast<uint16_t>(popup_layout::scale480(18));
// Target for the bounded low-latency camera path. The PPA rotation takes
// about 17 ms per frame on the 800x1280 panels; since the UI loop no longer
// waits for the panel refresh after each swap, 30 FPS keeps about the loop
// share 24 FPS had before.
inline constexpr uint8_t kFps = 30;
// Bridges before v0.7.1b9 reject more than 24 FPS; the popup then asks again
// at this rate.
inline constexpr uint8_t kFallbackFps = 24;

// ESP32-P4's JPEG hardware decoder writes in 16-pixel-aligned dimensions.
// LVGL still receives the visible width/height and the aligned row stride.
inline constexpr uint16_t kDecodedWidth = (kWidth + 15U) & ~15U;
inline constexpr uint16_t kDecodedHeight = (kHeight + 15U) & ~15U;

static_assert(kWidth >= 320 && kWidth <= 752,
              "Camera popup width is outside the supported P4 range");
static_assert(kHeight >= 180 && kHeight <= 424,
              "Camera popup height is outside the supported P4 range");

}  // namespace camera_geometry
