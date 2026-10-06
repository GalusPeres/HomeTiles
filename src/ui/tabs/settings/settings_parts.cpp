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
  lv_obj_set_style_margin_top(label, first ? 0 : kSectionTop, 0);
  lv_obj_set_style_margin_bottom(label, kSectionBottom, 0);
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
  lv_obj_set_size(r.row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(r.row, kRowHeight, 0);
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
  r.title = lv_label_create(r.text);
  lv_label_set_text(r.title, title ? title : "");
  lv_label_set_long_mode(r.title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(r.title, LV_PCT(100));
  lv_obj_set_style_text_font(r.title, row_font(), 0);
  lv_obj_set_style_text_color(r.title, lv_color_white(), 0);
  if (sub) {
    r.sub = lv_label_create(r.text);
    lv_label_set_text(r.sub, sub);
    lv_label_set_long_mode(r.sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r.sub, LV_PCT(100));
    lv_obj_set_style_text_font(r.sub, small_font(), 0);
    grey_text(r.sub);
    lv_obj_set_style_margin_top(r.sub, kSubTop, 0);
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
  return label;
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
