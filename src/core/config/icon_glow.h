#pragma once

#include <stdint.h>

// Glow strength of colored icon discs, a global display setting in percent of
// full opacity. A glowing disc tints itself with the icon hue at this
// strength; tile and popup borders stay the neutral hairline. The Web Admin
// preview (grid-preview.js applyIconDiscTint) uses the same formula.
namespace icon_glow {

inline constexpr uint8_t kMinimum = 10;
inline constexpr uint8_t kMaximum = 60;
inline constexpr uint8_t kStep = 5;
inline constexpr uint8_t kDefault = 25;

inline uint8_t clamp(int percent) {
  if (percent < kMinimum) return kMinimum;
  if (percent > kMaximum) return kMaximum;
  return static_cast<uint8_t>(percent);
}

// Percent to an 8-bit opacity, rounded to the nearest value.
inline uint8_t to_opa(int percent) {
  return static_cast<uint8_t>((percent * 255 + 50) / 100);
}

inline uint8_t disc_opa(int percent) { return to_opa(clamp(percent)); }

}  // namespace icon_glow
