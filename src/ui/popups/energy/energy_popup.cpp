#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/energy/energy_popup.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/clock/clock_format.h"
#include "src/types/energy/energy_data.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"

namespace {

constexpr int kCardWidth = popup_layout::kCardWidth;
constexpr int kCardPad = popup_layout::kCardPad;
constexpr int kChartHeight = popup_layout::contentScale(325);
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kTimeAxisHeight = 20;  // full native UI14 line plus breathing room
#else
constexpr int kTimeAxisHeight = popup_layout::scale(20);
#endif
constexpr int kTimeAxisGap = popup_layout::scale(14);
constexpr int kRangeButtonWidth = popup_layout::scale(92);
constexpr int kRangeButtonHeight = popup_layout::kNavHeight;
constexpr int kRangeButtonGap = popup_layout::scale(10);
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kContentLiftY = 6;
#else
constexpr int kContentLiftY = 0;
#endif
constexpr uint8_t kDaySlotCount = 24;
constexpr uint8_t kWeekSlotCount = 7;
constexpr int kLabelOverhang = popup_layout::scale(12);
constexpr int kMinBarHeight = popup_layout::scale(2);

struct EnergyPopupContext {
  bool body_ready = false;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* value_label = nullptr;
  lv_obj_t* subtitle_label = nullptr;
  lv_obj_t* range_row = nullptr;
  lv_obj_t* chart_wrap = nullptr;
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  lv_obj_t* x_axis = nullptr;
  lv_obj_t* x_labels[ENERGY_VALUES_MAX] = {};
  lv_obj_t* x_lines[ENERGY_VALUES_MAX] = {};
  lv_obj_t* y_max_label = nullptr;
  lv_obj_t* y_zero_label = nullptr;
  lv_obj_t* y_min_label = nullptr;
  lv_obj_t* y_max_line = nullptr;
  lv_obj_t* y_zero_line = nullptr;
  lv_obj_t* y_min_line = nullptr;
  lv_obj_t* bars[ENERGY_VALUES_MAX] = {};
  lv_obj_t* day_btn = nullptr;
  lv_obj_t* week_btn = nullptr;
  lv_obj_t* day_label = nullptr;
  lv_obj_t* week_label = nullptr;
  String entity_id;
  String title;
  String unit;
  String period = "day";
  uint8_t decimals = 1;
  uint32_t bg_color = 0x2A2A2A;
};

struct PendingPopupRefresh {
  String period;
  bool valid = false;
};

EnergyPopupContext* g_energy_popup_ctx = nullptr;
PendingPopupRefresh g_pending_refresh;
PopupBody g_energy_body;
bool g_energy_open_pending = false;

const lv_font_t* value_font() {
#if defined(DEVICE_LAYOUT_480X480)
  return &ui_font_24;
#else
  return popup_layout::font32();
#endif
}

const char* today_label() {
  return i18n::weather_today_label(configManager.getConfig().language);
}

const char* loading_label() {
  return i18n::strings(configManager.getConfig().language).loading;
}

String format_number(float value, uint8_t decimals) {
  return i18n::format_number(
      configManager.getConfig().language, value, decimals);
}

String format_value_with_unit(float value, const String& unit, uint8_t decimals) {
  String out = format_number(value, decimals);
  if (out != "--" && unit.length()) {
    out += " ";
    out += unit;
  }
  return out;
}

bool popup_visible(const EnergyPopupContext* ctx) {
  return ctx && ctx->card && !lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
}

void style_period_button(lv_obj_t* btn, lv_obj_t* label, bool active) {
  if (!btn || !label) return;
  lv_color_t active_text_color = lv_color_hex(0x2A2A2A);
  lv_obj_t* row = lv_obj_get_parent(btn);
  lv_obj_t* card = row ? lv_obj_get_parent(row) : nullptr;
  if (card) {
    active_text_color = lv_obj_get_style_bg_color(card, LV_PART_MAIN);
  }

  auto apply_selector = [&](lv_style_selector_t selector) {
    const bool pressed = selector == LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(btn, lv_color_white(), selector);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_COVER : (pressed ? LV_OPA_20 : LV_OPA_TRANSP), selector);
    lv_obj_set_style_border_color(btn, lv_color_white(), selector);
    lv_obj_set_style_border_width(btn, 0, selector);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_transform_width(btn, 0, selector);
    lv_obj_set_style_transform_height(btn, 0, selector);
    lv_obj_set_style_translate_y(btn, 0, selector);
  };

  apply_selector(0);
  apply_selector(LV_STATE_PRESSED);

  lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
  lv_obj_set_style_text_font(label, popup_layout::font24(), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), 0);
  lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), LV_STATE_PRESSED);
}

void update_period_buttons(EnergyPopupContext* ctx) {
  if (!ctx) return;
  const bool day = ctx->period == "day";
  style_period_button(ctx->day_btn, ctx->day_label, day);
  style_period_button(ctx->week_btn, ctx->week_label, !day);
}

