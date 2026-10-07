#include "src/core/diagnostics/text_fit_probe.h"

#if defined(HOMETILES_TEXT_FIT_PROBE)

#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>

#include <string.h>

#include "src/core/config/config_manager.h"

namespace text_fit_probe {
namespace {

constexpr uint32_t kIntervalMs = 2000;
// Each finding is logged once; the list is bounded.
constexpr size_t kSeenCapacity = 384;
uint32_t g_seen[kSeenCapacity];
size_t g_seen_count = 0;
bool g_seen_full_logged = false;

uint32_t hash_text(const char* text, uint32_t seed) {
  uint32_t hash = seed ^ 2166136261u;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    // Every digit counts as one: a sensor value that keeps changing is one
    // finding, not a new one per value (they filled the list within hours
    // and later real findings were no longer logged).
    const unsigned char c = (*p >= '0' && *p <= '9') ? '0' : *p;
    hash = (hash ^ c) * 16777619u;
  }
  return hash;
}

// True the first time a finding is seen.
bool first_time(uint32_t hash) {
  for (size_t i = 0; i < g_seen_count; ++i) {
    if (g_seen[i] == hash) return false;
  }
  if (g_seen_count >= kSeenCapacity) {
    if (!g_seen_full_logged) {
      Serial.println("[TextFit] Finding limit reached, further findings are not logged");
      g_seen_full_logged = true;
    }
    return false;
  }
  g_seen[g_seen_count++] = hash;
  return true;
}

void report(lv_obj_t* label, const char* text, const char* reason, int32_t need, int32_t have) {
  const char* language = configManager.getConfig().language;
  if (!first_time(hash_text(text, hash_text(reason, hash_text(language, 0))))) return;
  lv_area_t area;
  lv_obj_get_coords(label, &area);
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  char shown[96];
  size_t n = 0;
  const char* p = text;
  for (; *p && n + 1 < sizeof(shown); ++p) {
    shown[n++] = (*p == '\n') ? '|' : *p;
  }
  // A shortened text must not end inside a multi-byte UTF-8 character.
  if (*p) {
    while (n > 0 && (static_cast<unsigned char>(shown[n - 1]) & 0xC0) == 0x80) --n;
    if (n > 0 && (static_cast<unsigned char>(shown[n - 1]) & 0xC0) == 0xC0) --n;
  }
  shown[n] = '\0';
  Serial.printf("[TextFit] %s lang=%s need=%ld have=%ld line=%d at=%ld,%ld text=\"%s\"\n", reason,
                language, static_cast<long>(need), static_cast<long>(have),
                font ? static_cast<int>(font->line_height) : 0, static_cast<long>(area.x1),
                static_cast<long>(area.y1), shown);
}

// The label's whole text, in a fixed buffer (no allocation per scan). A
// label in dots mode keeps its text with "..." written over it
// (lv_label_set_dots); the replaced bytes and the rest after the dots give
// the original back.
char g_text[512];
// lv_label.c's private "no dots" marker for dot_begin (LVGL 9.6).
constexpr uint32_t kNoDots = 0xFFFFFFFF;

const char* full_text(lv_obj_t* obj, bool& dotted) {
  const lv_label_t* label = reinterpret_cast<const lv_label_t*>(obj);
  const char* text = lv_label_get_text(obj);
  dotted = text && label->dot_begin != kNoDots && !label->static_txt;
  if (!text || !dotted) return text ? text : "";
  size_t n = 0;
  auto append = [&n](const char* from, size_t count) {
    for (size_t i = 0; i < count && from[i] && n + 1 < sizeof(g_text); ++i) g_text[n++] = from[i];
  };
  append(text, label->dot_begin);
  const size_t saved = strnlen(label->dot, LV_LABEL_DOT_NUM + 1);
  append(label->dot, saved);
  if (saved == LV_LABEL_DOT_NUM + 1) append(text + label->dot_begin + LV_LABEL_DOT_NUM + 1, sizeof(g_text));
  g_text[n] = '\0';
  return g_text;
}

void check_label(lv_obj_t* label) {
  bool dotted = false;
  const char* text = full_text(label, dotted);
  if (!text[0]) return;
  const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (!font) return;
  const int32_t letter = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
  const int32_t line = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
  const int32_t width = lv_obj_get_content_width(label);
  const int32_t height = lv_obj_get_content_height(label);

  // Measure like lv_label does: with recoloring on, "#RRGGBB text#" codes
  // take no space (they were reported as cut before).
  const lv_text_flag_t flags = reinterpret_cast<const lv_label_t*>(label)->recolor
                                   ? LV_TEXT_FLAG_RECOLOR
                                   : LV_TEXT_FLAG_NONE;
  // The text on its own lines, without wrapping.
  lv_point_t natural;
  lv_text_get_size(&natural, text, font, letter, line, LV_COORD_MAX, flags);
  if (dotted || lv_label_get_long_mode(label) != LV_LABEL_LONG_MODE_WRAP) {
    if (dotted || natural.x > width + 1) {
      report(label, text, "cut", natural.x, width);
      return;
    }
  } else {
    lv_point_t wrapped;
    lv_text_get_size(&wrapped, text, font, letter, line, width, flags);
    if (wrapped.y > height + 1) {
      report(label, text, "too-tall", wrapped.y, height);
      return;
    }
  }

  // A label wider than a parent that clips its children loses its edges.
  // Vertical overflow is left out: lists scroll vertically.
  lv_obj_t* parent = lv_obj_get_parent(label);
  if (!parent || lv_obj_has_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) return;
  lv_area_t own, box;
  lv_obj_get_coords(label, &own);
  lv_obj_get_coords(parent, &box);
  if (own.x1 < box.x1 - 1 || own.x2 > box.x2 + 1) {
    report(label, text, "outside-parent", lv_area_get_width(&own), lv_area_get_width(&box));
  }
}

void walk(lv_obj_t* obj) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_check_type(obj, &lv_label_class)) {
    if (lv_obj_is_visible(obj)) check_label(obj);
    return;
  }
  const uint32_t count = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < count; ++i) walk(lv_obj_get_child(obj, static_cast<int32_t>(i)));
}

void scan(lv_timer_t* timer) {
  (void)timer;
  walk(lv_screen_active());
  walk(lv_layer_top());
  walk(lv_layer_sys());
}

}  // namespace

void start() {
  static lv_timer_t* timer = nullptr;
  if (timer) return;
  timer = lv_timer_create(scan, kIntervalMs, nullptr);
  Serial.println("[TextFit] Checking visible labels every 2 s");
}

}  // namespace text_fit_probe

#endif  // HOMETILES_TEXT_FIT_PROBE
