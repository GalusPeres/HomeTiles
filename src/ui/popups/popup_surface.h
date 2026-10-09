#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "src/ui/shared/ui_theme.h"

// Popups inherit the look of their tile: the card is the tile's current
// background (own color or rules tint, tile_icon_source::popup_background).
// Neutral surfaces inside a popup (tracks, pills, pressed and disabled fills,
// cut-out text) are therefore derived from the card color instead of fixed
// greys: a lighter step is white mixed into the card, so a grey card keeps
// today's greys and a colored card gets matching lighter shades. Colored
// accents (HVAC mode, light and cover colors) stay as they are.
namespace popup_surface {

// The popup card when a caller passes no color (0); the light theme's card.
inline uint32_t default_card() { return ui_theme::popup_card(); }

inline uint32_t card_or_default(uint32_t card) { return card ? card : default_card(); }

// White shares (lv_color_mix, LV_COLOR_MIX_ROUND_OFS 0) that reproduce the
// former fixed greys exactly on the default 0x2A2A2A card.
inline constexpr lv_opa_t kPill = 15;       // 0x363636: pills, toggles, menus
inline constexpr lv_opa_t kTrack = 32;      // 0x444444: arc and slider tracks
inline constexpr lv_opa_t kPressed = 58;    // 0x5A5A5A: pressed pills
inline constexpr lv_opa_t kDisabled = 78;   // 0x6B6B6B: disabled icons and dashes
inline constexpr lv_opa_t kMarker = 151;    // 0xA8A8A8: light neutral markers

// A lighter step of the card: `step` of white mixed into it.
inline lv_color_t lighter(uint32_t card, lv_opa_t step) {
  // The light theme steps towards its dark text, so pills, tracks and
  // captions stay visible on a white card.
  const lv_color_t toward = ui_theme::light() ? lv_color_hex(ui_theme::text()) : lv_color_white();
  return lv_color_mix(toward, lv_color_hex(card_or_default(card)), step);
}

// The card color itself, for cut-out text and notches drawn on white or
// colored surfaces.
inline lv_color_t card(uint32_t card) { return lv_color_hex(card_or_default(card)); }

}  // namespace popup_surface
