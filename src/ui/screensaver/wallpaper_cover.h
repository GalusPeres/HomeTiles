#pragma once

#include <stdint.h>
#include <string.h>

// Fits a decoded wallpaper (byte-swapped RGB565 as the JPEG decoders write
// it) into the screen like CSS object-fit: cover, with the stored focus and
// zoom, and rounds the corners towards black. Pure pixel code: the P4 path
// (image_screensaver.cpp make_cover_dsc) and the host test share it.
namespace wallpaper_cover {

// Pixel coverage inside a rounded corner, using 4x4 supersampling:
// 16 means fully visible, 0 means entirely the border color.
inline uint8_t rounded_pixel_coverage(uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h,
                                      uint16_t radius) {
  if (radius == 0 || w < radius * 2 || h < radius * 2) return 16;
  const bool at_left = x < radius;
  const bool at_right = x >= w - radius;
  const bool at_top = y < radius;
  const bool at_bottom = y >= h - radius;
  if ((!at_left && !at_right) || (!at_top && !at_bottom)) return 16;

  const uint16_t edge_x = at_left ? x : static_cast<uint16_t>(w - 1 - x);
  const uint16_t edge_y = at_top ? y : static_cast<uint16_t>(h - 1 - y);
  constexpr int32_t kSamples = 4;
  constexpr int32_t kUnitsPerPixel = kSamples * 2;
  const int32_t center = static_cast<int32_t>(radius) * kUnitsPerPixel;
  const int32_t radius_sq = center * center;
  uint8_t covered = 0;
  for (int32_t sample_y = 0; sample_y < kSamples; ++sample_y) {
    const int32_t py = static_cast<int32_t>(edge_y) * kUnitsPerPixel +
                       sample_y * 2 + 1;
    const int32_t dy = center - py;
    for (int32_t sample_x = 0; sample_x < kSamples; ++sample_x) {
      const int32_t px = static_cast<int32_t>(edge_x) * kUnitsPerPixel +
                         sample_x * 2 + 1;
      const int32_t dx = center - px;
      if (dx * dx + dy * dy <= radius_sq) ++covered;
    }
  }
  return covered;
}

inline uint16_t blend_swapped_rgb565_with_black(uint16_t swapped, uint8_t coverage) {
  if (coverage == 0) return 0;
  if (coverage >= 16) return swapped;
  const uint16_t color =
      static_cast<uint16_t>((swapped >> 8) | (swapped << 8));
  const uint16_t red =
      static_cast<uint16_t>((((color >> 11) & 0x1F) * coverage + 8) / 16);
  const uint16_t green =
      static_cast<uint16_t>((((color >> 5) & 0x3F) * coverage + 8) / 16);
  const uint16_t blue =
      static_cast<uint16_t>(((color & 0x1F) * coverage + 8) / 16);
  const uint16_t blended =
      static_cast<uint16_t>((red << 11) | (green << 5) | blue);
  return static_cast<uint16_t>((blended >> 8) | (blended << 8));
}

// The source rectangle shown on an image_w x image_h area.
struct Crop {
  uint32_t x0 = 0;
  uint32_t y0 = 0;
  uint32_t w = 0;
  uint32_t h = 0;
};

inline bool crop_for(uint16_t src_w, uint16_t src_h, uint16_t image_w, uint16_t image_h,
                     uint16_t focus_x, uint16_t focus_y, uint16_t zoom, Crop& crop) {
  if (src_w == 0 || src_h == 0 || image_w == 0 || image_h == 0) return false;
  uint32_t crop_w = src_w;
  uint32_t crop_h =
      static_cast<uint32_t>((static_cast<uint64_t>(crop_w) * image_h) / image_w);
  if (crop_h > src_h || crop_h == 0) {
    crop_h = src_h;
    crop_w =
        static_cast<uint32_t>((static_cast<uint64_t>(crop_h) * image_w) / image_h);
    if (crop_w > src_w) crop_w = src_w;
  }
  if (crop_w == 0 || crop_h == 0) return false;
  if (zoom < 1000) zoom = 1000;
  if (zoom > 3000) zoom = 3000;
  crop_w = (crop_w * 1000U) / zoom;
  crop_h = (crop_h * 1000U) / zoom;
  if (crop_w == 0) crop_w = 1;
  if (crop_h == 0) crop_h = 1;
  if (crop_w > src_w) crop_w = src_w;
  if (crop_h > src_h) crop_h = src_h;
  if (focus_x > 1000) focus_x = 1000;
  if (focus_y > 1000) focus_y = 1000;
  crop.x0 = ((src_w - crop_w) * focus_x) / 1000U;
  crop.y0 = ((src_h - crop_h) * focus_y) / 1000U;
  crop.w = crop_w;
  crop.h = crop_h;
  return true;
}

// Writes image_w x image_h pixels to dst (dst_stride pixels per row): the
// nearest source pixel per target pixel, then the rounded corners. A source
// row shown at its own width is copied whole; the source column of every
// target column is computed once (column_map, image_w entries, may be null);
// only the corner pixels take the coverage math. The same pixels as one
// division, one coverage test and one blend per pixel (183 ms for 1280x800
// on the V2, b303).
// to_native turns every finished row into native RGB565 while it is still
// in the cache: LVGL then draws the picture with a plain copy instead of a
// copy and a byte swap of each row (b305; the swapped picture cost about
// 140 ms of every screensaver composite on the V2).
inline void swap_row(uint16_t* row, uint32_t count) {
  for (uint32_t x = 0; x < count; ++x) {
    row[x] = static_cast<uint16_t>((row[x] >> 8) | (row[x] << 8));
  }
}

inline void cover_pixels(const uint16_t* src, uint16_t src_w, const Crop& crop,
                         uint16_t* dst, uint32_t dst_stride,
                         uint16_t image_w, uint16_t image_h, uint16_t radius,
                         uint16_t* column_map, bool to_native = false) {
  const bool copy_rows = crop.w == image_w;
  if (column_map && !copy_rows) {
    for (uint32_t x = 0; x < image_w; ++x) {
      column_map[x] = static_cast<uint16_t>(crop.x0 + (x * crop.w) / image_w);
    }
  }
  const bool rounded = radius != 0 && image_w >= radius * 2 && image_h >= radius * 2;
  for (uint32_t y = 0; y < image_h; ++y) {
    const uint32_t sy = crop.y0 + (y * crop.h) / image_h;
    const uint16_t* src_row = src + static_cast<size_t>(sy) * src_w;
    uint16_t* dst_row = dst + static_cast<size_t>(y) * dst_stride;
    if (copy_rows) {
      memcpy(dst_row, src_row + crop.x0, static_cast<size_t>(image_w) * sizeof(uint16_t));
    } else if (column_map) {
      for (uint32_t x = 0; x < image_w; ++x) dst_row[x] = src_row[column_map[x]];
    } else {
      for (uint32_t x = 0; x < image_w; ++x) {
        dst_row[x] = src_row[crop.x0 + (x * crop.w) / image_w];
      }
    }
    if (rounded && (y < radius || y >= static_cast<uint32_t>(image_h - radius))) {
      for (uint32_t x = 0; x < radius; ++x) {
        dst_row[x] = blend_swapped_rgb565_with_black(
            dst_row[x], rounded_pixel_coverage(x, y, image_w, image_h, radius));
      }
      for (uint32_t x = image_w - radius; x < image_w; ++x) {
        dst_row[x] = blend_swapped_rgb565_with_black(
            dst_row[x], rounded_pixel_coverage(x, y, image_w, image_h, radius));
      }
    }
    if (to_native) swap_row(dst_row, image_w);
  }
}

}  // namespace wallpaper_cover
