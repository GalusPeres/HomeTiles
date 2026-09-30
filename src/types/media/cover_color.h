#pragma once

#include <stddef.h>
#include <stdint.h>

// The color a Media tile takes with "From cover" (icon color and tile color):
// the most prominent saturated hue of the album cover, not its average (an
// average turns most covers into a muddy brown or grey). Grey, near-black and
// near-white pixels are ignored; a cover with too few colored pixels (black
// and white artwork) gives no color, so the tile stays neutral like a white
// icon. The cover is sampled on a grid of at most 32 x 32 pixels, once per
// cover change. No Arduino/LVGL dependency, so host tests compile it
// unchanged.
namespace media_cover_color {

inline constexpr unsigned kSampleSide = 32;
inline constexpr unsigned kHueBuckets = 24;         // 15 degrees each
inline constexpr unsigned kMinChroma = 48;          // below: grey, white or black
inline constexpr unsigned kMinBrightest = 56;       // darker pixels are ignored
inline constexpr unsigned kMinSharePermille = 40;   // colored samples, else no color

// One pixel of LV_COLOR_FORMAT_RGB565_SWAPPED (big-endian RGB565 bytes, the
// cover decoder's output) as 0xRRGGBB.
inline uint32_t rgb_of(const uint8_t* p) {
  const unsigned v = (static_cast<unsigned>(p[0]) << 8) | p[1];
  const unsigned r = ((v >> 11) * 255 + 15) / 31;
  const unsigned g = (((v >> 5) & 0x3F) * 255 + 31) / 63;
  const unsigned b = ((v & 0x1F) * 255 + 15) / 31;
  return (r << 16) | (g << 8) | b;
}

// Hue bucket of a pixel with a chroma above zero.
inline unsigned hue_bucket(int r, int g, int b, int brightest, int chroma) {
  int hue;
  if (brightest == r) hue = 60 * (g - b) / chroma;
  else if (brightest == g) hue = 120 + 60 * (b - r) / chroma;
  else hue = 240 + 60 * (r - g) / chroma;
  if (hue < 0) hue += 360;
  return static_cast<unsigned>(hue) * kHueBuckets / 360 % kHueBuckets;
}

// The cover color of `width` x `height` RGB565_SWAPPED pixels with `stride`
// bytes per row (0 = packed). Each colored sample counts with its chroma, so
// large and saturated areas win. The bucket with the most weight, together
// with its two neighbours (a hue may straddle a bucket border), gives the
// chroma-weighted average of its pixels. False without a clear color.
inline bool pick(const uint8_t* data, uint16_t width, uint16_t height, uint32_t stride, uint32_t& rgb) {
  if (!data || width == 0 || height == 0) return false;
  if (stride == 0) stride = static_cast<uint32_t>(width) * 2u;
  uint32_t weight[kHueBuckets] = {};
  uint32_t sum_r[kHueBuckets] = {};
  uint32_t sum_g[kHueBuckets] = {};
  uint32_t sum_b[kHueBuckets] = {};
  const unsigned columns = width < kSampleSide ? width : kSampleSide;
  const unsigned rows = height < kSampleSide ? height : kSampleSide;
  unsigned samples = 0;
  unsigned colored = 0;
  for (unsigned j = 0; j < rows; ++j) {
    const unsigned y = (j * 2u + 1u) * height / (rows * 2u);
    for (unsigned i = 0; i < columns; ++i) {
      const unsigned x = (i * 2u + 1u) * width / (columns * 2u);
      const uint32_t c = rgb_of(data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 2u);
      const int r = static_cast<int>((c >> 16) & 0xFF);
      const int g = static_cast<int>((c >> 8) & 0xFF);
      const int b = static_cast<int>(c & 0xFF);
      const int brightest = r > g ? (r > b ? r : b) : (g > b ? g : b);
      const int darkest = r < g ? (r < b ? r : b) : (g < b ? g : b);
      const int chroma = brightest - darkest;
      ++samples;
      if (chroma < static_cast<int>(kMinChroma) || brightest < static_cast<int>(kMinBrightest)) continue;
      ++colored;
      const unsigned bucket = hue_bucket(r, g, b, brightest, chroma);
      weight[bucket] += static_cast<uint32_t>(chroma);
      sum_r[bucket] += static_cast<uint32_t>(r * chroma);
      sum_g[bucket] += static_cast<uint32_t>(g * chroma);
      sum_b[bucket] += static_cast<uint32_t>(b * chroma);
    }
  }
  if (colored == 0 || colored * 1000u < samples * kMinSharePermille) return false;
  unsigned best = 0;
  uint32_t best_weight = 0;
  for (unsigned k = 0; k < kHueBuckets; ++k) {
    const uint32_t around = weight[(k + kHueBuckets - 1) % kHueBuckets] + weight[k] +
                            weight[(k + 1) % kHueBuckets];
    if (around > best_weight) {
      best_weight = around;
      best = k;
    }
  }
  uint32_t total = 0, r = 0, g = 0, b = 0;
  for (unsigned d = 0; d < 3; ++d) {
    const unsigned k = (best + kHueBuckets - 1 + d) % kHueBuckets;
    total += weight[k];
    r += sum_r[k];
    g += sum_g[k];
    b += sum_b[k];
  }
  if (total == 0) return false;
  rgb = ((r + total / 2) / total) << 16 | ((g + total / 2) / total) << 8 | ((b + total / 2) / total);
  return true;
}

}  // namespace media_cover_color
