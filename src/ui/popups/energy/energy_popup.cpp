#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/energy/energy_popup.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utility>

#include "src/core/config/config_manager.h"
#include "src/core/config/week_start.h"
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
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/calendar/calendar_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
#include "src/ui/popups/popup_graph_readout.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/shared/value_unit.h"

namespace {

constexpr int kCardWidth = popup_layout::kCardWidth;
constexpr int kCardPad = popup_layout::kCardPad;
// Room between the readout value and the cursor dot above the plot. On the
// 480 px layout the dot touched the value's descenders (a comma, user
// 2026-10-29 on the S3); the 600 and 720 px high layouts kept 12-15 px, the
// 800 px ones 39. The plot gives up that room at its top; its bottom, the
// time axis and the buttons stay where they were (build_chart_view).
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kChartTopTrim = 12;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kChartTopTrim = 4;
#else
constexpr int kChartTopTrim = popup_layout::kCompactHeightLift ? 4 : 0;
#endif
constexpr int kChartHeight = popup_layout::contentScale(325) - kChartTopTrim;
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
constexpr uint8_t kYearSlotCount = 12;
constexpr int kLabelOverhang = popup_layout::scale(12);
constexpr int kMinBarHeight = popup_layout::scale(2);
// Bars like the Sensor graph's area under its line (user 2026-10-29, mockup
// energy-popup-color/entscheidung.png): filled in the header circle's tone
// with the value's edge in the icon color; the read bar is all icon color.
// A white dot above the plot marks it, with a line from the bar's top edge
// (the zero line for negative or empty bars) up to the dot.
constexpr int kBarEdgeWidth = popup_layout::scale(3) > 2 ? popup_layout::scale(3) : 2;
constexpr int kReadoutLineWidth = popup_layout::scale(2) > 1 ? popup_layout::scale(2) : 1;
constexpr int kReadoutDotSize = popup_layout::scale(12);
// From the plot's top edge up to the dot's center.
constexpr int kReadoutDotLift = popup_layout::scale(14);
// Grid lines like the Sensor graph's: 1 px, 8 % of the text color.
constexpr lv_opa_t kGridLineOpa = 20;

// The fixed periods (user 2026-10-29): this year by the month, this month
// and this week by the day, today by the hour. Buttons left to right Year,
// Month, Week, Today: the longer period reaches further into the past.
enum class EnergyPeriod : uint8_t { Day, Week, Month, Year, Count };
constexpr uint8_t kPeriodCount = static_cast<uint8_t>(EnergyPeriod::Count);

const char* period_name(EnergyPeriod period) {
  switch (period) {
    case EnergyPeriod::Week: return "week";
    case EnergyPeriod::Month: return "month";
    case EnergyPeriod::Year: return "year";
    default: return "day";
  }
}

EnergyPeriod period_from_name(const String& name) {
  for (uint8_t p = 0; p < kPeriodCount; ++p) {
    if (name == period_name(static_cast<EnergyPeriod>(p))) return static_cast<EnergyPeriod>(p);
  }
  return EnergyPeriod::Day;
}

// One chart per period, built with the popup and filled from the cache, so
// a period switch swaps finished charts instead of redrawing every bar, line
// and label (155-410 ms on V2/S3 in b88-b90). The context holds the shown
// chart; `hidden` holds the other periods'. Each chart has the bars, labels
// and lines its period needs at most (period_capacity).
struct EnergyChartView {
  EnergyPeriod period = EnergyPeriod::Day;
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
  EnergyEntryData shown_entry;
  uint8_t shown_slots = 0;
  int plot_left = 0;
  int plot_w = 0;
  int zero_y = 0;  // zero line of the shown bars, relative to chart_wrap
  // The bar colors this chart was drawn in (style_bars).
  uint32_t bar_body = 0;
  uint32_t bar_edge = 0;
};

struct EnergyPopupContext : EnergyChartView {
  EnergyChartView hidden[kPeriodCount - 1];
  bool body_ready = false;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* value_label = nullptr;
  lv_obj_t* value_unit_label = nullptr;  // value_unit.h
  lv_obj_t* subtitle_label = nullptr;
  lv_obj_t* range_row = nullptr;
  lv_obj_t* period_btns[kPeriodCount] = {};    // by EnergyPeriod
  lv_obj_t* period_labels[kPeriodCount] = {};
  String entity_id;
  String title;
  String unit;
  EnergyPeriod shown_period = EnergyPeriod::Day;  // the selected button
  uint8_t prefetched = 0;  // periods loaded in the background this opening, bit per EnergyPeriod
  uint8_t decimals = 1;
  uint32_t bg_color = ui_theme::popup_card();
  // Bar readout in the old value row; texts are static label buffers, so
  // dragging allocates nothing. It opens on the newest bar, stays where the
  // finger leaves it and follows the newest bar while it shows that bar.
  PopupGraphScrub readout;
  // Holds the popup queues until the opening's content frame is drawn.
  PopupFirstFrame spare_frame;
  lv_obj_t* readout_time_label = nullptr;
  lv_obj_t* readout_value_label = nullptr;
  lv_obj_t* readout_unit_label = nullptr;
  int readout_slot = -1;
  int readout_x = 0;  // marker, relative to chart_wrap
  int readout_line_bottom = 0;  // top edge of the read bar, relative to chart_wrap
  int readout_pin_slot = -1;
  bool readout_latest = true;
  char readout_time_text[72] = "";
  char readout_value_text[96] = "";
};

// New cache data per period (EnergyPeriod); the shown chart is refreshed
// first.
struct PendingPopupRefresh {
  bool period[kPeriodCount] = {};
  bool any() const {
    for (bool pending : period) {
      if (pending) return true;
    }
    return false;
  }
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

// The round Today button: the standard short form where the word is long
// (French "Auj."), like the Weather popup footer.
const char* today_button_label() {
  return i18n::weather_today_button_label(configManager.getConfig().language);
}

const char* loading_label() {
  return i18n::strings(configManager.getConfig().language).loading;
}

String format_number(float value, uint8_t decimals) {
  return i18n::format_number(
      configManager.getConfig().language, value, decimals);
}

// The header's total and its unit, the unit a step smaller after it
// (value_unit.h, user 2026-10-26).
void show_total(EnergyPopupContext* ctx, float value, const String& unit) {
  const String number = format_number(value, ctx->decimals);
  value_unit::show(ctx->value_label, ctx->value_unit_label, number, number != "--" ? unit : String());
}

bool popup_visible(const EnergyPopupContext* ctx) {
  return ctx && ctx->card && !lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
}

// The time axis holds one full line of its labels, so hour and weekday
// labels are never clipped at the bottom.
int time_axis_height() {
  const int line = lv_font_get_line_height(popup_layout::font20());
  return line > kTimeAxisHeight ? line : kTimeAxisHeight;
}

void style_period_button(EnergyPopupContext* ctx, lv_obj_t* btn, lv_obj_t* label, bool active) {
  if (!ctx || !btn || !label) return;
  const lv_color_t popup =
      ctx->card ? lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN) : lv_color_hex(ui_theme::popup_card());
  const lv_color_t icon =
      ctx->icon_label ? lv_obj_get_style_text_color(ctx->icon_label, LV_PART_MAIN) : lv_color_hex(ui_theme::text());
  lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
  lv_obj_set_style_text_font(label, popup_layout::font24(), LV_STATE_PRESSED);
  popup_nav_style::style_toggle(btn, label, popup, icon, active);
}

int text_width(const char* text, const lv_font_t* font) {
  lv_point_t size{};
  lv_text_get_size(&size, text ? text : "", font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

// A period button's word in the display language: Year, Month, Week, and
// Today (Energy counts from midnight, not the last 24 hours).
const char* period_button_label(EnergyPeriod period) {
  const char* language = configManager.getConfig().language;
  switch (period) {
    case EnergyPeriod::Year: return i18n::energy_period_label(language, i18n::EnergyPeriodLabel::Year);
    case EnergyPeriod::Month: return i18n::energy_period_label(language, i18n::EnergyPeriodLabel::Month);
    case EnergyPeriod::Week: return i18n::energy_period_label(language, i18n::EnergyPeriodLabel::Week);
    default: return today_button_label();
  }
}

// The labels keep the 24 px font; a longer word (French "Semaine",
// "Aujourd'hui") lengthens its button into a pill instead of shrinking.
void fit_period_buttons(EnergyPopupContext* ctx) {
  if (!ctx) return;
  int row_w = 0;
  for (uint8_t p = 0; p < kPeriodCount; ++p) {
    lv_obj_t* btn = ctx->period_btns[p];
    lv_obj_t* label = ctx->period_labels[p];
    if (!btn || !label) continue;
    const char* text = period_button_label(static_cast<EnergyPeriod>(p));
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
    // Room for the round ends: a quarter of the button height on each side.
    const int width = text_width(text, popup_layout::font24()) + kRangeButtonHeight / 2;
    const int button_w = width > kRangeButtonWidth ? width : kRangeButtonWidth;
    if (lv_obj_get_style_width(btn, LV_PART_MAIN) != button_w) lv_obj_set_width(btn, button_w);
    row_w += (row_w ? kRangeButtonGap : 0) + button_w;
  }
  if (ctx->range_row && lv_obj_get_style_width(ctx->range_row, LV_PART_MAIN) != row_w)
    lv_obj_set_width(ctx->range_row, row_w);
}

void style_bars(EnergyPopupContext* ctx, int read_slot);

void update_period_buttons(EnergyPopupContext* ctx) {
  if (!ctx) return;
  for (uint8_t p = 0; p < kPeriodCount; ++p) {
    style_period_button(ctx, ctx->period_btns[p], ctx->period_labels[p],
                        ctx->shown_period == static_cast<EnergyPeriod>(p));
  }
  fit_period_buttons(ctx);
}

}  // namespace

// While open, the popup follows its tile's current background (a rules tint
// that changes with the entity state); tile_icon_source calls this.
void energy_popup_follow_tile_color(uint32_t color) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  EnergyPopupContext* ctx = g_energy_popup_ctx;
  if (!ctx || !ctx->card || lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN) || ctx->bg_color == color) return;
  ctx->bg_color = color;
  lv_obj_set_style_bg_color(ctx->card, lv_color_hex(color), 0);
  update_period_buttons(ctx);
  // The bars' circle tone follows the card; hidden charts follow when shown.
  if (ctx->shown_slots) style_bars(ctx, ctx->readout_slot);
}

