#pragma once

#include <lvgl.h>

// Return an owned, PSRAM-backed thumbnail, or nullptr when the original can
// be used directly / optional PSRAM is unavailable. Never consumes the source.
lv_image_dsc_t* make_media_tile_cover_dsc(const lv_image_dsc_t* source,
                                        uint16_t target_side);
