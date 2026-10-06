#include "src/ui/tabs/settings/settings_parts.h"

#include <cstring>

#include "src/fonts/ui_fonts.h"
#include "src/tiles/icons/mdi_icons.h"

namespace settings_parts {
namespace {

using namespace settings_style;

void grey_text(lv_obj_t* label) {
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_opa(label, kGreyOpa, 0);
}

// ---------- Slider ----------

struct SliderState {
  int32_t min;
  int32_t max;
  int32_t value;
  uint32_t accent;
  uint32_t thumb;
  SliderCallback on_change;
};

SliderState* slider_state(lv_obj_t* slider) {
  return slider ? static_cast<SliderState*>(lv_obj_get_user_data(slider)) : nullptr;
}

void draw_rect(lv_layer_t* layer, const lv_area_t& area, uint32_t color, int32_t radius) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.bg_color = lv_color_hex(color);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = radius;
  lv_draw_rect(layer, &dsc, &area);
}

// Fill width: one circle (the track height) at the minimum, the whole track
// at the maximum (mockup .sl .f).
int32_t fill_width(const SliderState& s, int32_t width, int32_t height) {
  if (s.max <= s.min || width <= height) return width < height ? width : height;
  const int64_t span = s.max - s.min;
  const int64_t step = static_cast<int64_t>(s.value - s.min) * (width - height);
  return height + static_cast<int32_t>((step + span / 2) / span);
}

void slider_draw_cb(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  SliderState* s = slider_state(slider);
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!s || !layer) return;
  lv_area_t area;
  lv_obj_get_coords(slider, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  if (width < 4 || height < 4) return;
  int32_t radius = lv_obj_get_style_radius(slider, LV_PART_MAIN);
  if (radius > height / 2) radius = height / 2;
  const int32_t fill = fill_width(*s, width, height);
  const lv_area_t fill_area = {area.x1, area.y1, area.x1 + fill - 1, area.y2};
  draw_rect(layer, fill_area, s->accent, radius);
  // The thumb: a short line in the card color, 40 % of the track high, its
  // right edge half a track height minus 2 px before the fill's end.
  const int32_t thumb_right = area.x1 + fill - (height / 2 - 2);
  const int32_t thumb_h = (height * 40 + 50) / 100;
  const int32_t thumb_y = area.y1 + (height - thumb_h) / 2;
  const lv_area_t thumb = {thumb_right - kSliderThumbWidth, thumb_y, thumb_right - 1, thumb_y + thumb_h - 1};
  draw_rect(layer, thumb, s->thumb, 2);
}

int32_t value_at_point(lv_obj_t* slider, const SliderState& s) {
  lv_indev_t* indev = lv_indev_active();
  if (!indev) return s.value;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t area;
  lv_obj_get_coords(slider, &area);
  const int32_t width = lv_area_get_width(&area);
  const int32_t height = lv_area_get_height(&area);
  const int32_t travel = width - height;
  if (travel <= 0 || s.max <= s.min) return s.value;
  // The finger is the fill's end: half a circle inside it (mockup applyDrag).
  int32_t x = point.x - area.x1 - height / 2;
  if (x < 0) x = 0;
  if (x > travel) x = travel;
  const int64_t span = s.max - s.min;
  return s.min + static_cast<int32_t>((static_cast<int64_t>(x) * span + travel / 2) / travel);
}

void slider_event_cb(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  SliderState* s = slider_state(slider);
  if (!s) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DELETE) {
    lv_obj_set_user_data(slider, nullptr);
    lv_free(s);
    return;
  }
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    const int32_t value = value_at_point(slider, *s);
    // A press always reports once, so a tap on the current value still
    // previews it (the screensaver brightness).
    if (value == s->value && code == LV_EVENT_PRESSING) return;
    s->value = value;
    lv_obj_invalidate(slider);
    if (s->on_change) s->on_change(slider, value, false);
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (s->on_change) s->on_change(slider, s->value, true);
  }
}

// ---------- Segment ----------

struct SegmentState {
  SegmentCallback on_select;
  uint8_t selected;
};

