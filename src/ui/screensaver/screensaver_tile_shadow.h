#pragma once

#include <stdint.h>

// The optional screensaver tile shadow: a black card shadow (LVGL width and
// spread in display pixels, opacity 60 %). The Web Admin preview draws the
// same shadow (web_admin_styles.cpp).
namespace screensaver_tile_shadow {
constexpr int kWidth = 32;
constexpr int kSpread = 3;
constexpr uint8_t kOpa = 153;
}  // namespace screensaver_tile_shadow
