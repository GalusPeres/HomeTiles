#pragma once

#include <stdint.h>

// Glow strength of the icon discs, a global display setting in percent. A
// glowing disc tints itself with the icon hue at this percent of full
// opacity; the white disc of other icons scales with it (kNeutralOpa at the
// default), so the slider strengthens every disc and 0 hides them. The Web
// Admin preview (grid-preview.js applyIconDiscTint) uses the same formulas.
namespace icon_glow {

inline constexpr uint8_t kMinimum = 0;
inline constexpr uint8_t kMaximum = 100;
inline constexpr uint8_t kStep = 5;
inline constexpr uint8_t kDefault = 25;
// The white disc opacity at the default strength (tile_icon_disc::kOpa).
inline constexpr uint8_t kNeutralOpa = 38;

inline uint8_t clamp(int percent) {
  if (percent < static_cast<int>(kMinimum)) return kMinimum;
  if (percent > kMaximum) return kMaximum;
  return static_cast<uint8_t>(percent);
}

// Percent to an 8-bit opacity, rounded to the nearest value.
inline uint8_t to_opa(int percent) {
  return static_cast<uint8_t>((percent * 255 + 50) / 100);
}

inline uint8_t disc_opa(int percent) { return to_opa(clamp(percent)); }

// The white disc of uncolored icons: kNeutralOpa at the default strength,
// scaled linearly (0 at 0 %, 152 at 100 %).
inline uint8_t neutral_opa(int percent) {
  return static_cast<uint8_t>((kNeutralOpa * clamp(percent) + kDefault / 2) / kDefault);
}

}  // namespace icon_glow