void style_option(lv_obj_t* option, bool selected) {
  lv_obj_set_style_bg_color(option, lv_color_hex(kSelectedBg), 0);
  lv_obj_set_style_bg_opa(option, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_t* label = lv_obj_get_child(option, 0);
  if (label) {
    lv_obj_set_style_text_color(label, selected ? lv_color_hex(kSelectedText) : lv_color_white(), 0);
  }
}

void segment_option_cb(lv_event_t* e) {
  lv_obj_t* option = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_obj_t* segment = lv_obj_get_parent(option);
  SegmentState* s = segment ? static_cast<SegmentState*>(lv_obj_get_user_data(segment)) : nullptr;
  if (!s) return;
  const uint8_t index = static_cast<uint8_t>(lv_obj_get_index(option));
  if (index == s->selected) return;
  segment_select(segment, index);
  if (s->on_select) s->on_select(segment, index);
}

void segment_delete_cb(lv_event_t* e) {
  lv_obj_t* segment = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  void* state = lv_obj_get_user_data(segment);
  lv_obj_set_user_data(segment, nullptr);
  lv_free(state);
}

// ---------- Tappable row ----------

// The pressed fill: a rounded rectangle in the group's rounding that reaches
// past the row where another row follows, so the row's clip area cuts it
// square there (mockup .grp > .row:first-child / :last-child).
void tap_draw_cb(lv_event_t* e) {
  lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);
  if (!layer || !lv_obj_has_state(row, LV_STATE_PRESSED)) return;
  lv_obj_t* group = lv_obj_get_parent(row);
  const int32_t index = lv_obj_get_index(row);
  const int32_t count = static_cast<int32_t>(lv_obj_get_child_count(group));
  const int32_t radius = lv_obj_get_style_radius(group, LV_PART_MAIN);
  lv_area_t area;
  lv_obj_get_coords(row, &area);
  if (index > 0) area.y1 -= radius;
  if (index + 1 < count) area.y2 += radius;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.base.layer = layer;
  dsc.bg_color = lv_obj_get_style_bg_color(row, LV_PART_MAIN);
  dsc.bg_opa = LV_OPA_COVER;
  dsc.border_opa = LV_OPA_TRANSP;
  dsc.radius = radius;
  lv_draw_rect(layer, &dsc, &area);
}

void tap_state_cb(lv_event_t* e) { lv_obj_invalidate(static_cast<lv_obj_t*>(lv_event_get_current_target(e))); }

// ---------- Option list ----------

struct OptionState {
  lv_obj_t* overlay;
  uint8_t tag;
  OptionHandler handler;
  // A tap waiting for the list to close (after the event that reported it).
  bool pick_pending;
  uint8_t pick;
  bool tap_pending;
  lv_point_t tap;
};
OptionState g_options = {};

void overlay_delete_cb(lv_event_t* e) {
  if (lv_event_get_current_target(e) == g_options.overlay) g_options.overlay = nullptr;
}

// Closing deletes the list, so it waits until the touch event is over.
void options_async_cb(void*) {
  const OptionHandler handler = g_options.handler;
  const uint8_t tag = g_options.tag;
  const bool picked = g_options.pick_pending;
  const uint8_t pick = g_options.pick;
  const bool tapped = g_options.tap_pending;
  const lv_point_t point = g_options.tap;
  g_options.pick_pending = false;
  g_options.tap_pending = false;
  close_options();
  if (handler.closed) handler.closed(tag, tapped ? &point : nullptr);
  if (picked && handler.picked) handler.picked(tag, pick);
}

void option_clicked_cb(lv_event_t* e) {
  if (g_options.pick_pending || g_options.tap_pending) return;
  g_options.pick = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_options.pick_pending = true;
  lv_async_call(options_async_cb, nullptr);
}

void overlay_clicked_cb(lv_event_t*) {
  if (g_options.pick_pending || g_options.tap_pending) return;
  lv_indev_t* indev = lv_indev_active();
  g_options.tap = {0, 0};
  if (indev) lv_indev_get_point(indev, &g_options.tap);
  g_options.tap_pending = true;
  lv_async_call(options_async_cb, nullptr);
}

}  // namespace

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

