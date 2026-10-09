// A classic guard: HomeTiles.ino and the headers reach this file under two
// path spellings (\ and /), which #pragma once does not see as one file.
#ifndef HOMETILES_UI_THEME_H
#define HOMETILES_UI_THEME_H

#include <stdint.h>

// The UI's color roles for the dark theme (the firmware's original look) and
// the light theme. Every dark value is exactly the color the code used before
// the roles, so the dark theme draws the same pixels. The light theme mirrors
// the dark one's rules: where the dark theme steps lighter on a dark card, the
// light theme steps darker on a light card; its values were set in the
// simulator with the user (2026-10-09, build/analysis/2026-10-09-analyse.md
// "Heller Modus"):
//   text and the head's icons  #1A1A1A   (dark: white)
//   an icon beside a text      #464646   (dark: white)
//   off and unavailable icons  #8B8B8B   (dark: #B0B0B0; the dark step from
//                                         white to #B0B0B0 above #464646)
//   secondary text             #5F6368
//   accents as text            their dark tone (OKLCH L 0.58)
// The theme is read once at boot (ConfigManager "theme"); a change applies
// after a restart, like the layout.
namespace ui_theme {

enum class Theme : uint8_t { Dark = 0, Light = 1 };

inline Theme g_theme = Theme::Dark;

inline bool light() { return g_theme == Theme::Light; }
inline void set(uint8_t stored) { g_theme = stored == 1 ? Theme::Light : Theme::Dark; }

// The screen behind tiles, head and Settings.
inline uint32_t screen() { return light() ? 0xE9EBEE : 0x000000; }
// The card of a tile, group or popup without its own color.
inline uint32_t card() { return light() ? 0xFFFFFF : 0x1A1A1A; }
// A popup's card when its opener passes no color (popup_surface::default_card).
inline uint32_t popup_card() { return light() ? card() : 0x2A2A2A; }
// Text, values and the head's icons (Home, folder, gear, X).
inline uint32_t text() { return light() ? 0x1A1A1A : 0xFFFFFF; }
// An icon beside a text (tile icons, Settings rows, media controls) and the
// white fills of the dark theme (the play circle, slider knobs).
inline uint32_t icon() { return light() ? 0x464646 : 0xFFFFFF; }
// Off: a switched-off light or switch.
inline uint32_t icon_off() { return light() ? 0x8B8B8B : 0xB0B0B0; }
// Inactive: an unavailable entity, the device grey (fan off, disarmed); the
// light theme's off grey.
inline uint32_t icon_inactive() { return light() ? 0x8B8B8B : 0x9E9E9E; }
// The dark theme's resting grey (folders, a binary sensor at rest, a closed
// cover, the gear): a normal icon in the light theme.
inline uint32_t icon_rest() { return light() ? 0x464646 : 0x9E9E9E; }
// Secondary text: subtitles, the media artist.
inline uint32_t text_secondary() { return light() ? 0x5F6368 : 0xD8DEE9; }
// The softer text of the dark theme's #D8D8D8 (pressed play circle, captions).
inline uint32_t text_soft() { return light() ? 0x3A3A3A : 0xD8D8D8; }

}  // namespace ui_theme

#endif  // HOMETILES_UI_THEME_H
