#pragma once

// LVGL unpacks a compressed font glyph again for every piece of a label it
// draws: a 48 px tile icon crossing three 28-row draw bands is unpacked three
// times per frame (V2 b590: ~0.3 ms per piece, 25 ms of a 137 ms Home frame).
// Plain 4-bit text glyphs are widened to 8 bits for every piece the same way
// (V2 b598: the 20 px titles 130 us per piece). The icon fonts (mdi_icons_<px>,
// weather_icons_<px>, mdi_bar_icons_<px>) and the text fonts (ui_font_*,
// ui_symbols_*) use glyph_cache_bitmap as their get_glyph_bitmap: each glyph is
// unpacked once into a bounded PSRAM cache and drawn from there for every later
// piece and frame. Plain C, shared by the generated fonts and C++.

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct glyph_cache_stats_t {
  uint32_t hits;    // pieces drawn from the cache
  uint32_t misses;  // glyphs unpacked and kept
  uint32_t clears;  // times the full cache started over
  uint32_t glyphs;  // glyphs kept now
  uint32_t bytes;   // bytes kept now
  uint32_t off;     // 1 when the cache memory could not be allocated
} glyph_cache_stats_t;

// LVGL get_glyph_bitmap for fmt_txt fonts: the same A8 bitmap as
// lv_font_get_bitmap_fmt_txt, from the cache when the glyph was unpacked
// before (then in a buffer of the cache, valid until the next glyph).
const void* glyph_cache_bitmap(lv_font_glyph_dsc_t* g_dsc, lv_draw_buf_t* draw_buf);

// Drops every kept glyph (the icon file changed).
void glyph_cache_clear(void);

glyph_cache_stats_t glyph_cache_stats(void);

#ifdef __cplusplus
}  // extern "C"
#endif
