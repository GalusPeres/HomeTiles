#pragma once

#include <lvgl.h>
#include <algorithm>
#include "src/core/text/title_text.h"

namespace hometiles_title {

struct State {
  std::string text;
  bool top_aligned = false;
  bool updating = false;
};

inline int text_width(lv_obj_t* label, const std::string& text) {
  lv_point_t size;
  lv_text_get_size(&size, text.c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN),
      lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0,
      LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

inline std::string fit_line(lv_obj_t* label, std::string line, int width) {
  if (width <= 0 || text_width(label, line) <= width) return line;
  while (!line.empty() && text_width(label, line + "...") > width) {
    size_t last = line.size() - 1;
    while (last && (static_cast<unsigned char>(line[last]) & 0xC0) == 0x80) --last;
    line.resize(last);
  }
  return line + "...";
}

inline void render(lv_obj_t* label, State* state) {
  if (state->updating) return;
  state->updating = true;
  const int width = lv_obj_get_content_width(label);
  const size_t split = state->text.find('\n');
  std::string shown = fit_line(label, state->text.substr(0, split), width);
  if (split != std::string::npos)
    shown += "\n" + fit_line(label, state->text.substr(split + 1), width);
  const int line_height = lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
  const int extra = split == std::string::npos ? 0 : line_height;
  lv_obj_set_height(label, line_height + extra);
  lv_obj_set_style_translate_y(label, state->top_aligned ? -extra / 2 : 0, 0);
  lv_label_set_text(label, shown.c_str());
  state->updating = false;
}

inline void event(lv_event_t* e) {
  auto* state = static_cast<State*>(lv_event_get_user_data(e));
  if (lv_event_get_code(e) == LV_EVENT_DELETE) { delete state; return; }
  if (lv_event_get_code(e) == LV_EVENT_SIZE_CHANGED || lv_event_get_code(e) == LV_EVENT_STYLE_CHANGED)
    render(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), state);
}

inline State* state_for(lv_obj_t* label) {
  for (uint32_t i = 0; i < lv_obj_get_event_count(label); ++i) {
    auto* descriptor = lv_obj_get_event_dsc(label, i);
    if (lv_event_dsc_get_cb(descriptor) == event)
      return static_cast<State*>(lv_event_dsc_get_user_data(descriptor));
  }
  return nullptr;
}

// Preserve the full title independently of its fitted visual text. Updates and
// width changes reuse one bounded label owner; deletion releases it with LVGL.
inline void set(lv_obj_t* label, const char* text, bool top_aligned = false) {
  if (!label) return;
  auto* state = state_for(label);
  if (!state) {
    state = new State;
    lv_obj_add_event_cb(label, event, LV_EVENT_ALL, state);
  }
  state->text = normalize(text);
  state->top_aligned = top_aligned;
  state->updating = true;
  if (lv_obj_get_style_width(label, LV_PART_MAIN) == LV_SIZE_CONTENT)
    lv_obj_set_width(label, LV_PCT(100));
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  state->updating = false;
  render(label, state);
}

inline void tile(lv_obj_t* label, const char* text, bool top_aligned = true) {
  if (!top_aligned) lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  set(label, text, top_aligned);
}

inline const char* text(lv_obj_t* label) {
  auto* state = state_for(label);
  return state ? state->text.c_str() : lv_label_get_text(label);
}

}  // namespace hometiles_title
