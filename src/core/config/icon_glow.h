#pragma once

#include <stdint.h>

// Circle strength of the icon discs ("Glow"), a global display setting in
// percent. It sets how far every circle and control sits above its tile
// (src/ui/shared/tone_color.h: 0.06 OKLCH lightness at the default 25 %,
// shown with this percent as the shared opacity); 0 hides the circles. The
// Web Admin preview (grid-preview.js applyIconDiscTint) uses the same
// formulas.
namespace icon_glow {

inline constexpr uint8_t kMinimum = 0;
inline constexpr uint8_t kMaximum = 100;
inline constexpr uint8_t kStep = 5;
inline constexpr uint8_t kDefault = 25;

inline uint8_t clamp(int percent) {
  if (percent < static_cast<int>(kMinimum)) return kMinimum;
  if (percent > kMaximum) return kMaximum;
  return static_cast<uint8_t>(percent);
}

}  // namespace icon_glow