lv_obj_t* section(lv_obj_t* parent, const char* text, bool first) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_font(label, small_font(), 0);
  grey_text(label);
  lv_obj_set_style_margin_left(label, kSectionLeft, 0);
  browser_line(label, kSmallFontPx, first ? 0 : kSectionTop, kSectionBottom);
  return label;
}

lv_obj_t* group(lv_obj_t* parent, const Colors& colors) {
  lv_obj_t* g = plain(parent);
  lv_obj_set_size(g, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(g, lv_color_hex(colors.group), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
  settings_style::apply_radius(g, kGroupRadius);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_COLUMN);
  return g;
}

Row row(lv_obj_t* group, const char* icon_name, const char* title, const char* sub) {
  Row r;
  const bool first = lv_obj_get_child_count(group) == 0;
  r.row = plain(group);
  lv_obj_set_size(r.row, LV_PCT(100), kRowHeight);
  lv_obj_set_style_pad_left(r.row, kRowPadLeft, 0);
  lv_obj_set_style_pad_right(r.row, kRowPadRight, 0);
  lv_obj_set_style_pad_column(r.row, kRowGap, 0);
  lv_obj_set_flex_flow(r.row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (!first) {
    // Hairline between rows, inset like the row's content.
    lv_obj_t* line = plain(r.row);
    lv_obj_add_flag(line, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(line, LV_PCT(100), 1);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(line, kSeparatorOpa, 0);
  }
  if (icon_name && icon_name[0]) {
    r.icon = lv_label_create(r.row);
    lv_label_set_text(r.icon, getMdiChar(icon_name).c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(r.icon, FONT_MDI_ICONS, 0);
    grey_text(r.icon);
  }
  r.text = plain(r.row);
  lv_obj_set_size(r.text, 1, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(r.text, 1);
  lv_obj_set_flex_flow(r.text, LV_FLEX_FLOW_COLUMN);
  // Its labels reach a little past it (browser_line).
  lv_obj_add_flag(r.text, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  r.title = lv_label_create(r.text);
  lv_label_set_text(r.title, title ? title : "");
  lv_label_set_long_mode(r.title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(r.title, LV_PCT(100));
  lv_obj_set_style_text_font(r.title, row_font(), 0);
  lv_obj_set_style_text_color(r.title, lv_color_white(), 0);
  browser_line(r.title, kRowFontPx);
  if (sub) {
    r.sub = lv_label_create(r.text);
    lv_label_set_text(r.sub, sub);
    lv_label_set_long_mode(r.sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r.sub, LV_PCT(100));
    lv_obj_set_style_text_font(r.sub, small_font(), 0);
    grey_text(r.sub);
    browser_line(r.sub, kSmallFontPx, kSubTop);
  }
  return r;
}

lv_obj_t* value_label(lv_obj_t* row, const char* text) {
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(label, kValueWidth);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(label, row_font(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  browser_line(label, kRowFontPx);
  return label;
}

void make_tap(lv_obj_t* row, uint32_t pressed, lv_event_cb_t on_click, void* user_data) {
  if (!row) return;
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  // The fill color rides on the row's (transparent) background.
  lv_obj_set_style_bg_color(row, lv_color_hex(pressed), 0);
  lv_obj_add_event_cb(row, tap_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
  lv_obj_add_event_cb(row, tap_state_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(row, tap_state_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(row, tap_state_cb, LV_EVENT_PRESS_LOST, nullptr);
  if (on_click) lv_obj_add_event_cb(row, on_click, LV_EVENT_CLICKED, user_data);
}

lv_obj_t* trailing_text(lv_obj_t* row, const char* text, int max_width) {
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label, row_font(), 0);
  const int width = text_width(row_font(), text);
  lv_obj_set_width(label, width < max_width ? width : max_width);
  grey_text(label);
  browser_line(label, kRowFontPx);
  return label;
}

lv_obj_t* trailing_icon(lv_obj_t* row, const char* icon_name) {
  lv_obj_t* icon = lv_label_create(row);
  lv_label_set_text(icon, getMdiChar(icon_name).c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  grey_text(icon);
  return icon;
}

void open_options(const OptionList& spec) {
  close_options();
  if (!spec.host || !spec.row || spec.count == 0 || !spec.handler.text) return;
  lv_obj_update_layout(spec.host);
  lv_area_t host_area;
  lv_area_t host_content;
  lv_area_t row_area;
  lv_obj_get_coords(spec.host, &host_area);
  lv_obj_get_content_coords(spec.host, &host_content);
  lv_obj_get_coords(spec.row, &row_area);

  // The cover over the whole host catches taps beside the list.
  lv_obj_t* overlay = plain(spec.host);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(overlay, host_area.x1 - host_content.x1, host_area.y1 - host_content.y1);
  lv_obj_set_size(overlay, lv_area_get_width(&host_area), lv_area_get_height(&host_area));
  lv_obj_add_event_cb(overlay, overlay_clicked_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(overlay, overlay_delete_cb, LV_EVENT_DELETE, nullptr);
  g_options = {};
  g_options.overlay = overlay;
  g_options.tag = spec.tag;
  g_options.handler = spec.handler;

  // Height: the options, their inset and the hairline, six at most; then
  // the side with room (mockup placeDropdown).
  const int edge = kOptionInset + 1;
  const int visible = spec.count < kOptionsVisible ? spec.count : kOptionsVisible;
  int height = visible * kOptionHeight + 2 * edge;
  const int below = spec.bounds.y2 + 1 - (row_area.y2 + 1) - kOptionInset;
  const int above = row_area.y1 - spec.bounds.y1 - kOptionInset;
  const bool down = below >= height || below >= above;
  const int room = down ? below : above;
  if (height > room) height = room;
  const int y = down ? row_area.y2 + 1 + kOptionInset : row_area.y1 - kOptionInset - height;

  lv_obj_t* list = plain(overlay);
  // Taps on its padding stay in the list.
  lv_obj_add_flag(list, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_pos(list, row_area.x1 - host_area.x1, y - host_area.y1);
  lv_obj_set_size(list, lv_area_get_width(&row_area), height);
  lv_obj_set_style_bg_color(list, lv_color_hex(spec.card), 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
  settings_style::apply_radius(list, kGroupRadius);
  ui_surface_style::apply_global_tile_border(list);
  lv_obj_set_style_pad_all(list, edge, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  const int text_pad = spec.text_x - edge;
  for (uint8_t i = 0; i < spec.count; ++i) {
    lv_obj_t* option = plain(list);
    lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(option, LV_PCT(100), kOptionHeight);
    settings_style::apply_radius(option, kGroupRadius - kOptionInset);
    lv_obj_set_style_bg_color(option, lv_color_hex(spec.selected_color), 0);
    lv_obj_set_style_bg_opa(option, i == spec.selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_pad_left(option, text_pad > 0 ? text_pad : 0, 0);
    lv_obj_set_flex_flow(option, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(option, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(option, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_t* label = lv_label_create(option);
    const char* text = spec.handler.text(spec.tag, i);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, row_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    browser_line(label, kRowFontPx);
    lv_obj_add_event_cb(option, option_clicked_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }
  // The selected option in the middle of the list.
  if (spec.selected < spec.count) {
    lv_obj_update_layout(list);
    const int top = spec.selected * kOptionHeight - (height - 2 * edge - kOptionHeight) / 2;
    lv_obj_scroll_to_y(list, top > 0 ? top : 0, LV_ANIM_OFF);
  }
}

void close_options() {
  lv_obj_t* overlay = g_options.overlay;
  g_options.overlay = nullptr;
  if (overlay) lv_obj_delete(overlay);
}

lv_obj_t* button(lv_obj_t* parent, const char* text, const char* icon_name, ButtonKind kind, uint32_t accent,
                 const Colors& colors, int height, bool large_text, lv_event_cb_t on_click, void* user_data) {
  const uint32_t fill = kind == ButtonKind::Accent ? accent : kind == ButtonKind::Danger ? kDangerColor : colors.button;
  const lv_color_t ink = lv_color_hex(kind == ButtonKind::Accent ? kAccentText : 0xFFFFFF);
  lv_obj_t* b = plain(parent);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, height);
  settings_style::apply_radius(b, height / 2);
  lv_obj_set_style_bg_color(b, lv_color_hex(fill), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  // Normal buttons step up like the controls; colored ones get a little
  // lighter (mockup :active brightness 1.12).
  lv_obj_set_style_bg_color(b, kind == ButtonKind::Normal ? lv_color_hex(colors.pressed)
                                                          : lv_color_lighten(lv_color_hex(fill), 31),
                            LV_STATE_PRESSED);
  lv_obj_set_style_pad_hor(b, kButtonPad, 0);
  lv_obj_set_style_pad_column(b, kButtonGap, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(b, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (icon_name && icon_name[0]) {
    lv_obj_t* icon = lv_label_create(b);
    lv_label_set_text(icon, getMdiChar(icon_name).c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
    lv_obj_set_style_text_color(icon, ink, 0);
  }
  lv_obj_t* label = lv_label_create(b);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(label, large_text ? row_font() : small_font(), 0);
  lv_obj_set_style_text_color(label, ink, 0);
  browser_line(label, large_text ? kRowFontPx : kSmallFontPx);
  if (on_click) lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, user_data);
  return b;
}

void button_set_enabled(lv_obj_t* b, bool enabled) {
  if (!b) return;
  // Mockup .dis: 38 %.
  lv_obj_set_style_opa(b, enabled ? LV_OPA_COVER : 97, 0);
  if (enabled) {
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  }
}

lv_obj_t* dialog(lv_obj_t* host, uint32_t card, const char* title, lv_event_cb_t on_veil) {
  lv_obj_update_layout(host);
  lv_area_t host_area;
  lv_area_t host_content;
  lv_obj_get_coords(host, &host_area);
  lv_obj_get_content_coords(host, &host_content);
  lv_obj_t* root = plain(host);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(root, host_area.x1 - host_content.x1, host_area.y1 - host_content.y1);
  lv_obj_set_size(root, lv_area_get_width(&host_area), lv_area_get_height(&host_area));
  lv_obj_set_style_bg_color(root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root, kVeilOpa, 0);
  if (on_veil) lv_obj_add_event_cb(root, on_veil, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* box = plain(root);
  // Taps on the card stay in it.
  lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(box, kDialogWidth, LV_SIZE_CONTENT);
  lv_obj_center(box);
  lv_obj_set_style_bg_color(box, lv_color_hex(card), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  settings_style::apply_tile_radius(box);
  ui_surface_style::apply_global_tile_border(box);
  lv_obj_set_style_pad_top(box, kDialogPadTop, 0);
  lv_obj_set_style_pad_hor(box, kDialogPadSide, 0);
  lv_obj_set_style_pad_bottom(box, kDialogPadBottom, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* label = lv_label_create(box);
  lv_label_set_text(label, title ? title : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  return box;
}

lv_obj_t* toggle(lv_obj_t* row, bool on, uint32_t accent, uint32_t track, bool enabled, lv_event_cb_t on_click,
                 void* user_data) {
  lv_obj_t* t = plain(row);
  lv_obj_set_size(t, kToggleWidth, kToggleHeight);
  lv_obj_set_style_bg_color(t, lv_color_hex(on ? accent : track), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
  const int knob = kToggleHeight - 2 * kToggleInset;
  lv_obj_t* k = plain(t);
  lv_obj_set_size(k, knob, knob);
  lv_obj_set_pos(k, on ? kToggleWidth - kToggleHeight + kToggleInset : kToggleInset, kToggleInset);
  lv_obj_set_style_bg_color(k, lv_color_hex(on ? 0xFFFFFF : 0x8A8A8A), 0);
  lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
  if (on_click) lv_obj_add_event_cb(t, on_click, LV_EVENT_CLICKED, user_data);
  // Touch over the row's height.
  lv_obj_set_ext_click_area(t, (kRowHeight - kToggleHeight) / 2);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  button_set_enabled(t, enabled);
  return t;
}

void two_tone_sub(Row& r, const char* first, uint32_t color, const char* rest) {
  if (!r.sub) return;
  lv_obj_t* line = plain(r.text);
  lv_obj_set_size(line, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_add_flag(line, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_move_to_index(line, lv_obj_get_index(r.sub));
  lv_obj_set_parent(r.sub, line);
  lv_label_set_text(r.sub, first ? first : "");
  lv_obj_set_width(r.sub, LV_SIZE_CONTENT);
  lv_obj_set_style_text_color(r.sub, lv_color_hex(color), 0);
  lv_obj_set_style_text_opa(r.sub, LV_OPA_COVER, 0);
  lv_obj_t* tail = lv_label_create(line);
  lv_label_set_text(tail, rest ? rest : "");
  lv_label_set_long_mode(tail, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tail, 1);
  lv_obj_set_flex_grow(tail, 1);
  lv_obj_set_style_text_font(tail, small_font(), 0);
  grey_text(tail);
  browser_line(tail, kSmallFontPx, kSubTop);
}

lv_obj_t* qr_code(lv_obj_t* parent, int size, const char* text) {
#if LV_USE_QRCODE
  lv_obj_t* qr = lv_qrcode_create(parent);
  // The size first: setting it clears the code.
  lv_qrcode_set_size(qr, size);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_qrcode_set_quiet_zone(qr, true);
  lv_qrcode_update(qr, text, strlen(text));
  lv_obj_set_style_bg_color(qr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qr, LV_OPA_COVER, 0);
  settings_style::apply_radius(qr, size / 12);
  lv_obj_set_style_clip_corner(qr, true, 0);
  return qr;
#else
  (void)parent;
  (void)size;
  (void)text;
  return nullptr;
#endif
}

lv_obj_t* dialog_text(lv_obj_t* box, const char* text) {
  lv_obj_t* label = lv_label_create(box);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, small_font(), 0);
  grey_text(label);
  lv_obj_set_style_margin_top(label, kDialogGap * 6 / 10, 0);
  return label;
}

lv_obj_t* dialog_buttons(lv_obj_t* box) {
  lv_obj_t* row = plain(box);
  lv_obj_set_size(row, LV_PCT(100), kDialogButtonHeight);
  lv_obj_set_style_margin_top(row, kDialogButtonsTop, 0);
  lv_obj_set_style_pad_column(row, kDialogGap, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  return row;
}

lv_obj_t* slider(lv_obj_t* row, int width, int32_t min, int32_t max, int32_t value, uint32_t accent,
                 uint32_t track, uint32_t thumb, SliderCallback on_change) {
  auto* state = static_cast<SliderState*>(lv_malloc(sizeof(SliderState)));
  if (!state) return nullptr;
  if (value < min) value = min;
  if (value > max) value = max;
  *state = {min, max, value, accent & 0xFFFFFF, thumb & 0xFFFFFF, on_change};
  lv_obj_t* s = lv_obj_create(row);
  lv_obj_remove_style_all(s);
  lv_obj_set_size(s, width, kSliderHeight);
  lv_obj_set_style_bg_color(s, lv_color_hex(track), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  settings_style::apply_radius(s, kSliderHeight / 2);
  lv_obj_set_user_data(s, state);
  // The slider owns its touches: a drag never scrolls or presses the row.
  lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  // Touch over the row's whole height.
  lv_obj_set_ext_click_area(s, (kRowHeight - kSliderHeight) / 2);
  lv_obj_add_event_cb(s, slider_draw_cb, LV_EVENT_DRAW_MAIN_END, nullptr);
  lv_obj_add_event_cb(s, slider_event_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(s, slider_event_cb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(s, slider_event_cb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(s, slider_event_cb, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_add_event_cb(s, slider_event_cb, LV_EVENT_DELETE, nullptr);
  return s;
}

void slider_set_value(lv_obj_t* slider, int32_t value) {
  SliderState* s = slider_state(slider);
  if (!s) return;
  if (value < s->min) value = s->min;
  if (value > s->max) value = s->max;
  if (value == s->value) return;
  s->value = value;
  lv_obj_invalidate(slider);
}

int32_t slider_value(lv_obj_t* slider) {
  SliderState* s = slider_state(slider);
  return s ? s->value : 0;
}

lv_obj_t* segment(lv_obj_t* row, const char* const* labels, uint8_t count, uint8_t selected,
                  uint32_t track, SegmentCallback on_select, int min_width, int pad) {
  auto* state = static_cast<SegmentState*>(lv_malloc(sizeof(SegmentState)));
  if (!state) return nullptr;
  *state = {on_select, selected};
  lv_obj_t* seg = plain(row);
  lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(seg, lv_color_hex(track), 0);
  lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
  settings_style::apply_radius(seg, kSegmentHeight / 2 + kSegmentInset);
  lv_obj_set_style_pad_all(seg, kSegmentInset, 0);
  lv_obj_set_style_pad_column(seg, kSegmentInset, 0);
  lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
  lv_obj_set_user_data(seg, state);
  lv_obj_add_event_cb(seg, segment_delete_cb, LV_EVENT_DELETE, nullptr);
  for (uint8_t i = 0; i < count; ++i) {
    lv_obj_t* option = plain(seg);
    lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(option, LV_SIZE_CONTENT, kSegmentHeight);
    lv_obj_set_style_min_width(option, min_width, 0);
    lv_obj_set_style_pad_hor(option, pad, 0);
    settings_style::apply_radius(option, kSegmentHeight / 2);
    lv_obj_set_flex_flow(option, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(option, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* label = lv_label_create(option);
    lv_label_set_text(label, labels[i]);
    lv_obj_set_style_text_font(label, small_font(), 0);
    browser_line(label, kSmallFontPx);
    lv_obj_add_flag(option, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    style_option(option, i == selected);
    lv_obj_add_event_cb(option, segment_option_cb, LV_EVENT_CLICKED, nullptr);
  }
  return seg;
}

void segment_select(lv_obj_t* segment, uint8_t index) {
  auto* s = segment ? static_cast<SegmentState*>(lv_obj_get_user_data(segment)) : nullptr;
  if (!s) return;
  s->selected = index;
  const uint32_t count = lv_obj_get_child_count(segment);
  for (uint32_t i = 0; i < count; ++i) {
    style_option(lv_obj_get_child(segment, static_cast<int32_t>(i)), i == index);
  }
}

// Inter's vertical metrics (units per em 2048): ascender 1984, descender 494.
constexpr float kInterAscent = 1984.0f / 2048.0f;
constexpr float kInterLine = (1984.0f + 494.0f) / 2048.0f;

void browser_line(lv_obj_t* label, int px, int margin_top, int margin_bottom) {
  if (!label) return;
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (!font) return;
  // How far the LVGL baseline sits below the browser's, and what the box
  // has left below the text after moving it up by that.
  const float shift = static_cast<float>(font->line_height - font->base_line) - kInterAscent * px;
  const float rest = kInterLine * px - font->line_height + shift;
  lv_obj_set_style_margin_top(label, margin_top - static_cast<int>(lroundf(shift)), 0);
  lv_obj_set_style_margin_bottom(label, margin_bottom + static_cast<int>(lroundf(rest)), 0);
}

float browser_line_height(int px) {
  return kInterLine * px;
}

int browser_label_y(const lv_font_t* font, int px, float box_top, float box) {
  if (!font) return static_cast<int>(lroundf(box_top));
  const float baseline = box_top + (box - kInterLine * px) / 2 + kInterAscent * px;
  return static_cast<int>(lroundf(baseline - (font->line_height - font->base_line)));
}

int text_width(const lv_font_t* font, const char* text) {
  if (!font || !text) return 0;
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

int icon_width() {
  return FONT_MDI_ICONS ? lv_font_get_glyph_width(FONT_MDI_ICONS, 0xF0001, 0) : 0;
}

}  // namespace settings_parts
