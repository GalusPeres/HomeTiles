#pragma once

#include <lvgl.h>

#include "src/ui/popups/popup_layout.h"

// The PIN entry's keypad geometry, shared with the Alarm popup, whose mode
// keys fill the same block (user 01.10.).
namespace pin_keypad {

// The keypad layout, shared by every screen size: all distances are fixed
// shares of the key height (percent), so a small and a large display look
// the same. From top to bottom below the header: the prompt ("Enter PIN" or
// the error), the dots line (one dot per digit, never a digit), four key
// rows.
constexpr int kKeyGapPct = 16;        // between keys
constexpr int kPromptGapPct = 20;     // prompt to the dots line
constexpr int kDotsGapPct = 26;       // dots line to the keys
constexpr int kMarginPct = 8;         // at least this above and below the block
constexpr int kKeyWidthPct = 130;     // keys are a bit wider than tall
constexpr int kDotPct = 22;           // dot diameter, share of the key height
// Keys are never rounder than a tile and follow the global tile radius
// (ui_surface_style::apply_radius).
constexpr int kKeyRadius = tile_radius::kMinimum;

// Positions below the header, in the card's content coordinates.
struct KeypadGeometry {
  int key_w = 0;
  int key_h = 0;
  int gap = 0;
  int prompt_y = 0;
  int prompt_h = 0;
  int dots_y = 0;
  int keys_x = 0;
  int keys_y = 0;
  int dot = 0;
};

inline KeypadGeometry keypad_geometry(lv_obj_t* card, const lv_font_t* prompt_font) {
  KeypadGeometry g;
  const int pad = lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  const int content_w = popup_layout::kContentWidth;
  const int content_h = popup_layout::kCardHeight - 2 * pad;
  const int top = popup_layout::kHeaderCenterY - pad + popup_layout::kHeaderIconDiscSize / 2;
  // The prompt and the dots line are one text line high each.
  g.prompt_h = lv_font_get_line_height(prompt_font);
  const int available = content_h - top;
  // Height: prompt, dots line and the key-height shares fill the space below
  // the header; width: three keys and two gaps fit the content width.
  const int height_pct = 400 + 3 * kKeyGapPct + kPromptGapPct + kDotsGapPct + 2 * kMarginPct;
  const int width_pct = 3 * kKeyWidthPct + 2 * kKeyGapPct;
  g.key_h = (available - 2 * g.prompt_h) * 100 / height_pct;
  const int width_limit = content_w * 100 / width_pct;
  if (width_limit < g.key_h) g.key_h = width_limit;
  const int size_limit = popup_layout::kCardHeight * popup_layout::kKeypadKeyMaxPermille / 1000;
  if (size_limit < g.key_h) g.key_h = size_limit;
  if (g.key_h < 1) g.key_h = 1;
  g.key_w = g.key_h * kKeyWidthPct / 100;
  g.gap = g.key_h * kKeyGapPct / 100;
  const int prompt_gap = g.key_h * kPromptGapPct / 100;
  const int dots_gap = g.key_h * kDotsGapPct / 100;
  const int block = 2 * g.prompt_h + prompt_gap + dots_gap + 4 * g.key_h + 3 * g.gap;
  g.keys_x = (content_w - (3 * g.key_w + 2 * g.gap)) / 2;
  g.dot = g.key_h * kDotPct / 100;
  if (g.dot > g.prompt_h) g.dot = g.prompt_h;
  // The keys stay where the centered block puts them. Above them the prompt
  // and the dots split the room into three equal visible gaps: header to the
  // prompt's capitals, prompt baseline to the dots, dots to the keys.
  g.keys_y = top + (available - block) / 2 + 2 * g.prompt_h + prompt_gap + dots_gap;
  const int baseline = g.prompt_h - prompt_font->base_line;  // from the label top
  int cap_top = 0;
  lv_font_glyph_dsc_t cap;
  if (lv_font_get_glyph_dsc(prompt_font, &cap, 'E', 0)) cap_top = baseline - cap.box_h - cap.ofs_y;
  const int even = (g.keys_y - top - (baseline - cap_top) - g.dot) / 3;
  g.prompt_y = top + even - cap_top;
  g.dots_y = g.prompt_y + baseline + even - (g.prompt_h - g.dot) / 2;
  return g;
}

}  // namespace pin_keypad
