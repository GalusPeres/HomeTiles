#pragma once

#include <stdint.h>

#include "src/devices/device_select.h"
#include "src/ui/popups/popup_layout.h"

namespace camera_geometry {

// Nearest even value of numerator / denominator.
constexpr uint16_t evenRound(uint32_t numerator, uint32_t denominator) {
  return static_cast<uint16_t>(
      (numerator + denominator) / (2U * denominator) * 2U);
}

// The camera frame fills the popup width, rounded down to a multiple of 8:
// the ESP32-P4 JPEG decoder rejects frames whose width * height is not
// divisible by 8 (IDF jpeg_parse_marker.c), which hit the 1024x600 layout
// (558x314, issue #63). The 16:9 height is the nearest even value so FFmpeg
// can produce yuv420 frames and the Bridge's 16:9 check (|w*9 - h*16| <= 16)
// accepts it. 1280x800 752x424, 1280x720 672x378 and 480x480 448x252 are
// unchanged; 1024x600 is 552x310, centred in its 558 px content width.
// The stream keeps the sizes the Bridge was tested with: the content width
// of the card with its former 3 or 4 px screen margin (the card reaches the
// edge since 2026-10-08; the frame sits centred in the wider content).
#if defined(DEVICE_LAYOUT_480X480)
inline constexpr int kFormerCardMargin = 3;
#else
inline constexpr int kFormerCardMargin = 4;
#endif
inline constexpr uint16_t kWidth =
    static_cast<uint16_t>((popup_layout::kContentWidth - 2 * kFormerCardMargin) & ~7);
inline constexpr uint16_t kHeight = evenRound(
    static_cast<uint32_t>(kWidth) * 9U, 16U);
inline constexpr uint16_t kCornerRadius =
    static_cast<uint16_t>(popup_layout::scale480(18));
#if defined(DEVICE_ESP32_S3_RGB_480)
// The ESP32-S3 decodes in software (camera_stream.cpp, ~1.1 us per pixel):
// the popup's frame about 8 times a second at most, its full screen (the
// whole 480 x 480, black bars, nothing cut) about 3 times. The Bridge thins
// its stream to this; the rest is measured on the panel (user 2026-10-09:
// "schauen wir einfach was möglich ist").
inline constexpr uint8_t kFps = 8;
inline constexpr uint8_t kFallbackFps = 8;
inline constexpr uint16_t kSoftFullWidth = 480;
inline constexpr uint16_t kSoftFullHeight = 480;
// JPEG quality asked of the Bridge (FFmpeg -q:v, 2 best .. 31 smallest; 0 =
// the Bridge's own 11). b329 asked for 5: frames 2-3 times larger, no visible
// difference (user 2026-10-09), each KB ~6 ms more decoding (4.5 -> 3.5 FPS).
inline constexpr uint8_t kJpegQuality = 0;
// The full screen gets the whole picture without bars, within 480 x 480; the
// panel draws the black around it (with "contain" the bars were almost half
// of each decoded frame: 2 FPS instead of the popup's 4.5).
inline constexpr const char* kFullFit = "inside";
#else
// Target for the bounded low-latency camera path. The PPA rotation takes
// about 17 ms per frame on the 800x1280 panels; since the UI loop no longer
// waits for the panel refresh after each swap, 30 FPS keeps about the loop
// share 24 FPS had before.
inline constexpr uint8_t kFps = 30;
// Bridges before v0.7.1b9 reject more than 24 FPS; the popup then asks again
// at this rate.
inline constexpr uint8_t kFallbackFps = 24;
// 0: no "quality" in the request, the Bridge's own JPEG quality.
inline constexpr uint8_t kJpegQuality = 0;
// Frames exactly the framebuffer's size, black bars from the Bridge: the
// hardware decoder writes them straight into the framebuffer.
inline constexpr const char* kFullFit = "contain";
#endif

// ESP32-P4's JPEG hardware decoder writes in 16-pixel-aligned dimensions.
// LVGL still receives the visible width/height and the aligned row stride.
inline constexpr uint16_t kDecodedWidth = (kWidth + 15U) & ~15U;
inline constexpr uint16_t kDecodedHeight = (kHeight + 15U) & ~15U;

static_assert(kWidth >= 320 && kWidth <= 752,
              "Camera popup width is outside the supported P4 range");
static_assert(kHeight >= 180 && kHeight <= 424,
              "Camera popup height is outside the supported P4 range");
static_assert((static_cast<uint32_t>(kWidth) * kHeight) % 8U == 0U,
              "The P4 JPEG decoder needs width * height divisible by 8");
static_assert(kHeight % 2U == 0U, "yuv420 JPEG frames need an even height");
static_assert(static_cast<int32_t>(kWidth) * 9 - static_cast<int32_t>(kHeight) * 16 <= 16 &&
                  static_cast<int32_t>(kHeight) * 16 - static_cast<int32_t>(kWidth) * 9 <= 16,
              "The Bridge accepts 16:9 frames within 16 of w*9 == h*16");

}  // namespace camera_geometry
