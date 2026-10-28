#pragma once

#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_rule_icon.h"
#include "src/ui/shared/ui_theme.h"

namespace tile_icon_color_rules {

// Per-tile icon colors (tile_icon_colors.h) on a state update of a Sensor
// family, Binary sensor or Energy tile: the color bar for numeric states or
// the first matching state color, else the tile's fixed icon color, else
// `fallback`, the type's own color for this state. Unavailable or unknown
// states (`known` false) always use `fallback`. The color goes through
// tile_icon_disc::set_icon_color(), so the disc glow follows; unchanged colors
// are skipped and nothing is allocated.
// A state line's own "Fuellen" as the card keeps it (tile_icon_disc rule_fill).
inline uint8_t rule_fill_of(tile_icon_colors::RowFill fill) {
  return fill == tile_icon_colors::RowFill::On ? tile_icon_disc::kRuleFillOn
         : fill == tile_icon_colors::RowFill::Off ? tile_icon_disc::kRuleFillOff
                                                  : tile_icon_disc::kRuleFillNone;
}

// The matching state line's own "Fuellen" (tile_icon_colors::state_fill)
// goes to the tile card; the icon color hook then fills or clears the card
// (tile_icon_source on_icon_color). Rules on another entity set it in
// tile_icon_source::refresh_card.
inline bool note_fill(lv_obj_t* card, const char* record, bool known, const char* state, const char* display) {
  if (!card || !record) return false;
  if (tile_icon_colors::source_of(record).mode != tile_icon_colors::SourceMode::None &&
      !tile_icon_colors::own_state_colors_icon(record)) {
    return false;
  }
  return tile_icon_disc::set_rule_fill(card, rule_fill_of(tile_icon_colors::state_fill(record, known, state, display)));
}

// The matching own rule's icon (tile_icon_colors::state_icon), else the
// tile's own icon again (tile_rule_icon.h). Rules of a layer (another
// entity, switched off) leave the icon to tile_icon_source::refresh_card.
inline void apply_icon(lv_obj_t* icon, const char* record, bool known, const char* state, const char* display) {
  if (!icon || !record || !tile_icon_colors::own_state_colors_icon(record)) return;
  tile_rule_icon::show(icon, tile_icon_colors::state_icon(record, known, state, display));
}

inline void apply(lv_obj_t* icon, const char* record, bool known,
                  const char* state, const char* display, lv_color_t fallback) {
  if (!icon) return;
  apply_icon(icon, record, known, state, display);
  // Rules on another entity, switched off or not coloring the icon leave the
  // icon to the fixed color here; the rule layer forces its own color.
  const lv_color_t color = lv_color_hex(tile_icon_colors::state_icon_color(
      record, known, state, display, lv_color_to_u32(fallback) & 0xFFFFFF));
  // A changed row fill shows even when the color stays (two rows, one color).
  const bool fill_changed = note_fill(tile_surface::card_of(icon), record, known, state, display);
  lv_color_t forced;
  if (!fill_changed && !tile_icon_disc::forced_color(icon, forced) &&
      lv_color_eq(lv_obj_get_style_text_color(icon, LV_PART_MAIN), color)) {
    return;
  }
  tile_icon_disc::set_icon_color(icon, color);
}

// Icon-and-title tiles (Scene, Folder, Back, Camera) have no entity state:
// the icon takes the tile's fixed icon color, else stays white. Called once
// when the tile is built; the disc reads the color when its options apply.
inline void apply_fixed(lv_obj_t* icon, const char* record) {
  apply(icon, record, true, "", nullptr, lv_color_hex(ui_theme::text()));
}

}  // namespace tile_icon_color_rules
