#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "src/core/config/icon_glow.h"
#include "src/core/config/tile_color.h"
#include "src/tiles/config/tile_tint.h"
#include "src/ui/shared/ui_theme.h"

// Colors of the icon circle, the controls and the icon itself, derived from
// the icon color in fixed steps of perceived lightness (OKLCH L), like the
// tonal systems of Material 3, Radix Colors and Adobe Leonardo:
// - the circle and every control sit a fixed step above the card, in the
//   icon's hue with part of its chroma (white icons: the tile's own color,
//   lighter), so every color gets the same visible circle;
// - the icon keeps the color it was given while it is at least kIconMinStep
//   above the circle it has with the default settings; a darker icon only
//   gets lighter, in the same hue. The global tile color and the Circle
//   strength never change an icon's color.
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
// The light theme shows an icon above this lightness without a hue (white) in
// the icon grey; Home Assistant's light grey of a white light (HSV value 225,
// OKLCH L 0.91) stays.
inline constexpr float kWhiteIconL = 0.95f;
// The circle keeps this share of the icon's chroma, like a container tone.
inline constexpr float kCircleChroma = 0.55f;

struct Oklch {
  float L, C, h;
};

inline float srgb_to_linear_exact(uint8_t value) {
  const float v = value / 255.0f;
  return v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
}

