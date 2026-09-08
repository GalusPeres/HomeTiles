#include "src/types/media/artwork_scale.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <esp_heap_caps.h>

lv_image_dsc_t* make_media_tile_cover_dsc(const lv_image_dsc_t* source, uint16_t target_side) {
  if (!source || !source->data ||
      source->header.cf != LV_COLOR_FORMAT_RGB565_SWAPPED ||
      source->header.w == 0 || source->header.h == 0) {
    return nullptr;
  }

  const uint16_t kTileCoverMaxSide = std::max<uint16_t>(1, std::min<uint16_t>(240, target_side));
  const size_t source_stride = source->header.stride ? source->header.stride : source->header.w * 2U;
  if (source_stride < source->header.w * 2U ||
      source->data_size < source_stride * (source->header.h - 1U) + source->header.w * 2U) return nullptr;
  if (source->header.w <= kTileCoverMaxSide &&
      source->header.h <= kTileCoverMaxSide) {
    return nullptr;
  }

  uint16_t dst_w = source->header.w;
  uint16_t dst_h = source->header.h;
  if (dst_w >= dst_h) {
    dst_w = kTileCoverMaxSide;
    dst_h = static_cast<uint16_t>((static_cast<uint32_t>(source->header.h) * dst_w) /
                                  source->header.w);
  } else {
    dst_h = kTileCoverMaxSide;
    dst_w = static_cast<uint16_t>((static_cast<uint32_t>(source->header.w) * dst_h) /
                                  source->header.h);
  }
  if (dst_w == 0) dst_w = 1;
  if (dst_h == 0) dst_h = 1;

  const size_t bytes = static_cast<size_t>(dst_w) * dst_h * sizeof(uint16_t);
  uint16_t* pixels = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) return nullptr;

  const uint16_t src_w = source->header.w;
  const uint16_t src_h = source->header.h;
  for (uint16_t y = 0; y < dst_h; ++y) {
    const uint16_t sy = static_cast<uint16_t>((static_cast<uint32_t>(y) * src_h) / dst_h);
    for (uint16_t x = 0; x < dst_w; ++x) {
      const uint16_t sx = static_cast<uint16_t>((static_cast<uint32_t>(x) * src_w) / dst_w);
      memcpy(&pixels[static_cast<size_t>(y) * dst_w + x],
             source->data + static_cast<size_t>(sy) * source_stride + sx * 2U, 2);
    }
  }

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(
      malloc(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(pixels);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  dsc->header.w = dst_w;
  dsc->header.h = dst_h;
  dsc->header.stride = dst_w * 2;
  dsc->data_size = bytes;
  dsc->data = reinterpret_cast<const uint8_t*>(pixels);
  return dsc;
}
