#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/icons/mdi_icons.h"

// A rule's own icon (tile_icon_colors.h "RRGGBB,<icon>", user 2026-10-28:
// an icon per rule) on an MDI icon label. While a rule shows its icon, the
// label keeps the codepoint of the tile's own icon and of the rule's icon as
// local style values in a selector no label state uses, so the tile's icon
// comes back when no rule with an icon applies. A renderer that changes the
// tile's icon meanwhile (state icons) changes only what comes back
// (set_glyph); a text set past it becomes the new own icon on the next show.
// Nothing is allocated.
namespace tile_rule_icon {

inline constexpr lv_style_selector_t kStore = LV_PART_MAIN | LV_STATE_USER_2 | LV_STATE_USER_3 | LV_STATE_USER_4;

// The codepoint of a text that is exactly one character, else 0.
inline uint32_t codepoint_of(const char* text) {
  if (!text || !*text) return 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
  const size_t length = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3
                        : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
  if (!length) return 0;
  static constexpr uint8_t kLeadBits[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
  uint32_t cp = p[0] & kLeadBits[length];
  for (size_t i = 1; i < length; ++i) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  return p[length] ? 0 : cp;
}

inline void encode(uint32_t cp, char (&out)[5]) {
  size_t n = 0;
  if (cp < 0x80) {
    out[n++] = static_cast<char>(cp);
  } else if (cp < 0x800) {
    out[n++] = static_cast<char>(0xC0 | (cp >> 6));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out[n++] = static_cast<char>(0xE0 | (cp >> 12));
    out[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out[n++] = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
    out[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[n++] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  out[n] = '\0';
}

// Codepoints fit the 24 bits of a color value.
inline bool stored(lv_obj_t* label, lv_style_prop_t prop, uint32_t& cp) {
  lv_style_value_t value;
  if (lv_obj_get_local_style_prop(label, prop, &value, kStore) != LV_STYLE_RES_FOUND) return false;
  cp = lv_color_to_u32(value.color) & 0xFFFFFF;
  return true;
}

inline void keep(lv_obj_t* label, lv_style_prop_t prop, uint32_t cp) {
  lv_style_value_t value;
  value.color = lv_color_hex(cp);
  lv_obj_set_local_style_prop(label, prop, value, kStore);
}

// A label reused for another tile (a popup header) forgets an earlier rule
// icon without touching its text.
inline void forget(lv_obj_t* label) {
  if (!label) return;
  lv_obj_remove_local_style_prop(label, LV_STYLE_BORDER_COLOR, kStore);
  lv_obj_remove_local_style_prop(label, LV_STYLE_BG_COLOR, kStore);
}

// The codepoint of the rule icon a label shows; false without one.
inline bool shown(lv_obj_t* label, uint32_t& cp) {
  uint32_t own = 0;
  return label && stored(label, LV_STYLE_BORDER_COLOR, cp) && stored(label, LV_STYLE_BG_COLOR, own);
}

// Shows the rule icon `cp`, or with 0 the tile's own icon again. A label
// without a single glyph (a hidden or image icon) stays as it is.
inline void show_codepoint(lv_obj_t* label, uint32_t cp) {
  if (!label) return;
  uint32_t own = 0;
  uint32_t rule = 0;
  bool showing = stored(label, LV_STYLE_BORDER_COLOR, rule) && stored(label, LV_STYLE_BG_COLOR, own);
  const uint32_t current = codepoint_of(lv_label_get_text(label));
  if (showing && current != rule) {
    // The text changed past set_glyph: that is the tile's own icon now.
    if (current) {
      own = current;
      keep(label, LV_STYLE_BG_COLOR, own);
    } else {
      forget(label);
      showing = false;
    }
  }
  char text[5];
  if (!cp) {
    if (!showing) return;
    forget(label);
    if (current != own) {
      encode(own, text);
      lv_label_set_text(label, text);
    }
    return;
  }
  if (!showing) {
    if (!current) return;
    keep(label, LV_STYLE_BG_COLOR, current);
  }
  keep(label, LV_STYLE_BORDER_COLOR, cp);
  if (current != cp) {
    encode(cp, text);
    lv_label_set_text(label, text);
  }
}

// Shows a rule's icon by name (unknown names show the panel's unknown-icon
// glyph like every tile icon), or the tile's own icon for an empty one.
inline void show(lv_obj_t* label, const tile_icon_colors::Icon& icon) {
  if (!icon.length || icon.length > tile_icon_colors::kMaxIconBytes) {
    show_codepoint(label, 0);
    return;
  }
  char name[tile_icon_colors::kMaxIconBytes + 1];
  memcpy(name, icon.name, icon.length);
  name[icon.length] = '\0';
  show_codepoint(label, getMdiCodepoint(String(name)));
}

// A renderer's own icon text for a tile icon label: shown at once (an
// unchanged text is not set again), or kept to come back later while a rule
// shows its icon. An empty text ends a rule icon.
inline void set_glyph(lv_obj_t* label, const char* text) {
  if (!label || !text) return;
  uint32_t rule = 0;
  const uint32_t cp = codepoint_of(text);
  if (shown(label, rule)) {
    if (cp) {
      keep(label, LV_STYLE_BG_COLOR, cp);
      return;
    }
    forget(label);
  }
  const char* current = lv_label_get_text(label);
  if (!current || strcmp(current, text) != 0) lv_label_set_text(label, text);
}

}  // namespace tile_rule_icon