lv_coord_t measure_label_text_width(lv_obj_t* label) {
  if (!label) return 0;
  const char* txt = lv_label_get_text(label);
  if (!txt || !*txt) return 0;
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_obj_update_layout(label);
  return lv_obj_get_width(label);
}

String format_axis_value(float value, const String& unit, uint8_t decimals) {
  if (!isfinite(value)) return String("");
  uint8_t d = decimals > 6 ? 6 : decimals;
  if (fabsf(value - roundf(value)) < 0.05f && d > 1) {
    d = 1;
  }
  String out = i18n::format_number(
      configManager.getConfig().language, value, d);
  if (unit.length()) {
    out += " ";
    out += unit;
  }
  return out;
}

uint8_t slot_count_for_entry(const EnergyEntryData& entry) {
  if (entry.period == "week") return kWeekSlotCount;
  if (entry.period == "day") return kDaySlotCount;
  return entry.value_count > 0 ? entry.value_count : 1;
}

void clear_chart(EnergyPopupContext* ctx) {
  if (!ctx || !ctx->chart || !ctx->series) return;
  lv_chart_set_point_count(ctx->chart, kDaySlotCount);
  lv_chart_set_all_value(ctx->chart, ctx->series, LV_CHART_POINT_NONE);
  lv_chart_set_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
  lv_chart_refresh(ctx->chart);
  if (ctx->y_max_label) lv_label_set_text(ctx->y_max_label, "");
  if (ctx->y_zero_label) lv_label_set_text(ctx->y_zero_label, "");
  if (ctx->y_min_label) lv_label_set_text(ctx->y_min_label, "");
  if (ctx->y_max_line) lv_obj_add_flag(ctx->y_max_line, LV_OBJ_FLAG_HIDDEN);
  if (ctx->y_zero_line) lv_obj_add_flag(ctx->y_zero_line, LV_OBJ_FLAG_HIDDEN);
  if (ctx->y_min_line) lv_obj_add_flag(ctx->y_min_line, LV_OBJ_FLAG_HIDDEN);
  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    if (ctx->bars[i]) lv_obj_add_flag(ctx->bars[i], LV_OBJ_FLAG_HIDDEN);
    if (ctx->x_lines[i]) lv_obj_add_flag(ctx->x_lines[i], LV_OBJ_FLAG_HIDDEN);
    if (!ctx->x_labels[i]) continue;
    lv_label_set_text(ctx->x_labels[i], "");
    lv_obj_add_flag(ctx->x_labels[i], LV_OBJ_FLAG_HIDDEN);
  }
}

bool parse_iso_start(const String& iso, int& year, int& month, int& day, int& hour) {
  year = month = day = hour = 0;
  return sscanf(iso.c_str(), "%d-%d-%dT%d", &year, &month, &day, &hour) >= 3;
}

String iso_date_with_offset(const String& start, int offset_days) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  if (!parse_iso_start(start, year, month, day, hour)) return "";

  struct tm tmv = {};
  tmv.tm_year = year - 1900;
  tmv.tm_mon = month - 1;
  tmv.tm_mday = day + offset_days;
  tmv.tm_hour = 12;
  mktime(&tmv);

  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           tmv.tm_year + 1900,
           tmv.tm_mon + 1,
           tmv.tm_mday);
  return String(buf);
}

String week_x_label_for_entry(const EnergyEntryData& entry, uint8_t index) {
  String iso = iso_date_with_offset(entry.start, index);
  if (!iso.length()) return String(index + 1);
  return i18n::weather_weekday_short(configManager.getConfig().language, iso);
}

String day_marker_label(uint8_t hour) {
  const unsigned h = static_cast<unsigned>(hour % 24);
  const DeviceConfig& cfg = configManager.getConfig();
  const uint8_t time_format = clock_tile::resolve_time_format(
      clock_tile::TIME_FORMAT_AUTO, cfg.global_time_format, cfg.language);
  char buf[12];
  if (time_format == clock_tile::TIME_FORMAT_12H) {
    unsigned hour12 = h % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, sizeof(buf), "%u %s", hour12, (h < 12) ? "AM" : "PM");
  } else {
    snprintf(buf, sizeof(buf), "%u%s", h,
             i18n::locale(cfg.language).hour_axis_suffix);
  }
  return String(buf);
}

int clamped_label_x(lv_obj_t* label, int center_x, int avail_w) {
  if (!label) return center_x;
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_obj_update_layout(label);
  lv_coord_t label_w = lv_obj_get_width(label);
  int x = center_x - (label_w / 2);
  if (x < 0) x = 0;
  int max_x = avail_w - label_w;
  if (max_x < 0) max_x = 0;
  if (x > max_x) x = max_x;
  return x;
}