namespace {

// A label's content width, computed from its font. lv_obj_update_layout()
// runs a layout pass over the whole screen (the grid and every cached folder
// behind the popup), and a 24H/7D switch asked for it about 20 times.
lv_coord_t measure_label_text_width(lv_obj_t* label) {
  if (!label) return 0;
  const char* txt = lv_label_get_text(label);
  if (!txt || !*txt) return 0;
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_point_t size{};
  lv_text_get_size(&size, txt, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                   lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0,
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x + lv_obj_get_style_pad_left(label, LV_PART_MAIN) +
         lv_obj_get_style_pad_right(label, LV_PART_MAIN);
}

// Height of a one-line label, from its font (see above).
lv_coord_t measure_label_line_height(lv_obj_t* label) {
  if (!label) return 0;
  return lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)) +
         lv_obj_get_style_pad_top(label, LV_PART_MAIN) +
         lv_obj_get_style_pad_bottom(label, LV_PART_MAIN);
}

// An axis label's number; its unit follows a step smaller (value_unit.h),
// like the Sensor popup's axis.
String format_axis_number(float value, uint8_t decimals) {
  if (!isfinite(value)) return String("");
  uint8_t d = decimals > 6 ? 6 : decimals;
  if (fabsf(value - roundf(value)) < 0.05f && d > 1) {
    d = 1;
  }
  return i18n::format_number(configManager.getConfig().language, value, d);
}

bool parse_iso_start(const String& iso, int& year, int& month, int& day, int& hour);

int days_in_month(int year, int month) {
  static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 31;
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : kDays[month - 1];
}

// The whole period on the axis, the bars only up to now: 24 hours, 7 days,
// the month's days, 12 months.
uint8_t slot_count_for_entry(const EnergyEntryData& entry) {
  const EnergyPeriod period = period_from_name(entry.period);
  if (period == EnergyPeriod::Week) return kWeekSlotCount;
  if (period == EnergyPeriod::Year) return kYearSlotCount;
  if (period == EnergyPeriod::Month) {
    int year = 0, month = 0, day = 0, hour = 0;
    if (parse_iso_start(entry.start, year, month, day, hour)) return static_cast<uint8_t>(days_in_month(year, month));
    return entry.value_count > 0 ? entry.value_count : 1;
  }
  return kDaySlotCount;
}

