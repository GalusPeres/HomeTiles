#pragma once

#include <math.h>
#include <stdint.h>

// Tile background tint of the rule layer (tile_icon_colors.h "tile=NN"): the
// tile color mixed with the rule color at NN percent, then darkened in 5 %
// steps until white text keeps a WCAG contrast of at least 4.5:1. The Web
// Admin preview (grid-preview.js tileTintBackground) uses the same integer
// steps. No Arduino/LVGL dependency, so host tests compile it unchanged.
namespace tile_tint {

inline double channel(uint32_t value) {
  const double c = static_cast<double>(value & 0xFF) / 255.0;
  return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

inline double luminance(uint32_t rgb) {
  return 0.2126 * channel(rgb >> 16) + 0.7152 * channel(rgb >> 8) + 0.0722 * channel(rgb);
}

// White text contrast (WCAG): (1.0 + 0.05) / (L + 0.05).
inline double white_contrast(uint32_t rgb) { return 1.05 / (luminance(rgb) + 0.05); }

inline uint32_t mix(uint32_t base, uint32_t color, unsigned percent) {
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const unsigned from = (base >> shift) & 0xFF;
    const unsigned to = (color >> shift) & 0xFF;
    out |= ((from * (100 - percent) + to * percent + 50) / 100) << shift;
  }
  return out;
}

inline uint32_t scale(uint32_t rgb, unsigned percent) {
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    out |= ((((rgb >> shift) & 0xFF) * percent + 50) / 100) << shift;
  }
  return out;
}

inline uint32_t background(uint32_t base, uint32_t color, unsigned percent) {
  uint32_t out = mix(base & 0xFFFFFF, color & 0xFFFFFF, percent > 100 ? 100 : percent);
  for (int i = 0; i < 40 && white_contrast(out) < 4.5; ++i) out = scale(out, 95);
  return out;
}

}  // namespace tile_tint