void update_x_axis(EnergyPopupContext* ctx,
                   const EnergyEntryData& entry,
                   uint8_t slot_count,
                   int plot_left,
                   int plot_w) {
  if (!ctx || !ctx->x_axis || slot_count == 0 || plot_w <= 0) return;
  lv_obj_set_pos(ctx->x_axis, 0, kLabelOverhang + kChartHeight + kTimeAxisGap);
  lv_obj_set_size(ctx->x_axis, LV_PCT(100), kTimeAxisHeight);

  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    if (ctx->x_lines[i]) lv_obj_add_flag(ctx->x_lines[i], LV_OBJ_FLAG_HIDDEN);
    if (ctx->x_labels[i]) {
      lv_label_set_text(ctx->x_labels[i], "");
      lv_obj_add_flag(ctx->x_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  const int avail_w = kCardWidth - (kCardPad * 2);
  uint8_t label_index = 0;
  uint8_t line_index = 0;

  auto show_x_label = [&](uint8_t index, const String& label, int center_x) {
    if (index >= ENERGY_VALUES_MAX || !ctx->x_labels[index] || !label.length()) return;
    lv_label_set_text(ctx->x_labels[index], label.c_str());
    int x = clamped_label_x(ctx->x_labels[index], center_x, avail_w);
    lv_obj_set_pos(ctx->x_labels[index], x, 0);
    lv_obj_clear_flag(ctx->x_labels[index], LV_OBJ_FLAG_HIDDEN);
  };

  auto show_x_line = [&](uint8_t index, int x, lv_opa_t opa) {
    if (index >= ENERGY_VALUES_MAX || !ctx->x_lines[index]) return;
    lv_obj_set_pos(ctx->x_lines[index], x, kLabelOverhang);
    lv_obj_set_size(ctx->x_lines[index], 1, kChartHeight);
    lv_obj_set_style_bg_opa(ctx->x_lines[index], opa, 0);
    lv_obj_clear_flag(ctx->x_lines[index], LV_OBJ_FLAG_HIDDEN);
  };

  if (entry.period == "week") {
    for (uint8_t i = 0; i < slot_count && label_index < ENERGY_VALUES_MAX; ++i) {
      int sl = plot_left + static_cast<int>(lroundf(
          (static_cast<float>(i) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
      int sr = plot_left + static_cast<int>(lroundf(
          (static_cast<float>(i + 1) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
      show_x_label(label_index++, week_x_label_for_entry(entry, i), sl + (sr - sl) / 2);
    }
    for (uint8_t boundary = 1; boundary < slot_count && line_index < ENERGY_VALUES_MAX; ++boundary) {
      int x = plot_left + static_cast<int>(lroundf((static_cast<float>(boundary) /
                                                    static_cast<float>(slot_count)) *
                                                   static_cast<float>(plot_w)));
      show_x_line(line_index++, x, LV_OPA_20);
    }
    return;
  }

  if (slot_count >= kDaySlotCount) {
    const uint8_t markers[] = {0, 6, 12, 18, 24};
    for (uint8_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
      const uint8_t marker = markers[i];
      int x = plot_left + static_cast<int>(lroundf((static_cast<float>(marker) /
                                                    static_cast<float>(kDaySlotCount)) *
                                                   static_cast<float>(plot_w)));
      show_x_line(line_index++, x, LV_OPA_20);
      show_x_label(label_index++, day_marker_label(marker), x);
    }
    return;
  }

  for (uint8_t i = 0; i < slot_count && label_index < ENERGY_VALUES_MAX; ++i) {
    int sl = plot_left + static_cast<int>(lroundf(
        (static_cast<float>(i) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
    int sr = plot_left + static_cast<int>(lroundf(
        (static_cast<float>(i + 1) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
    show_x_label(label_index++, String(i + 1), sl + (sr - sl) / 2);
  }
}

void update_header_value(EnergyPopupContext* ctx, const EnergyEntryData& fallback) {
  if (!ctx || !ctx->value_label || !ctx->subtitle_label) return;

  EnergyEntryData day_entry;
  const EnergyEntryData* display_entry = &fallback;
  String subtitle = fallback.period == "week" ? String("7D") : String(today_label());
  if (energy_find_entry(ctx->entity_id, "day", day_entry)) {
    display_entry = &day_entry;
    subtitle = today_label();
  }

  String unit = ctx->unit.length() ? ctx->unit : display_entry->unit;
  lv_label_set_text(ctx->value_label,
                    format_value_with_unit(display_entry->total, unit, ctx->decimals).c_str());
  lv_label_set_text(ctx->subtitle_label, subtitle.c_str());
}

void update_loading_header(EnergyPopupContext* ctx) {
  if (!ctx || !ctx->value_label || !ctx->subtitle_label) return;

  EnergyEntryData day_entry;
  if (energy_find_entry(ctx->entity_id, "day", day_entry)) {
    String unit = ctx->unit.length() ? ctx->unit : day_entry.unit;
    lv_label_set_text(ctx->value_label,
                      format_value_with_unit(day_entry.total, unit, ctx->decimals).c_str());
    lv_label_set_text(ctx->subtitle_label, today_label());
    return;
  }

  lv_label_set_text(ctx->value_label, loading_label());
  lv_label_set_text(ctx->subtitle_label,
                    (ctx->period == "week" ? String("7D") : String(today_label())).c_str());
}

void apply_entry_to_chart(EnergyPopupContext* ctx, const EnergyEntryData& entry) {
  if (!ctx || !ctx->chart || !ctx->series) return;

  update_header_value(ctx, entry);

  const uint8_t slot_count = slot_count_for_entry(entry);
  if (slot_count == 0) {
    clear_chart(ctx);
    return;
  }
  const uint8_t available_count = entry.value_count < slot_count ? entry.value_count : slot_count;

  float data_max = 0.0f;
  float data_min = 0.0f;
  bool any = false;
  bool has_negative = false;
  for (uint8_t i = 0; i < available_count; ++i) {
    if (!entry.value_valid[i]) continue;
    float v = entry.values[i];
    if (!isfinite(v)) continue;
    if (!any) {
      data_max = v;
      data_min = v;
      any = true;
    } else {
      if (v > data_max) data_max = v;
      if (v < data_min) data_min = v;
    }
    if (v < -0.0001f) has_negative = true;
  }

  if (!any) {
    data_max = 1.0f;
    data_min = 0.0f;
  }

  float max_v, min_v;
  bool show_zero;
  if (has_negative && data_max > 0.0001f) {
    float abs_max = fmaxf(fabsf(data_max), fabsf(data_min));
    if (abs_max < 0.0001f) abs_max = 1.0f;
    max_v = abs_max;
    min_v = -abs_max;
    show_zero = true;
  } else if (has_negative) {
    max_v = 0.0f;
    min_v = data_min;
    if (fabsf(min_v) < 0.0001f) min_v = -1.0f;
    show_zero = false;
  } else {
    max_v = data_max;
    min_v = 0.0f;
    if (max_v < 0.0001f) max_v = 1.0f;
    show_zero = false;
  }

  const float span = max_v - min_v;

  int scale = 1;
  if (span <= 10.0f) scale = 100;
  else if (span <= 100.0f) scale = 10;
  float max_abs_for_scale = fmaxf(fabsf(max_v), fabsf(min_v));
  while (scale > 1 && (max_abs_for_scale * scale) > 30000.0f) {
    scale /= 10;
  }

  lv_chart_set_point_count(ctx->chart, slot_count);
  lv_chart_set_all_value(ctx->chart, ctx->series, LV_CHART_POINT_NONE);
  lv_chart_set_range(ctx->chart,
                     LV_CHART_AXIS_PRIMARY_Y,
                     static_cast<lv_coord_t>(floorf(min_v * scale)),
                     static_cast<lv_coord_t>(ceilf(max_v * scale)));
  lv_chart_refresh(ctx->chart);

  String axis_unit = ctx->unit.length() ? ctx->unit : entry.unit;
  if (show_zero) {
    if (ctx->y_max_label) lv_label_set_text(ctx->y_max_label, format_axis_value(max_v, axis_unit, ctx->decimals).c_str());
    if (ctx->y_min_label) lv_label_set_text(ctx->y_min_label, format_axis_value(min_v, axis_unit, ctx->decimals).c_str());
    if (ctx->y_zero_label) lv_label_set_text(ctx->y_zero_label, "0");
  } else if (has_negative) {
    String zero_label = String("0 ") + axis_unit;
    if (ctx->y_max_label) lv_label_set_text(ctx->y_max_label, zero_label.c_str());
    if (ctx->y_min_label) lv_label_set_text(ctx->y_min_label, format_axis_value(min_v, axis_unit, ctx->decimals).c_str());
    if (ctx->y_zero_label) lv_label_set_text(ctx->y_zero_label, "");
  } else {
    String zero_label = String("0 ") + axis_unit;
    if (ctx->y_max_label) lv_label_set_text(ctx->y_max_label, format_axis_value(max_v, axis_unit, ctx->decimals).c_str());
    if (ctx->y_min_label) lv_label_set_text(ctx->y_min_label, zero_label.c_str());
    if (ctx->y_zero_label) lv_label_set_text(ctx->y_zero_label, "");
  }

  constexpr int kLineOverlap = 6;
  constexpr int kLabelGap = 16;
  constexpr int kMinAxisW = 10;
  constexpr int kTimeAxisEdgeGap = 6;
  const int avail_w = kCardWidth - (kCardPad * 2);

  lv_coord_t max_w = measure_label_text_width(ctx->y_max_label);
  lv_coord_t min_w = measure_label_text_width(ctx->y_min_label);
  lv_coord_t zero_w = show_zero ? measure_label_text_width(ctx->y_zero_label) : 0;
  lv_coord_t text_w = max_w;
  if (min_w > text_w) text_w = min_w;
  if (zero_w > text_w) text_w = zero_w;
  if (text_w < kMinAxisW) text_w = kMinAxisW;
  const int label_w = static_cast<int>(text_w) + 2;
  const int axis_w = label_w + kLabelGap;
  int plot_left = axis_w;

  int max_x_label_w = 0;
  if (entry.period == "week") {
    for (uint8_t i = 0; i < slot_count && i < ENERGY_VALUES_MAX; ++i) {
      if (!ctx->x_labels[i]) continue;
      lv_label_set_text(ctx->x_labels[i], week_x_label_for_entry(entry, i).c_str());
      lv_coord_t w = measure_label_text_width(ctx->x_labels[i]);
      if (static_cast<int>(w) > max_x_label_w) max_x_label_w = static_cast<int>(w);
    }
  } else if (slot_count >= kDaySlotCount) {
    const uint8_t pre_markers[] = {0, 6, 12, 18, 24};
    for (uint8_t m = 0; m < 5 && m < ENERGY_VALUES_MAX; ++m) {
      if (!ctx->x_labels[m]) continue;
      lv_label_set_text(ctx->x_labels[m], day_marker_label(pre_markers[m]).c_str());
      lv_coord_t w = measure_label_text_width(ctx->x_labels[m]);
      if (static_cast<int>(w) > max_x_label_w) max_x_label_w = static_cast<int>(w);
    }
  }
  int chart_right_reserve = (max_x_label_w > 0) ? (max_x_label_w / 2 + kTimeAxisEdgeGap) : 8;

  int plot_w = avail_w - plot_left - chart_right_reserve;
  if (plot_w < 10) plot_w = 10;

  auto set_axis_label_width = [&](lv_obj_t* label) {
    if (!label) return;
    lv_obj_set_width(label, label_w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
  };
  set_axis_label_width(ctx->y_max_label);
  set_axis_label_width(ctx->y_min_label);
  set_axis_label_width(ctx->y_zero_label);

  auto value_to_y = [&](float value) -> int {
    const float denom = max_v - min_v;
    if (denom <= 0.0001f) return kLabelOverhang + kChartHeight;
    float t = (max_v - value) / denom;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return kLabelOverhang + static_cast<int>(lroundf(t * static_cast<float>(kChartHeight - 1)));
  };

  const int chart_top = kLabelOverhang;
  const int chart_bottom = kLabelOverhang + kChartHeight - 1;
  const int zero_y = value_to_y(0.0f);
  const int line_start = axis_w - kLineOverlap;
  int line_w = avail_w - line_start - chart_right_reserve;
  if (line_w < 10) line_w = 10;

  auto place_line = [&](lv_obj_t* line, int y, lv_opa_t opa) {
    if (!line) return;
    lv_obj_set_pos(line, line_start, y);
    lv_obj_set_size(line, line_w, 1);
    lv_obj_set_style_bg_opa(line, opa, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
  };
  place_line(ctx->y_max_line, chart_top, LV_OPA_30);
  place_line(ctx->y_min_line, chart_bottom, LV_OPA_30);
  if (show_zero) {
    place_line(ctx->y_zero_line, zero_y, LV_OPA_40);
  } else {
    if (ctx->y_zero_line) lv_obj_add_flag(ctx->y_zero_line, LV_OBJ_FLAG_HIDDEN);
  }

  auto place_axis_label = [&](lv_obj_t* label, int line_y, bool visible) {
    if (!label) return;
    if (!visible) {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    lv_obj_update_layout(label);
    int h = lv_obj_get_height(label);
    int y = line_y - (h / 2);
    if (y < 0) y = 0;
    const int max_y = kLabelOverhang + kChartHeight + kTimeAxisHeight - h;
    if (y > max_y) y = max_y;
    lv_obj_set_pos(label, 0, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
  };
  place_axis_label(ctx->y_max_label, chart_top, true);
  place_axis_label(ctx->y_min_label, chart_bottom, true);
  place_axis_label(ctx->y_zero_label, zero_y, show_zero);

  lv_obj_set_pos(ctx->chart, 0, kLabelOverhang);
  lv_obj_set_style_pad_left(ctx->chart, plot_left, 0);
  lv_obj_set_style_pad_right(ctx->chart, chart_right_reserve, 0);

  const float slot_w = static_cast<float>(plot_w) / static_cast<float>(slot_count);
  int bar_w = 0;
  if (entry.period == "week") {
    bar_w = static_cast<int>(floorf(slot_w * 0.72f));
    if (bar_w > 90) bar_w = 90;
  } else {
    bar_w = static_cast<int>(floorf(slot_w * 0.68f));
    if (bar_w > 20) bar_w = 20;
  }
  if (bar_w < 4) bar_w = 4;

  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    if (!ctx->bars[i]) continue;
    lv_obj_add_flag(ctx->bars[i], LV_OBJ_FLAG_HIDDEN);
    if (i >= slot_count || i >= entry.value_count || !entry.value_valid[i]) continue;

    const float value = entry.values[i];
    if (!isfinite(value) || fabsf(value) < 0.0001f) continue;

    const int value_y = value_to_y(value);
    int bar_y = zero_y;
    int bar_h = 0;
    if (value >= 0.0f) {
      bar_y = value_y;
      bar_h = zero_y - value_y;
      if (bar_h < kMinBarHeight) {
        bar_h = kMinBarHeight;
        bar_y = zero_y - bar_h;
      }
    } else {
      bar_y = zero_y;
      bar_h = value_y - zero_y;
      if (bar_h < kMinBarHeight) bar_h = kMinBarHeight;
    }
    if (bar_y < chart_top) bar_y = chart_top;
    if (bar_y + bar_h > chart_bottom + 1) bar_h = chart_bottom + 1 - bar_y;
    if (bar_h <= 0) continue;

    const int slot_l = plot_left + static_cast<int>(lroundf(
        (static_cast<float>(i) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
    const int slot_r = plot_left + static_cast<int>(lroundf(
        (static_cast<float>(i + 1) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
    const int center_x = slot_l + (slot_r - slot_l) / 2;
    lv_obj_set_pos(ctx->bars[i], center_x - (bar_w / 2), bar_y);
    lv_obj_set_size(ctx->bars[i], bar_w, bar_h);
    lv_obj_clear_flag(ctx->bars[i], LV_OBJ_FLAG_HIDDEN);
  }

  update_x_axis(ctx, entry, slot_count, plot_left, plot_w);
}

void show_empty_chart(EnergyPopupContext* ctx) {
  if (!ctx) return;

  EnergyEntryData entry;
  entry.id = ctx->entity_id;
  entry.name = ctx->title;
  entry.unit = ctx->unit;
  entry.period = ctx->period;
  entry.total = 0.0f;
  apply_entry_to_chart(ctx, entry);
  update_loading_header(ctx);
}

void refresh_from_cache(EnergyPopupContext* ctx) {
  if (!ctx || !popup_visible(ctx)) return;
  EnergyEntryData entry;
  if (!energy_find_entry(ctx->entity_id, ctx->period.c_str(), entry)) {
    show_empty_chart(ctx);
    return;
  }
  apply_entry_to_chart(ctx, entry);
}

void apply_init_to_context(EnergyPopupContext* ctx, const EnergyPopupInit& init,
                           bool reset_body = true) {
  if (!ctx) return;
  ctx->entity_id = init.entity_id;
  ctx->title = init.title;
  ctx->unit = init.unit;
  ctx->decimals = init.decimals > 6 ? 6 : init.decimals;
  ctx->bg_color = init.bg_color ? init.bg_color : 0x2A2A2A;
  ctx->period = "day";

  if (ctx->card) {
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(ctx->bg_color), 0);
  }
  if (ctx->title_label) {
    hometiles_title::set(ctx->title_label, ctx->title.length() ? ctx->title.c_str() : ctx->entity_id.c_str());
  }
  if (ctx->icon_label) {
    String icon_name = init.icon_name;
    icon_name.trim();
    if (!icon_name.length() || isMdiIconDisabled(icon_name)) {
      lv_label_set_text(ctx->icon_label, "");
      lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      String icon_char = getMdiChar(icon_name);
      if (icon_char.length()) {
        lv_label_set_text(ctx->icon_label, icon_char.c_str());
        lv_obj_clear_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(ctx->icon_label, "");
        lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
  update_period_buttons(ctx);
  if (reset_body) clear_chart(ctx);
}

void on_close_click(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  EnergyPopupContext* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->overlay || !ctx->card) return;

  g_energy_open_pending = false;
  g_energy_body.restore();
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void on_overlay_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)e;
}

void on_overlay_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  EnergyPopupContext* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  if (g_energy_popup_ctx == ctx) {

    g_energy_open_pending = false;
    g_energy_body.forget();
    g_energy_popup_ctx = nullptr;
  }
  delete ctx;
}

void on_period_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  EnergyPopupContext* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  String next = (target == ctx->week_btn) ? "week" : "day";
  if (ctx->period == next) return;
  ctx->period = next;
  update_period_buttons(ctx);
  clear_chart(ctx);
  update_loading_header(ctx);
  energy_request_period(ctx->period.c_str(), true);
  refresh_from_cache(ctx);
}

lv_obj_t* make_button_label(lv_obj_t* parent, const char* text, lv_obj_t** out_label) {
  lv_obj_t* btn = lv_button_create(parent);
  disable_pressed_button_animation(btn);
  lv_obj_set_size(btn, kRangeButtonWidth, kRangeButtonHeight);
  lv_obj_set_style_radius(btn, kRangeButtonHeight / 2, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_set_style_anim_time(btn, 0, 0);
  lv_obj_set_style_anim_time(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(btn, 0, 0);
  lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(btn, 0, 0);
  lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(btn, 0, 0);
  lv_obj_set_style_translate_y(btn, 0, LV_STATE_PRESSED);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_t* label = lv_label_create(btn);
  set_label_style(label, lv_color_white(), popup_layout::font24());
  lv_label_set_text(label, text);
  lv_obj_center(label);
  if (out_label) *out_label = label;
  return btn;
}

void build_popup_ui(EnergyPopupContext* ctx, const EnergyPopupInit& init) {
  const auto parts = create_popup_body(on_close_click, ctx, init.bg_color ? init.bg_color : 0x2A2A2A);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* overlay = parts.overlay;
  lv_obj_t* card = parts.card;
  lv_obj_t* title = parts.title;
  lv_obj_t* icon = parts.icon;
  lv_obj_t* close_btn = parts.close;

  disable_pressed_button_animation(close_btn);

  lv_obj_t* period_row = lv_obj_create(card);
  ctx->range_row = period_row;
  lv_obj_remove_style_all(period_row);
  lv_obj_set_size(period_row, (kRangeButtonWidth * 2) + kRangeButtonGap, popup_layout::kNavHeight);
  lv_obj_align(period_row, LV_ALIGN_BOTTOM_MID, 0, -popup_layout::kNavBottomInset);
  lv_obj_set_style_bg_opa(period_row, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(period_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(period_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(period_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(period_row, kRangeButtonGap, 0);
  lv_obj_clear_flag(period_row, LV_OBJ_FLAG_CLICKABLE);

  ctx->day_btn = make_button_label(period_row, "24H", &ctx->day_label);
  ctx->week_btn = make_button_label(period_row, "7D", &ctx->week_label);
  lv_obj_add_event_cb(ctx->day_btn, on_period_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(ctx->week_btn, on_period_click, LV_EVENT_CLICKED, ctx);

  lv_obj_t* value_box = lv_obj_create(card);
  lv_obj_remove_style_all(value_box);
  lv_obj_set_size(value_box, LV_PCT(100), popup_layout::kValueHeight);
  lv_obj_align(
      value_box, LV_ALIGN_TOP_MID, 0,
      popup_layout::kValueY - kContentLiftY);
  lv_obj_set_style_bg_opa(value_box, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(value_box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(value_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(value_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(value_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(value_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* value = lv_label_create(value_box);
  ctx->value_label = value;
  set_label_style(value, lv_color_white(), value_font());
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_translate_y(value, popup_layout::kLargeValueTextOffsetY, 0);
  lv_obj_set_width(value, LV_PCT(100));

  lv_obj_t* subtitle = lv_label_create(value_box);
  ctx->subtitle_label = subtitle;
  set_label_style(subtitle, lv_color_hex(0xD9D9D9),
                  popup_layout::font20());
  lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(subtitle, LV_PCT(100));
  lv_obj_add_flag(subtitle, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* body_box = lv_obj_create(card);
  lv_obj_remove_style_all(body_box);
  lv_obj_set_size(body_box, LV_PCT(100), popup_layout::kBodyHeight);
  lv_obj_align(
      body_box, LV_ALIGN_TOP_MID, 0,
      popup_layout::kBodyY - kContentLiftY);
  lv_obj_set_style_bg_opa(body_box, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(body_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_clear_flag(body_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(body_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* chart_wrap = lv_obj_create(body_box);
  ctx->chart_wrap = chart_wrap;
  lv_obj_remove_style_all(chart_wrap);
  lv_obj_set_size(chart_wrap, LV_PCT(100), kLabelOverhang + kChartHeight + kTimeAxisGap + kTimeAxisHeight);
  lv_obj_center(chart_wrap);
  lv_obj_set_style_bg_opa(chart_wrap, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(chart_wrap, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* chart = lv_chart_create(chart_wrap);
  ctx->chart = chart;
  lv_obj_set_size(chart, LV_PCT(100), kChartHeight);
  lv_obj_set_pos(chart, 0, kLabelOverhang);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_left(chart, 0, 0);
  lv_obj_set_style_pad_right(chart, 0, 0);
  lv_obj_set_style_pad_top(chart, 0, 0);
  lv_obj_set_style_pad_bottom(chart, 0, 0);
  lv_obj_set_style_bg_color(chart, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_radius(chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(chart, kDaySlotCount);
  ctx->series = lv_chart_add_series(chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);

  auto make_axis_label = [&](void) -> lv_obj_t* {
    lv_obj_t* label = lv_label_create(chart_wrap);
    set_label_style(label, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
  };
  ctx->y_max_label = make_axis_label();
  ctx->y_zero_label = make_axis_label();
  ctx->y_min_label = make_axis_label();

  auto make_guide_line = [&](lv_opa_t opa) -> lv_obj_t* {
    lv_obj_t* line = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, popup_layout::scale480(10), 1);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(line, opa, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
  };
  ctx->y_max_line = make_guide_line(LV_OPA_30);
  ctx->y_zero_line = make_guide_line(LV_OPA_40);
  ctx->y_min_line = make_guide_line(LV_OPA_30);

  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    ctx->x_lines[i] = make_guide_line(LV_OPA_20);
  }

  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    lv_obj_t* bar = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, popup_layout::scale480(4), 0);
    lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    ctx->bars[i] = bar;
  }

  lv_obj_t* x_axis = lv_obj_create(chart_wrap);
  ctx->x_axis = x_axis;
  lv_obj_remove_style_all(x_axis);
  lv_obj_set_size(x_axis, LV_PCT(100), kTimeAxisHeight);
  lv_obj_set_pos(x_axis, 0, kLabelOverhang + kChartHeight + kTimeAxisGap);

  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    lv_obj_t* label = lv_label_create(x_axis);
    ctx->x_labels[i] = label;
    set_label_style(label, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
  }

  apply_init_to_context(ctx, init);
  lv_obj_move_foreground(icon);
  lv_obj_move_foreground(title);
  lv_obj_move_foreground(period_row);
  lv_obj_move_foreground(close_btn);

  lv_obj_add_event_cb(overlay, on_overlay_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
}

}  // namespace

static void finish_energy_popup_open() {
  if (!g_energy_popup_ctx || !g_energy_open_pending) return;
  g_energy_open_pending = false;
  g_energy_body.restore();
  if (popup_visible(g_energy_popup_ctx)) {
    refresh_from_cache(g_energy_popup_ctx);
    g_energy_popup_ctx->body_ready = true;
    energy_request_period("day", true);
  }
}

void show_energy_popup(const EnergyPopupInit& init) {
  hide_pin_popup();
  hide_camera_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;

  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_media_popup();

  if (g_energy_popup_ctx && g_energy_popup_ctx->overlay && g_energy_popup_ctx->card) {
    auto* ctx = g_energy_popup_ctx;
    if (!ctx->body_ready || ctx->entity_id != init.entity_id || ctx->period != "day" ||
        ctx->unit != init.unit || ctx->decimals != init.decimals) {
      ctx->body_ready = false;
      g_energy_body.hide(ctx->card, ctx->title_label, ctx->icon_label, ctx->close_button);
    }
    apply_init_to_context(g_energy_popup_ctx, init, false);
    lv_obj_clear_flag(g_energy_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_energy_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  } else {
    EnergyPopupContext* ctx = new EnergyPopupContext();
    g_energy_popup_ctx = ctx;
    build_popup_ui(ctx, init);
    g_energy_body.hide(ctx->card, ctx->title_label, ctx->icon_label, ctx->close_button);
  }

  g_energy_open_pending = true;
  defer_popup_content(g_energy_popup_ctx->card, finish_energy_popup_open);

  lv_obj_invalidate(g_energy_popup_ctx->card);
  if (g_energy_popup_ctx && g_energy_popup_ctx->card) viewNavigationPopupShown(g_energy_popup_ctx->card, init.entity_id.c_str());
  show_popup_shell(g_energy_popup_ctx->overlay, g_energy_popup_ctx->card, g_energy_popup_ctx->title_label, g_energy_popup_ctx->icon_label, g_energy_popup_ctx->close_button);
}

void preload_energy_popup() {
  if (g_energy_popup_ctx && g_energy_popup_ctx->overlay && g_energy_popup_ctx->card) return;

  EnergyPopupInit init;
  init.entity_id = "__preload__";
  init.title = "";
  init.icon_name = "";
  init.unit = "";
  init.decimals = 1;
  init.bg_color = 0;

  EnergyPopupContext* ctx = new EnergyPopupContext();
  g_energy_popup_ctx = ctx;
  build_popup_ui(ctx, init);

  if (g_energy_popup_ctx && g_energy_popup_ctx->card && g_energy_popup_ctx->overlay) {
    hide_popup_shell(g_energy_popup_ctx->card);
    cancel_popup_open(g_energy_popup_ctx->card);
    lv_obj_add_flag(g_energy_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_energy_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  }
}

void hide_energy_popup() {

  g_energy_open_pending = false;
  g_energy_body.restore();
  if (!g_energy_popup_ctx || !g_energy_popup_ctx->card || !g_energy_popup_ctx->overlay) return;
  hide_popup_shell(g_energy_popup_ctx->card);
  cancel_popup_open(g_energy_popup_ctx->card);
  lv_obj_add_flag(g_energy_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_energy_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void queue_energy_popup_refresh(const char* period) {
  g_pending_refresh.period = (period && *period) ? period : "day";
  g_pending_refresh.valid = true;
}

void process_energy_popup_queue() {
  if (!g_energy_popup_ctx || !g_energy_popup_ctx->card) {
    g_pending_refresh.valid = false;
    return;
  }
  if (PopupFirstFrame::any_pending()) return;
  if (g_energy_open_pending) {
    g_energy_open_pending = false;
    g_energy_body.restore();
    if (popup_visible(g_energy_popup_ctx)) {
      refresh_from_cache(g_energy_popup_ctx);
      energy_request_period("day", true);
    }
    return;
  }
  if (!g_pending_refresh.valid) return;

  String period = g_pending_refresh.period;
  g_pending_refresh.valid = false;
  if (!popup_visible(g_energy_popup_ctx)) return;
  if (!g_energy_popup_ctx->period.equalsIgnoreCase(period)) return;
  refresh_from_cache(g_energy_popup_ctx);
}
