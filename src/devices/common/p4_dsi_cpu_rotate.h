#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace p4_dsi_cpu_rotate {

// Quarter-turn copy of a landscape LVGL area (w x h pixels, row-major) into
// the portrait panel framebuffer, where it becomes an h x w rectangle at
// (dst_x, dst_y). `rotation_bit2` is the drivers' `g_rotation & 0x02` case;
// the pixel mapping is exactly the one of the former rotate-buffer loops.
//
// The framebuffer (PSRAM) is written row by row, so each destination cache
// line is filled in one go, while the source band, which lives in internal
// RAM, is read column-wise. The former path rotated into a PSRAM buffer first
// (one cache line per pixel write) and then copied that buffer again.
// The caller writes the touched rows back for the DMA (cache sync).
inline void rotate_into(uint16_t* fb, size_t fb_stride, int32_t dst_x,
                        int32_t dst_y, int32_t w, int32_t h,
                        const uint16_t* src, bool rotation_bit2) {
  const size_t src_stride = static_cast<size_t>(w);
  for (int32_t row = 0; row < w; ++row) {
    uint16_t* dst =
        fb + static_cast<size_t>(dst_y + row) * fb_stride + dst_x;
    if (rotation_bit2) {
      // Destination row `row` is source column w-1-row, top to bottom.
      const uint16_t* column = src + (w - 1 - row);
      for (int32_t col = 0; col < h; ++col) {
        dst[col] = column[static_cast<size_t>(col) * src_stride];
      }
    } else {
      // Destination row `row` is source column `row`, bottom to top.
      const uint16_t* column = src + row;
      for (int32_t col = 0; col < h; ++col) {
        dst[col] = column[static_cast<size_t>(h - 1 - col) * src_stride];
      }
    }
  }
}

// Straight copy of an upright LVGL area (w x h pixels, row-major) into the
// panel framebuffer at (x, y), or turned by 180 degrees (`flipped`, the
// drivers' `g_rotation & 0x02`) into the mirrored place. The caller writes the
// touched rows back for the DMA (cache sync).
inline void copy_into(uint16_t* fb, size_t fb_stride, int32_t fb_w, int32_t fb_h, int32_t x,
                      int32_t y, int32_t w, int32_t h, const uint16_t* src, bool flipped) {
  const size_t src_stride = static_cast<size_t>(w);
  if (!flipped) {
    for (int32_t row = 0; row < h; ++row) {
      std::memcpy(fb + static_cast<size_t>(y + row) * fb_stride + x, src + static_cast<size_t>(row) * src_stride,
                  static_cast<size_t>(w) * sizeof(uint16_t));
    }
    return;
  }
  const int32_t dst_x = fb_w - x - w;
  const int32_t dst_y = fb_h - y - h;
  for (int32_t row = 0; row < h; ++row) {
    uint16_t* dst = fb + static_cast<size_t>(dst_y + row) * fb_stride + dst_x;
    const uint16_t* line = src + static_cast<size_t>(h - 1 - row) * src_stride;
    for (int32_t col = 0; col < w; ++col) dst[col] = line[w - 1 - col];
  }
}

}  // namespace p4_dsi_cpu_rotate
