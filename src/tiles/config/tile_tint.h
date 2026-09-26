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

// True for a real color; white, grey and black (the off and default colors)
// never tint a tile (tile_icon_disc::icon_color_tints uses the same rule).
inline bool has_hue(uint32_t rgb) {
  const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return r != g || g != b;
}

// The tint a tile shows, the one rule for the device (tile_icon_source.cpp)
// and the Web Admin preview (icon-colors.js tileTintChoice):
//   1. Tile color "From icon" (`fill_percent`): the tile always follows the
//      color the icon shows (own, Home Assistant or a rule's "Color icon"
//      color); a rule's "Tint tile" does not apply (the editor hides it);
//   2. else a rule "Tint tile" while the rule applies (`rule_active`);
//   3. else no tint: the tile keeps its own or the global color.
// Only real colors tint. A percent of 0 means no tint.
struct Choice {
  uint32_t color = 0;
  uint8_t percent = 0;
};

inline Choice choose(bool rule_active, uint32_t rule_rgb, uint8_t rule_percent, uint8_t fill_percent,
                     uint32_t icon_rgb) {
  Choice choice;
  if (fill_percent) {
    if (has_hue(icon_rgb)) {
      choice.color = icon_rgb & 0xFFFFFF;
      choice.percent = fill_percent;
    }
  } else if (rule_active && rule_percent && has_hue(rule_rgb)) {
    choice.color = rule_rgb & 0xFFFFFF;
    choice.percent = rule_percent;
  }
  return choice;
}

}  // namespace tile_tint