// The table in the heap (PSRAM on the S3), not in internal RAM: the S3 draw
// band gets what internal RAM leaves above its reserve (psram_static.h).
inline float srgb_to_linear(uint8_t value) {
  static float* const table = [] {
    float* made = static_cast<float*>(malloc(256 * sizeof(float)));
    if (made) {
      for (int i = 0; i < 256; ++i) made[i] = srgb_to_linear_exact(static_cast<uint8_t>(i));
    }
    return made;
  }();
  return table ? table[value] : srgb_to_linear_exact(value);
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
  // The light theme steps down from its light cards, by 5/6 of the step
  // (light mockup: -0.05 L for +0.06 L).
  if (ui_theme::light()) step = -step * ui_theme::kLightStepNum / ui_theme::kLightStepDen;
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
// The card the tile color "From icon" gives an icon color at `percent` (0 =
// its default strength) over the global default tile color, at rest or
// pressed; tile_icon_source registers it at startup. "Circle in icon color"
// is computed for this card on Global and Custom cards and in popups too
// (tile_icon_disc::circle_card), so card, circle and icon stay one family. A
// popup opened from a "From icon" tile passes that tile's strength, so its
// circle is exactly the tile's. Null (host tests): a circle keeps its own
// card.
inline uint32_t (*g_from_icon_card)(uint32_t icon, bool pressed, uint8_t percent) = nullptr;

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
  // Popups restyle on every sync: keep the last few results (per theme: the
  // theme switches while running).
  struct Entry {
    uint32_t card, icon;
    uint8_t percent;
    bool tinted, see_through, light, used;
    Fill fill;
  };
  static Entry cache[4] = {};
  static uint8_t next = 0;
  const bool light = ui_theme::light();
  for (const Entry& entry : cache) {
    if (entry.used && entry.card == card && entry.icon == icon && entry.percent == percent &&
        entry.tinted == tinted && entry.see_through == see_through && entry.light == light) {
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
  slot = {card, icon, percent, tinted, see_through, light, true, result};
  return result;
}

// The light theme: a tint this pale (OKLCH chroma) counts as no color
// (readable_icon).
inline constexpr float kNoColorChroma = 0.08f;

// An icon color without a color: white, almost white or grey-white (a white
// light, the dark theme's default icon color).
inline bool no_color(uint32_t rgb) {
  rgb &= 0xFFFFFF;
  const Oklch seed = from_rgb(rgb);
  return seed.L > 0.9f && (!tile_tint::has_hue(rgb) || seed.C < kNoColorChroma);
}

// Whether text, icons and circles on `rgb` follow the light theme's rules
// (ui_theme::kLightSurfaceL).
inline bool light_surface(uint32_t rgb) { return from_rgb(rgb & 0xFFFFFF).L > ui_theme::kLightSurfaceL; }

// A card while it is pressed, the one rule for every tile and Settings card
// (user 2026-10-11, chosen in build/design-mockups/tile-colors): a dark card
// (white words: the dark theme, a filled tile) 0x10 lighter per channel as
// always; a light card (dark words: the light theme's white card)
// kLightPressStep darker (two of the light theme's control steps), since
// white cannot get lighter (a white tile showed no press) and one step left a
// pressed Settings card in the screen's grey.
inline constexpr float kLightPressStep = 0.10f;
inline uint32_t pressed_card(uint32_t card) {
  card &= 0xFFFFFF;
  if (light_surface(card)) {
    const Oklch base = from_rgb(card);
    return to_rgb(base.L - kLightPressStep, base.C, base.h);
  }
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const uint32_t channel = ((card >> shift) & 0xFFu) + 0x10u;
    out |= (channel > 0xFFu ? 0xFFu : channel) << shift;
  }
  return out;
}

// A mark on a colored surface (the power symbol, a slider notch): dark on a
// light surface, white on a dark one (lab 2026-10-10). The dark theme keeps
// its card-colored cut-out on light surfaces.
inline uint32_t mark_on(uint32_t surface, uint32_t cutout) {
  if (light_surface(surface)) return ui_theme::light() ? ui_theme::text() : cutout & 0xFFFFFF;
  return 0xFFFFFF;
}

// A slider's handle line on its fill: the card's color cut out of a light
// fill, in both themes (user 2026-10-10: "der müsste den Untergrund von der
// Karte haben"; Home Assistant's white handle on its white card), white on a
// dark fill.
inline uint32_t notch_on(uint32_t fill, uint32_t card) {
  return light_surface(fill) ? card & 0xFFFFFF : 0xFFFFFF;
}

// The color a light tile circle and control track are made of (light theme):
// a white light's light grey would vanish on the light card, so a color
// without a hue that light takes Home Assistant's neutral control grey
// (--disabled-color #BDBDBD at the same share, user 2026-10-10).
inline uint32_t circle_source(uint32_t shown) {
  return no_color(shown) ? ui_theme::kLight.icon_unavailable : shown & 0xFFFFFF;
}

// A light's color as Home Assistant shows it (ha-state-control-light-brightness):
// an HSV saturation below 0.4 is raised to 0.4; white (below 0.1) becomes a
// grey of value 225, so it never vanishes on the light card.
inline uint32_t ha_light_color(uint32_t rgb) {
  rgb &= 0xFFFFFF;
  const float r = ((rgb >> 16) & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = (rgb & 0xFF) / 255.0f;
  const float max = r > g ? (r > b ? r : b) : (g > b ? g : b);
  const float min = r < g ? (r < b ? r : b) : (g < b ? g : b);
  const float d = max - min;
  float sat = max > 0.0f ? d / max : 0.0f, value = max;
  if (sat >= 0.4f || max <= 0.0f) return rgb;
  float hue = 0.0f;
  if (d > 0.0f) {
    if (max == r) hue = fmodf((g - b) / d, 6.0f);
    else if (max == g) hue = (b - r) / d + 2.0f;
    else hue = (r - g) / d + 4.0f;
    hue *= 60.0f;
    if (hue < 0.0f) hue += 360.0f;
  }
  if (sat < 0.1f) value = 225.0f / 255.0f;
  else sat = 0.4f;
  auto channel = [&](float n) {
    float k = fmodf(n + hue / 60.0f, 6.0f);
    float m = k < 4.0f - k ? k : 4.0f - k;
    if (m > 1.0f) m = 1.0f;
    if (m < 0.0f) m = 0.0f;
    const float v = value - value * sat * m;
    return static_cast<uint32_t>(v * 255.0f + 0.5f);
  };
  return (channel(5.0f) << 16) | (channel(3.0f) << 8) | channel(1.0f);
}

// A light's fill, power button and popup icon: Home Assistant's color of the
// light in both themes (user 2026-10-10 light theme, 2026-10-26 dark theme
// too, #130: "wie in HA"), white light its light grey.
inline uint32_t light_accent(uint32_t rgb) { return ha_light_color(rgb & 0xFFFFFF); }

// The icon of a light (its popup header): exactly its power button's color
// (light_accent).
inline uint32_t light_icon(uint32_t rgb) { return light_accent(rgb); }

// The tile color "From icon" at its default strength
// (tile_icon_colors::kTintDefault).
inline constexpr uint8_t kReferenceTint = 20;

// A switch (the Switch tile bar, the Light popup switch, the Web Admin
// preview): its track is the control fill; off, the thumb is one circle step
// above the track in the track's own color and its symbol has the grey of an
// off icon.
inline uint32_t off_icon() { return ui_theme::icon_off(); }
inline constexpr float kThumbStep = 0.06f;
inline uint32_t switch_thumb_off(uint32_t track) { return lifted(track, track, false, kThumbStep); }

// A filled tile without a color in the light theme (off, unavailable, idle):
// the idle grey (ui_theme::kJewelIdleL).
inline uint32_t jewel_idle_card() { return to_rgb(ui_theme::kJewelIdleL, 0.0f, 0.0f); }

// A tinted tile in the light theme (ui_theme kJewel*): its card from the
// icon's own color at `strength` percent (tile color "From icon" strength,
// 0 = the default 20 %).
inline uint32_t jewel_card(uint32_t icon, uint8_t strength) {
  if (no_color(icon)) return to_rgb(ui_theme::kJewelNeutralL, 0.0f, 0.0f);
  const Oklch seed = from_rgb(icon & 0xFFFFFF);
  float L = seed.L - ui_theme::kJewelBelowIcon;
  if (L > ui_theme::kJewelCardMaxL) L = ui_theme::kJewelCardMaxL;
  if (L < ui_theme::kJewelCardMinL) L = ui_theme::kJewelCardMinL;
  const float cap = ui_theme::kJewelChroma * (strength ? strength : 20) / 20.0f;
  const float C = seed.C * ui_theme::kJewelCardChromaShare;
  return to_rgb(L, C < cap ? C : cap, seed.h);
}

// Its circle and controls: one step above the card in the icon's hue (the
// step follows the Circle strength `percent`, kJewelCircleStep at 25 %); a
// raised control two steps.
inline Fill jewel_fill(uint32_t card, uint32_t icon, uint8_t percent, uint8_t strength) {
  card &= 0xFFFFFF;
  icon &= 0xFFFFFF;
  if (percent > 100) percent = 100;
  const Oklch base = from_rgb(card), seed = from_rgb(icon);
  const float cap = ui_theme::kJewelChroma * ui_theme::kJewelCircleChromaCap * (strength ? strength : 20) / 20.0f;
  float C = no_color(icon) ? 0.0f : seed.C * ui_theme::kJewelCircleChromaShare;
  if (C > cap) C = cap;
  const float step = ui_theme::kJewelCircleStep * percent / icon_glow::kDefault;
  const float control = step > kControlMinStep ? step : kControlMinStep;
  Fill result{};
  result.tinted = true;
  result.disc_opa = disc_opa(percent);
  result.control_opa = 255;
  result.disc = percent ? to_rgb(base.L + step, C, seed.h) : card;
  result.control = to_rgb(base.L + control, C, seed.h);
  result.raised = to_rgb(base.L + 2.0f * control, C, seed.h);
  result.disc_color = result.disc;
  result.control_color = result.control;
  result.raised_color = result.raised;
  return result;
}


// The icon as shown: unchanged while it is at least kIconMinStep above the
// circle it gets with the default settings (the default tile color, tile
// color "From icon" at its default strength, the default Circle strength),
// otherwise raised to that step in its own hue. Measured against these fixed
// defaults, the global tile color, the Circle strength and the circle options
// never change an icon's color (user 2026-10-01: a lighter global tile color
// and a stronger circle lifted red, green and blue icons towards pastel);
// a dark icon is lifted the same everywhere.
inline uint32_t readable_icon(uint32_t icon) {
  icon &= 0xFFFFFF;
  // The light theme shows every colored icon in its own color, on every
  // surface (user 2026-10-10: "wir haben doch die echten Farben"): filled
  // tiles and tiles in an own dark color too.
  if (ui_theme::theme_light() && tile_tint::has_hue(icon)) return icon;
  const bool light = ui_theme::light();
  // Tiles and popups restyle often: keep the last few results, keyed with the
  // theme (bit 24; the theme switches while running).
  const uint32_t key = icon | (light ? 0x1000000u : 0u);
  static uint32_t cached_icon[4] = {}, cached_shown[4] = {};
  static uint8_t used = 0, next = 0;
  for (uint8_t i = 0; i < used; ++i) {
    if (cached_icon[i] == key) return cached_shown[i];
  }
  const bool tinted = tile_tint::has_hue(icon);
  const Oklch seed = from_rgb(icon);
  uint32_t shown = icon;
  if (!light) {
    const uint32_t base = tile_color::kDefault;
    const uint32_t card = tinted ? tile_tint::background(base, icon, kReferenceTint) : base;
    const uint32_t circle = lifted(card, icon, tinted, icon_glow::kDefault * kStepPerPercent);
    const float minimum = from_rgb(circle).L + kIconMinStep;
    if (seed.L < minimum) shown = to_rgb(minimum, seed.C, seed.h);
  } else if (icon == ui_theme::text() || (no_color(icon) && seed.L > kWhiteIconL)) {
    // The light theme shows an icon's own color, like Home Assistant (and
    // the bars, sliders and circles that follow it). Only an icon without a
    // color - white, almost white or the text color - takes the icon grey
    // (user 2026-10-10: "Icons haben eine Farbe oder eben nicht").
    shown = ui_theme::icon();
  }
  cached_icon[next] = key;
  cached_shown[next] = shown;
  next = static_cast<uint8_t>((next + 1) % 4);
  if (used < 4) ++used;
  return shown;
}

// The light theme's tile circle and controls (lab 2026-10-10, like Home
// Assistant): the shown icon color at kLightCircleShare of the Circle
// strength over the card (20 % at 25 %), a raised control twice that. Opaque
// cards only; see-through screensaver tiles keep fill().
inline Fill tile_fill(uint32_t card, uint32_t shown, bool tinted, uint8_t percent) {
  card &= 0xFFFFFF;
  shown &= 0xFFFFFF;
  if (percent > 100) percent = 100;
  const float share = percent * ui_theme::kLightCircleShare / 100.0f;
  // Controls stay visible without a circle (kControlMinStep's role).
  const float control = share > 0.1f ? share : 0.1f;
  auto over = [&](float amount) {
    const float clamped = amount > 1.0f ? 1.0f : amount;
    return blend(card, shown, static_cast<uint8_t>(clamped * 255.0f + 0.5f));
  };
  Fill result{};
  result.tinted = tinted;
  result.disc_opa = disc_opa(percent);
  result.control_opa = 255;
  result.disc = percent ? over(share) : card;
  result.control = over(control);
  result.raised = over(2.0f * control);
  result.disc_color = result.disc;
  result.control_color = result.control;
  result.raised_color = result.raised;
  return result;
}

}  // namespace tone_color
