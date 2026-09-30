#pragma once

#include <math.h>
#include <stdint.h>

// Colors of the icon circle, the controls and the icon itself, derived from
// the icon color in fixed steps of perceived lightness (OKLCH L), like the
// tonal systems of Material 3, Radix Colors and Adobe Leonardo:
// - the circle and every control sit a fixed step above the card, in the
//   icon's hue with part of its chroma (white icons: the tile's own color,
//   lighter), so every color gets the same visible circle;
// - the icon keeps the color it was given while it is at least kIconMinStep
//   above the circle; a darker icon only gets lighter, in the same hue.
// Circles and controls are drawn opaque in exactly these colors. A
// translucent fill calibrated to land on them missed on the panels: their
// 16-bit framebuffers blend with 32 opacity levels and truncate, so a grey
// circle kept about half its step and turned greenish, and a dark saturated
// circle over a grey tile could not drop below a grey floor. Only cards that
// let the background through (screensaver tiles below full Tile opacity)
// keep the translucent veil, so the wallpaper still shows through. The Web
// Admin preview (grid-preview.js toneFill) uses the same formulas.
namespace tone_color {

// 0.06 L at the default 25 % Circle strength.
inline constexpr float kStepPerPercent = 0.0024f;
// Controls stay visible below 12.5 %: at least this step (a veil: opacity
// 32).
inline constexpr float kControlMinStep = 0.03f;
inline constexpr uint8_t kControlMinOpa = 32;
// The icon stays at least this far above the circle.
inline constexpr float kIconMinStep = 0.22f;
// The circle keeps this share of the icon's chroma, like a container tone.
inline constexpr float kCircleChroma = 0.55f;

struct Oklch {
  float L, C, h;
};

inline float srgb_to_linear(uint8_t value) {
  static float table[256];
  static bool ready = false;
  if (!ready) {
    for (int i = 0; i < 256; ++i) {
      const float v = i / 255.0f;
      table[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    ready = true;
  }
  return table[value];
}

inline uint8_t linear_to_srgb(float value) {
  if (value <= 0.0f) return 0;
  if (value >= 1.0f) return 255;
  const float v = value <= 0.0031308f ? 12.92f * value : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

inline Oklch from_rgb(uint32_t rgb) {
  const float r = srgb_to_linear((rgb >> 16) & 0xFF);
  const float g = srgb_to_linear((rgb >> 8) & 0xFF);
  const float b = srgb_to_linear(rgb & 0xFF);
  const float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  const float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  const float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  const float L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
  const float A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
  const float B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
  return {L, sqrtf(A * A + B * B), atan2f(B, A)};
}

inline void linear_from_oklch(float L, float C, float h, float out[3]) {
  const float A = C * cosf(h), B = C * sinf(h);
  const float l = L + 0.3963377774f * A + 0.2158037573f * B;
  const float m = L - 0.1055613458f * A - 0.0638541728f * B;
  const float s = L - 0.0894841775f * A - 1.2914855480f * B;
  const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
  out[0] = 4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3;
  out[1] = -1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3;
  out[2] = -0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3;
}

// Back to sRGB; a color outside the screen's range loses chroma until it
// fits, hue and lightness stay.
inline uint32_t to_rgb(float L, float C, float h) {
  if (L < 0.0f) L = 0.0f;
  if (L > 1.0f) L = 1.0f;
  float linear[3];
  auto fits = [&](float chroma) {
    linear_from_oklch(L, chroma, h, linear);
    for (float v : linear) {
      if (v < -0.0005f || v > 1.0005f) return false;
    }
    return true;
  };
  if (!fits(C)) {
    float lo = 0.0f, hi = C;
    for (int i = 0; i < 16; ++i) {
      const float mid = (lo + hi) * 0.5f;
      if (fits(mid)) lo = mid;
      else hi = mid;
    }
    C = lo;
  }
  linear_from_oklch(L, C, h, linear);
  return (static_cast<uint32_t>(linear_to_srgb(linear[0])) << 16) |
         (static_cast<uint32_t>(linear_to_srgb(linear[1])) << 8) | linear_to_srgb(linear[2]);
}

// `over` at `opa` over `under`, like LVGL's blending.
inline uint32_t blend(uint32_t under, uint32_t over, uint8_t opa) {
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const unsigned a = (under >> shift) & 0xFF, b = (over >> shift) & 0xFF;
    out |= ((a * (255u - opa) + b * opa + 127u) / 255u) << shift;
  }
  return out;
}

// The card lifted by `step`: in the icon's hue when tinted, else in the
// card's own color (a lighter red on a red tile, like a white veil; a
// neutral grey only on a grey tile).
inline uint32_t lifted(uint32_t card, uint32_t icon, bool tinted, float step) {
  const Oklch base = from_rgb(card);
  if (!tinted) return to_rgb(base.L + step, base.C, base.h);
  const Oklch seed = from_rgb(icon);
  return to_rgb(base.L + step, seed.C * kCircleChroma, seed.h);
}

// The opacity a circle is drawn with at the Circle strength: opaque, none at
// 0 %; on a see-through card the veil opacity of the strength.
inline uint8_t disc_opa(uint8_t percent, bool see_through = false) {
  if (percent > 100) percent = 100;
  if (!see_through) return percent ? 255 : 0;
  return static_cast<uint8_t>((percent * 255 + 50) / 100);
}

// The opacity of the controls: opaque; a veil of at least kControlMinOpa on a
// see-through card.
inline uint8_t control_opa(uint8_t percent, bool see_through = false) {
  if (!see_through) return 255;
  const uint8_t opa = disc_opa(percent, true);
  return opa > kControlMinOpa ? opa : kControlMinOpa;
}

// What a circle and the controls draw over `card`: `disc_color` at
// `disc_opa` for the circle and `control_color` at `control_opa` for the
// controls. At 12.5 % and more both show the same color.
struct Fill {
  uint32_t disc_color;
  uint32_t control_color;
  uint8_t disc_opa;
  uint8_t control_opa;
  // The opaque colors they show over the card.
  uint32_t disc;
  uint32_t control;
  // A control pressed on a control surface (a PIN key, a button on a pill):
  // one more control step, drawn at `control_opa` (a veil stacks by itself).
  uint32_t raised_color;
  uint32_t raised;
  // Whether they take the icon's hue.
  bool tinted;
};

inline Fill fill(uint32_t card, uint32_t icon, bool tinted, uint8_t percent, bool see_through = false) {
  card &= 0xFFFFFF;
  icon &= 0xFFFFFF;
  if (percent > 100) percent = 100;
  // Popups restyle on every sync: keep the last few results.
  struct Entry {
    uint32_t card, icon;
    uint8_t percent;
    bool tinted, see_through, used;
    Fill fill;
  };
  static Entry cache[4] = {};
  static uint8_t next = 0;
  for (const Entry& entry : cache) {
    if (entry.used && entry.card == card && entry.icon == icon && entry.percent == percent &&
        entry.tinted == tinted && entry.see_through == see_through) {
      return entry.fill;
    }
  }
  Fill result{};
  result.tinted = tinted;
  result.disc_opa = disc_opa(percent, see_through);
  result.control_opa = control_opa(percent, see_through);
  const float disc_step = percent * kStepPerPercent;
  const bool full_step = disc_step >= kControlMinStep;
  if (!see_through) {
    // Opaque: exactly the steps.
    const float control_step = full_step ? disc_step : kControlMinStep;
    result.control = lifted(card, icon, tinted, control_step);
    result.disc = !percent ? card : full_step ? result.control : lifted(card, icon, tinted, disc_step);
    result.raised = lifted(card, icon, tinted, 2.0f * control_step);
    result.disc_color = result.disc;
    result.control_color = result.control;
    result.raised_color = result.raised;
  } else {
    // Veil: the color that lands on the step at the control opacity; the
    // circle shows it at its own, lower opacity below 12.5 %.
    const uint32_t target = lifted(card, icon, tinted, result.disc_opa > kControlMinOpa ? disc_step : kControlMinStep);
    uint32_t color = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
      const int under = (card >> shift) & 0xFF, want = (target >> shift) & 0xFF;
      int value = under + ((want - under) * 255 + (want >= under ? result.control_opa / 2 : -result.control_opa / 2)) /
                              result.control_opa;
      if (value < 0) value = 0;
      if (value > 255) value = 255;
      color |= static_cast<uint32_t>(value) << shift;
    }
    result.disc_color = color;
    result.control_color = color;
    result.raised_color = color;
    result.control = blend(card, color, result.control_opa);
    result.disc = blend(card, color, result.disc_opa);
    result.raised = blend(result.control, color, result.control_opa);
  }
  Entry& slot = cache[next];
  next = static_cast<uint8_t>((next + 1) % 4);
  slot = {card, icon, percent, tinted, see_through, true, result};
  return result;
}

// The icon as shown on `circle`: unchanged while it is light enough,
// otherwise raised to the minimum step in its own hue.
inline uint32_t readable_icon(uint32_t icon, uint32_t circle) {
  icon &= 0xFFFFFF;
  const Oklch seed = from_rgb(icon);
  const float minimum = from_rgb(circle & 0xFFFFFF).L + kIconMinStep;
  if (seed.L >= minimum) return icon;
  return to_rgb(minimum, seed.C, seed.h);
}

}  // namespace tone_color
