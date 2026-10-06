#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "src/ui/tabs/settings/settings_style.h"

// Building blocks of the Settings pages and the first-start setup, as in the
// approved mockup (settings-menu.html): section headings, groups of rows with
// hairline separators, rows (grey icon, title, optional grey sub line, a
// control on the right), sliders like the tiles' brightness bars, segment
// switches. Every part takes its colors from settings_style::Colors.
namespace settings_parts {

// A transparent container without padding, border or scrolling.
lv_obj_t* plain(lv_obj_t* parent);

// Grey heading above a group; `first` drops the space above it.
lv_obj_t* section(lv_obj_t* parent, const char* text, bool first);

// A rounded group of rows in the group color.
lv_obj_t* group(lv_obj_t* parent, const settings_style::Colors& colors);

struct Row {
  lv_obj_t* row = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* sub = nullptr;
  // Holds the title and the sub line; takes the free width.
  lv_obj_t* text = nullptr;
};

// One row of a group: grey icon, title (one line, ellipsis) and an optional
// grey sub line. Controls added afterwards sit on the right. A row after the
// first one gets the hairline separator.
Row row(lv_obj_t* group, const char* icon_name, const char* title, const char* sub = nullptr);

// Right-aligned value text of a slider row (fixed column width).
lv_obj_t* value_label(lv_obj_t* row, const char* text);

// Slider like the tiles' brightness bars: the track in `track`, the fill in
// `accent` (at least a circle wide), a short line in `thumb` near the fill's
// end. `on_change` runs while dragging (final = false) and once on release
// (final = true).
using SliderCallback = void (*)(lv_obj_t* slider, int32_t value, bool final);
lv_obj_t* slider(lv_obj_t* row, int width, int32_t min, int32_t max, int32_t value, uint32_t accent,
                 uint32_t track, uint32_t thumb, SliderCallback on_change);
void slider_set_value(lv_obj_t* slider, int32_t value);
int32_t slider_value(lv_obj_t* slider);

// Segment switch: a track in the card color with the selected option on a
// light pill. `on_select` gets the option index.
using SegmentCallback = void (*)(lv_obj_t* segment, uint8_t index);
lv_obj_t* segment(lv_obj_t* row, const char* const* labels, uint8_t count, uint8_t selected,
                  uint32_t track, SegmentCallback on_select, int min_width = settings_style::kSegmentMinWidth,
                  int pad = settings_style::kSegmentPad);
void segment_select(lv_obj_t* segment, uint8_t index);

// The width of `text` in `font`.
int text_width(const lv_font_t* font, const char* text);
// The width of the device's MDI icon glyphs.
int icon_width();

}  // namespace settings_parts