void clear_chart(EnergyChartView* ctx) {
  if (!ctx || !ctx->chart || !ctx->series) return;
  ctx->shown_slots = 0;
  // One dirty area for the whole chart: its bars, lines and labels alone
  // exceed LVGL's 32 areas, and LVGL then redraws the whole screen, every
  // tile behind the popup included (244 ms frames on V2 b90).
  if (ctx->chart_wrap) lv_obj_invalidate(ctx->chart_wrap);
  lv_chart_set_point_count(ctx->chart, kDaySlotCount);
  lv_chart_set_all_value(ctx->chart, ctx->series, LV_CHART_POINT_NONE);
  lv_chart_set_range(ctx->chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
  lv_chart_refresh(ctx->chart);
  // value_unit::show hides the unit beside the number too.
  if (ctx->y_max_label) value_unit::show(ctx->y_max_label, "", "");
  if (ctx->y_zero_label) value_unit::show(ctx->y_zero_label, "", "");
  if (ctx->y_min_label) value_unit::show(ctx->y_min_label, "", "");
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
  const lv_coord_t label_w = measure_label_text_width(label);
  int x = center_x - (label_w / 2);
  if (x < 0) x = 0;
  int max_x = avail_w - label_w;
  if (max_x < 0) max_x = 0;
  if (x > max_x) x = max_x;
  return x;
}

// The time axis of a period: labels under a slot boundary or centered under
// a slot (`label_slot` plus `label_centered`), and faint lines at slot
// boundaries.
struct AxisMarks {
  uint8_t labels = 0;
  uint8_t lines = 0;
  String label_text[ENERGY_VALUES_MAX];
  uint8_t label_slot[ENERGY_VALUES_MAX] = {};
  bool label_centered = true;
  uint8_t line_slot[ENERGY_VALUES_MAX] = {};
};

// The date of a day slot: the entry's start plus `offset` days, at noon so a
// clock change never moves it to another day.
bool slot_date(const EnergyEntryData& entry, int offset, struct tm& out) {
  int year = 0, month = 0, day = 0, hour = 0;
  if (!parse_iso_start(entry.start, year, month, day, hour)) return false;
  out = {};
  out.tm_year = year - 1900;
  out.tm_mon = month - 1;
  out.tm_mday = day + offset;
  out.tm_hour = 12;
  mktime(&out);
  return true;
}

// Today: lines and hours every six hours. Week: each weekday, lines between
// the days. Month: a line and the day's number at each week's first day
// (Settings > Localization), like the Sensor graph's 30D. Year: every second
// month's short name, a line before each pair.
void collect_axis_marks(const EnergyEntryData& entry, uint8_t slot_count, AxisMarks& marks) {
  marks.labels = marks.lines = 0;
  auto add_label = [&](uint8_t slot, const String& text) {
    if (marks.labels >= ENERGY_VALUES_MAX || !text.length()) return;
    marks.label_slot[marks.labels] = slot;
    marks.label_text[marks.labels++] = text;
  };
  auto add_line = [&](uint8_t slot) {
    if (marks.lines < ENERGY_VALUES_MAX) marks.line_slot[marks.lines++] = slot;
  };
  const EnergyPeriod period = period_from_name(entry.period);
  if (period == EnergyPeriod::Day) {
    marks.label_centered = false;
    for (uint8_t marker = 0; marker <= kDaySlotCount; marker += 6) {
      add_line(marker);
      add_label(marker, day_marker_label(marker));
    }
    return;
  }
  marks.label_centered = true;
  if (period == EnergyPeriod::Week) {
    for (uint8_t i = 0; i < slot_count; ++i) add_label(i, week_x_label_for_entry(entry, i));
    for (uint8_t i = 1; i < slot_count; ++i) add_line(i);
    return;
  }
  if (period == EnergyPeriod::Month) {
    const int first_weekday = week_start::panel();
    for (uint8_t i = 0; i < slot_count; ++i) {
      struct tm date;
      if (!slot_date(entry, i, date) || date.tm_wday != first_weekday) continue;
      if (i > 0) add_line(i);
      add_label(i, String(date.tm_mday));
    }
    return;
  }
  int year = 0, month = 1, day = 0, hour = 0;
  if (!parse_iso_start(entry.start, year, month, day, hour)) month = 1;
  const char* language = configManager.getConfig().language;
  // Every second month on every panel (user 2026-10-29: twelve names and
  // eleven lines looked crowded on the large panels): a line before Mar, May,
  // Jul, Sep and Nov, each pair's first month named under its bar, like the
  // month view's weeks.
  for (uint8_t i = 0; i < slot_count; i += 2) {
    if (i > 0) add_line(i);
    add_label(i, i18n::weather_month_short(language, (month - 1 + i) % 12 + 1));
  }
}

int slot_x(int slot, uint8_t slot_count, int plot_left, int plot_w) {
  return plot_left + static_cast<int>(lroundf(
      (static_cast<float>(slot) / static_cast<float>(slot_count)) * static_cast<float>(plot_w)));
}

void update_x_axis(EnergyPopupContext* ctx,
                   const AxisMarks& marks,
                   uint8_t slot_count,
                   int plot_left,
                   int plot_w,
                   int max_label_w) {
  if (!ctx || !ctx->x_axis || slot_count == 0 || plot_w <= 0) return;
  lv_obj_set_pos(ctx->x_axis, 0, kLabelOverhang + kChartHeight + kTimeAxisGap);
  lv_obj_set_size(ctx->x_axis, LV_PCT(100), time_axis_height());

  for (uint8_t i = 0; i < ENERGY_VALUES_MAX; ++i) {
    if (ctx->x_lines[i]) lv_obj_add_flag(ctx->x_lines[i], LV_OBJ_FLAG_HIDDEN);
    if (ctx->x_labels[i]) {
      lv_label_set_text(ctx->x_labels[i], "");
      lv_obj_add_flag(ctx->x_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  const int avail_w = kCardWidth - (kCardPad * 2);
  // Labels closer than their width plus a gap show every second (third ...)
  // one: twelve month names on the 480 px panels.
  constexpr int kAxisLabelGap = popup_layout::scale(8);
  uint8_t step = 1;
  if (marks.labels > 1) {
    const int spacing = slot_x(marks.label_slot[1], slot_count, plot_left, plot_w) -
                        slot_x(marks.label_slot[0], slot_count, plot_left, plot_w);
    while (step < marks.labels && spacing * step < max_label_w + kAxisLabelGap) ++step;
  }
  uint8_t label_index = 0;
  for (uint8_t i = 0; i < marks.labels; i += step) {
    lv_obj_t* label = label_index < ENERGY_VALUES_MAX ? ctx->x_labels[label_index++] : nullptr;
    if (!label) continue;
    const uint8_t slot = marks.label_slot[i];
    int center_x = slot_x(slot, slot_count, plot_left, plot_w);
    if (marks.label_centered) {
      // Same slot geometry as the bars in apply_entry_to_chart().
      center_x += (slot_x(slot + 1, slot_count, plot_left, plot_w) - center_x) / 2;
    }
    lv_label_set_text(label, marks.label_text[i].c_str());
    lv_obj_set_pos(label, clamped_label_x(label, center_x, avail_w), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
  }
  for (uint8_t i = 0; i < marks.lines && i < ENERGY_VALUES_MAX; ++i) {
    lv_obj_t* line = ctx->x_lines[i];
    if (!line) continue;
    lv_obj_set_pos(line, slot_x(marks.line_slot[i], slot_count, plot_left, plot_w), kLabelOverhang);
    lv_obj_set_size(line, 1, kChartHeight);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
  }
}

void update_header_value(EnergyPopupContext* ctx, const EnergyEntryData& fallback) {
  if (!ctx || !ctx->value_label || !ctx->subtitle_label) return;

  // The header keeps today's value, like the tile, whichever period shows.
  EnergyEntryData day_entry;
  const EnergyEntryData* display_entry = &fallback;
  String subtitle = period_button_label(period_from_name(fallback.period));
  if (energy_find_entry(ctx->entity_id, "day", day_entry)) {
    display_entry = &day_entry;
    subtitle = today_label();
  }

  String unit = ctx->unit.length() ? ctx->unit : display_entry->unit;
  show_total(ctx, display_entry->total, unit);
  lv_label_set_text(ctx->subtitle_label, subtitle.c_str());
}

void update_loading_header(EnergyPopupContext* ctx) {
  if (!ctx || !ctx->value_label || !ctx->subtitle_label) return;

  EnergyEntryData day_entry;
  if (energy_find_entry(ctx->entity_id, "day", day_entry)) {
    String unit = ctx->unit.length() ? ctx->unit : day_entry.unit;
    show_total(ctx, day_entry.total, unit);
    lv_label_set_text(ctx->subtitle_label, today_label());
    return;
  }

  value_unit::show(ctx->value_label, ctx->value_unit_label, loading_label(), String());
  lv_label_set_text(ctx->subtitle_label, period_button_label(ctx->shown_period));
}

bool readout_twelve_hour() {
  const DeviceConfig& cfg = configManager.getConfig();
  return clock_tile::resolve_time_format(clock_tile::TIME_FORMAT_AUTO,
                                         cfg.global_time_format, cfg.language) !=
         clock_tile::TIME_FORMAT_24H;
}

// The bar colors: the header circle's tone (popup_nav_style::fill over the
// card, also the selected period button's) and the icon color, like the
// Sensor graph's area and line.
void bar_colors(const EnergyPopupContext* ctx, uint32_t& body, uint32_t& edge) {
  const lv_color_t card =
      ctx->card ? lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN) : lv_color_hex(ui_theme::popup_card());
  const lv_color_t icon =
      ctx->icon_label ? lv_obj_get_style_text_color(ctx->icon_label, LV_PART_MAIN) : lv_color_hex(ui_theme::text());
  lv_color_t fill;
  lv_opa_t fill_opa;
  popup_nav_style::fill(card, icon, fill, fill_opa);
  body = lv_color_to_u32(lv_color_mix(fill, card, fill_opa)) & 0xFFFFFF;
  edge = lv_color_to_u32(icon) & 0xFFFFFF;
}

// One bar: the body with the value's edge, or all icon color while the
// readout shows it. Only colors change, so a finger moving over the bars
// moves nothing.
void style_bar(EnergyPopupContext* ctx, int index, bool read) {
  lv_obj_t* bar = index >= 0 && index < ENERGY_VALUES_MAX ? ctx->bars[index] : nullptr;
  if (!bar) return;
  lv_obj_set_style_bg_color(bar, lv_color_hex(read ? ctx->bar_edge : ctx->bar_body), 0);
  lv_obj_set_style_border_width(bar, read ? 0 : kBarEdgeWidth, 0);
}

// The chart's bars in the current colors, `read_slot` all icon color: after
// new bars, and when the card or the icon color changed since it was drawn.
void style_bars(EnergyPopupContext* ctx, int read_slot) {
  bar_colors(ctx, ctx->bar_body, ctx->bar_edge);
  const lv_color_t edge = lv_color_hex(ctx->bar_edge);
  for (int i = 0; i < ENERGY_VALUES_MAX; ++i) {
    if (!ctx->bars[i]) continue;
    lv_obj_set_style_border_color(ctx->bars[i], edge, 0);
    style_bar(ctx, i, i == read_slot);
  }
}

bool bar_colors_current(const EnergyPopupContext* ctx) {
  uint32_t body = 0, edge = 0;
  bar_colors(ctx, body, edge);
  return body == ctx->bar_body && edge == ctx->bar_edge;
}

// The dot sits above the plot, partly outside chart_wrap, which extends its
// drawing area by this much (on_energy_cursor_ext_draw).
constexpr int kReadoutMarkerOverhang =
    kReadoutDotLift + kReadoutDotSize / 2 + 1 - kLabelOverhang > 0
        ? kReadoutDotLift + kReadoutDotSize / 2 + 1 - kLabelOverhang
        : 0;

// Marker area: the dot above the plot down to the bottom of the plot.
void invalidate_energy_cursor(EnergyPopupContext* ctx) {
  if (!ctx->chart_wrap || ctx->readout_slot < 0) return;
  lv_area_t coords;
  lv_obj_get_coords(ctx->chart_wrap, &coords);
  const int x = coords.x1 + ctx->readout_x;
  const int reach = kReadoutDotSize / 2 + kReadoutLineWidth + 1;
  lv_area_t area = {x - reach,
                    coords.y1 + kLabelOverhang - kReadoutDotLift - kReadoutDotSize / 2 - 1,
                    x + reach, coords.y1 + kLabelOverhang + kChartHeight};
  lv_obj_invalidate_area(ctx->chart_wrap, &area);
}

void on_energy_cursor_ext_draw(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  auto* size = static_cast<int32_t*>(lv_event_get_param(event));
  if (size && *size < kReadoutMarkerOverhang) *size = kReadoutMarkerOverhang;
}

// Drawn with chart_wrap's own layer, which reaches above the plot (the
// extended draw size); the post-draw layer after the children would clip the
// dot to chart_wrap. The marker needs no object that moves while dragging and
// never overlaps a bar: the line ends at the read bar's top edge.
void on_energy_cursor_draw(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  auto* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(event));
  lv_layer_t* layer = lv_event_get_layer(event);
  if (!ctx || !layer || ctx->readout_slot < 0) return;
  lv_area_t coords;
  lv_obj_get_coords(ctx->chart_wrap, &coords);
  const int x = coords.x1 + ctx->readout_x;
  const int dot_y = coords.y1 + kLabelOverhang - kReadoutDotLift;
  lv_draw_rect_dsc_t mark;
  lv_draw_rect_dsc_init(&mark);
  mark.base.layer = layer;
  mark.bg_color = lv_color_hex(ui_theme::text());
  mark.bg_opa = LV_OPA_COVER;
  mark.radius = 0;
  const int line_x = x - kReadoutLineWidth / 2;
  lv_area_t line = {line_x, dot_y, line_x + kReadoutLineWidth - 1,
                    coords.y1 + ctx->readout_line_bottom - 1};
  if (line.y2 >= line.y1) lv_draw_rect(layer, &mark, &line);
  mark.radius = LV_RADIUS_CIRCLE;
  const int dot_x = x - kReadoutDotSize / 2;
  const int dot_top = dot_y - kReadoutDotSize / 2;
  lv_area_t dot = {dot_x, dot_top, dot_x + kReadoutDotSize - 1, dot_top + kReadoutDotSize - 1};
  lv_draw_rect(layer, &mark, &dot);
}

// Hiding, opening, a period change and new chart data restore the normal bars.
void clear_energy_readout(EnergyPopupContext* ctx) {
  if (!ctx) return;
  invalidate_energy_cursor(ctx);
  if (ctx->readout_slot >= 0) style_bar(ctx, ctx->readout_slot, false);
  ctx->readout_slot = -1;
  popup_graph_readout::show_band_labels(ctx->readout_time_label, ctx->readout_value_label,
                                        false);
}

// The newest bar with a value: the current hour or day.
int latest_energy_slot(const EnergyPopupContext* ctx) {
  const EnergyEntryData& entry = ctx->shown_entry;
  int slot = entry.value_count < ctx->shown_slots ? entry.value_count : ctx->shown_slots;
  while (--slot >= 0) {
    if (entry.value_valid[slot]) return slot;
  }
  return -1;
}

// Hourly bars read as their hour range, the week's bars as their weekday,
// the month's as their date ("29. Okt.") and the year's as their month.
void format_energy_slot_time(EnergyPopupContext* ctx, int slot) {
  char* out = ctx->readout_time_text;
  const size_t size = sizeof(ctx->readout_time_text);
  out[0] = 0;
  const EnergyEntryData& entry = ctx->shown_entry;
  const char* language = configManager.getConfig().language;
  const EnergyPeriod period = period_from_name(entry.period);
  if (period == EnergyPeriod::Day) {
    const bool twelve_hour = readout_twelve_hour();
    size_t used = popup_graph_readout::append_hour(out, size, 0, slot, twelve_hour);
    used = popup_graph_readout::append(out, size, used, popup_graph_readout::kRangeSeparator);
    popup_graph_readout::append_hour(out, size, used, slot + 1, twelve_hour);
  } else if (period == EnergyPeriod::Year) {
    int year = 0, month = 1, day = 0, hour = 0;
    if (!parse_iso_start(entry.start, year, month, day, hour)) return;
    popup_graph_readout::append(out, size, 0, i18n::month_name(language, (month - 1 + slot) % 12 + 1));
  } else {
    struct tm date;
    if (!slot_date(entry, slot, date)) return;
    if (period == EnergyPeriod::Week) {
      popup_graph_readout::append(out, size, 0, clock_tile::weekday_name(date.tm_wday, language));
    } else {
      // Formatted only when the read bar changes, never per frame.
      const String text =
          i18n::format_short_date(language, date.tm_mday, i18n::weather_month_short(language, date.tm_mon + 1));
      popup_graph_readout::append(out, size, 0, text.c_str());
    }
  }
}

bool energy_readout_ready(const EnergyPopupContext* ctx) {
  return popup_visible(ctx) && ctx->shown_slots > 0 && ctx->plot_w > 0 &&
         ctx->shown_entry.value_count && ctx->readout_time_label &&
         ctx->readout_value_label;
}

// The read bar turns all icon color and a thin line marks it. Only two
// bars' colors, the line and the two static labels change.
void show_energy_slot(EnergyPopupContext* ctx, int slot) {
  const int slots = ctx->shown_slots;
  if (slot >= slots) slot = slots - 1;
  if (slot < 0 || slot == ctx->readout_slot) return;
  if (ctx->readout_slot >= 0) style_bar(ctx, ctx->readout_slot, false);
  style_bar(ctx, slot, true);
  invalidate_energy_cursor(ctx);
  ctx->readout_slot = slot;
  // Same slot geometry as the bars in apply_entry_to_chart().
  const int slot_l = ctx->plot_left + static_cast<int>(lroundf(
      (static_cast<float>(slot) / static_cast<float>(slots)) * static_cast<float>(ctx->plot_w)));
  const int slot_r = ctx->plot_left + static_cast<int>(lroundf(
      (static_cast<float>(slot + 1) / static_cast<float>(slots)) * static_cast<float>(ctx->plot_w)));
  ctx->readout_x = slot_l + (slot_r - slot_l) / 2;
  // Negative bars start at the zero line; empty slots have no bar. The style
  // position is current before LVGL's next layout pass; the bars are
  // top-left aligned in chart_wrap, so it equals lv_obj_get_y() afterwards.
  lv_obj_t* bar = slot < ENERGY_VALUES_MAX ? ctx->bars[slot] : nullptr;
  ctx->readout_line_bottom =
      bar && !lv_obj_has_flag(bar, LV_OBJ_FLAG_HIDDEN) ? lv_obj_get_style_y(bar, LV_PART_MAIN) : ctx->zero_y;
  invalidate_energy_cursor(ctx);

  format_energy_slot_time(ctx, slot);
  const EnergyEntryData& entry = ctx->shown_entry;
  const bool valid = slot < entry.value_count && entry.value_valid[slot];
  const String& unit = ctx->unit.length() ? ctx->unit : entry.unit;
  // The number in the static buffer (no allocation while dragging), its
  // unit a step smaller after it (value_unit.h).
  popup_graph_readout::format_number(
      ctx->readout_value_text, sizeof(ctx->readout_value_text),
      valid ? entry.values[slot] : NAN, ctx->decimals,
      i18n::locale(configManager.getConfig().language).decimal_separator[0], nullptr);
  value_unit::reserve(ctx->readout_value_label, ctx->readout_unit_label, valid ? unit.c_str() : "");
  lv_label_set_text_static(ctx->readout_time_label, ctx->readout_time_text);
  lv_label_set_text_static(ctx->readout_value_label, ctx->readout_value_text);
  value_unit::place(ctx->readout_value_label, ctx->readout_unit_label);
  popup_graph_readout::show_band_labels(ctx->readout_time_label, ctx->readout_value_label,
                                        true);
}

// The touched bar: the finger's x over the plot, clamped to the bars.
void apply_energy_readout(EnergyPopupContext* ctx, const lv_point_t& point) {
  if (!energy_readout_ready(ctx)) {
    clear_energy_readout(ctx);
    return;
  }
  lv_area_t wrap;
  lv_obj_get_coords(ctx->chart_wrap, &wrap);
  const int x = point.x - wrap.x1 - ctx->plot_left;
  show_energy_slot(ctx, x <= 0 ? 0 : x * ctx->shown_slots / ctx->plot_w);
}

void on_energy_readout_apply(void* owner, lv_obj_t*, const lv_point_t& point) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  auto* ctx = static_cast<EnergyPopupContext*>(owner);
  if (ctx) apply_energy_readout(ctx, point);
}

// Release keeps the read bar; it follows new data while it is the newest bar.
void on_energy_readout_end(void* owner, bool keep) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  auto* ctx = static_cast<EnergyPopupContext*>(owner);
  if (!ctx) return;
  if (keep && ctx->readout_slot >= 0) {
    ctx->readout_pin_slot = ctx->readout_slot;
    ctx->readout_latest = ctx->readout_slot == latest_energy_slot(ctx);
    return;
  }
  clear_energy_readout(ctx);
  if (!keep) {
    ctx->readout_latest = true;
    ctx->readout_pin_slot = -1;
  }
}

// After new bars: the newest bar, or the bar the finger left.
void refresh_energy_readout(EnergyPopupContext* ctx) {
  if (ctx->readout.active()) return;
  clear_energy_readout(ctx);
  if (!energy_readout_ready(ctx)) return;
  show_energy_slot(ctx, ctx->readout_latest ? latest_energy_slot(ctx) : ctx->readout_pin_slot);
}

// `shown` is false while the hidden chart of the other period is filled: the
// readout and the header belong to the shown chart.
void apply_entry_to_chart(EnergyPopupContext* ctx, const EnergyEntryData& entry,
                          bool shown = true) {
  if (!ctx || !ctx->chart || !ctx->series) return;

  // One dirty area for the whole chart, see clear_chart().
  if (ctx->chart_wrap) lv_obj_invalidate(ctx->chart_wrap);
  // New bars replace what a finger may be reading; a kept readout returns
  // with refresh_energy_readout() below.
  if (shown) {
    if (ctx->readout.active()) ctx->readout.cancel();
    clear_energy_readout(ctx);
    update_header_value(ctx, entry);
  }
  ctx->shown_slots = 0;

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

  // The numbers with their unit a step smaller after them (value_unit.h,
  // user 2026-10-29: like the Sensor popup's axis); sized to their text
  // first, so the unit always splits off, then one width right-aligned below.
  const String axis_unit = ctx->unit.length() ? ctx->unit : entry.unit;
  auto show_axis_label = [&](lv_obj_t* label, const String& number) {
    if (!label) return;
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    value_unit::show(label, number, number.length() ? axis_unit : String());
  };
  const String zero_text = "0";  // without decimals, as before
  if (show_zero) {
    show_axis_label(ctx->y_max_label, format_axis_number(max_v, ctx->decimals));
    show_axis_label(ctx->y_min_label, format_axis_number(min_v, ctx->decimals));
    show_axis_label(ctx->y_zero_label, zero_text);
  } else if (has_negative) {
    show_axis_label(ctx->y_max_label, zero_text);
    show_axis_label(ctx->y_min_label, format_axis_number(min_v, ctx->decimals));
    show_axis_label(ctx->y_zero_label, String());
  } else {
    show_axis_label(ctx->y_max_label, format_axis_number(max_v, ctx->decimals));
    show_axis_label(ctx->y_min_label, zero_text);
    show_axis_label(ctx->y_zero_label, String());
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

  AxisMarks marks;
  collect_axis_marks(entry, slot_count, marks);
  int max_x_label_w = 0;
  for (uint8_t i = 0; i < marks.labels; ++i) {
    const int w = text_width(marks.label_text[i].c_str(), popup_layout::font20());
    if (w > max_x_label_w) max_x_label_w = w;
  }
  int chart_right_reserve = (max_x_label_w > 0) ? (max_x_label_w / 2 + kTimeAxisEdgeGap) : 8;

  int plot_w = avail_w - plot_left - chart_right_reserve;
  if (plot_w < 10) plot_w = 10;

  // One width, right-aligned: the units stand in one column at the plot.
  auto set_axis_label_width = [&](lv_obj_t* label) {
    if (!label) return;
    lv_obj_set_width(label, label_w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    value_unit::place(label);
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
  place_line(ctx->y_max_line, chart_top, kGridLineOpa);
  place_line(ctx->y_min_line, chart_bottom, kGridLineOpa);
  if (show_zero) {
    place_line(ctx->y_zero_line, zero_y, kGridLineOpa);
  } else {
    if (ctx->y_zero_line) lv_obj_add_flag(ctx->y_zero_line, LV_OBJ_FLAG_HIDDEN);
  }

  auto place_axis_label = [&](lv_obj_t* label, int line_y, bool visible) {
    if (!label) return;
    if (!visible) {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    const int h = measure_label_line_height(label);
    int y = line_y - (h / 2);
    if (y < 0) y = 0;
    const int max_y = kLabelOverhang + kChartHeight + time_axis_height() - h;
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
  const EnergyPeriod period = period_from_name(entry.period);
  if (period == EnergyPeriod::Week || period == EnergyPeriod::Year) {
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
    if (!isfinite(value)) continue;
    // A measured 0: only its edge on the zero line (below it where the zero
    // line is the plot's top), so it does not read as a missing hour (user
    // 2026-10-29); a missing hour shows nothing.
    const bool zero = fabsf(value) < 0.0001f;

    const int value_y = value_to_y(value);
    int bar_y = zero_y;
    int bar_h = 0;
    if (zero) {
      bar_h = kBarEdgeWidth;
      bar_y = zero_y - bar_h >= chart_top ? zero_y - bar_h : zero_y;
    } else if (value >= 0.0f) {
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
    // The value's edge: on top of a bar above zero, at the bottom of one
    // below it.
    lv_obj_set_style_border_side(ctx->bars[i], value >= 0.0f ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_clear_flag(ctx->bars[i], LV_OBJ_FLAG_HIDDEN);
  }
  // The readout below restyles its bar (refresh_energy_readout).
  style_bars(ctx, -1);

  update_x_axis(ctx, marks, slot_count, plot_left, plot_w, max_x_label_w);
  ctx->shown_entry = entry;
  ctx->shown_slots = slot_count;
  ctx->plot_left = plot_left;
  ctx->plot_w = plot_w;
  ctx->zero_y = zero_y;
  if (shown) refresh_energy_readout(ctx);
}

void show_empty_chart(EnergyPopupContext* ctx) {
  if (!ctx) return;

  EnergyEntryData entry;
  entry.id = ctx->entity_id;
  entry.name = ctx->title;
  entry.unit = ctx->unit;
  entry.period = period_name(ctx->period);
  entry.total = 0.0f;
  apply_entry_to_chart(ctx, entry);
  update_loading_header(ctx);
}

// True when an entry would draw the same bars, axis and header.
bool same_chart_data(const EnergyEntryData& a, const EnergyEntryData& b) {
  if (a.id != b.id || a.period != b.period || a.start != b.start || a.unit != b.unit ||
      a.total != b.total || a.value_count != b.value_count) {
    return false;
  }
  for (uint8_t i = 0; i < a.value_count && i < ENERGY_VALUES_MAX; ++i) {
    if (a.value_valid[i] != b.value_valid[i]) return false;
    if (a.value_valid[i] && a.values[i] != b.values[i]) return false;
  }
  return true;
}

// The hidden chart of a period, nullptr for the shown one.
EnergyChartView* hidden_view(EnergyPopupContext* ctx, EnergyPeriod period) {
  for (EnergyChartView& view : ctx->hidden) {
    if (view.period == period) return &view;
  }
  return nullptr;
}

void swap_chart_views(EnergyPopupContext* ctx, EnergyChartView& view) {
  std::swap(static_cast<EnergyChartView&>(*ctx), view);
}

// Shows the chart of ctx->shown_period and hides the others.
void show_period_view(EnergyPopupContext* ctx) {
  if (ctx->period != ctx->shown_period) {
    if (EnergyChartView* view = hidden_view(ctx, ctx->shown_period)) swap_chart_views(ctx, *view);
  }
  for (EnergyChartView& view : ctx->hidden) {
    if (view.chart_wrap) lv_obj_add_flag(view.chart_wrap, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->chart_wrap) lv_obj_remove_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
  // A chart drawn before the card or the icon color changed (another tile's
  // popup, a rule tint) takes the current colors.
  if (ctx->shown_slots && !bar_colors_current(ctx)) style_bars(ctx, ctx->readout_slot);
}

void refresh_from_cache(EnergyPopupContext* ctx) {
  if (!ctx || !popup_visible(ctx)) return;
  EnergyEntryData entry;
  if (!energy_find_entry(ctx->entity_id, period_name(ctx->period), entry)) {
    show_empty_chart(ctx);
    return;
  }
  // A prepared chart that already shows this entry only needs its readout.
  if (ctx->shown_slots && same_chart_data(ctx->shown_entry, entry)) {
    update_header_value(ctx, entry);
    refresh_energy_readout(ctx);
    return;
  }
  apply_entry_to_chart(ctx, entry);
}

// Fills the hidden chart of a period from the cache.
void refresh_hidden_from_cache(EnergyPopupContext* ctx, EnergyPeriod period) {
  EnergyChartView* view = ctx ? hidden_view(ctx, period) : nullptr;
  if (!view || !view->chart) return;
  EnergyEntryData entry;
  if (!energy_find_entry(ctx->entity_id, period_name(period), entry)) {
    if (view->shown_slots) clear_chart(view);
    return;
  }
  if (view->shown_slots && same_chart_data(view->shown_entry, entry)) return;
  swap_chart_views(ctx, *view);
  apply_entry_to_chart(ctx, entry, false);
  swap_chart_views(ctx, *view);
}

// The other periods load in the background one after another, so a switch
// shows cached bars at once instead of waiting for the Bridge (250-600 ms):
// the Bridge runs one energy request per panel and queues two
// (request_limits.py), four at once lost one. Each period loads once per
// opening and not when its data is less than a minute old; a switch asks
// again (energy_request_period with force).
void prefetch_next_period(EnergyPopupContext* ctx) {
  if (!popup_visible(ctx) || energy_request_waiting()) return;
  for (const EnergyPeriod period : {EnergyPeriod::Week, EnergyPeriod::Month, EnergyPeriod::Year}) {
    const uint8_t bit = 1u << static_cast<uint8_t>(period);
    if ((ctx->prefetched & bit) || period == ctx->shown_period) continue;
    ctx->prefetched |= bit;
    if (energy_period_fresh(period_name(period))) continue;
    energy_request_period(period_name(period), false);
    return;
  }
}

void apply_init_to_context(EnergyPopupContext* ctx, const EnergyPopupInit& init,
                           bool reset_body = true) {
  if (!ctx) return;
  ctx->entity_id = init.entity_id;
  ctx->title = init.title;
  ctx->unit = init.unit;
  ctx->decimals = init.decimals > 6 ? 6 : init.decimals;
  ctx->bg_color = init.bg_color ? init.bg_color : ui_theme::popup_card();
  ctx->shown_period = EnergyPeriod::Day;
  ctx->prefetched = 0;

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
        lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(init.icon_color), 0);
        lv_obj_clear_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(ctx->icon_label, "");
        lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
  update_period_buttons(ctx);
  // After the card and icon colors: a kept chart may need them (show_period_view).
  show_period_view(ctx);
  if (reset_body) {
    clear_chart(ctx);
    for (EnergyChartView& view : ctx->hidden) clear_chart(&view);
  }
  // The header value is part of the first frame, before the deferred body.
  update_loading_header(ctx);
}

void on_close_click(lv_event_t* e) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  EnergyPopupContext* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->overlay || !ctx->card) return;

  ctx->readout.cancel();
  g_energy_open_pending = false;
  g_energy_body.restore();
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void on_overlay_click(lv_event_t* e) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)e;
}

void on_overlay_delete(lv_event_t* e) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  EnergyPopupContext* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  // Detach the display refresh hook before the context goes away.
  ctx->readout.cancel();
  if (g_energy_popup_ctx == ctx) {

    g_energy_open_pending = false;
    g_energy_body.forget();
    g_energy_popup_ctx = nullptr;
  }
  delete ctx;
}

void on_period_click(lv_event_t* e) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  EnergyPopupContext* ctx = static_cast<EnergyPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  EnergyPeriod next = ctx->shown_period;
  for (uint8_t p = 0; p < kPeriodCount; ++p) {
    if (target == ctx->period_btns[p]) next = static_cast<EnergyPeriod>(p);
  }
  if (ctx->shown_period == next) return;
  const uint32_t started_ms = millis();
  // One dirty area for the old chart, see clear_chart().
  if (ctx->chart_wrap) lv_obj_invalidate(ctx->chart_wrap);
  ctx->readout.cancel();
  ctx->shown_period = next;
  update_period_buttons(ctx);
  update_loading_header(ctx);
  // The prepared chart of the period takes over; it is only redrawn when the
  // cache holds newer data than it shows.
  show_period_view(ctx);
  const bool prepared = ctx->shown_slots != 0;
  refresh_from_cache(ctx);
  energy_request_period(period_name(next), true);
  // One line per tap: whether the prepared chart was shown, and how long the
  // switch took on the UI task. The response line follows in energy_data.
  Serial.printf("[EnergyPopup] Period %s: %s in %lu ms\n", period_name(next),
                prepared ? "prepared chart shown" : "chart built",
                static_cast<unsigned long>(millis() - started_ms));
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
  set_label_style(label, lv_color_hex(ui_theme::text()), popup_layout::font24());
  lv_label_set_text(label, text);
  lv_obj_center(label);
  if (out_label) *out_label = label;
  return btn;
}

// The objects a period's chart needs at most: bars, axis labels and lines
// (collect_axis_marks).
struct PeriodCapacity {
  uint8_t bars;
  uint8_t labels;
  uint8_t lines;
};

PeriodCapacity period_capacity(EnergyPeriod period) {
  switch (period) {
    case EnergyPeriod::Week: return {kWeekSlotCount, kWeekSlotCount, kWeekSlotCount - 1};
    // At most five first weekdays in a month, a line at each but the 1st.
    case EnergyPeriod::Month: return {31, 5, 5};
    // Every second month named, a line before each pair but the first.
    case EnergyPeriod::Year: return {kYearSlotCount, kYearSlotCount / 2, kYearSlotCount / 2 - 1};
    default: return {kDaySlotCount, 5, 5};
  }
}

// Builds one period's chart into the context's shown view.
void build_chart_view(EnergyPopupContext* ctx, lv_obj_t* body_box, EnergyPeriod period) {
  ctx->period = period;
  const PeriodCapacity capacity = period_capacity(period);
  lv_obj_t* chart_wrap = lv_obj_create(body_box);
  ctx->chart_wrap = chart_wrap;
  lv_obj_remove_style_all(chart_wrap);
  lv_obj_set_size(chart_wrap, LV_PCT(100), kLabelOverhang + kChartHeight + kTimeAxisGap + time_axis_height());
  // Centered as before the trim, then down by half of it: the bottom stays.
  lv_obj_align(chart_wrap, LV_ALIGN_CENTER, 0, kChartTopTrim / 2);
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
  lv_obj_set_style_bg_color(chart, lv_color_hex(ui_theme::icon()), LV_PART_ITEMS);
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
  ctx->series = lv_chart_add_series(chart, lv_color_hex(ui_theme::text()), LV_CHART_AXIS_PRIMARY_Y);

  auto make_axis_label = [&](void) -> lv_obj_t* {
    lv_obj_t* label = lv_label_create(chart_wrap);
    set_label_style(label, lv_color_hex(ui_theme::text()), popup_layout::font20());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return label;
  };
  ctx->y_max_label = make_axis_label();
  ctx->y_zero_label = make_axis_label();
  ctx->y_min_label = make_axis_label();

  auto make_guide_line = [&]() -> lv_obj_t* {
    lv_obj_t* line = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, popup_layout::scale480(10), 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(ui_theme::text()), 0);
    lv_obj_set_style_bg_opa(line, kGridLineOpa, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
  };
  ctx->y_max_line = make_guide_line();
  ctx->y_zero_line = make_guide_line();
  ctx->y_min_line = make_guide_line();

  for (uint8_t i = 0; i < capacity.lines; ++i) {
    ctx->x_lines[i] = make_guide_line();
  }

  for (uint8_t i = 0; i < capacity.bars; ++i) {
    lv_obj_t* bar = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, popup_layout::scale480(4), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    ctx->bars[i] = bar;
  }

  lv_obj_t* x_axis = lv_obj_create(chart_wrap);
  ctx->x_axis = x_axis;
  lv_obj_remove_style_all(x_axis);
  // Touches on the axis belong to the bar readout.
  lv_obj_remove_flag(x_axis, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(x_axis, LV_PCT(100), time_axis_height());
  lv_obj_set_pos(x_axis, 0, kLabelOverhang + kChartHeight + kTimeAxisGap);

  for (uint8_t i = 0; i < capacity.labels; ++i) {
    lv_obj_t* label = lv_label_create(x_axis);
    ctx->x_labels[i] = label;
    set_label_style(label, lv_color_hex(ui_theme::text()), popup_layout::font20());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_label_set_text(label, "");
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
  }

  ctx->readout.attach(chart_wrap);
  lv_obj_add_event_cb(chart_wrap, on_energy_cursor_draw, LV_EVENT_DRAW_MAIN, ctx);
  lv_obj_add_event_cb(chart_wrap, on_energy_cursor_ext_draw, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
  lv_obj_refresh_ext_draw_size(chart_wrap);
}

void build_popup_ui(EnergyPopupContext* ctx, const EnergyPopupInit& init) {
  const auto parts = create_popup_body(on_close_click, ctx, init.bg_color ? init.bg_color : ui_theme::popup_card());
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
  lv_obj_set_size(period_row, (kRangeButtonWidth * kPeriodCount) + kRangeButtonGap * (kPeriodCount - 1),
                  popup_layout::kNavHeight);
  lv_obj_align(period_row, LV_ALIGN_BOTTOM_MID, 0, -popup_layout::kNavBottomInset);
  lv_obj_set_style_bg_opa(period_row, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(period_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(period_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(period_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(period_row, kRangeButtonGap, 0);
  lv_obj_clear_flag(period_row, LV_OBJ_FLAG_CLICKABLE);

  // Year, Month, Week, Today: the longer period reaches further into the past.
  for (const EnergyPeriod period : {EnergyPeriod::Year, EnergyPeriod::Month, EnergyPeriod::Week, EnergyPeriod::Day}) {
    const uint8_t p = static_cast<uint8_t>(period);
    ctx->period_btns[p] = make_button_label(period_row, period_button_label(period), &ctx->period_labels[p]);
    lv_obj_add_event_cb(ctx->period_btns[p], on_period_click, LV_EVENT_CLICKED, ctx);
  }

  lv_obj_t* value_box = lv_obj_create(card);
  lv_obj_remove_style_all(value_box);
  lv_obj_set_size(value_box, LV_PCT(100), popup_layout::kValueHeight);
  lv_obj_align(
      value_box, LV_ALIGN_TOP_MID, 0,
      popup_layout::kValueY - kContentLiftY);
  lv_obj_set_style_bg_opa(value_box, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(value_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(value_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(value_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // The shared header shows the current value from this hidden label. The old
  // value row below is the bar readout: time above, value below, centered.
  ctx->value_label = lv_label_create(card);
  ctx->value_unit_label = nullptr;
  ctx->readout_unit_label = nullptr;
  lv_label_set_text(ctx->value_label, "");
  lv_obj_add_flag(ctx->value_label, LV_OBJ_FLAG_HIDDEN);
  ctx->readout_time_label = popup_graph_readout::create_band_label(
      value_box, popup_layout::font20(), ctx->readout_time_text);
  ctx->readout_value_label = popup_graph_readout::create_band_label(
      value_box, value_font(), ctx->readout_value_text);
  popup_graph_readout::align_band_labels(ctx->readout_time_label, ctx->readout_value_label);

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

  // Every period gets a chart (EnergyChartView). Week, Month and Year are
  // built first and wait hidden in ctx->hidden; show_period_view() swaps.
  ctx->readout.init(ctx, on_energy_readout_apply, on_energy_readout_end);
  for (uint8_t p = 1; p < kPeriodCount; ++p) {
    build_chart_view(ctx, body_box, static_cast<EnergyPeriod>(p));
    lv_obj_add_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    swap_chart_views(ctx, ctx->hidden[p - 1]);
  }
  build_chart_view(ctx, body_box, EnergyPeriod::Day);

  apply_init_to_context(ctx, init);
  lv_obj_move_foreground(icon);
  lv_obj_move_foreground(title);
  lv_obj_move_foreground(period_row);
  lv_obj_move_foreground(close_btn);

  lv_obj_add_event_cb(overlay, on_overlay_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
}

}  // namespace

// The hidden charts are filled from the cache after the content frame, one
// per loop pass. Filled in the same loop pass, one delayed that frame
// (content frame 142-169 ms in b91 against 72-123 ms in b90).
static void queue_hidden_after_frame(EnergyPopupContext* ctx) {
  for (const EnergyChartView& view : ctx->hidden) queue_energy_popup_refresh(period_name(view.period));
  ctx->spare_frame.begin();
}

static void finish_energy_popup_open() {
  if (!g_energy_popup_ctx || !g_energy_open_pending) return;
  g_energy_open_pending = false;
  g_energy_body.restore();
  if (popup_visible(g_energy_popup_ctx)) {
    refresh_from_cache(g_energy_popup_ctx);
    g_energy_popup_ctx->body_ready = true;
    energy_request_period("day", true);
    // Week, Month and Year follow in the background (prefetch_next_period),
    // their answers fill the hidden charts.
    queue_hidden_after_frame(g_energy_popup_ctx);
  }
}

void show_energy_popup(const EnergyPopupInit& init) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  hide_pin_popup();
  hide_camera_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;

  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_media_popup();
  hide_device_popup();
  hide_calendar_popup();

  if (g_energy_popup_ctx && g_energy_popup_ctx->overlay && g_energy_popup_ctx->card) {
    auto* ctx = g_energy_popup_ctx;
    ctx->readout.cancel();
    if (!ctx->body_ready || ctx->entity_id != init.entity_id || ctx->period != EnergyPeriod::Day ||
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
  show_popup_shell(g_energy_popup_ctx->overlay, g_energy_popup_ctx->card, g_energy_popup_ctx->title_label, g_energy_popup_ctx->icon_label, g_energy_popup_ctx->close_button,
                   nullptr, g_energy_popup_ctx->value_label);
}

void preload_energy_popup() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
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
  if (g_energy_popup_ctx) g_energy_popup_ctx->readout.cancel();
  g_energy_open_pending = false;
  g_energy_body.restore();
  if (!g_energy_popup_ctx || !g_energy_popup_ctx->card || !g_energy_popup_ctx->overlay) return;
  hide_popup_shell(g_energy_popup_ctx->card);
  cancel_popup_open(g_energy_popup_ctx->card);
  lv_obj_add_flag(g_energy_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_energy_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void queue_energy_popup_refresh(const char* period) {
  g_pending_refresh.period[static_cast<uint8_t>(period_from_name(period ? period : ""))] = true;
}

void process_energy_popup_queue() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (!g_energy_popup_ctx || !g_energy_popup_ctx->card) {
    g_pending_refresh = PendingPopupRefresh{};
    return;
  }
  if (PopupFirstFrame::any_pending()) return;
  if (g_energy_open_pending) {
    g_energy_open_pending = false;
    g_energy_body.restore();
    if (popup_visible(g_energy_popup_ctx)) {
      refresh_from_cache(g_energy_popup_ctx);
      energy_request_period("day", true);
      queue_hidden_after_frame(g_energy_popup_ctx);  // see finish_energy_popup_open
    }
    return;
  }
  // The other periods, one request after another's answer.
  prefetch_next_period(g_energy_popup_ctx);
  if (!g_pending_refresh.any()) return;
  // Chart data waits while a finger reads a bar, so only the readout changes.
  if (g_energy_popup_ctx->readout.active()) return;
  if (!popup_visible(g_energy_popup_ctx)) {
    g_pending_refresh = PendingPopupRefresh{};
    return;
  }

  // One chart per loop pass: the shown one first, then a hidden one.
  const EnergyPeriod shown = g_energy_popup_ctx->period;
  bool& shown_pending = g_pending_refresh.period[static_cast<uint8_t>(shown)];
  if (!shown_pending) {
    for (uint8_t p = 0; p < kPeriodCount; ++p) {
      if (!g_pending_refresh.period[p]) continue;
      g_pending_refresh.period[p] = false;
      refresh_hidden_from_cache(g_energy_popup_ctx, static_cast<EnergyPeriod>(p));
      return;
    }
    return;
  }
  shown_pending = false;
  // Every period switch asks the Bridge again, and its answer usually repeats
  // the cached bars just drawn; redrawing them took 170-330 ms (P4/S3 b88).
  EnergyEntryData entry;
  if (g_energy_popup_ctx->shown_slots &&
      energy_find_entry(g_energy_popup_ctx->entity_id, period_name(shown), entry) &&
      same_chart_data(g_energy_popup_ctx->shown_entry, entry)) {
    return;
  }
  const uint32_t started_ms = millis();
  refresh_from_cache(g_energy_popup_ctx);
  Serial.printf("[EnergyPopup] New %s data shown in %lu ms\n", period_name(shown),
                static_cast<unsigned long>(millis() - started_ms));
}
