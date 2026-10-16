#pragma once

#include <Arduino.h>

#include "src/tiles/config/tile_config.h"

// Per-tile look of a screensaver tile (Tile::ss_*): its fill opacity,
// whether its background is filled, an own card color, whether its icon
// shows and an own text color. Stored in /_screensaver/tile_looks.json by
// ScreensaverConfigStore; normal grids keep the defaults and their own color
// rule (color from the icon, "Fill").
namespace screensaver_tile_look {

// Set bit of a stored color (Tile::ss_card_color, Tile::ss_text_color):
// black stays a valid own color.
inline constexpr uint32_t kColorSet = 0x01000000u;

inline bool has_card_color(const Tile& tile) { return (tile.ss_card_color & kColorSet) != 0; }
inline uint32_t card_rgb(const Tile& tile) { return tile.ss_card_color & 0xFFFFFFu; }

inline bool has_text_color(const Tile& tile) { return (tile.ss_text_color & kColorSet) != 0; }
inline uint32_t text_rgb(const Tile& tile) { return tile.ss_text_color & 0xFFFFFFu; }

// "#RRGGBB" -> stored value; anything else is no own color.
inline uint32_t parse_color(const String& raw) {
  String text = raw;
  text.trim();
  if (text.length() != 7 || text[0] != '#') return 0;
  uint32_t rgb = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = text[i];
    uint32_t nibble;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    else return 0;
    rgb = (rgb << 4) | nibble;
  }
  return kColorSet | rgb;
}

inline String format_color(uint32_t stored) {
  if ((stored & kColorSet) == 0) return String();
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06X", static_cast<unsigned>(stored & 0xFFFFFFu));
  return String(hex);
}

// Background opacity of a screensaver card. A tile's own Opacity is the real
// opacity of its card, like the clock card's (50 % draws at 50 %); a tile that
// leaves it at its default (255) takes the global fill opacity
// (ScreensaverConfigData::tile_opacity). Only the fill fades, never the text
// or the icon.
inline uint8_t fill_opacity(uint8_t global_opacity, const Tile& tile) {
  return tile.ss_opacity == 255 ? global_opacity : tile.ss_opacity;
}

inline bool is_default(const Tile& tile) {
  return tile.ss_opacity == 255 && tile.ss_fill && tile.ss_show_icon && !has_card_color(tile) &&
         !has_text_color(tile);
}

inline void reset(Tile& tile) {
  tile.ss_opacity = 255;
  tile.ss_fill = true;
  tile.ss_show_icon = true;
  tile.ss_card_color = 0;
  tile.ss_text_color = 0;
}

}  // namespace screensaver_tile_look
