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

// A row that reacts to a tap: `pressed` fills it while the finger is down,
// with the group's rounding where it touches the group's edge.
void make_tap(lv_obj_t* row, uint32_t pressed, lv_event_cb_t on_click, void* user_data);
// A grey value on the right of a row, at most `max_width` wide (ellipsis).
lv_obj_t* trailing_text(lv_obj_t* row, const char* text, int max_width);
// A grey icon on the right of a row (chevrons).
lv_obj_t* trailing_icon(lv_obj_t* row, const char* icon_name);

// The option list of a dropdown row (mockup .ddl, like the Select popup's
// list): the row's width, a small gap below the row, the card color with the
// tile hairline, option text aligned with the row labels, the selected option
// inset in `selected_color`. It opens below the row when it fits there, else
// above; when neither side holds it whole, on the larger side, and it scrolls
// in itself. It never leaves `bounds`. One list at a time; a tap beside it
// only closes it.
struct OptionHandler {
  const char* (*text)(uint8_t tag, uint8_t index);
  void (*picked)(uint8_t tag, uint8_t index);
  // After a tap closed the list: `tapped` is where a tap beside the list
  // landed, nullptr after a pick.
  void (*closed)(uint8_t tag, const lv_point_t* tapped);
};
struct OptionList {
  lv_obj_t* host;  // covered while the list is open (taps beside it close it)
  lv_obj_t* row;
  lv_area_t bounds;
  uint8_t tag;
  uint8_t count;
  uint8_t selected;
  uint32_t card;
  uint32_t selected_color;
  int text_x;  // the row labels' x from the row's left edge
  OptionHandler handler;
};
void open_options(const OptionList& list);
// Closes an open list without calling its handler.
void close_options();

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

// Text where a browser draws it (the mockup is the reference). The firmware's
// Inter fonts have taller line boxes than a browser's (20 px: 27 instead of
// 24.2) with the baseline lower in them, so stacked and centered labels moved
// down. A label in a flex layout gets margins that give it the browser's
// line height and baseline; `px` is its font size.
void browser_line(lv_obj_t* label, int px, int margin_top = 0, int margin_bottom = 0);
// The browser's line height of an Inter font size ("normal", 1.21 em).
float browser_line_height(int px);
// The y of a label placed by hand: its baseline where a browser puts it in a
// line box `box` px high whose top is at `box_top`.
int browser_label_y(const lv_font_t* font, int px, float box_top, float box);

// The width of `text` in `font`.
int text_width(const lv_font_t* font, const char* text);
// The width of the device's MDI icon glyphs.
int icon_width();

}  // namespace settings_parts
