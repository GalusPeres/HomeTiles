#pragma once

#include <lvgl.h>

// Return an owned, PSRAM-backed thumbnail, or nullptr when the original can
// be used directly / optional PSRAM is unavailable. Never consumes the source.
lv_image_dsc_t* make_media_tile_cover_dsc(const lv_image_dsc_t* source,
                                        uint16_t target_side);

// The Media popup's cover: the source's centred square as an owned
// side x side RGB565 picture in PSRAM (averaged when smaller, interpolated
// when larger, copied when equal); nullptr for another format or without
// memory. Never consumes the source.
lv_image_dsc_t* make_media_popup_cover_dsc(const lv_image_dsc_t* source, uint16_t side);
