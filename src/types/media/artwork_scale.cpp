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

namespace {

struct Rgb {
  uint32_t r = 0, g = 0, b = 0;
};

inline uint16_t read_pixel(const uint8_t* at, bool swapped) {
  return swapped ? static_cast<uint16_t>((at[0] << 8) | at[1]) : static_cast<uint16_t>(at[0] | (at[1] << 8));
}

inline void write_pixel(uint8_t* at, uint16_t value, bool swapped) {
  if (swapped) {
    at[0] = static_cast<uint8_t>(value >> 8);
    at[1] = static_cast<uint8_t>(value);
  } else {
    at[0] = static_cast<uint8_t>(value);
    at[1] = static_cast<uint8_t>(value >> 8);
  }
}

inline void add(Rgb& sum, uint16_t value, uint32_t weight) {
  sum.r += ((value >> 11) & 0x1F) * weight;
  sum.g += ((value >> 5) & 0x3F) * weight;
  sum.b += (value & 0x1F) * weight;
}

inline uint16_t pack(const Rgb& sum, uint32_t total) {
  const uint32_t half = total / 2;
  return static_cast<uint16_t>((((sum.r + half) / total) << 11) | (((sum.g + half) / total) << 5) |
                               ((sum.b + half) / total));
}

}  // namespace

lv_image_dsc_t* make_media_popup_cover_dsc(const lv_image_dsc_t* source, uint16_t side) {
  if (!source || !source->data || side == 0 || source->header.w == 0 || source->header.h == 0) return nullptr;
  const bool swapped = source->header.cf == LV_COLOR_FORMAT_RGB565_SWAPPED;
  if (!swapped && source->header.cf != LV_COLOR_FORMAT_RGB565) return nullptr;
  const size_t stride = source->header.stride ? source->header.stride : source->header.w * 2U;
  if (stride < source->header.w * 2U ||
      source->data_size < stride * (source->header.h - 1U) + source->header.w * 2U) return nullptr;

  // The popup's cover is square: the source's centred square.
  const uint32_t crop = std::min<uint32_t>(source->header.w, source->header.h);
  const uint8_t* origin = source->data + ((source->header.h - crop) / 2) * stride + ((source->header.w - crop) / 2) * 2U;
  const size_t bytes = static_cast<size_t>(side) * side * 2U;
  uint8_t* pixels = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) return nullptr;

  if (crop == side) {
    for (uint32_t y = 0; y < side; ++y) memcpy(pixels + y * side * 2U, origin + y * stride, side * 2U);
  } else if (crop > side) {
    // Smaller: every target pixel averages its box of source pixels.
    for (uint32_t y = 0; y < side; ++y) {
      const uint32_t y0 = y * crop / side;
      const uint32_t y1 = std::max(y0 + 1, (y + 1) * crop / side);
      for (uint32_t x = 0; x < side; ++x) {
        const uint32_t x0 = x * crop / side;
        const uint32_t x1 = std::max(x0 + 1, (x + 1) * crop / side);
        Rgb sum;
        for (uint32_t sy = y0; sy < y1; ++sy) {
          const uint8_t* row = origin + sy * stride;
          for (uint32_t sx = x0; sx < x1; ++sx) add(sum, read_pixel(row + sx * 2U, swapped), 1);
        }
        write_pixel(pixels + (y * side + x) * 2U, pack(sum, (y1 - y0) * (x1 - x0)), swapped);
      }
    }
  } else {
    // Larger (a cover that came smaller): bilinear, in 1/256 steps.
    for (uint32_t y = 0; y < side; ++y) {
      const int32_t fy = static_cast<int32_t>(((2 * y + 1) * crop * 128) / side) - 128;
      const uint32_t ya = fy < 0 ? 0 : static_cast<uint32_t>(fy) >> 8;
      const uint32_t yb = std::min(ya + 1, crop - 1);
      const uint32_t wy = fy < 0 ? 0 : static_cast<uint32_t>(fy) & 0xFF;
      for (uint32_t x = 0; x < side; ++x) {
        const int32_t fx = static_cast<int32_t>(((2 * x + 1) * crop * 128) / side) - 128;
        const uint32_t xa = fx < 0 ? 0 : static_cast<uint32_t>(fx) >> 8;
        const uint32_t xb = std::min(xa + 1, crop - 1);
        const uint32_t wx = fx < 0 ? 0 : static_cast<uint32_t>(fx) & 0xFF;
        Rgb sum;
        add(sum, read_pixel(origin + ya * stride + xa * 2U, swapped), (256 - wx) * (256 - wy));
        add(sum, read_pixel(origin + ya * stride + xb * 2U, swapped), wx * (256 - wy));
        add(sum, read_pixel(origin + yb * stride + xa * 2U, swapped), (256 - wx) * wy);
        add(sum, read_pixel(origin + yb * stride + xb * 2U, swapped), wx * wy);
        write_pixel(pixels + (y * side + x) * 2U, pack(sum, 65536), swapped);
      }
    }
  }

  lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
  if (!dsc) {
    free(pixels);
    return nullptr;
  }
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = source->header.cf;
  dsc->header.w = side;
  dsc->header.h = side;
  dsc->header.stride = side * 2U;
  dsc->data_size = bytes;
  dsc->data = pixels;
  return dsc;
}
