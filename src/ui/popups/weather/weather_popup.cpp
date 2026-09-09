#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/core/json_scan.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/core/display/display_manager.h"
#include "src/core/power/power_manager.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/clock/clock_format.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

namespace {

constexpr int kCardWidth = popup_layout::kCardWidth;
constexpr int kCardPad = popup_layout::kCardPad;
constexpr int kCols = 7;
constexpr int kForecastHoursPerDay = 24;
constexpr int kForecastTempPointCount = (kCols * kForecastHoursPerDay) + 1;
constexpr int kHourlyForecastMax = 168;
#if defined(DEVICE_ESP32_S3_RGB_480)
constexpr int kHourlyParseBatchSize = 12;
constexpr int kHourlyInputObjectLimit = kHourlyForecastMax;
#else
constexpr int kHourlyParseBatchSize = 0;
constexpr int kHourlyInputObjectLimit = 0;
#endif
constexpr int kDetailMarkerCount = 5;
constexpr int kDetailChartPointCount = 25;
constexpr int kModeButtonWidth = popup_layout::scale(96);
constexpr int kModeButtonHeight = popup_layout::scale(54);
constexpr int kModeButtonGap = popup_layout::scale(12);
constexpr int kModeRowOffsetY =
    popup_layout::kValueY + ((popup_layout::kValueHeight - kModeButtonHeight) / 2);
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kSummaryRowTop = popup_layout::kValueY - 24;
constexpr int kForecastRowTop = popup_layout::kBodyY - 16;
constexpr int kForecastRowHeight = popup_layout::kBodyHeight + 48;
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kSummaryRowTop = popup_layout::kValueY - 10;
constexpr int kForecastRowTop = popup_layout::kBodyY;
constexpr int kForecastRowHeight = popup_layout::kBodyHeight;
#else
constexpr int kSummaryRowTop = popup_layout::kValueY;
constexpr int kForecastRowTop = popup_layout::kBodyY;
constexpr int kForecastRowHeight = popup_layout::kBodyHeight;
#endif
constexpr int kDetailHeaderSubrowTop = kForecastRowTop;
constexpr int kForecastSidePad = popup_layout::scale(4);
constexpr int kForecastColGap = popup_layout::scale(4);
constexpr int kForecastDayTop = popup_layout::contentScale(2);
#if defined(DEVICE_LAYOUT_1024X600)
// Keep the compact forecast icons clear of the chart line below them.
constexpr int kForecastDayIconGap = -3;
// day top 2 + compact 16 px font line height 21 - gap 3 = 20
constexpr int kForecastIconTop = 20;
#else
constexpr int kForecastDayIconGap = popup_layout::contentScale(6);
// Keep the same 720x720 relationship when the square layout is scaled.
constexpr int kForecastIconTop = popup_layout::scale480(35);
#endif
constexpr int kForecastTempChartTop = popup_layout::contentScale(102);
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kForecastTempChartHeight =
    popup_layout::contentScale(142) + 32;
constexpr int kForecastPrecipChartTop =
    popup_layout::contentScale(288) + 32;
constexpr int kForecastAmountTop =
    popup_layout::contentScale(323) + 28;
constexpr int kForecastProbabilityTop =
    popup_layout::contentScale(355) + 28;
#else
constexpr int kForecastTempChartHeight = popup_layout::contentScale(142);
constexpr int kForecastPrecipChartTop = popup_layout::contentScale(288);
constexpr int kForecastAmountTop = popup_layout::contentScale(323);
constexpr int kForecastProbabilityTop = popup_layout::contentScale(355);
#endif
constexpr int kForecastHighLabelGap = popup_layout::contentScale(6);
constexpr int kForecastLowLabelGap = popup_layout::contentScale(8);
constexpr int kForecastPrecipChartHeight = popup_layout::contentScale(12);
constexpr int kForecastColumnTouchHeight =
    kForecastProbabilityTop + popup_layout::contentScale(44);
constexpr int kForecastBarWidth = popup_layout::scale(10);
constexpr int kForecastBarSideInset = popup_layout::scale(5);
constexpr int kForecastChartSideInset = popup_layout::scale(8);
constexpr int kForecastPlotTotalW = kCardWidth - (kCardPad * 2);
constexpr int kForecastAlignedColW =
    (kForecastPlotTotalW - (kForecastChartSideInset * 2) - (kForecastColGap * (kCols - 1))) / kCols;
constexpr int kForecastPlotColW =
    (kForecastPlotTotalW - (kForecastSidePad * 2) - (kForecastColGap * (kCols - 1))) / kCols;
constexpr int kForecastFirstCenter = kForecastSidePad + (kForecastPlotColW / 2);
constexpr int kForecastLastCenter =
    kForecastSidePad + ((kCols - 1) * (kForecastPlotColW + kForecastColGap)) + (kForecastPlotColW / 2);
constexpr int kDetailRowTop = kForecastRowTop;
constexpr int kDetailTitleTop = kDetailHeaderSubrowTop + popup_layout::scale(6);
constexpr int kFooterButtonHeight = popup_layout::kNavHeight;
constexpr int kFooterActionButtonWidth = popup_layout::scale(92);
constexpr int kFooterButtonRadius = kFooterButtonHeight / 2;
constexpr int kFooterButtonGap = popup_layout::scale(10);
constexpr int kFooterInsetX = popup_layout::scale(6);
constexpr int kFooterOffsetY = -popup_layout::scale(6);
constexpr lv_opa_t kFooterIndicatorOpa = LV_OPA_20;
constexpr int kDetailNavButtonSize = kFooterButtonHeight;
constexpr int kFooterNextButtonX = -kFooterInsetX;
constexpr int kFooterPrevButtonX = -(kFooterInsetX + kDetailNavButtonSize + kFooterButtonGap);
constexpr int kFooterWeekButtonX =
    -(kFooterInsetX + (kDetailNavButtonSize * 2) + (kFooterButtonGap * 2));
constexpr int kFooterTodayButtonX =
    -(kFooterInsetX + (kDetailNavButtonSize * 2) + (kFooterButtonGap * 3) +
      kFooterActionButtonWidth);
constexpr int kFooterContentWidth = kCardWidth - (kCardPad * 2);
constexpr int kFooterDatePillWidth =
    kFooterContentWidth + kFooterTodayButtonX - kFooterActionButtonWidth -
    kFooterButtonGap - kFooterInsetX;
constexpr float kDetailNowCollisionHours = 3.0f;
constexpr int kDetailNowGuideWidth = 1;
constexpr int kDetailNowGuideOpa = LV_OPA_30;
constexpr int kDetailChartWrapTop = 0;
constexpr int kDetailLabelOverhang = popup_layout::scale(12);
constexpr int kDetailYAxisWidth = popup_layout::scale(18);
constexpr int kDetailTempChartHeight =
    kForecastTempChartHeight - popup_layout::scale(20);
constexpr int kDetailPrecipChartHeight =
    kForecastPrecipChartHeight + popup_layout::scale(20);
constexpr int kDetailChartGap =
    kForecastPrecipChartTop - kForecastTempChartTop - kForecastTempChartHeight;
constexpr int kDetailProbabilityGap = popup_layout::scale(4);
constexpr int kDetailProbabilityHeight = popup_layout::scale(22);
constexpr int kDetailTempValueGap = popup_layout::scale(28);
constexpr int kDetailTempMarkerSize = popup_layout::scale(12);
constexpr int kDetailCurrentHourDotSize = popup_layout::scale(18);
constexpr int kDetailPrecipBarWidth = popup_layout::scale(12);
constexpr int kDetailSectionGutter = 0;
constexpr int kDetailRightInsetAdjust = 0;
constexpr int kDetailChartColumnInset = kForecastChartSideInset + (kForecastAlignedColW / 2);
constexpr int kDetailChartLeftInset = kDetailChartColumnInset;
constexpr int kDetailChartRightInset = kDetailChartColumnInset;
constexpr int kDetailTimeLabelY = kForecastDayTop;
constexpr int kDetailIconY = kForecastIconTop;
constexpr int kDetailPastOverlayRadius = popup_layout::scale(16);
constexpr int kDetailDisabledBlockGap = popup_layout::scale(5);
constexpr int kDetailDisabledLineGap = popup_layout::scale(10);
constexpr int kDetailTempChartTop = kForecastTempChartTop;
constexpr int kDetailPrecipChartTop =
    kForecastPrecipChartTop - popup_layout::scale(20);
constexpr int kDetailAmountTop = kForecastAmountTop;
constexpr int kDetailProbabilityTop = kForecastProbabilityTop;
constexpr int kDetailDisabledIconBlockTop =
    kDetailIconY + popup_layout::scale(4);
constexpr int kDetailDisabledTempBlockTop =
    kDetailTempChartTop - popup_layout::scale(6);
constexpr int kDetailDisabledIconBlockHeight =
    kDetailDisabledTempBlockTop - kDetailDisabledBlockGap - kDetailDisabledIconBlockTop;
constexpr int kDetailDisabledPrecipBlockTop =
    kDetailPrecipChartTop - popup_layout::scale(6);
constexpr int kDetailDisabledTempBlockHeight =
    kDetailDisabledPrecipBlockTop - kDetailDisabledBlockGap - kDetailDisabledTempBlockTop;
constexpr int kDetailDisabledPrecipBlockHeight =
    (kDetailProbabilityTop + kDetailProbabilityHeight + popup_layout::scale(4)) -
    kDetailDisabledPrecipBlockTop;
constexpr int kDetailDisabledOverlayTop = kDetailDisabledIconBlockTop;
constexpr int kDetailDisabledOverlayHeight =
    (kDetailProbabilityTop + kDetailProbabilityHeight + popup_layout::scale(4)) -
    kDetailDisabledOverlayTop;
constexpr int kDetailDisabledSeparator1Top = kDetailDisabledIconBlockHeight;
constexpr int kDetailDisabledSeparator2Top =
    (kDetailDisabledTempBlockTop + kDetailDisabledTempBlockHeight) - kDetailDisabledOverlayTop;
constexpr int kDetailDisabledLineGapDelta = kDetailDisabledLineGap - kDetailDisabledBlockGap;
constexpr int kDetailDisabledLineInsetTop = (kDetailDisabledLineGapDelta + 1) / 2;
constexpr int kDetailDisabledLineInsetBottom = kDetailDisabledLineGapDelta / 2;
constexpr int kDetailTempGuideTop = kDetailDisabledTempBlockTop + kDetailDisabledLineInsetTop;
constexpr int kDetailTempGuideHeight =
    kDetailDisabledTempBlockHeight - kDetailDisabledLineGapDelta;
constexpr int kDetailPrecipGuideTop = kDetailPrecipChartTop + kDetailDisabledLineInsetTop;
constexpr int kDetailPrecipGuideHeight =
    kDetailPrecipChartHeight - kDetailDisabledLineInsetTop;
constexpr int kDetailChartWrapHeight =
    kDetailProbabilityTop + kDetailProbabilityHeight + kDetailLabelOverhang;
constexpr int kHeaderPadTop = popup_layout::scale(4);

const lv_font_t* weather_unit_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return LV_FONT_DEFAULT;
#else
  return FONT_SMALL;
#endif
}

struct ForecastWidgets {
  lv_obj_t* column = nullptr;
  lv_obj_t* day_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* high_label = nullptr;
  lv_obj_t* high_unit_label = nullptr;
  lv_obj_t* low_label = nullptr;
  lv_obj_t* low_unit_label = nullptr;
  lv_obj_t* precip_bar = nullptr;
  lv_obj_t* precip_amount_label = nullptr;
  lv_obj_t* precip_amount_unit_label = nullptr;
  lv_obj_t* precip_probability_label = nullptr;
  lv_obj_t* precip_probability_unit_label = nullptr;
};

struct ForecastData {
  bool active = false;
  String day;
  String date_local;
  String icon;
  bool has_high = false;
  float high = 0.0f;
  bool has_low = false;
  float low = 0.0f;
  bool has_precipitation = false;
  float precipitation = 0.0f;
  bool has_precipitation_probability = false;
  float precipitation_probability = 0.0f;
};

struct HourlyForecastData {
  bool active = false;
  String date_local;
  int hour_local = -1;
  String icon;
  bool has_temp = false;
  float temp = 0.0f;
  bool has_precipitation = false;
  float precipitation = 0.0f;
  bool has_precipitation_probability = false;
  float precipitation_probability = 0.0f;
};

enum class WeatherPopupViewMode {
  Week,
  Day,
};

struct WeatherPopupContext {
  String entity_id;
  String rendered_entity_id;
  String title;
  uint32_t bg_color = 0;
  uint32_t rendered_payload_hash = 0;
  size_t rendered_payload_length = 0;
  bool has_rendered_data = false;
  String rendered_language;
  String unit;
  String precipitation_unit = "mm";
  bool current_has_temp = false;
  float current_temp = 0.0f;
  String current_icon;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* location_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* header_today_btn = nullptr;
  lv_obj_t* header_week_btn = nullptr;
  lv_obj_t* value_row = nullptr;
  lv_obj_t* condition_label = nullptr;
  lv_obj_t* condition_sep_label = nullptr;
  lv_obj_t* temp_label = nullptr;
  lv_obj_t* week_range_pill = nullptr;
  lv_obj_t* week_range_label = nullptr;
  lv_obj_t* mode_row = nullptr;
  lv_obj_t* mode_week_btn = nullptr;
  lv_obj_t* mode_day_btn = nullptr;
  lv_obj_t* forecast_row = nullptr;
  lv_obj_t* forecast_temp_chart = nullptr;
  lv_chart_series_t* forecast_temp_series = nullptr;
  lv_obj_t* forecast_precip_base = nullptr;
  lv_obj_t* forecast_dividers[kCols - 1] = {};
  lv_obj_t* forecast_time_lines[kCols] = {};
  lv_obj_t* forecast_y_max_line = nullptr;
  lv_obj_t* forecast_y_min_line = nullptr;
  int forecast_temp_min = 0;
  int forecast_temp_max = 100;
  lv_obj_t* detail_wrap = nullptr;
  lv_obj_t* detail_title_pill = nullptr;
  lv_obj_t* detail_title_label = nullptr;
  lv_obj_t* detail_prev_btn = nullptr;
  lv_obj_t* detail_next_btn = nullptr;
  lv_obj_t* detail_past_overlay = nullptr;
  lv_obj_t* detail_end_overlay = nullptr;
  lv_obj_t* detail_disabled_separators[4] = {};
  int detail_disabled_separator_count = 0;
  lv_obj_t* detail_y_max_label = nullptr;
  lv_obj_t* detail_y_min_label = nullptr;
  lv_obj_t* detail_y_max_line = nullptr;
  lv_obj_t* detail_y_min_line = nullptr;
  lv_obj_t* detail_icon_guides[kDetailMarkerCount] = {};
  lv_obj_t* detail_time_lines[kDetailMarkerCount] = {};
  lv_obj_t* detail_time_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_icon_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_temp_point_dots[kDetailMarkerCount] = {};
  lv_obj_t* detail_current_dot = nullptr;
  lv_obj_t* detail_now_time_line = nullptr;
  lv_obj_t* detail_now_precip_guide = nullptr;
  lv_obj_t* detail_now_time_label = nullptr;
  lv_obj_t* detail_now_icon_label = nullptr;
  lv_obj_t* detail_now_temp_value_label = nullptr;
  lv_obj_t* detail_now_temp_unit_label = nullptr;
  lv_obj_t* detail_now_precip_amount_label = nullptr;
  lv_obj_t* detail_now_precip_amount_unit_label = nullptr;
  lv_obj_t* detail_now_probability_label = nullptr;
  lv_obj_t* detail_now_probability_unit_label = nullptr;
  lv_obj_t* detail_temp_value_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_temp_unit_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_precip_amount_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_precip_amount_unit_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_probability_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_probability_unit_labels[kDetailMarkerCount] = {};
  lv_obj_t* detail_precip_guides[kDetailMarkerCount] = {};
  lv_obj_t* detail_precip_bars[kDetailChartPointCount] = {};
  lv_obj_t* detail_temp_chart = nullptr;
  lv_chart_series_t* detail_temp_series = nullptr;
  lv_obj_t* detail_precip_chart = nullptr;
  lv_chart_series_t* detail_precip_series = nullptr;
  int detail_temp_min = 0;
  int detail_temp_max = 100;
  WeatherPopupViewMode view_mode = WeatherPopupViewMode::Week;
  int selected_day_index = -1;
  lv_timer_t* detail_title_timer = nullptr;
  int detail_title_today_minute = -1;
  ForecastWidgets forecast[kCols];
  ForecastData forecast_data[kCols];
  HourlyForecastData hourly[kHourlyForecastMax];
};

struct PendingWeatherUpdate {
  String entity_id;
  String payload;
  uint32_t payload_hash = 0;
  size_t payload_length = 0;
  bool valid = false;
  bool build_ui_pending = false;
  String build_entity_id;
  uint32_t build_payload_hash = 0;
  size_t build_payload_length = 0;
  int hourly_cursor = 0;
  int hourly_objects_seen = 0;
  int hourly_count = 0;
  bool parse_hourly_pending = false;
  uint32_t parse_started_ms = 0;
  String previous_selected_date;
  WeatherPopupViewMode previous_mode = WeatherPopupViewMode::Week;
  bool previous_view_captured = false;
  int pending_day_nav = -1;
};

static WeatherPopupContext* g_weather_popup_ctx = nullptr;
static PopupBody g_weather_body;
static WeatherPopupInit g_pending_weather_init;
static bool g_weather_open_pending = false;
static PendingWeatherUpdate g_pending_weather;

static int find_active_day_index(const WeatherPopupContext* ctx, const String& date_local);
static int find_prev_active_day_index(const WeatherPopupContext* ctx, int from_index);
static int find_next_active_day_index(const WeatherPopupContext* ctx, int from_index);
static const char* weather_today_button_text();
static bool get_local_now_parts(String& date_out, int& hour_out, int* minute_out = nullptr);
static String iso_date_add_days(const String& iso, int day_offset);
static int iso_date_day_offset(const String& base_iso, const String& target_iso);
static String weekday_from_iso(const String& iso);

static void cancel_weather_refresh_work() {
  g_pending_weather.parse_hourly_pending = false;
  g_pending_weather.hourly_cursor = 0;
  g_pending_weather.hourly_objects_seen = 0;
  g_pending_weather.hourly_count = 0;
  g_pending_weather.parse_started_ms = 0;
  g_pending_weather.build_ui_pending = false;
  g_pending_weather.build_entity_id.remove(0);
  g_pending_weather.build_payload_hash = 0;
  g_pending_weather.build_payload_length = 0;
}

static void reset_pending_weather_update() {
  cancel_weather_refresh_work();
  g_pending_weather.valid = false;
  g_pending_weather.entity_id.remove(0);
  g_pending_weather.payload.remove(0);
  g_pending_weather.payload_hash = 0;
  g_pending_weather.payload_length = 0;
  g_pending_weather.previous_selected_date.remove(0);
  g_pending_weather.previous_mode = WeatherPopupViewMode::Week;
  g_pending_weather.previous_view_captured = false;
  g_pending_weather.pending_day_nav = -1;
}

static bool weather_refresh_in_progress(const WeatherPopupContext* ctx) {
  if (g_weather_open_pending) return true;
  if (!ctx) return false;
  const bool queued_for_entity =
      g_pending_weather.valid &&
      g_pending_weather.entity_id.equalsIgnoreCase(ctx->entity_id);
  const bool parsing_for_entity =
      g_pending_weather.parse_hourly_pending &&
      g_pending_weather.build_entity_id.equalsIgnoreCase(ctx->entity_id);
  const bool building_for_entity =
      g_pending_weather.build_ui_pending &&
      g_pending_weather.build_entity_id.equalsIgnoreCase(ctx->entity_id);
  return queued_for_entity || parsing_for_entity || building_for_entity;
}

static bool is_popup_visible(WeatherPopupContext* ctx) {
  if (!ctx || !ctx->card) return false;
  return !lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
}

static uint32_t hash_weather_payload(const char* payload, size_t* length_out = nullptr) {
  uint32_t hash = 2166136261u;
  size_t length = 0;
  if (payload) {
    while (payload[length] != '\0') {
      hash ^= static_cast<uint8_t>(payload[length]);
      hash *= 16777619u;
      ++length;
    }
  }
  if (length_out) *length_out = length;
  return hash;
}

static void decode_basic_json_escapes(String& text) {
  if (!text.length()) return;
  text.replace("\\u00b0", "\xC2\xB0");
  text.replace("\\u00B0", "\xC2\xB0");
  text.replace("\\/", "/");
  text.replace("\\\"", "\"");
  text.replace("\\\\", "\\");
}

static bool extract_json_string_field(const String& src, const char* key, String& out) {
  int begin = 0;
  int end = 0;
  if (!hometiles_json::stringSpan(src.c_str(), src.length(), key, &begin, &end)) {
    return false;
  }
  out = src.substring(begin, end);
  return true;
}

static int find_json_array_start(const String& src, const char* key) {
  return hometiles_json::arrayStart(src.c_str(), src.length(), key);
}

static bool extract_json_object_field(const String& src, const char* key, String& out) {
  int begin = 0;
  int end = 0;
  if (!hometiles_json::objectSpan(src.c_str(), src.length(), key, &begin, &end)) {
    return false;
  }
  out = src.substring(begin, end);
  return true;
}

static bool extract_json_number_field(const String& src, const char* key, float& out) {
  return hometiles_json::number(src.c_str(), src.length(), key, &out);
}

static bool extract_json_number_or_string_field(const String& src, const char* key, float& out) {
  if (extract_json_number_field(src, key, out)) return true;
  String text;
  if (!extract_json_string_field(src, key, text)) return false;
  text.trim();
  if (!text.length()) return false;
  text.replace(",", ".");
  char* end = nullptr;
  float val = strtof(text.c_str(), &end);
  if (!end || end == text.c_str()) return false;
  out = val;
  return true;
}

static String format_weather_temp(float temp, const String& unit) {
  String text = i18n::format_number(
      configManager.getConfig().language, temp, 1, true);
  if (unit.length()) {
    text += " ";
    text += unit;
  }
  return text;
}

static String format_precipitation_amount(float amount, const String& unit) {
  String text = i18n::format_number(
      configManager.getConfig().language, amount, 1, true);
  text += " ";
  text += unit.length() ? unit : "mm";
  return text;
}

static String format_precipitation_amount_value(float amount) {
  return i18n::format_number(
      configManager.getConfig().language, amount, 1, true);
}

static const char* weather_unit_gap() {
#if defined(DEVICE_LAYOUT_1024X600)
  return " ";
#else
  return "\xE2\x80\x89";
#endif
}

static String format_precipitation_amount_unit(const String& unit) {
  return String(weather_unit_gap()) + (unit.length() ? unit : "mm");
}

static String format_precipitation_probability(float probability) {
  return String(static_cast<int>(lroundf(probability))) + " %";
}

static String format_precipitation_probability_value(float probability) {
  return String(static_cast<int>(lroundf(probability)));
}

static String format_temperature_unit_label(const String& unit) {
  String text = unit;
  text.trim();
  if (!text.length()) text = "\xC2\xB0\x43";
  return String(weather_unit_gap()) + text;
}

static void position_value_unit_centered(
    lv_obj_t* val_label, lv_obj_t* unit_label,
    lv_coord_t center_x, lv_coord_t y,
    lv_coord_t wrap_w) {
  lv_obj_update_layout(val_label);
  lv_obj_update_layout(unit_label);
  lv_coord_t val_w = lv_obj_get_width(val_label);
  lv_coord_t unit_w = lv_obj_get_width(unit_label);
  lv_coord_t total_w = val_w + unit_w;
  lv_coord_t x = center_x - (total_w / 2);
  if (x < 0) x = 0;
  if (x + total_w > wrap_w) x = wrap_w - total_w;
  lv_obj_set_pos(val_label, x, y);
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
  const lv_coord_t unit_y_offset = 0;
#else
  const lv_coord_t unit_y_offset = 3;
#endif
  lv_obj_set_pos(unit_label, x + val_w, y + unit_y_offset);
}

static constexpr lv_coord_t weather_detail_unit_y_offset() {
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
  return 0;
#else
  return 3;
#endif
}

static String format_forecast_chart_temp(float temp) {
  String text = i18n::format_number(
      configManager.getConfig().language, temp, 1, true);
  text += "\xC2\xB0";
  return text;
}

static String format_forecast_chart_temp_value(float temp) {
  return i18n::format_number(
      configManager.getConfig().language, temp, 1, true);
}

static int scale_forecast_temp(float temp) {
  return static_cast<int>(lroundf(temp * 10.0f));
}

static lv_coord_t map_forecast_chart_y(const WeatherPopupContext* ctx, int scaled_temp) {
  if (!ctx) return kForecastTempChartTop + (kForecastTempChartHeight / 2);
  int min_temp = ctx->forecast_temp_min;
  int max_temp = ctx->forecast_temp_max;
  if (max_temp <= min_temp) return kForecastTempChartTop + (kForecastTempChartHeight / 2);
  const lv_coord_t pad = 4;
  const lv_coord_t draw_h = kForecastTempChartHeight - pad * 2;
  const float ratio = static_cast<float>(max_temp - scaled_temp) /
                      static_cast<float>(max_temp - min_temp);
  return kForecastTempChartTop + pad + static_cast<lv_coord_t>(lroundf(ratio * draw_h));
}

static lv_coord_t map_forecast_chart_x(const WeatherPopupContext* ctx, int point_id) {
  if (!ctx || !ctx->forecast_temp_chart || !ctx->forecast_temp_series) return 0;
  const int clamped_point =
      (point_id < 0) ? 0 : ((point_id >= kForecastTempPointCount) ? (kForecastTempPointCount - 1) : point_id);
  lv_point_t point;
  lv_chart_get_point_pos_by_id(ctx->forecast_temp_chart, ctx->forecast_temp_series, clamped_point, &point);
  return lv_obj_get_x(ctx->forecast_temp_chart) + point.x;
}

static lv_coord_t map_detail_chart_y(const WeatherPopupContext* ctx, int scaled_temp) {
  if (!ctx) return kDetailTempChartTop + (kDetailTempChartHeight / 2);
  int min_temp = ctx->detail_temp_min;
  int max_temp = ctx->detail_temp_max;
  if (max_temp <= min_temp) return kDetailTempChartTop + (kDetailTempChartHeight / 2);
  const lv_coord_t pad_top = 18;
  const lv_coord_t pad_bottom = 3;
  const lv_coord_t draw_h = kDetailTempChartHeight - pad_top - pad_bottom;
  const float ratio = static_cast<float>(max_temp - scaled_temp) /
                      static_cast<float>(max_temp - min_temp);
  return kDetailTempChartTop + pad_top + static_cast<lv_coord_t>(lroundf(ratio * draw_h));
}

static lv_coord_t map_detail_chart_x(const WeatherPopupContext* ctx, int point_id) {
  if (!ctx || !ctx->detail_temp_chart || !ctx->detail_temp_series) return 0;
  const int clamped_point =
      (point_id < 0) ? 0 : ((point_id >= kDetailChartPointCount) ? (kDetailChartPointCount - 1) : point_id);
  lv_point_t point;
  lv_chart_get_point_pos_by_id(ctx->detail_temp_chart, ctx->detail_temp_series, clamped_point, &point);
  return lv_obj_get_x(ctx->detail_temp_chart) + point.x;
}

static lv_coord_t map_detail_chart_xf(const WeatherPopupContext* ctx, float point_pos) {
  if (!ctx) return 0;
  float clamped = point_pos;
  if (clamped < 0.0f) clamped = 0.0f;
  if (clamped > static_cast<float>(kDetailChartPointCount - 1)) {
    clamped = static_cast<float>(kDetailChartPointCount - 1);
  }
  const int left = static_cast<int>(floorf(clamped));
  const int right = static_cast<int>(ceilf(clamped));
  const lv_coord_t left_x = map_detail_chart_x(ctx, left);
  if (left == right) return left_x;
  const lv_coord_t right_x = map_detail_chart_x(ctx, right);
  const float ratio = clamped - static_cast<float>(left);
  return left_x + static_cast<lv_coord_t>(lroundf((right_x - left_x) * ratio));
}

static void clear_forecast_chart(WeatherPopupContext* ctx) {
  if (!ctx || !ctx->forecast_temp_chart || !ctx->forecast_temp_series) return;
  lv_chart_set_point_count(ctx->forecast_temp_chart, kForecastTempPointCount);
  lv_chart_set_all_value(ctx->forecast_temp_chart, ctx->forecast_temp_series, LV_CHART_POINT_NONE);
  lv_chart_set_range(ctx->forecast_temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_refresh(ctx->forecast_temp_chart);
}

static void update_forecast_graph(WeatherPopupContext* ctx) {
  if (!ctx) return;

  String today_date;
  int today_hour = 0;
  const bool has_today = get_local_now_parts(today_date, today_hour);
  const lv_color_t popup_bg_color =
      ctx->card ? lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN) : lv_color_hex(ctx->bg_color);
  const lv_color_t inactive_day_color = lv_color_mix(lv_color_white(), popup_bg_color, 96);

  bool has_temp = false;
  int min_temp = 0;
  int max_temp = 0;
  bool has_precipitation = false;
  float max_precipitation = 0.0f;

  bool day_has_low_anchor[kCols] = {};
  bool day_has_high_anchor[kCols] = {};
  float day_low_label_temp[kCols] = {};
  float day_high_label_temp[kCols] = {};

  if (ctx->forecast_temp_chart && ctx->forecast_temp_series) {
    lv_chart_set_point_count(ctx->forecast_temp_chart, kForecastTempPointCount);
    lv_chart_set_all_value(ctx->forecast_temp_chart, ctx->forecast_temp_series, LV_CHART_POINT_NONE);
  }

  for (int i = 0; i < kCols; ++i) {
    const ForecastData& data = ctx->forecast_data[i];
    if (data.has_precipitation) {
      if (!has_precipitation || data.precipitation > max_precipitation) {
        max_precipitation = data.precipitation;
      }
      has_precipitation = true;
    }

    if (data.has_low) day_low_label_temp[i] = data.low;
    if (data.has_high) day_high_label_temp[i] = data.high;
  }

  for (int i = 0; i < kHourlyForecastMax; ++i) {
    const HourlyForecastData& hour = ctx->hourly[i];
    if (!hour.active || !hour.has_temp) continue;
    if (hour.hour_local < 0 || hour.hour_local >= kForecastHoursPerDay) continue;

    const int day_index = find_active_day_index(ctx, hour.date_local);
    if (day_index < 0 || day_index >= kCols) continue;

    const int point_id = (day_index * kForecastHoursPerDay) + hour.hour_local;
    const int scaled = scale_forecast_temp(hour.temp);

    if (ctx->forecast_temp_chart && ctx->forecast_temp_series) {
      lv_chart_set_value_by_id(ctx->forecast_temp_chart,
                               ctx->forecast_temp_series,
                               point_id,
                               scaled);
    }

    if (!has_temp) {
      min_temp = scaled;
      max_temp = scaled;
      has_temp = true;
    } else {
      if (scaled < min_temp) min_temp = scaled;
      if (scaled > max_temp) max_temp = scaled;
    }

    // Partial hourly coverage must not replace the provider's full-day extrema.
    if (!ctx->forecast_data[day_index].has_low &&
        (!day_has_low_anchor[day_index] || hour.temp < day_low_label_temp[day_index])) {
      day_has_low_anchor[day_index] = true;
      day_low_label_temp[day_index] = hour.temp;
    }

    if (!ctx->forecast_data[day_index].has_high &&
        (!day_has_high_anchor[day_index] || hour.temp > day_high_label_temp[day_index])) {
      day_has_high_anchor[day_index] = true;
      day_high_label_temp[day_index] = hour.temp;
    }
  }

  if (has_temp) {
    int base_padding = (max_temp - min_temp) / 4;
    if (base_padding < 8) base_padding = 8;
    int top_padding = base_padding + 52;
    int bottom_padding = base_padding + 52;
    if (max_temp == min_temp) {
      top_padding = 58;
      bottom_padding = 58;
    }
    ctx->forecast_temp_min = min_temp - bottom_padding;
    ctx->forecast_temp_max = max_temp + top_padding;
  } else {
    ctx->forecast_temp_min = 0;
    ctx->forecast_temp_max = 100;
  }

  if (ctx->forecast_temp_chart) {
    lv_chart_set_range(ctx->forecast_temp_chart,
                       LV_CHART_AXIS_PRIMARY_Y,
                       ctx->forecast_temp_min,
                       ctx->forecast_temp_max);
    lv_chart_refresh(ctx->forecast_temp_chart);
    lv_obj_update_layout(ctx->forecast_temp_chart);
  }

  lv_obj_t* forecast_row = ctx->forecast_temp_chart ? lv_obj_get_parent(ctx->forecast_temp_chart) : nullptr;
  if (!forecast_row) return;
  lv_obj_update_layout(forecast_row);

  bool has_day_slot = false;
  int first_day_slot = -1;
  int last_day_slot = -1;
  for (int i = 0; i < kCols; ++i) {
    if (ctx->forecast_data[i].date_local.length()) {
      has_day_slot = true;
      if (first_day_slot < 0) first_day_slot = i;
      last_day_slot = i;
    }
  }

  const lv_coord_t separator_left =
      (has_day_slot && first_day_slot >= 0)
          ? map_forecast_chart_x(ctx, first_day_slot * kForecastHoursPerDay)
          : 0;
  const lv_coord_t separator_right =
      (has_day_slot && last_day_slot >= 0)
          ? map_forecast_chart_x(ctx, (last_day_slot + 1) * kForecastHoursPerDay)
          : separator_left;
  const lv_coord_t separator_width =
      (separator_right >= separator_left) ? (separator_right - separator_left + 1) : 0;
  const lv_coord_t top_separator_y =
      kDetailDisabledOverlayTop + kDetailDisabledSeparator1Top + (kDetailDisabledBlockGap / 2);
  const lv_coord_t temp_guide_bottom = kDetailTempGuideTop + kDetailTempGuideHeight;
  const lv_coord_t bottom_separator_y =
      temp_guide_bottom + ((kDetailPrecipGuideTop - temp_guide_bottom) / 2);
  const lv_coord_t guide_top_overhang = 6;
  const lv_coord_t guide_bottom_overhang = 8;
  const lv_coord_t full_guide_top = top_separator_y - guide_top_overhang;
  lv_coord_t full_guide_bottom =
      kDetailPrecipGuideTop + kDetailPrecipGuideHeight + guide_bottom_overhang;
  if (full_guide_bottom > lv_obj_get_height(forecast_row)) {
    full_guide_bottom = lv_obj_get_height(forecast_row);
  }
  const lv_coord_t full_guide_height = full_guide_bottom - full_guide_top;

  if (ctx->forecast_y_max_line) {
    if (has_day_slot && separator_width > 0) {
      lv_obj_set_pos(ctx->forecast_y_max_line, separator_left, top_separator_y);
      lv_obj_set_size(ctx->forecast_y_max_line, separator_width, 1);
      lv_obj_clear_flag(ctx->forecast_y_max_line, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->forecast_y_max_line, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (ctx->forecast_y_min_line) {
    if (has_day_slot && separator_width > 0) {
      lv_obj_set_pos(ctx->forecast_y_min_line, separator_left, bottom_separator_y);
      lv_obj_set_size(ctx->forecast_y_min_line, separator_width, 1);
      lv_obj_clear_flag(ctx->forecast_y_min_line, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->forecast_y_min_line, LV_OBJ_FLAG_HIDDEN);
    }
  }

  for (int i = 0; i < kCols; ++i) {
    if (!ctx->forecast_time_lines[i]) continue;
    if (i > 0 && ctx->forecast_data[i].date_local.length()) {
      const lv_coord_t x = map_forecast_chart_x(ctx, i * kForecastHoursPerDay);
      lv_obj_set_pos(ctx->forecast_time_lines[i], x, full_guide_top);
      lv_obj_set_size(ctx->forecast_time_lines[i], 1, full_guide_height);
      lv_obj_clear_flag(ctx->forecast_time_lines[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->forecast_time_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  for (int i = 0; i < kCols; ++i) {
    ForecastWidgets& fw = ctx->forecast[i];
    const ForecastData& data = ctx->forecast_data[i];
    if (!fw.column) continue;

    const lv_coord_t col_w = lv_obj_get_width(fw.column);
    const lv_coord_t center_x = col_w / 2;
    const bool slot_has_date = data.date_local.length();

    if (fw.day_label) {
      if (slot_has_date) {
        String text;
        if (has_today && data.date_local == today_date) {
          text = weather_today_button_text();
        } else {
          text = weekday_from_iso(data.date_local);
          if (!text.length()) text = data.day;
        }
        lv_label_set_text(fw.day_label, text.c_str());
        lv_obj_set_style_text_color(fw.day_label,
                                    data.active ? lv_color_white() : inactive_day_color,
                                    0);
        lv_obj_clear_flag(fw.day_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(fw.day_label);
      } else {
        lv_label_set_text(fw.day_label, "");
        lv_obj_add_flag(fw.day_label, LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (fw.icon_label) {
      if (data.active && data.icon.length()) {
        String icon_char = getMdiChar(data.icon);
        if (icon_char.length()) {
          lv_label_set_text(fw.icon_label, icon_char.c_str());
          lv_obj_clear_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
          if (fw.day_label && !lv_obj_has_flag(fw.day_label, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_align_to(fw.icon_label, fw.day_label, LV_ALIGN_OUT_BOTTOM_MID, 0, kForecastDayIconGap);
          } else {
            lv_obj_align(fw.icon_label, LV_ALIGN_TOP_MID, 0, kForecastIconTop);
          }
        } else {
          lv_label_set_text(fw.icon_label, "");
          lv_obj_add_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
        }
      } else {
        lv_label_set_text(fw.icon_label, "");
        lv_obj_add_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (fw.high_label) {
      lv_obj_t* hu = fw.high_unit_label;
      if (data.active && (data.has_high || day_has_high_anchor[i])) {
        lv_label_set_text(fw.high_label, format_forecast_chart_temp_value(day_high_label_temp[i]).c_str());
        if (hu) {
          lv_label_set_text(hu, format_temperature_unit_label(ctx->unit).c_str());
          lv_obj_update_layout(fw.high_label);
          lv_coord_t y =
              top_separator_y + popup_layout::scale480(14);
          position_value_unit_centered(fw.high_label, hu, center_x, y, col_w);
          lv_obj_clear_flag(hu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(fw.high_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(fw.high_label, "");
        lv_obj_add_flag(fw.high_label, LV_OBJ_FLAG_HIDDEN);
        if (hu) {
          lv_label_set_text(hu, "");
          lv_obj_add_flag(hu, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }

    if (fw.low_label) {
      lv_obj_t* lu = fw.low_unit_label;
      if (data.active && (data.has_low || day_has_low_anchor[i])) {
        lv_label_set_text(fw.low_label, format_forecast_chart_temp_value(day_low_label_temp[i]).c_str());
        if (lu) {
          lv_label_set_text(lu, format_temperature_unit_label(ctx->unit).c_str());
          lv_obj_update_layout(fw.low_label);
#if defined(DEVICE_LAYOUT_1024X600)
          lv_coord_t y = bottom_separator_y - lv_obj_get_height(fw.low_label) - 8;
#else
          lv_coord_t y = bottom_separator_y -
                         lv_obj_get_height(fw.low_label) -
                         popup_layout::scale480(16);
#endif
          position_value_unit_centered(fw.low_label, lu, center_x, y, col_w);
          lv_obj_clear_flag(lu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(fw.low_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(fw.low_label, "");
        lv_obj_add_flag(fw.low_label, LV_OBJ_FLAG_HIDDEN);
        if (lu) {
          lv_label_set_text(lu, "");
          lv_obj_add_flag(lu, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }

    if (fw.precip_amount_label) {
      lv_obj_t* au = fw.precip_amount_unit_label;
      if (data.active && data.has_precipitation) {
        lv_label_set_text(fw.precip_amount_label,
                          format_precipitation_amount_value(data.precipitation).c_str());
        if (au) {
          lv_label_set_text(au, format_precipitation_amount_unit(ctx->precipitation_unit).c_str());
          position_value_unit_centered(fw.precip_amount_label, au, center_x, kForecastAmountTop, col_w);
          lv_obj_clear_flag(au, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(fw.precip_amount_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(fw.precip_amount_label, "");
        lv_obj_add_flag(fw.precip_amount_label, LV_OBJ_FLAG_HIDDEN);
        if (au) {
          lv_label_set_text(au, "");
          lv_obj_add_flag(au, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }

    if (fw.precip_probability_label) {
      lv_obj_t* pu = fw.precip_probability_unit_label;
      if (data.active && data.has_precipitation_probability) {
        lv_label_set_text(fw.precip_probability_label,
                          format_precipitation_probability_value(data.precipitation_probability).c_str());
        if (pu) {
          lv_label_set_text(pu, (String(weather_unit_gap()) + "%").c_str());
          position_value_unit_centered(fw.precip_probability_label, pu, center_x, kForecastProbabilityTop, col_w);
          lv_obj_clear_flag(pu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(fw.precip_probability_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(fw.precip_probability_label, "");
        lv_obj_add_flag(fw.precip_probability_label, LV_OBJ_FLAG_HIDDEN);
        if (pu) {
          lv_label_set_text(pu, "");
          lv_obj_add_flag(pu, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }

    if (fw.precip_bar) {
      if (data.active && data.has_precipitation && data.precipitation > 0.0f &&
          has_precipitation && max_precipitation > 0.0f) {
        lv_coord_t bar_h = static_cast<lv_coord_t>(lroundf(
            (data.precipitation / max_precipitation) * kForecastPrecipChartHeight));
        if (bar_h < 4) bar_h = 4;
        if (bar_h > kForecastPrecipChartHeight) bar_h = kForecastPrecipChartHeight;
        const lv_coord_t col_x = lv_obj_get_x(fw.column);
        const lv_coord_t slot_left = map_forecast_chart_x(ctx, i * kForecastHoursPerDay);
        const lv_coord_t slot_right = map_forecast_chart_x(ctx, (i + 1) * kForecastHoursPerDay);
        lv_coord_t slot_w = slot_right - slot_left;
        if (slot_w < 0) slot_w = 0;
        lv_coord_t bar_w = slot_w - (kForecastBarSideInset * 2);
        if (bar_w < 1) bar_w = 1;
        lv_coord_t bar_x = (slot_left - col_x) + ((slot_w - bar_w) / 2);
        lv_obj_set_size(fw.precip_bar, bar_w, bar_h);
        lv_obj_set_pos(fw.precip_bar,
                       bar_x,
                       kForecastPrecipChartTop + kForecastPrecipChartHeight - bar_h);
        lv_obj_clear_flag(fw.precip_bar, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(fw.precip_bar, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

static String weather_icon_from_condition(const String& condition) {
  String key = condition;
  key.trim();
  key.toLowerCase();
  if (!key.length()) return "";
  if (key == "clear-night") return "mdi:weather-night";
  if (key == "cloudy") return "mdi:weather-cloudy";
  if (key == "exceptional") return "mdi:alert-circle-outline";
  if (key == "fog") return "mdi:weather-fog";
  if (key == "hail") return "mdi:weather-hail";
  if (key == "lightning") return "mdi:weather-lightning";
  if (key == "lightning-rainy") return "mdi:weather-lightning-rainy";
  if (key == "partlycloudy") return "mdi:weather-partly-cloudy";
  if (key == "pouring") return "mdi:weather-pouring";
  if (key == "rainy") return "mdi:weather-rainy";
  if (key == "snowy") return "mdi:weather-snowy";
  if (key == "snowy-rainy") return "mdi:weather-snowy-rainy";
  if (key == "sunny") return "mdi:weather-sunny";
  if (key == "windy") return "mdi:weather-windy";
  if (key == "windy-variant") return "mdi:weather-windy-variant";
  return "";
}

static bool extract_weather_condition_field(const String& json, String& condition_out) {
  condition_out = "";
  if (extract_json_string_field(json, "state", condition_out)) return true;
  if (extract_json_string_field(json, "condition", condition_out)) return true;
  return extract_json_string_field(json, "c", condition_out);
}

static bool extract_weather_icon_field(const String& json, String& icon_out) {
  icon_out = "";
  if (extract_json_string_field(json, "icon", icon_out)) return true;
  return extract_json_string_field(json, "i", icon_out);
}

static void resolve_weather_visual_fields(const String& json,
                                          String& condition_out,
                                          String& icon_out) {
  String state_value;
  condition_out = "";
  extract_weather_icon_field(json, icon_out);
  if (!extract_json_string_field(json, "condition", condition_out)) {
    if (!extract_json_string_field(json, "c", condition_out)) {
      extract_json_string_field(json, "state", condition_out);
    }
  }
  if (!icon_out.length() && condition_out.length()) {
    icon_out = weather_icon_from_condition(condition_out);
  }
  if (!condition_out.length()) {
    extract_json_string_field(json, "state", condition_out);
  }
  if (!icon_out.length() && extract_json_string_field(json, "state", state_value)) {
    icon_out = weather_icon_from_condition(state_value);
  }
}

static String weather_condition_display_label(const String& condition) {
  return i18n::weather_condition_label(configManager.getConfig().language, condition);
}

static bool parse_iso_date(const String& iso, int& y, int& m, int& d) {
  if (iso.length() < 10) return false;
  if (iso.charAt(4) != '-' || iso.charAt(7) != '-') return false;
  y = iso.substring(0, 4).toInt();
  m = iso.substring(5, 7).toInt();
  d = iso.substring(8, 10).toInt();
  return (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31);
}

static String iso_date_add_days(const String& iso, int day_offset) {
  int y = 0, m = 0, d = 0;
  if (!parse_iso_date(iso, y, m, d)) return "";

  struct tm date_tm = {};
  date_tm.tm_year = y - 1900;
  date_tm.tm_mon = m - 1;
  date_tm.tm_mday = d + day_offset;
  date_tm.tm_hour = 12;

  time_t date_value = mktime(&date_tm);
  if (date_value < 0) return "";

  struct tm normalized_tm;
  if (!localtime_r(&date_value, &normalized_tm)) return "";

  char date_buf[11];
  if (strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &normalized_tm) == 0) return "";
  return date_buf;
}

static int iso_date_day_offset(const String& base_iso, const String& target_iso) {
  int base_y = 0, base_m = 0, base_d = 0;
  int target_y = 0, target_m = 0, target_d = 0;
  if (!parse_iso_date(base_iso, base_y, base_m, base_d) ||
      !parse_iso_date(target_iso, target_y, target_m, target_d)) {
    return -32768;
  }

  struct tm base_tm = {};
  base_tm.tm_year = base_y - 1900;
  base_tm.tm_mon = base_m - 1;
  base_tm.tm_mday = base_d;
  base_tm.tm_hour = 12;

  struct tm target_tm = {};
  target_tm.tm_year = target_y - 1900;
  target_tm.tm_mon = target_m - 1;
  target_tm.tm_mday = target_d;
  target_tm.tm_hour = 12;

  time_t base_value = mktime(&base_tm);
  time_t target_value = mktime(&target_tm);
  if (base_value < 0 || target_value < 0) return -32768;

  return static_cast<int>((target_value - base_value) / 86400);
}

static String weekday_from_iso(const String& iso) {
  return i18n::weather_weekday_short(configManager.getConfig().language, iso);
}

static String format_detail_time_axis_label(int hour24) {
  int normalized = hour24 % 24;
  if (normalized < 0) normalized += 24;
  const DeviceConfig& cfg = configManager.getConfig();
  const uint8_t time_format =
      clock_tile::resolve_time_format(clock_tile::TIME_FORMAT_AUTO, cfg.global_time_format, cfg.language);
  if (time_format == clock_tile::TIME_FORMAT_24H) {
    return String(normalized) + ":00";
  }

  int hour12 = normalized % 12;
  if (hour12 == 0) hour12 = 12;
  return String(hour12) + (normalized < 12 ? "am" : "pm");
}

static String format_localized_short_date_from_iso(const String& iso) {
  if (iso.length() < 10) return iso;
  const DeviceConfig& cfg = configManager.getConfig();
  const uint8_t date_format =
      clock_tile::resolve_date_format(clock_tile::DATE_FORMAT_AUTO, cfg.global_date_format, cfg.language);
  const String yy = iso.substring(2, 4);
  const String mm = iso.substring(5, 7);
  const String dd = iso.substring(8, 10);
  switch (date_format) {
    case clock_tile::DATE_FORMAT_MDY:
      return mm + "/" + dd;
    case clock_tile::DATE_FORMAT_YMD:
      return yy + "/" + mm + "/" + dd;
    default:
      return dd + "." + mm + ".";
  }
}

static String format_weather_popup_date_from_iso(const String& iso) {
  int y = 0, m = 0, d = 0;
  if (!parse_iso_date(iso, y, m, d)) return format_localized_short_date_from_iso(iso);

  const char* month =
      i18n::weather_month_short(configManager.getConfig().language, m);
  if (!month || !*month) return format_localized_short_date_from_iso(iso);

  return i18n::format_short_date(configManager.getConfig().language, d, month);
}

static const char* weather_today_button_text() {
  return i18n::weather_today_label(configManager.getConfig().language);
}

static String iso_date_part(const String& text) {
  if (text.length() < 10) return "";
  return text.substring(0, 10);
}

static int iso_hour_part(const String& text) {
  if (text.length() < 13) return -1;
  return text.substring(11, 13).toInt();
}

static bool get_local_now_parts(String& date_out, int& hour_out, int* minute_out) {
  time_t now = time(nullptr);
  if (now < 1704067200) return false;

  struct tm local_tm;
  if (!localtime_r(&now, &local_tm)) return false;

  char date_buf[11];
  if (strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local_tm) == 0) return false;

  date_out = date_buf;
  hour_out = local_tm.tm_hour;
  if (minute_out) *minute_out = local_tm.tm_min;
  return true;
}

static String format_detail_day_title(const ForecastData& data) {
  String today_date;
  int today_hour = 0;
  const bool has_now = get_local_now_parts(today_date, today_hour);
  const bool is_today = has_now && data.date_local == today_date;

  String title = is_today ? String(weather_today_button_text())
                          : weekday_from_iso(data.date_local);
  if (!title.length()) title = data.day;
  if (!data.date_local.length()) return title.length() ? title : String("--");
  if (data.date_local.length() < 10) return title.length() ? title : data.date_local;

  String date_text = format_weather_popup_date_from_iso(data.date_local);

  if (!title.length()) return date_text;
  if (!date_text.length()) return title;

  title += ", ";
  title += date_text;
  return title;
}

static String format_forecast_range_title(const WeatherPopupContext* ctx) {
  if (!ctx) return "";
  String start_date;
  String end_date;
  for (int i = 0; i < kCols; ++i) {
    if (ctx->forecast_data[i].date_local.length()) {
      if (!start_date.length()) start_date = ctx->forecast_data[i].date_local;
      end_date = ctx->forecast_data[i].date_local;
    }
  }
  if (!start_date.length()) return "";
  String start_text = format_weather_popup_date_from_iso(start_date);
  if (!end_date.length() || end_date == start_date) return start_text;
  return start_text + " - " + format_weather_popup_date_from_iso(end_date);
}

static void set_label_style(lv_obj_t* lbl, lv_color_t color, const lv_font_t* font) {
  if (!lbl) return;
  lv_obj_set_style_text_color(lbl, color, 0);
  if (font) {
    lv_obj_set_style_text_font(lbl, font, 0);
    if (font == FONT_MDI_ICONS) {
      popup_layout::applyIconScale(lbl);
    }
  }
}

static void update_week_range_label(WeatherPopupContext* ctx) {
  if (!ctx || !ctx->week_range_label) return;
  String text = format_forecast_range_title(ctx);
  lv_label_set_text(ctx->week_range_label, text.c_str());
  lv_obj_t* pill = ctx->week_range_pill ? ctx->week_range_pill : ctx->week_range_label;
  if (text.length()) {
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool next_json_object_in_array(const String& array, int& cursor, String& out) {
  int begin = 0;
  int end = 0;
  if (!hometiles_json::nextObjectInArray(
          array.c_str(), array.length(), &cursor, &begin, &end)) {
    return false;
  }
  out = array.substring(begin, end);
  return true;
}

static void style_mode_button(lv_obj_t* btn, bool active) {
  if (!btn) return;
  lv_color_t active_text_color = lv_color_hex(0x2A2A2A);
  lv_obj_t* row = lv_obj_get_parent(btn);
  lv_obj_t* card = row ? lv_obj_get_parent(row) : nullptr;
  if (card) {
    active_text_color = lv_obj_get_style_bg_color(card, LV_PART_MAIN);
  }

  auto apply_selector = [&](lv_style_selector_t selector) {
    lv_obj_set_style_bg_color(btn, lv_color_white(), selector);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_COVER : LV_OPA_TRANSP, selector);
    lv_obj_set_style_border_color(btn, lv_color_white(), selector);
    lv_obj_set_style_border_width(btn, 2, selector);
    lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, selector);
    lv_obj_set_style_transform_width(btn, 0, selector);
    lv_obj_set_style_transform_height(btn, 0, selector);
    lv_obj_set_style_translate_y(btn, 0, selector);
  };

  apply_selector(0);
  apply_selector(LV_STATE_PRESSED);

  lv_obj_t* label = lv_obj_get_child(btn, 0);
  if (label) {
    lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), 0);
    lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), LV_STATE_PRESSED);
  }
}

static void style_header_action_button(WeatherPopupContext* ctx, lv_obj_t* btn, bool active) {
  if (!ctx || !btn) return;
  lv_color_t active_text_color = lv_color_hex(0x2A2A2A);
  if (ctx->card) {
    active_text_color = lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN);
  }

  auto apply_selector = [&](lv_style_selector_t selector) {
    const bool pressed = selector == LV_STATE_PRESSED;
    lv_obj_set_style_bg_color(btn, lv_color_white(), selector);
    const lv_opa_t bg_opa = active ? LV_OPA_COVER : (pressed ? kFooterIndicatorOpa : LV_OPA_TRANSP);
    lv_obj_set_style_bg_opa(btn, bg_opa, selector);
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

  lv_obj_t* label = lv_obj_get_child(btn, 0);
  if (label) {
    lv_obj_set_style_text_font(label, FONT_UNIT, 0);
    lv_obj_set_style_text_font(label, FONT_UNIT, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), 0);
    lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_text_outline_stroke_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_outline_stroke_opa(label, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_text_outline_stroke_width(label, 0, 0);
    lv_obj_set_style_text_outline_stroke_width(label, 0, LV_STATE_PRESSED);
  }
}

static void update_mode_buttons(WeatherPopupContext* ctx) {
  if (!ctx) return;
  if (ctx->mode_week_btn) {
    lv_obj_t* label = lv_obj_get_child(ctx->mode_week_btn, 0);
    if (label) {
      set_label_style(label, lv_color_white(), FONT_MDI_ICONS);
      lv_label_set_text(label, getMdiChar("arrow-left").c_str());
    }
    style_mode_button(ctx->mode_week_btn, true);
  }
  if (ctx->mode_row) {
    lv_obj_add_flag(ctx->mode_row, LV_OBJ_FLAG_HIDDEN);
  }
  String today_date;
  int today_hour = 0;
  const int today_day =
      get_local_now_parts(today_date, today_hour) ? find_active_day_index(ctx, today_date) : -1;
  if (ctx->header_today_btn) {
    lv_obj_t* label = lv_obj_get_child(ctx->header_today_btn, 0);
    if (label) {
      set_label_style(label, lv_color_white(), FONT_UNIT);
      lv_label_set_text(label, weather_today_button_text());
    }
    style_header_action_button(ctx,
                               ctx->header_today_btn,
                               ctx->view_mode == WeatherPopupViewMode::Day &&
                                   today_day >= 0 &&
                                   ctx->selected_day_index == today_day);
    if (today_day >= 0) {
      lv_obj_clear_flag(ctx->header_today_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->header_today_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->header_week_btn) {
    lv_obj_t* label = lv_obj_get_child(ctx->header_week_btn, 0);
    if (label) {
      set_label_style(label, lv_color_white(), FONT_UNIT);
      lv_label_set_text(label, "7D");
    }
    style_header_action_button(ctx, ctx->header_week_btn, ctx->view_mode == WeatherPopupViewMode::Week);
    lv_obj_clear_flag(ctx->header_week_btn, LV_OBJ_FLAG_HIDDEN);
  }
  const bool show_detail_nav = ctx->view_mode == WeatherPopupViewMode::Day;
  const int prev_day = find_prev_active_day_index(ctx, ctx->selected_day_index);
  const int next_day = find_next_active_day_index(ctx, ctx->selected_day_index);
  const lv_color_t nav_bg_color =
      ctx->card ? lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN) : lv_color_hex(ctx->bg_color);
  const lv_color_t nav_inactive_color = lv_color_mix(lv_color_white(), nav_bg_color, 96);
  auto set_nav_state = [&](lv_obj_t* btn, bool visible, bool enabled) {
    if (!btn) return;
    if (!visible) {
      lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_t* icon = lv_obj_get_child(btn, 0);
    if (icon) {
      lv_obj_set_style_text_color(icon, enabled ? lv_color_white() : nav_inactive_color, 0);
      lv_obj_set_style_text_color(icon, enabled ? lv_color_white() : nav_inactive_color, LV_STATE_PRESSED);
    }
    if (enabled) {
      lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
  };
  set_nav_state(ctx->detail_prev_btn, true, show_detail_nav && prev_day >= 0);
  set_nav_state(ctx->detail_next_btn, true, show_detail_nav && next_day >= 0);
}

static void clear_hourly(WeatherPopupContext* ctx) {
  if (!ctx) return;
  for (int i = 0; i < kHourlyForecastMax; ++i) {
    HourlyForecastData& hour = ctx->hourly[i];
    hour.active = false;
    hour.date_local.remove(0);
    hour.hour_local = -1;
    hour.icon.remove(0);
    hour.has_temp = false;
    hour.temp = 0.0f;
    hour.has_precipitation = false;
    hour.precipitation = 0.0f;
    hour.has_precipitation_probability = false;
    hour.precipitation_probability = 0.0f;
  }
}

static int find_active_day_index(const WeatherPopupContext* ctx, const String& date_local) {
  if (!ctx || !date_local.length()) return -1;
  for (int i = 0; i < kCols; ++i) {
    if (ctx->forecast_data[i].date_local == date_local) {
      return i;
    }
  }
  return -1;
}

static int find_first_active_day_index(const WeatherPopupContext* ctx) {
  if (!ctx) return -1;
  for (int i = 0; i < kCols; ++i) {
    if (ctx->forecast_data[i].date_local.length()) return i;
  }
  return -1;
}

static int find_prev_active_day_index(const WeatherPopupContext* ctx, int from_index) {
  if (!ctx) return -1;
  for (int i = from_index - 1; i >= 0; --i) {
    if (ctx->forecast_data[i].date_local.length()) return i;
  }
  return -1;
}

static int find_next_active_day_index(const WeatherPopupContext* ctx, int from_index) {
  if (!ctx) return -1;
  for (int i = from_index + 1; i < kCols; ++i) {
    if (ctx->forecast_data[i].date_local.length()) return i;
  }
  return -1;
}

static int get_default_day_index(const WeatherPopupContext* ctx) {
  if (!ctx) return -1;
  String today_date;
  int today_hour = 0;
  if (get_local_now_parts(today_date, today_hour)) {
    int today_index = find_active_day_index(ctx, today_date);
    if (today_index >= 0) return today_index;
  }
  return find_first_active_day_index(ctx);
}

static void set_popup_view_mode(WeatherPopupContext* ctx, WeatherPopupViewMode mode) {
  if (!ctx) return;
  if (ctx->forecast_row) {
    if (mode == WeatherPopupViewMode::Week) {
      lv_obj_clear_flag(ctx->forecast_row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->forecast_row, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->detail_wrap) {
    if (mode == WeatherPopupViewMode::Day) {
      lv_obj_clear_flag(ctx->detail_wrap, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->detail_wrap, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->value_row) {
    if (mode == WeatherPopupViewMode::Week || mode == WeatherPopupViewMode::Day) {
      lv_obj_clear_flag(ctx->value_row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->value_row, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->week_range_label) {
    if (mode == WeatherPopupViewMode::Week) {
      update_week_range_label(ctx);
    } else {
      lv_obj_add_flag(ctx->week_range_pill ? ctx->week_range_pill : ctx->week_range_label,
                      LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->detail_title_label) {
    if (mode == WeatherPopupViewMode::Day) {
      const char* title_text = lv_label_get_text(ctx->detail_title_label);
      if (title_text && title_text[0] != '\0') {
        lv_obj_clear_flag(ctx->detail_title_pill ? ctx->detail_title_pill : ctx->detail_title_label,
                          LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ctx->detail_title_pill ? ctx->detail_title_pill : ctx->detail_title_label,
                        LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_obj_add_flag(ctx->detail_title_pill ? ctx->detail_title_pill : ctx->detail_title_label,
                      LV_OBJ_FLAG_HIDDEN);
    }
  }
  ctx->view_mode = mode;
  update_mode_buttons(ctx);
}

static void clear_detail_view(WeatherPopupContext* ctx) {
  if (!ctx) return;

  if (ctx->detail_title_label) {
    lv_label_set_text(ctx->detail_title_label, "");
    lv_obj_add_flag(ctx->detail_title_pill ? ctx->detail_title_pill : ctx->detail_title_label,
                    LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->week_range_label) {
    lv_label_set_text(ctx->week_range_label, "");
    lv_obj_add_flag(ctx->week_range_pill ? ctx->week_range_pill : ctx->week_range_label,
                    LV_OBJ_FLAG_HIDDEN);
  }

  if (ctx->detail_temp_chart && ctx->detail_temp_series) {
    lv_chart_set_point_count(ctx->detail_temp_chart, kDetailChartPointCount);
    lv_chart_set_all_value(ctx->detail_temp_chart, ctx->detail_temp_series, LV_CHART_POINT_NONE);
    lv_chart_set_range(ctx->detail_temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_refresh(ctx->detail_temp_chart);
  }

  if (ctx->detail_precip_chart && ctx->detail_precip_series) {
    lv_chart_set_point_count(ctx->detail_precip_chart, kDetailChartPointCount);
    lv_chart_set_all_value(ctx->detail_precip_chart, ctx->detail_precip_series, 0);
    lv_chart_set_range(ctx->detail_precip_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 10);
    lv_chart_refresh(ctx->detail_precip_chart);
  }
  if (ctx->detail_past_overlay) {
    lv_obj_add_flag(ctx->detail_past_overlay, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_end_overlay) {
    lv_obj_add_flag(ctx->detail_end_overlay, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < kDetailChartPointCount; ++i) {
    if (ctx->detail_precip_bars[i]) {
      lv_obj_add_flag(ctx->detail_precip_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (ctx->detail_y_max_label) {
    lv_label_set_text(ctx->detail_y_max_label, "");
    lv_obj_add_flag(ctx->detail_y_max_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_y_min_label) {
    lv_label_set_text(ctx->detail_y_min_label, "");
    lv_obj_add_flag(ctx->detail_y_min_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_y_max_line) lv_obj_add_flag(ctx->detail_y_max_line, LV_OBJ_FLAG_HIDDEN);
  if (ctx->detail_y_min_line) lv_obj_add_flag(ctx->detail_y_min_line, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < kDetailMarkerCount; ++i) {
    if (ctx->detail_icon_guides[i]) lv_obj_add_flag(ctx->detail_icon_guides[i], LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_precip_guides[i]) lv_obj_add_flag(ctx->detail_precip_guides[i], LV_OBJ_FLAG_HIDDEN);
  }

  for (int i = 0; i < kDetailMarkerCount; ++i) {
    if (ctx->detail_time_lines[i]) lv_obj_add_flag(ctx->detail_time_lines[i], LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_time_labels[i]) {
      lv_label_set_text(ctx->detail_time_labels[i], "");
      lv_obj_add_flag(ctx->detail_time_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_icon_labels[i]) {
      lv_label_set_text(ctx->detail_icon_labels[i], "");
      lv_obj_add_flag(ctx->detail_icon_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_temp_point_dots[i]) {
      lv_obj_add_flag(ctx->detail_temp_point_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_temp_value_labels[i]) {
      lv_label_set_text(ctx->detail_temp_value_labels[i], "");
      lv_obj_add_flag(ctx->detail_temp_value_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_temp_unit_labels[i]) {
      lv_label_set_text(ctx->detail_temp_unit_labels[i], "");
      lv_obj_add_flag(ctx->detail_temp_unit_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_precip_amount_labels[i]) {
      lv_label_set_text(ctx->detail_precip_amount_labels[i], "");
      lv_obj_add_flag(ctx->detail_precip_amount_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_precip_amount_unit_labels[i]) {
      lv_label_set_text(ctx->detail_precip_amount_unit_labels[i], "");
      lv_obj_add_flag(ctx->detail_precip_amount_unit_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_probability_labels[i]) {
      lv_label_set_text(ctx->detail_probability_labels[i], "");
      lv_obj_add_flag(ctx->detail_probability_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_probability_unit_labels[i]) {
      lv_label_set_text(ctx->detail_probability_unit_labels[i], "");
      lv_obj_add_flag(ctx->detail_probability_unit_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->detail_current_dot) {
    lv_obj_add_flag(ctx->detail_current_dot, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_time_line) lv_obj_add_flag(ctx->detail_now_time_line, LV_OBJ_FLAG_HIDDEN);
  if (ctx->detail_now_precip_guide) lv_obj_add_flag(ctx->detail_now_precip_guide, LV_OBJ_FLAG_HIDDEN);
  if (ctx->detail_now_time_label) {
    lv_label_set_text(ctx->detail_now_time_label, "");
    lv_obj_add_flag(ctx->detail_now_time_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_icon_label) {
    lv_label_set_text(ctx->detail_now_icon_label, "");
    lv_obj_add_flag(ctx->detail_now_icon_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_temp_value_label) {
    lv_label_set_text(ctx->detail_now_temp_value_label, "");
    lv_obj_add_flag(ctx->detail_now_temp_value_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_temp_unit_label) {
    lv_label_set_text(ctx->detail_now_temp_unit_label, "");
    lv_obj_add_flag(ctx->detail_now_temp_unit_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_precip_amount_label) {
    lv_label_set_text(ctx->detail_now_precip_amount_label, "");
    lv_obj_add_flag(ctx->detail_now_precip_amount_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_precip_amount_unit_label) {
    lv_label_set_text(ctx->detail_now_precip_amount_unit_label, "");
    lv_obj_add_flag(ctx->detail_now_precip_amount_unit_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_probability_label) {
    lv_label_set_text(ctx->detail_now_probability_label, "");
    lv_obj_add_flag(ctx->detail_now_probability_label, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_now_probability_unit_label) {
    lv_label_set_text(ctx->detail_now_probability_unit_label, "");
    lv_obj_add_flag(ctx->detail_now_probability_unit_label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void clear_forecast_data(WeatherPopupContext* ctx) {
  if (!ctx) return;

  for (int i = 0; i < kCols; ++i) {
    ForecastData& data = ctx->forecast_data[i];
    data.active = false;
    data.day.remove(0);
    data.date_local.remove(0);
    data.icon.remove(0);
    data.has_high = false;
    data.high = 0.0f;
    data.has_low = false;
    data.low = 0.0f;
    data.has_precipitation = false;
    data.precipitation = 0.0f;
    data.has_precipitation_probability = false;
    data.precipitation_probability = 0.0f;
  }
}

static void clear_forecast(WeatherPopupContext* ctx) {
  if (!ctx) return;

  clear_forecast_data(ctx);

  clear_forecast_chart(ctx);

  if (ctx->forecast_precip_base) lv_obj_add_flag(ctx->forecast_precip_base, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < kCols - 1; ++i) {
    if (ctx->forecast_dividers[i]) lv_obj_add_flag(ctx->forecast_dividers[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->forecast_y_max_line) lv_obj_add_flag(ctx->forecast_y_max_line, LV_OBJ_FLAG_HIDDEN);
  if (ctx->forecast_y_min_line) lv_obj_add_flag(ctx->forecast_y_min_line, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < kCols; ++i) {
    if (ctx->forecast_time_lines[i]) lv_obj_add_flag(ctx->forecast_time_lines[i], LV_OBJ_FLAG_HIDDEN);
  }

  for (int i = 0; i < kCols; ++i) {
    ForecastWidgets& fw = ctx->forecast[i];
    if (fw.day_label) {
      lv_label_set_text(fw.day_label, "");
      lv_obj_add_flag(fw.day_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.icon_label) {
      lv_label_set_text(fw.icon_label, "");
      lv_obj_add_flag(fw.icon_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.high_label) {
      lv_label_set_text(fw.high_label, "");
      lv_obj_add_flag(fw.high_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.high_unit_label) {
      lv_label_set_text(fw.high_unit_label, "");
      lv_obj_add_flag(fw.high_unit_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.low_label) {
      lv_label_set_text(fw.low_label, "");
      lv_obj_add_flag(fw.low_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.low_unit_label) {
      lv_label_set_text(fw.low_unit_label, "");
      lv_obj_add_flag(fw.low_unit_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.precip_bar) {
      lv_obj_add_flag(fw.precip_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.precip_amount_label) {
      lv_label_set_text(fw.precip_amount_label, "");
      lv_obj_add_flag(fw.precip_amount_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.precip_amount_unit_label) {
      lv_label_set_text(fw.precip_amount_unit_label, "");
      lv_obj_add_flag(fw.precip_amount_unit_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.precip_probability_label) {
      lv_label_set_text(fw.precip_probability_label, "");
      lv_obj_add_flag(fw.precip_probability_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (fw.precip_probability_unit_label) {
      lv_label_set_text(fw.precip_probability_unit_label, "");
      lv_obj_add_flag(fw.precip_probability_unit_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static bool update_detail_view(WeatherPopupContext* ctx, int day_index) {
  if (!ctx) return false;

  if (day_index < 0 || day_index >= kCols) return false;
  const ForecastData& day = ctx->forecast_data[day_index];
  if (!day.date_local.length()) return false;

  // Hide container during update to suppress per-element render invalidation.
  // LVGL skips invalidation for children of hidden parents, making bulk
  // updates much faster. The container is re-shown by set_popup_view_mode.
  if (ctx->detail_wrap && !lv_obj_has_flag(ctx->detail_wrap, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_flag(ctx->detail_wrap, LV_OBJ_FLAG_HIDDEN);
  }

  if (ctx->detail_title_label) {
    String title = format_detail_day_title(day);
    if (!title.length()) title = day.day.length() ? day.day : String("--");
    lv_label_set_text(ctx->detail_title_label, title.c_str());
  }

  if (!ctx->detail_temp_chart || !ctx->detail_temp_series ||
      !ctx->detail_precip_chart || !ctx->detail_precip_series) {
    return true;
  }

  const lv_color_t popup_bg_color =
      ctx->card ? lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN) : lv_color_hex(ctx->bg_color);
  const lv_color_t past_overlay_label_color = lv_color_mix(lv_color_white(), popup_bg_color, 96);

  constexpr int kDetailDataHourCount = 24;
  const int marker_hours[kDetailMarkerCount] = {0, 6, 12, 18, 24};
  bool has_temp = false;
  int min_temp = 0;
  int max_temp = 0;
  bool has_precipitation = false;
  int max_precipitation = 0;
  bool marker_has_icon[kDetailMarkerCount] = {};
  String marker_icon[kDetailMarkerCount];
  bool marker_has_temp[kDetailMarkerCount] = {};
  float marker_temp[kDetailMarkerCount] = {};
  bool marker_has_precipitation[kDetailMarkerCount] = {};
  float marker_precipitation[kDetailMarkerCount] = {};
  bool marker_has_probability[kDetailMarkerCount] = {};
  float marker_probability[kDetailMarkerCount] = {};
  bool point_has_temp[kDetailChartPointCount] = {};
  bool point_has_precipitation[kDetailChartPointCount] = {};
  float point_precipitation[kDetailChartPointCount] = {};
  String next_date_local;
  bool has_same_day_hourly_content = false;

  for (int i = day_index + 1; i < kCols; ++i) {
    if (ctx->forecast_data[i].date_local.length()) {
      next_date_local = ctx->forecast_data[i].date_local;
      break;
    }
  }

  String now_date;
  int now_hour = 0;
  const bool clip_today = get_local_now_parts(now_date, now_hour) && now_date == day.date_local;
  const float now_hour_pos = static_cast<float>(now_hour);
  bool now_marker_has_icon = false;
  String now_marker_icon;
  bool now_marker_has_temp = false;
  float now_marker_temp = 0.0f;
  bool now_marker_has_precipitation = false;
  float now_marker_precipitation = 0.0f;
  bool now_marker_has_probability = false;
  float now_marker_probability = 0.0f;

  lv_chart_set_point_count(ctx->detail_temp_chart, kDetailChartPointCount);
  lv_chart_set_all_value(ctx->detail_temp_chart, ctx->detail_temp_series, LV_CHART_POINT_NONE);
  lv_chart_set_point_count(ctx->detail_precip_chart, kDetailChartPointCount);
  lv_chart_set_all_value(ctx->detail_precip_chart, ctx->detail_precip_series, 0);

  for (int i = 0; i < kHourlyForecastMax; ++i) {
    const HourlyForecastData& hour = ctx->hourly[i];
    if (!hour.active) continue;
    if (hour.hour_local < 0 || hour.hour_local >= kDetailDataHourCount) continue;
    const bool is_same_day = hour.date_local == day.date_local;
    const bool is_next_day_midnight =
        next_date_local.length() && hour.date_local == next_date_local && hour.hour_local == 0;
    if (is_same_day) {
      if (clip_today && hour.hour_local < now_hour) continue;
    } else if (!is_next_day_midnight) {
      continue;
    }

    const bool hour_has_content =
        hour.icon.length() || hour.has_temp || hour.has_precipitation || hour.has_precipitation_probability;
    if (is_same_day && hour_has_content) {
      has_same_day_hourly_content = true;
    }

    if (hour.has_temp) {
      const int scaled = scale_forecast_temp(hour.temp);
      const int point_id = is_next_day_midnight ? 24 : hour.hour_local;
      lv_chart_set_value_by_id(ctx->detail_temp_chart, ctx->detail_temp_series, point_id, scaled);
      point_has_temp[point_id] = true;
      if (!has_temp) {
        min_temp = scaled;
        max_temp = scaled;
        has_temp = true;
      } else {
        if (scaled < min_temp) min_temp = scaled;
        if (scaled > max_temp) max_temp = scaled;
      }
    }

    if (hour.has_precipitation) {
      const int point_id = is_next_day_midnight ? 24 : hour.hour_local;
      const int scaled_precip = static_cast<int>(lroundf(hour.precipitation * 10.0f));
      point_has_precipitation[point_id] = true;
      point_precipitation[point_id] = hour.precipitation;
      if (scaled_precip > 0) {
        has_precipitation = true;
        if (scaled_precip > max_precipitation) max_precipitation = scaled_precip;
      }
    }

    if (clip_today && is_same_day && hour.hour_local == now_hour) {
      if (hour.icon.length()) {
        now_marker_has_icon = true;
        now_marker_icon = hour.icon;
      }
      if (hour.has_temp) {
        now_marker_has_temp = true;
        now_marker_temp = hour.temp;
      }
      if (hour.has_precipitation) {
        now_marker_has_precipitation = true;
        now_marker_precipitation = hour.precipitation;
      }
      if (hour.has_precipitation_probability) {
        now_marker_has_probability = true;
        now_marker_probability = hour.precipitation_probability;
      }
    }

    for (int marker = 0; marker < kDetailMarkerCount; ++marker) {
      const bool is_marker_same_day =
          is_same_day && hour.hour_local == marker_hours[marker];
      const bool is_marker_next_day =
          is_next_day_midnight && marker == (kDetailMarkerCount - 1);
      if (is_marker_same_day || is_marker_next_day) {
        if (hour.icon.length()) {
          marker_has_icon[marker] = true;
          marker_icon[marker] = hour.icon;
        }
        if (hour.has_temp) {
          marker_has_temp[marker] = true;
          marker_temp[marker] = hour.temp;
        }
        if (hour.has_precipitation) {
          marker_has_precipitation[marker] = true;
          marker_precipitation[marker] = hour.precipitation;
        }
        if (hour.has_precipitation_probability) {
          marker_has_probability[marker] = true;
          marker_probability[marker] = hour.precipitation_probability;
        }
        break;
      }
    }
  }

  const bool whole_day_missing = !has_same_day_hourly_content;
  if (whole_day_missing) {
    has_temp = false;
    has_precipitation = false;
    max_precipitation = 0;
    lv_chart_set_all_value(ctx->detail_temp_chart, ctx->detail_temp_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(ctx->detail_precip_chart, ctx->detail_precip_series, 0);
  }

  if (has_temp) {
    int base_padding = (max_temp - min_temp) / 4;
    if (base_padding < 10) base_padding = 10;
    int bottom_padding = base_padding / 6;
    if (bottom_padding < 1) bottom_padding = 1;
    int top_padding = base_padding + 34;
    if (max_temp == min_temp) {
      bottom_padding = 2;
      top_padding = 42;
    }
    ctx->detail_temp_min = min_temp - bottom_padding;
    ctx->detail_temp_max = max_temp + top_padding;
    lv_chart_set_range(ctx->detail_temp_chart,
                       LV_CHART_AXIS_PRIMARY_Y,
                       ctx->detail_temp_min,
                       ctx->detail_temp_max);
    lv_chart_refresh(ctx->detail_temp_chart);
  } else {
    ctx->detail_temp_min = 0;
    ctx->detail_temp_max = 100;
    lv_chart_set_range(ctx->detail_temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_refresh(ctx->detail_temp_chart);
  }

  if (!has_precipitation) {
    max_precipitation = 20;
  } else if (max_precipitation < 30) {
    max_precipitation = 30;
  }
  lv_chart_set_range(ctx->detail_precip_chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_precipitation);
  lv_chart_refresh(ctx->detail_precip_chart);

  lv_obj_t* chart_wrap = lv_obj_get_parent(ctx->detail_temp_chart);
  if (!chart_wrap) return true;
  lv_obj_update_layout(chart_wrap);
  lv_obj_update_layout(ctx->detail_temp_chart);
  lv_obj_update_layout(ctx->detail_precip_chart);
  const lv_coord_t first_point_x = map_detail_chart_x(ctx, 0);
  const lv_coord_t last_point_x = map_detail_chart_x(ctx, kDetailChartPointCount - 1);
  if (ctx->detail_past_overlay) {
    lv_obj_add_flag(ctx->detail_past_overlay, LV_OBJ_FLAG_HIDDEN);
  }
  const lv_coord_t chart_wrap_w = lv_obj_get_width(chart_wrap);
  const lv_coord_t chart_wrap_h = lv_obj_get_height(chart_wrap);
  const lv_coord_t separator_left = first_point_x;
  const lv_coord_t separator_right = last_point_x;
  const lv_coord_t separator_width =
      (separator_right >= separator_left) ? (separator_right - separator_left + 1) : 0;
  const lv_coord_t top_separator_y =
      kDetailDisabledOverlayTop + kDetailDisabledSeparator1Top + (kDetailDisabledBlockGap / 2);
  const lv_coord_t temp_guide_bottom = kDetailTempGuideTop + kDetailTempGuideHeight;
  const lv_coord_t bottom_separator_y =
      temp_guide_bottom + ((kDetailPrecipGuideTop - temp_guide_bottom) / 2);
  const lv_coord_t guide_top_overhang = 6;
  const lv_coord_t guide_bottom_overhang = 8;
  const lv_coord_t full_guide_top = top_separator_y - guide_top_overhang;
  lv_coord_t full_guide_bottom = kDetailPrecipGuideTop + kDetailPrecipGuideHeight + guide_bottom_overhang;
  if (full_guide_bottom > chart_wrap_h) {
    full_guide_bottom = chart_wrap_h;
  }
  const lv_coord_t full_guide_height = full_guide_bottom - full_guide_top;
  if (ctx->detail_y_max_line) {
    if (separator_width > 0) {
      lv_obj_set_pos(ctx->detail_y_max_line, separator_left, top_separator_y);
      lv_obj_set_size(ctx->detail_y_max_line, separator_width, 1);
      lv_obj_clear_flag(ctx->detail_y_max_line, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->detail_y_max_line, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->detail_y_min_line) {
    if (separator_width > 0) {
      lv_obj_set_pos(ctx->detail_y_min_line, separator_left, bottom_separator_y);
      lv_obj_set_size(ctx->detail_y_min_line, separator_width, 1);
      lv_obj_clear_flag(ctx->detail_y_min_line, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->detail_y_min_line, LV_OBJ_FLAG_HIDDEN);
    }
  }
  const bool last_marker_missing =
      !whole_day_missing &&
      !point_has_temp[kDetailChartPointCount - 1] &&
      !point_has_precipitation[kDetailChartPointCount - 1] &&
      !marker_has_icon[kDetailMarkerCount - 1] &&
      !marker_has_temp[kDetailMarkerCount - 1] &&
      !marker_has_precipitation[kDetailMarkerCount - 1] &&
      !marker_has_probability[kDetailMarkerCount - 1];
  if (ctx->detail_end_overlay) {
    lv_obj_add_flag(ctx->detail_end_overlay, LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < kDetailMarkerCount; ++i) {
    if (!ctx->detail_precip_guides[i]) continue;
    lv_obj_add_flag(ctx->detail_precip_guides[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int point_id = 0; point_id < kDetailChartPointCount; ++point_id) {
    if (!ctx->detail_precip_bars[point_id]) continue;
    lv_obj_add_flag(ctx->detail_precip_bars[point_id], LV_OBJ_FLAG_HIDDEN);
    if (whole_day_missing) continue;
    if (!point_has_precipitation[point_id] || point_precipitation[point_id] <= 0.0f || max_precipitation <= 0) {
      continue;
    }

    const lv_coord_t center_x = map_detail_chart_x(ctx, point_id);
    const float scaled = (point_precipitation[point_id] * 10.0f) / static_cast<float>(max_precipitation);
    lv_coord_t bar_h = static_cast<lv_coord_t>(lroundf(scaled * kDetailPrecipChartHeight));
    if (bar_h < 1) bar_h = 1;
    if (bar_h > kDetailPrecipChartHeight) bar_h = kDetailPrecipChartHeight;
    lv_obj_set_pos(ctx->detail_precip_bars[point_id],
                   center_x - (kDetailPrecipBarWidth / 2),
                   kDetailPrecipChartTop + kDetailPrecipChartHeight - bar_h);
    lv_obj_set_size(ctx->detail_precip_bars[point_id], kDetailPrecipBarWidth, bar_h);
    lv_obj_clear_flag(ctx->detail_precip_bars[point_id], LV_OBJ_FLAG_HIDDEN);
  }

  auto clamp_centered_x = [&](lv_obj_t* obj, lv_coord_t center_x) -> lv_coord_t {
    lv_obj_update_layout(obj);
    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t x = center_x - (w / 2);
    if (x < 0) x = 0;
    if (x > chart_wrap_w - w) x = chart_wrap_w - w;
    return x;
  };

  const bool show_now_marker = clip_today &&
                               !whole_day_missing &&
                               now_hour_pos >= 0.0f &&
                               now_hour_pos <= static_cast<float>(kDetailDataHourCount) &&
                               (now_marker_has_temp ||
                                now_marker_has_icon ||
                                now_marker_has_precipitation ||
                                now_marker_has_probability);
  const lv_coord_t now_x = show_now_marker ? map_detail_chart_xf(ctx, now_hour_pos) : 0;
  String now_time_text;
  lv_coord_t now_time_label_x = 0;
  lv_coord_t now_time_label_w = 0;
  if (show_now_marker && ctx->detail_now_time_label) {
    now_time_text = format_detail_time_axis_label(now_hour);
    lv_label_set_text(ctx->detail_now_time_label, now_time_text.c_str());
    now_time_label_x = clamp_centered_x(ctx->detail_now_time_label, now_x);
    now_time_label_w = lv_obj_get_width(ctx->detail_now_time_label);
  }
  String now_marker_icon_char;
  const bool now_marker_has_renderable_icon =
      now_marker_has_icon && (now_marker_icon_char = getMdiChar(now_marker_icon)).length();
  bool hide_marker_time_for_now[kDetailMarkerCount] = {};
  bool hide_marker_icon_for_now[kDetailMarkerCount] = {};
  bool hide_marker_temp_for_now[kDetailMarkerCount] = {};
  bool hide_marker_precip_for_now[kDetailMarkerCount] = {};
  bool hide_marker_probability_for_now[kDetailMarkerCount] = {};
  if (show_now_marker) {
    for (int marker = 0; marker < kDetailMarkerCount; ++marker) {
      const float marker_hour = static_cast<float>(marker_hours[marker]);
      bool time_collides = false;
      if (ctx->detail_time_labels[marker] && now_time_label_w > 0) {
        String marker_time_text = format_detail_time_axis_label(marker_hours[marker]);
        lv_label_set_text(ctx->detail_time_labels[marker], marker_time_text.c_str());
        const lv_coord_t marker_time_label_x =
            clamp_centered_x(ctx->detail_time_labels[marker], map_detail_chart_x(ctx, marker_hours[marker]));
        const lv_coord_t marker_time_label_w = lv_obj_get_width(ctx->detail_time_labels[marker]);
        constexpr lv_coord_t kNowTimeLabelGap = 6;
        const lv_coord_t marker_time_right = marker_time_label_x + marker_time_label_w;
        const lv_coord_t now_time_right = now_time_label_x + now_time_label_w;
        time_collides =
            !(marker_time_right + kNowTimeLabelGap <= now_time_label_x ||
              now_time_right + kNowTimeLabelGap <= marker_time_label_x);
      }
      const bool forward_collides =
          marker_hour > now_hour_pos &&
          (marker_hour - now_hour_pos) < kDetailNowCollisionHours;
      hide_marker_time_for_now[marker] = time_collides || forward_collides;
      hide_marker_icon_for_now[marker] = forward_collides && now_marker_has_renderable_icon;
      hide_marker_temp_for_now[marker] = forward_collides && now_marker_has_temp;
      hide_marker_precip_for_now[marker] = forward_collides && now_marker_has_precipitation;
      hide_marker_probability_for_now[marker] = forward_collides && now_marker_has_probability;
    }
  }

  for (int marker = 0; marker < kDetailMarkerCount; ++marker) {
    lv_coord_t marker_x = map_detail_chart_x(ctx, marker_hours[marker]);
    lv_coord_t marker_y = kDetailTempChartTop + (kDetailTempChartHeight / 2);
    const bool end_missing_marker =
        last_marker_missing && marker == (kDetailMarkerCount - 1);
    const bool hidden_time_for_now = show_now_marker && hide_marker_time_for_now[marker];
    const bool hidden_icon_for_now = show_now_marker && hide_marker_icon_for_now[marker];
    const bool hidden_temp_for_now = show_now_marker && hide_marker_temp_for_now[marker];
    const bool hidden_precip_for_now = show_now_marker && hide_marker_precip_for_now[marker];
    const bool hidden_probability_for_now =
        show_now_marker && hide_marker_probability_for_now[marker];
    if (ctx->detail_temp_chart && ctx->detail_temp_series) {
      lv_point_t point;
      lv_chart_get_point_pos_by_id(ctx->detail_temp_chart,
                                   ctx->detail_temp_series,
                                   marker_hours[marker],
                                   &point);
      if (marker_has_temp[marker]) {
        marker_y = lv_obj_get_y(ctx->detail_temp_chart) + point.y;
      }
    }

    if (ctx->detail_time_lines[marker]) {
      if (!whole_day_missing &&
          (!clip_today || marker_hours[marker] >= now_hour) &&
          !hidden_time_for_now &&
          !(end_missing_marker && marker == (kDetailMarkerCount - 1))) {
        lv_obj_set_pos(ctx->detail_time_lines[marker], marker_x, full_guide_top);
        lv_obj_set_size(ctx->detail_time_lines[marker], 1, full_guide_height);
        lv_obj_clear_flag(ctx->detail_time_lines[marker], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ctx->detail_time_lines[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (ctx->detail_precip_guides[marker]) {
      lv_obj_add_flag(ctx->detail_precip_guides[marker], LV_OBJ_FLAG_HIDDEN);
    }

    if (ctx->detail_time_labels[marker]) {
      if (hidden_time_for_now) {
        lv_label_set_text(ctx->detail_time_labels[marker], "");
        lv_obj_add_flag(ctx->detail_time_labels[marker], LV_OBJ_FLAG_HIDDEN);
      } else {
        const bool past_marker = clip_today && marker_hours[marker] < now_hour;
        lv_obj_set_style_text_color(ctx->detail_time_labels[marker],
                                    (past_marker || end_missing_marker || whole_day_missing)
                                        ? past_overlay_label_color
                                        : lv_color_white(),
                                    0);
        String label = format_detail_time_axis_label(marker_hours[marker]);
        lv_label_set_text(ctx->detail_time_labels[marker], label.c_str());
        lv_obj_set_pos(ctx->detail_time_labels[marker],
                       clamp_centered_x(ctx->detail_time_labels[marker], marker_x),
                       kDetailTimeLabelY);
        lv_obj_clear_flag(ctx->detail_time_labels[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (ctx->detail_icon_labels[marker]) {
      if (whole_day_missing || hidden_icon_for_now) {
        lv_label_set_text(ctx->detail_icon_labels[marker], "");
        lv_obj_add_flag(ctx->detail_icon_labels[marker], LV_OBJ_FLAG_HIDDEN);
      } else if (marker_has_icon[marker]) {
        String icon_char = getMdiChar(marker_icon[marker]);
        if (icon_char.length()) {
          lv_label_set_text(ctx->detail_icon_labels[marker], icon_char.c_str());
          lv_obj_set_pos(ctx->detail_icon_labels[marker],
                         clamp_centered_x(ctx->detail_icon_labels[marker], marker_x),
                         kDetailIconY);
          lv_obj_clear_flag(ctx->detail_icon_labels[marker], LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_label_set_text(ctx->detail_icon_labels[marker], "");
          lv_obj_add_flag(ctx->detail_icon_labels[marker], LV_OBJ_FLAG_HIDDEN);
        }
      } else {
        lv_label_set_text(ctx->detail_icon_labels[marker], "");
        lv_obj_add_flag(ctx->detail_icon_labels[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (ctx->detail_temp_point_dots[marker]) {
      if (whole_day_missing || hidden_temp_for_now || !marker_has_temp[marker]) {
        lv_obj_add_flag(ctx->detail_temp_point_dots[marker], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_set_pos(ctx->detail_temp_point_dots[marker],
                       marker_x - (kDetailTempMarkerSize / 2),
                       marker_y - (kDetailTempMarkerSize / 2));
        lv_obj_clear_flag(ctx->detail_temp_point_dots[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (ctx->detail_temp_value_labels[marker]) {
      lv_obj_t* tu = ctx->detail_temp_unit_labels[marker];
      if (whole_day_missing || hidden_temp_for_now || !marker_has_temp[marker]) {
        lv_label_set_text(ctx->detail_temp_value_labels[marker], "");
        lv_obj_add_flag(ctx->detail_temp_value_labels[marker], LV_OBJ_FLAG_HIDDEN);
        if (tu) { lv_label_set_text(tu, ""); lv_obj_add_flag(tu, LV_OBJ_FLAG_HIDDEN); }
      } else {
        String val = format_forecast_chart_temp_value(marker_temp[marker]);
        lv_label_set_text(ctx->detail_temp_value_labels[marker], val.c_str());
        if (tu) {
          lv_label_set_text(tu, (String(weather_unit_gap()) + "\xC2\xB0\x43").c_str());
          lv_obj_update_layout(ctx->detail_temp_value_labels[marker]);
          lv_obj_update_layout(tu);
          lv_coord_t val_w = lv_obj_get_width(ctx->detail_temp_value_labels[marker]);
          lv_coord_t unit_w = lv_obj_get_width(tu);
          lv_coord_t total_w = val_w + unit_w;
          lv_coord_t lbl_h = lv_obj_get_height(ctx->detail_temp_value_labels[marker]);
          lv_coord_t cx = marker_x - (total_w / 2);
          if (cx < 0) cx = 0;
          if (cx + total_w > chart_wrap_w) cx = chart_wrap_w - total_w;
          lv_coord_t y = marker_y - lbl_h - kDetailTempValueGap;
          const lv_coord_t min_y = kDetailIconY + 28;
          const lv_coord_t max_y = kDetailPrecipChartTop - lbl_h - 6;
          if (y < min_y) y = min_y;
          if (y > max_y) y = max_y;
          lv_obj_set_pos(ctx->detail_temp_value_labels[marker], cx, y);
          lv_obj_set_pos(
              tu, cx + val_w, y + weather_detail_unit_y_offset());
          lv_obj_clear_flag(tu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ctx->detail_temp_value_labels[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (ctx->detail_precip_amount_labels[marker]) {
      lv_obj_t* au = ctx->detail_precip_amount_unit_labels[marker];
      if (whole_day_missing || hidden_precip_for_now || !marker_has_precipitation[marker]) {
        lv_label_set_text(ctx->detail_precip_amount_labels[marker], "");
        lv_obj_add_flag(ctx->detail_precip_amount_labels[marker], LV_OBJ_FLAG_HIDDEN);
        if (au) { lv_label_set_text(au, ""); lv_obj_add_flag(au, LV_OBJ_FLAG_HIDDEN); }
      } else {
        String val = format_precipitation_amount_value(marker_precipitation[marker]);
        String unit = format_precipitation_amount_unit(ctx->precipitation_unit);
        lv_label_set_text(ctx->detail_precip_amount_labels[marker], val.c_str());
        if (au) {
          lv_label_set_text(au, unit.c_str());
          position_value_unit_centered(ctx->detail_precip_amount_labels[marker], au,
                                       marker_x, kDetailAmountTop, chart_wrap_w);
          lv_obj_clear_flag(au, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ctx->detail_precip_amount_labels[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }

    if (ctx->detail_probability_labels[marker]) {
      lv_obj_t* pu = ctx->detail_probability_unit_labels[marker];
      if (whole_day_missing || hidden_probability_for_now || !marker_has_probability[marker] ||
          (clip_today && marker_hours[marker] < now_hour)) {
        lv_label_set_text(ctx->detail_probability_labels[marker], "");
        lv_obj_add_flag(ctx->detail_probability_labels[marker], LV_OBJ_FLAG_HIDDEN);
        if (pu) { lv_label_set_text(pu, ""); lv_obj_add_flag(pu, LV_OBJ_FLAG_HIDDEN); }
      } else {
        String val = format_precipitation_probability_value(marker_probability[marker]);
        lv_label_set_text(ctx->detail_probability_labels[marker], val.c_str());
        if (pu) {
          lv_label_set_text(pu, (String(weather_unit_gap()) + "%").c_str());
          position_value_unit_centered(ctx->detail_probability_labels[marker], pu,
                                       marker_x, kDetailProbabilityTop, chart_wrap_w);
          lv_obj_clear_flag(pu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ctx->detail_probability_labels[marker], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  if (show_now_marker) {
    lv_coord_t now_y = kDetailTempChartTop + (kDetailTempChartHeight / 2);
    if (point_has_temp[now_hour]) {
      lv_point_t point;
      lv_chart_get_point_pos_by_id(ctx->detail_temp_chart,
                                   ctx->detail_temp_series,
                                   now_hour,
                                   &point);
      now_y = lv_obj_get_y(ctx->detail_temp_chart) + point.y;
    } else if (now_marker_has_temp) {
      now_y = map_detail_chart_y(ctx, scale_forecast_temp(now_marker_temp));
    }

    if (ctx->detail_now_time_line) {
      lv_obj_set_pos(ctx->detail_now_time_line, now_x - (kDetailNowGuideWidth / 2), full_guide_top);
      lv_obj_set_size(ctx->detail_now_time_line, kDetailNowGuideWidth, full_guide_height);
      lv_obj_clear_flag(ctx->detail_now_time_line, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_now_precip_guide) {
      lv_obj_add_flag(ctx->detail_now_precip_guide, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_now_time_label) {
      lv_label_set_text(ctx->detail_now_time_label, now_time_text.c_str());
      lv_obj_set_pos(ctx->detail_now_time_label, now_time_label_x, kDetailTimeLabelY);
      lv_obj_clear_flag(ctx->detail_now_time_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (ctx->detail_now_icon_label) {
      if (now_marker_has_renderable_icon) {
        if (now_marker_icon_char.length()) {
          lv_label_set_text(ctx->detail_now_icon_label, now_marker_icon_char.c_str());
          lv_obj_set_pos(ctx->detail_now_icon_label,
                         clamp_centered_x(ctx->detail_now_icon_label, now_x),
                         kDetailIconY);
          lv_obj_clear_flag(ctx->detail_now_icon_label, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    if (ctx->detail_now_temp_value_label) {
      lv_obj_t* ntu = ctx->detail_now_temp_unit_label;
      if (now_marker_has_temp) {
        String val = format_forecast_chart_temp_value(now_marker_temp);
        lv_label_set_text(ctx->detail_now_temp_value_label, val.c_str());
        if (ntu) {
          lv_label_set_text(ntu, (String(weather_unit_gap()) + "\xC2\xB0\x43").c_str());
          lv_obj_update_layout(ctx->detail_now_temp_value_label);
          lv_obj_update_layout(ntu);
          lv_coord_t val_w = lv_obj_get_width(ctx->detail_now_temp_value_label);
          lv_coord_t unit_w = lv_obj_get_width(ntu);
          lv_coord_t total_w = val_w + unit_w;
          lv_coord_t lbl_h = lv_obj_get_height(ctx->detail_now_temp_value_label);
          lv_coord_t cx = now_x - (total_w / 2);
          if (cx < 0) cx = 0;
          if (cx + total_w > chart_wrap_w) cx = chart_wrap_w - total_w;
          lv_coord_t y = now_y - lbl_h - kDetailTempValueGap;
          const lv_coord_t min_y = kDetailIconY + 28;
          const lv_coord_t max_y = kDetailPrecipChartTop - lbl_h - 6;
          if (y < min_y) y = min_y;
          if (y > max_y) y = max_y;
          lv_obj_set_pos(ctx->detail_now_temp_value_label, cx, y);
          lv_obj_set_pos(
              ntu, cx + val_w, y + weather_detail_unit_y_offset());
          lv_obj_clear_flag(ntu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ctx->detail_now_temp_value_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (ctx->detail_now_precip_amount_label) {
      lv_obj_t* nau = ctx->detail_now_precip_amount_unit_label;
      if (now_marker_has_precipitation) {
        String val = format_precipitation_amount_value(now_marker_precipitation);
        String unit = format_precipitation_amount_unit(ctx->precipitation_unit);
        lv_label_set_text(ctx->detail_now_precip_amount_label, val.c_str());
        if (nau) {
          lv_label_set_text(nau, unit.c_str());
          position_value_unit_centered(ctx->detail_now_precip_amount_label, nau,
                                       now_x, kDetailAmountTop, chart_wrap_w);
          lv_obj_clear_flag(nau, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ctx->detail_now_precip_amount_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (ctx->detail_now_probability_label) {
      lv_obj_t* npu = ctx->detail_now_probability_unit_label;
      if (now_marker_has_probability) {
        String val = format_precipitation_probability_value(now_marker_probability);
        lv_label_set_text(ctx->detail_now_probability_label, val.c_str());
        if (npu) {
          lv_label_set_text(npu, (String(weather_unit_gap()) + "%").c_str());
          position_value_unit_centered(ctx->detail_now_probability_label, npu,
                                       now_x, kDetailProbabilityTop, chart_wrap_w);
          lv_obj_clear_flag(npu, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_clear_flag(ctx->detail_now_probability_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (ctx->detail_current_dot && now_marker_has_temp) {
      lv_obj_set_pos(ctx->detail_current_dot,
                     now_x - (kDetailCurrentHourDotSize / 2),
                     now_y - (kDetailCurrentHourDotSize / 2));
      lv_obj_clear_flag(ctx->detail_current_dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(ctx->detail_current_dot);
    }
  } else {
    // Hide all now-marker elements when not showing now marker
    if (ctx->detail_current_dot) lv_obj_add_flag(ctx->detail_current_dot, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_time_line) lv_obj_add_flag(ctx->detail_now_time_line, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_precip_guide) lv_obj_add_flag(ctx->detail_now_precip_guide, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_time_label) lv_obj_add_flag(ctx->detail_now_time_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_icon_label) lv_obj_add_flag(ctx->detail_now_icon_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_temp_value_label) lv_obj_add_flag(ctx->detail_now_temp_value_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_temp_unit_label) lv_obj_add_flag(ctx->detail_now_temp_unit_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_precip_amount_label) lv_obj_add_flag(ctx->detail_now_precip_amount_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_precip_amount_unit_label) lv_obj_add_flag(ctx->detail_now_precip_amount_unit_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_probability_label) lv_obj_add_flag(ctx->detail_now_probability_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->detail_now_probability_unit_label) lv_obj_add_flag(ctx->detail_now_probability_unit_label, LV_OBJ_FLAG_HIDDEN);
  }

  if ((ctx->detail_past_overlay && !lv_obj_has_flag(ctx->detail_past_overlay, LV_OBJ_FLAG_HIDDEN)) ||
      (ctx->detail_end_overlay && !lv_obj_has_flag(ctx->detail_end_overlay, LV_OBJ_FLAG_HIDDEN))) {
    for (int i = 0; i < kDetailMarkerCount; ++i) {
      if (ctx->detail_time_labels[i]) lv_obj_move_foreground(ctx->detail_time_labels[i]);
      if (ctx->detail_icon_labels[i]) lv_obj_move_foreground(ctx->detail_icon_labels[i]);
      if (ctx->detail_temp_value_labels[i]) lv_obj_move_foreground(ctx->detail_temp_value_labels[i]);
      if (ctx->detail_temp_unit_labels[i]) lv_obj_move_foreground(ctx->detail_temp_unit_labels[i]);
      if (ctx->detail_precip_amount_labels[i]) lv_obj_move_foreground(ctx->detail_precip_amount_labels[i]);
      if (ctx->detail_precip_amount_unit_labels[i]) lv_obj_move_foreground(ctx->detail_precip_amount_unit_labels[i]);
      if (ctx->detail_probability_labels[i]) lv_obj_move_foreground(ctx->detail_probability_labels[i]);
      if (ctx->detail_probability_unit_labels[i]) lv_obj_move_foreground(ctx->detail_probability_unit_labels[i]);
    }
    if (ctx->detail_now_time_label) lv_obj_move_foreground(ctx->detail_now_time_label);
    if (ctx->detail_now_icon_label) lv_obj_move_foreground(ctx->detail_now_icon_label);
    if (ctx->detail_now_temp_value_label) lv_obj_move_foreground(ctx->detail_now_temp_value_label);
    if (ctx->detail_now_temp_unit_label) lv_obj_move_foreground(ctx->detail_now_temp_unit_label);
    if (ctx->detail_now_precip_amount_label) lv_obj_move_foreground(ctx->detail_now_precip_amount_label);
    if (ctx->detail_now_precip_amount_unit_label) lv_obj_move_foreground(ctx->detail_now_precip_amount_unit_label);
    if (ctx->detail_now_probability_label) lv_obj_move_foreground(ctx->detail_now_probability_label);
    if (ctx->detail_now_probability_unit_label) lv_obj_move_foreground(ctx->detail_now_probability_unit_label);
  }

  return true;
}

static void show_week_view(WeatherPopupContext* ctx) {
  if (!ctx) return;
  set_popup_view_mode(ctx, WeatherPopupViewMode::Week);
}

static void show_day_view(WeatherPopupContext* ctx, int day_index) {
  if (!ctx) return;
  if (day_index < 0 || day_index >= kCols || !ctx->forecast_data[day_index].date_local.length()) {
    day_index = get_default_day_index(ctx);
  }
  if (day_index < 0 || !update_detail_view(ctx, day_index)) {
    show_week_view(ctx);
    return;
  }
  ctx->selected_day_index = day_index;
  set_popup_view_mode(ctx, WeatherPopupViewMode::Day);
}

static void reset_weather_popup_content(WeatherPopupContext* ctx) {
  if (!ctx) return;

  if (ctx->icon_label) {
    lv_label_set_text(ctx->icon_label, "");
    lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (ctx->condition_label) {
    lv_label_set_text(ctx->condition_label, "");
    lv_obj_add_flag(ctx->condition_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (ctx->condition_sep_label) {
    lv_label_set_text(ctx->condition_sep_label, "");
    lv_obj_add_flag(ctx->condition_sep_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (ctx->temp_label) {
    lv_label_set_text(ctx->temp_label, "--");
    lv_obj_clear_flag(ctx->temp_label, LV_OBJ_FLAG_HIDDEN);
  }

  ctx->current_has_temp = false;
  ctx->current_temp = 0.0f;
  ctx->current_icon = "";
  ctx->selected_day_index = -1;
  clear_hourly(ctx);
  clear_forecast(ctx);
  clear_detail_view(ctx);
  show_week_view(ctx);
  // Hide both header action buttons until content loads (update_mode_buttons will show them)
  if (ctx->header_week_btn) lv_obj_add_flag(ctx->header_week_btn, LV_OBJ_FLAG_HIDDEN);
  if (ctx->header_today_btn) lv_obj_add_flag(ctx->header_today_btn, LV_OBJ_FLAG_HIDDEN);
  popup_layout::alignHeader(ctx->card, ctx->location_label, ctx->icon_label);
}

static void hide_weather_model_widgets(WeatherPopupContext* ctx) {
  if (!ctx) return;
  // Keep an in-flight parsed model intact, but never expose the previous
  // entity's graph while the replacement is being completed.
  if (ctx->forecast_row) lv_obj_add_flag(ctx->forecast_row, LV_OBJ_FLAG_HIDDEN);
  if (ctx->detail_wrap) lv_obj_add_flag(ctx->detail_wrap, LV_OBJ_FLAG_HIDDEN);
  if (ctx->week_range_pill) {
    lv_obj_add_flag(ctx->week_range_pill, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_title_pill) {
    lv_obj_add_flag(ctx->detail_title_pill, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->header_week_btn) {
    lv_obj_add_flag(ctx->header_week_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->header_today_btn) {
    lv_obj_add_flag(ctx->header_today_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_prev_btn) {
    lv_obj_add_flag(ctx->detail_prev_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->detail_next_btn) {
    lv_obj_add_flag(ctx->detail_next_btn, LV_OBJ_FLAG_HIDDEN);
  }
}

static void request_weather_for_context(WeatherPopupContext* ctx) {
  if (!ctx || !ctx->entity_id.length()) return;
  if (ctx->entity_id.startsWith("__")) return;
  mqttPublishWeatherRequest(ctx->entity_id.c_str());
}

static void apply_weather_header(WeatherPopupContext* ctx, const String& json) {
  if (!ctx || !json.length()) return;

  String condition;
  String icon_name;
  resolve_weather_visual_fields(json, condition, icon_name);

  float temperature = 0.0f;
  bool has_temp = extract_json_number_or_string_field(json, "temperature", temperature);

  String unit;
  String precipitation_unit;
  String units_obj;
  if (extract_json_object_field(json, "units", units_obj)) {
    extract_json_string_field(units_obj, "temperature", unit);
    extract_json_string_field(units_obj, "precipitation", precipitation_unit);
  } else {
    extract_json_string_field(json, "temperature_unit", unit);
    extract_json_string_field(json, "precipitation_unit", precipitation_unit);
  }
  decode_basic_json_escapes(unit);
  decode_basic_json_escapes(precipitation_unit);
  if (unit.length()) ctx->unit = unit;
  else unit = ctx->unit;
  if (precipitation_unit.length()) ctx->precipitation_unit = precipitation_unit;
  else precipitation_unit = ctx->precipitation_unit;
  if (!precipitation_unit.length()) {
    precipitation_unit = "mm";
    ctx->precipitation_unit = precipitation_unit;
  }

  ctx->current_has_temp = has_temp;
  ctx->current_temp = temperature;
  ctx->current_icon = icon_name;

  if (ctx->icon_label) {
    if (icon_name.length()) {
      String iconChar = getMdiChar(icon_name);
      if (iconChar.length()) {
        lv_label_set_text(ctx->icon_label, iconChar.c_str());
        lv_obj_clear_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  String condition_text = weather_condition_display_label(condition);
  bool show_condition = (condition_text.length() && condition_text != "--");

  if (ctx->temp_label) {
    String temp_text = has_temp ? format_weather_temp(temperature, unit) : String("--");
    lv_label_set_text(ctx->temp_label, temp_text.c_str());
    lv_obj_clear_flag(ctx->temp_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (ctx->condition_label) {
    if (show_condition) {
      lv_label_set_text(ctx->condition_label, condition_text.c_str());
      lv_obj_clear_flag(ctx->condition_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(ctx->condition_label, "");
      lv_obj_add_flag(ctx->condition_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (ctx->condition_sep_label) {
    if (show_condition) {
      lv_label_set_text(ctx->condition_sep_label, "|");
      lv_obj_clear_flag(ctx->condition_sep_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(ctx->condition_sep_label, "");
      lv_obj_add_flag(ctx->condition_sep_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (ctx->location_label) {
    String name = ctx->title;
    name.trim();
    if (!name.length() || name == "--") {
      String payload_name;
      if (extract_json_string_field(json, "name", payload_name)) {
        decode_basic_json_escapes(payload_name);
        name = payload_name;
      }
    }
    if (!name.length()) name = "--";
    lv_label_set_text(ctx->location_label, name.c_str());
    lv_obj_clear_flag(ctx->location_label, LV_OBJ_FLAG_HIDDEN);
  }
  popup_layout::alignHeader(ctx->card, ctx->location_label, ctx->icon_label);
}

// Phase 1a: Parse the small current/daily section and retain the hourly JSON
// for bounded processing. No LVGL graph work happens in this phase.
static void parse_weather_base_data(WeatherPopupContext* ctx,
                                    const String& json,
                                    int& hourly_cursor) {
  if (!ctx || !json.length()) return;

  apply_weather_header(ctx, json);

  // Only clear the parsed model here. The popup can keep its last rendered
  // snapshot until the replacement is complete.
  clear_forecast_data(ctx);
  clear_hourly(ctx);

  const int forecast_array_start = find_json_array_start(json, "forecast");
  if (forecast_array_start >= 0) {
    int cursor = forecast_array_start + 1;
    String base_forecast_date;
    String today_date;
    int today_hour = 0;
    if (get_local_now_parts(today_date, today_hour)) {
      base_forecast_date = today_date;
    }
    int fallback_slot = 0;
    int daily_objects_processed = 0;
    String obj;
    while (daily_objects_processed < kCols &&
           next_json_object_in_array(json, cursor, obj)) {
      ++daily_objects_processed;
      String f_condition;
      String f_icon;
      String f_day;
      String f_datetime;
      String f_date_local;
      float f_high = 0.0f;
      float f_low = 0.0f;
      float f_precipitation = 0.0f;
      float f_probability = 0.0f;
      bool f_has_high = extract_json_number_or_string_field(obj, "temperature", f_high);
      bool f_has_low = false;
      bool f_has_precipitation =
          extract_json_number_or_string_field(obj, "precipitation", f_precipitation);
      bool f_has_probability =
          extract_json_number_or_string_field(obj, "precipitation_probability", f_probability);
      if (extract_json_number_or_string_field(obj, "templow", f_low)) f_has_low = true;
      if (!f_has_low && extract_json_number_or_string_field(obj, "temperature_low", f_low)) f_has_low = true;
      if (!f_has_low && extract_json_number_or_string_field(obj, "temp_low", f_low)) f_has_low = true;
      if (!f_has_low && extract_json_number_or_string_field(obj, "low", f_low)) f_has_low = true;

      resolve_weather_visual_fields(obj, f_condition, f_icon);
      extract_json_string_field(obj, "datetime", f_datetime);
      extract_json_string_field(obj, "date_local", f_date_local);

      if (!f_date_local.length() && f_datetime.length()) {
        f_date_local = iso_date_part(f_datetime);
      }
      if (f_date_local.length()) {
        f_day = weekday_from_iso(f_date_local);
      } else if (f_datetime.length()) {
        f_day = weekday_from_iso(f_datetime);
      }

      if (!base_forecast_date.length() && f_date_local.length()) {
        base_forecast_date = f_date_local;
      }

      int slot_index = -1;
      if (base_forecast_date.length() && f_date_local.length()) {
        const int offset = iso_date_day_offset(base_forecast_date, f_date_local);
        if (offset >= 0 && offset < kCols) {
          slot_index = offset;
        }
      }
      if (slot_index < 0) {
        while (fallback_slot < kCols && ctx->forecast_data[fallback_slot].date_local.length()) {
          ++fallback_slot;
        }
        if (fallback_slot < kCols) {
          slot_index = fallback_slot++;
        }
      }
      if (slot_index < 0 || slot_index >= kCols) continue;

      ForecastData& data = ctx->forecast_data[slot_index];
      data.active = true;
      data.day = f_day.length() ? f_day : String("--");
      data.date_local = f_date_local;
      data.icon = f_icon;
      data.has_high = f_has_high;
      data.high = f_high;
      data.has_low = f_has_low;
      data.low = f_low;
      data.has_precipitation = f_has_precipitation;
      data.precipitation = f_precipitation;
      data.has_precipitation_probability = f_has_probability;
      data.precipitation_probability = f_probability;
    }

    if (base_forecast_date.length()) {
      for (int i = 0; i < kCols; ++i) {
        ForecastData& data = ctx->forecast_data[i];
        if (!data.date_local.length()) {
          data.date_local = iso_date_add_days(base_forecast_date, i);
        }
        if (!data.day.length() && data.date_local.length()) {
          data.day = weekday_from_iso(data.date_local);
        }
      }
    }
  }

  const int hourly_array_start =
      find_json_array_start(json, "forecast_hourly");
  hourly_cursor = hourly_array_start >= 0 ? hourly_array_start + 1 : -1;
}

static bool parse_hourly_weather_object(WeatherPopupContext* ctx,
                                        const String& obj,
                                        int& hourly_count) {
  if (!ctx || hourly_count < 0 || hourly_count >= kHourlyForecastMax) {
    return false;
  }

  String h_condition;
  String h_icon;
  String h_datetime;
  String h_date_local;
  float h_hour_local = -1.0f;
  float h_temp = 0.0f;
  float h_precipitation = 0.0f;
  float h_probability = 0.0f;

  resolve_weather_visual_fields(obj, h_condition, h_icon);
  extract_json_string_field(obj, "datetime", h_datetime);
  extract_json_string_field(obj, "date_local", h_date_local);
  if (!h_date_local.length()) {
    extract_json_string_field(obj, "d", h_date_local);
  }
  if (!extract_json_number_or_string_field(obj, "hour_local", h_hour_local)) {
    extract_json_number_or_string_field(obj, "h", h_hour_local);
  }

  if (!h_date_local.length() && h_datetime.length()) {
    h_date_local = iso_date_part(h_datetime);
  }
  if (h_hour_local < 0.0f && h_datetime.length()) {
    h_hour_local = static_cast<float>(iso_hour_part(h_datetime));
  }
  if (!h_date_local.length() || h_hour_local < 0.0f) return false;

  HourlyForecastData& hour = ctx->hourly[hourly_count];
  hour.active = true;
  hour.date_local = h_date_local;
  hour.hour_local = static_cast<int>(lroundf(h_hour_local));
  hour.icon = h_icon;
  hour.has_temp =
      extract_json_number_or_string_field(obj, "temperature", h_temp) ||
      extract_json_number_or_string_field(obj, "t", h_temp);
  hour.temp = h_temp;
  hour.has_precipitation =
      extract_json_number_or_string_field(obj, "precipitation", h_precipitation) ||
      extract_json_number_or_string_field(obj, "p", h_precipitation);
  hour.precipitation = h_precipitation;
  hour.has_precipitation_probability =
      extract_json_number_or_string_field(
          obj, "precipitation_probability", h_probability) ||
      extract_json_number_or_string_field(obj, "pp", h_probability);
  hour.precipitation_probability = h_probability;
  ++hourly_count;
  return true;
}

// Returns true when the array is exhausted or the bounded model is full.
static bool parse_weather_hourly_batch(WeatherPopupContext* ctx) {
  if (!ctx || !g_pending_weather.parse_hourly_pending) return true;

  int objects_processed = 0;
  String obj;
  while ((kHourlyParseBatchSize == 0 ||
          objects_processed < kHourlyParseBatchSize) &&
         (kHourlyInputObjectLimit == 0 ||
          g_pending_weather.hourly_objects_seen < kHourlyInputObjectLimit) &&
         g_pending_weather.hourly_count < kHourlyForecastMax) {
    if (!next_json_object_in_array(g_pending_weather.payload,
                                   g_pending_weather.hourly_cursor, obj)) {
      return true;
    }
    ++objects_processed;
    ++g_pending_weather.hourly_objects_seen;
    parse_hourly_weather_object(ctx, obj, g_pending_weather.hourly_count);
  }

  return (kHourlyInputObjectLimit != 0 &&
          g_pending_weather.hourly_objects_seen >=
              kHourlyInputObjectLimit) ||
         g_pending_weather.hourly_count >= kHourlyForecastMax;
}

// Phase 2: Build UI from parsed data (heavy LVGL work)
static void build_weather_ui(WeatherPopupContext* ctx,
                             const String& previous_selected_date,
                             WeatherPopupViewMode previous_mode) {
  if (!ctx) return;

  update_forecast_graph(ctx);
  update_week_range_label(ctx);

  int selected_day_index = ctx->selected_day_index;
  if (previous_selected_date.length()) {
    int by_date = find_active_day_index(ctx, previous_selected_date);
    if (by_date >= 0) selected_day_index = by_date;
  }
  if (selected_day_index < 0 || selected_day_index >= kCols ||
      !ctx->forecast_data[selected_day_index].date_local.length()) {
    selected_day_index = get_default_day_index(ctx);
  }
  ctx->selected_day_index = selected_day_index;

  if (previous_mode == WeatherPopupViewMode::Day) {
    show_day_view(ctx, selected_day_index);
  } else {
    show_week_view(ctx);
  }
}

static void apply_init_to_context(WeatherPopupContext* ctx, const WeatherPopupInit& init) {
  if (!ctx) return;
  ctx->entity_id = init.entity_id;
  ctx->title = init.title;
  ctx->bg_color = init.bg_color;
  uint32_t color = ctx->bg_color ? ctx->bg_color : 0x2A2A2A;
  if (ctx->card) {
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(color), 0);
  }
  for (int i = 0; i < kDetailMarkerCount; ++i) {
    if (ctx->detail_temp_value_labels[i])
      lv_obj_set_style_bg_color(ctx->detail_temp_value_labels[i], lv_color_hex(color), 0);
    if (ctx->detail_temp_unit_labels[i])
      lv_obj_set_style_bg_color(ctx->detail_temp_unit_labels[i], lv_color_hex(color), 0);
  }
  if (ctx->detail_now_temp_value_label)
    lv_obj_set_style_bg_color(ctx->detail_now_temp_value_label, lv_color_hex(color), 0);
  if (ctx->detail_now_temp_unit_label)
    lv_obj_set_style_bg_color(ctx->detail_now_temp_unit_label, lv_color_hex(color), 0);
  for (int i = 0; i < ctx->detail_disabled_separator_count; ++i) {
    if (ctx->detail_disabled_separators[i]) {
      lv_obj_set_style_bg_color(ctx->detail_disabled_separators[i], lv_color_hex(color), 0);
    }
  }
  if (ctx->location_label) {
    String title = ctx->title;
    title.trim();
    if (!title.length()) title = "--";
    hometiles_title::set(ctx->location_label, title.c_str());
  }
  update_mode_buttons(ctx);
  popup_layout::alignHeader(ctx->card, ctx->location_label, ctx->icon_label);
}

static void on_overlay_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)e;
}

static void on_close_click(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->overlay || !ctx->card) return;

  g_weather_open_pending = false;
  g_pending_weather_init = WeatherPopupInit{};
  g_weather_body.restore();
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

static void on_overlay_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  if (ctx->detail_title_timer) {
    lv_timer_delete(ctx->detail_title_timer);
    ctx->detail_title_timer = nullptr;
  }
  if (g_weather_popup_ctx == ctx) {

    g_weather_open_pending = false;
    g_pending_weather_init = WeatherPopupInit{};
    g_weather_body.forget();
    g_weather_popup_ctx = nullptr;
    reset_pending_weather_update();
  }
  delete ctx;
}

static void detail_title_tick_cb(lv_timer_t* timer) {
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_timer_get_user_data(timer));
  if (!ctx || !ctx->detail_title_label) return;
  if (weather_refresh_in_progress(ctx)) return;
  if (ctx->view_mode != WeatherPopupViewMode::Day) return;
  if (ctx->selected_day_index < 0 || ctx->selected_day_index >= kCols) return;
  const ForecastData& day = ctx->forecast_data[ctx->selected_day_index];
  String now_date;
  int now_hour = 0;
  int now_minute = 0;
  if (!get_local_now_parts(now_date, now_hour, &now_minute)) return;
  if (day.date_local != now_date) return;
  if (now_minute == ctx->detail_title_today_minute) return;
  ctx->detail_title_today_minute = now_minute;
  String title = format_detail_day_title(day);
  if (title.length()) {
    lv_label_set_text(ctx->detail_title_label, title.c_str());
  }
}

static void on_mode_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  if (target == ctx->mode_week_btn) {
    if (weather_refresh_in_progress(ctx)) {
      g_pending_weather.previous_mode = WeatherPopupViewMode::Week;
      g_pending_weather.pending_day_nav = -1;
    }
    show_week_view(ctx);
  }
}

static void on_detail_nav_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  if (target == ctx->detail_prev_btn) {
    int prev_day = find_prev_active_day_index(ctx, ctx->selected_day_index);
    if (prev_day >= 0) {
      if (weather_refresh_in_progress(ctx)) {
        g_pending_weather.pending_day_nav = prev_day;
        g_pending_weather.previous_mode = WeatherPopupViewMode::Day;
      } else {
        show_day_view(ctx, prev_day);
      }
    }
  } else if (target == ctx->detail_next_btn) {
    int next_day = find_next_active_day_index(ctx, ctx->selected_day_index);
    if (next_day >= 0) {
      if (weather_refresh_in_progress(ctx)) {
        g_pending_weather.pending_day_nav = next_day;
        g_pending_weather.previous_mode = WeatherPopupViewMode::Day;
      } else {
        show_day_view(ctx, next_day);
      }
    }
  }
}

static void on_header_action_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  if (target == ctx->header_week_btn) {
    if (weather_refresh_in_progress(ctx)) {
      g_pending_weather.previous_mode = WeatherPopupViewMode::Week;
      g_pending_weather.pending_day_nav = -1;
    }
    show_week_view(ctx);
    return;
  }

  if (target == ctx->header_today_btn) {
    String today_date;
    int today_hour = 0;
    if (get_local_now_parts(today_date, today_hour)) {
      const int today_day = find_active_day_index(ctx, today_date);
      if (today_day >= 0) {
        if (weather_refresh_in_progress(ctx)) {
          g_pending_weather.pending_day_nav = today_day;
          g_pending_weather.previous_mode = WeatherPopupViewMode::Day;
        } else {
          show_day_view(ctx, today_day);
        }
        return;
      }
    }
    const int default_day = get_default_day_index(ctx);
    if (default_day >= 0) {
      if (weather_refresh_in_progress(ctx)) {
        g_pending_weather.pending_day_nav = default_day;
        g_pending_weather.previous_mode = WeatherPopupViewMode::Day;
      } else {
        show_day_view(ctx, default_day);
      }
    }
  }
}

static void on_day_column_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  WeatherPopupContext* ctx = static_cast<WeatherPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  for (int i = 0; i < kCols; ++i) {
    if (ctx->forecast[i].column == target) {
      if (!ctx->forecast_data[i].date_local.length()) return;
      if (weather_refresh_in_progress(ctx)) {
        g_pending_weather.pending_day_nav = i;
        g_pending_weather.previous_mode = WeatherPopupViewMode::Day;
      } else {
        show_day_view(ctx, i);
      }
      return;
    }
  }
}

static void build_popup_ui(WeatherPopupContext* ctx, const WeatherPopupInit& init) {
  const auto parts = create_popup_body(on_close_click, ctx, init.bg_color ? init.bg_color : 0x2A2A2A);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->location_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* overlay = parts.overlay;
  lv_obj_t* card = parts.card;
  lv_obj_t* location = parts.title;
  lv_obj_t* icon = parts.icon;
  lv_obj_t* close_btn = parts.close;
  uint32_t popup_tile_bg_color = init.bg_color ? init.bg_color : 0x2A2A2A;
  ctx->bg_color = popup_tile_bg_color;
  lv_obj_add_flag(parts.icon, LV_OBJ_FLAG_HIDDEN);

  auto make_header_action_button = [&](const char* text, int x_ofs, const lv_font_t* font) -> lv_obj_t* {
    lv_obj_t* btn = lv_button_create(card);
    lv_obj_set_size(btn, kFooterActionButtonWidth, kFooterButtonHeight);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, kFooterIndicatorOpa, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(btn, kFooterButtonRadius, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_anim_time(btn, 0, 0);
    lv_obj_set_style_anim_time(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(btn, 0, 0);
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, 0, 0);
    lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(btn, 0, 0);
    lv_obj_set_style_translate_y(btn, 0, LV_STATE_PRESSED);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, x_ofs, kFooterOffsetY);
    lv_obj_set_ext_click_area(btn, popup_layout::scale480(12));
    lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, on_header_action_click, LV_EVENT_CLICKED, ctx);
    lv_obj_t* label = lv_label_create(btn);
    set_label_style(label, lv_color_white(), font);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
  };

  ctx->header_week_btn = make_header_action_button("7D", kFooterWeekButtonX, FONT_UNIT);
  ctx->header_today_btn =
      make_header_action_button(weather_today_button_text(), kFooterTodayButtonX, FONT_UNIT);

  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 8, 0);

  lv_obj_t* mode_row = lv_obj_create(card);
  ctx->mode_row = mode_row;
  lv_obj_remove_style_all(mode_row);
  lv_obj_set_size(mode_row, kModeButtonWidth, kModeButtonHeight);
  lv_obj_align(mode_row, LV_ALIGN_TOP_LEFT, 0, kModeRowOffsetY);
  lv_obj_set_style_bg_opa(mode_row, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(mode_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(mode_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(mode_row, kModeButtonGap, 0);
  lv_obj_clear_flag(mode_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(mode_row, LV_OBJ_FLAG_HIDDEN);

  auto make_mode_button = [&](const char* text) -> lv_obj_t* {
    lv_obj_t* btn = lv_button_create(mode_row);
    lv_obj_set_size(btn, kModeButtonWidth, kModeButtonHeight);
    lv_obj_set_style_radius(btn, popup_layout::scale480(16), 0);
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
    set_label_style(label, lv_color_white(), FONT_MDI_ICONS);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
  };

  ctx->mode_week_btn = make_mode_button(getMdiChar("arrow-left").c_str());
  ctx->mode_day_btn = nullptr;
  lv_obj_add_event_cb(ctx->mode_week_btn, on_mode_click, LV_EVENT_CLICKED, ctx);

  lv_obj_t* value_row = lv_obj_create(card);
  ctx->value_row = value_row;
  lv_obj_remove_style_all(value_row);
  lv_obj_set_size(value_row, popup_layout::kContentWidth, popup_layout::kValueHeight);
  lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(value_row, popup_layout::scale(14), 0);
  lv_obj_set_style_bg_opa(value_row, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(value_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(value_row, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* condition_label = lv_label_create(value_row);
  ctx->condition_label = condition_label;
  set_label_style(condition_label, lv_color_white(), FONT_VALUE);
  lv_label_set_long_mode(condition_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(condition_label, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(
      condition_label, popup_layout::scale(250), 0);
  lv_label_set_text(condition_label, "--");
  lv_obj_add_flag(condition_label, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* sep_label = lv_label_create(value_row);
  ctx->condition_sep_label = sep_label;
  set_label_style(sep_label, lv_color_white(), FONT_VALUE);
  lv_label_set_text(sep_label, "|");
  lv_obj_add_flag(sep_label, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* temp_label = lv_label_create(value_row);
  ctx->temp_label = temp_label;
  set_label_style(temp_label, lv_color_white(), FONT_VALUE);
  lv_label_set_text(temp_label, "--");
  lv_obj_align(value_row, LV_ALIGN_TOP_MID, 0, kSummaryRowTop);

  lv_obj_t* week_range_pill = lv_obj_create(card);
  ctx->week_range_pill = week_range_pill;
  lv_obj_remove_style_all(week_range_pill);
  lv_obj_set_size(week_range_pill, kFooterDatePillWidth, kFooterButtonHeight);
  lv_obj_set_style_bg_color(week_range_pill, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(week_range_pill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(week_range_pill, 0, 0);
  lv_obj_set_style_border_opa(week_range_pill, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(week_range_pill, kFooterButtonRadius, 0);
  lv_obj_set_style_pad_all(week_range_pill, 0, 0);
  lv_obj_clear_flag(week_range_pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(week_range_pill, LV_ALIGN_BOTTOM_LEFT, kFooterInsetX, kFooterOffsetY);
  lv_obj_add_flag(week_range_pill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* week_range_label = lv_label_create(week_range_pill);
  ctx->week_range_label = week_range_label;
  set_label_style(week_range_label, lv_color_hex(ctx->bg_color), FONT_UNIT);
  lv_label_set_long_mode(week_range_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(week_range_label, kFooterDatePillWidth - 24);
  lv_obj_set_style_text_align(week_range_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(week_range_label, "");
  lv_obj_center(week_range_label);

  const lv_coord_t forecast_total_w = kCardWidth - (kCardPad * 2);
  const lv_coord_t forecast_plot_left = kForecastChartSideInset;
  const lv_coord_t forecast_plot_right = forecast_total_w - kForecastChartSideInset;
  const lv_coord_t forecast_plot_w =
      (forecast_plot_right > forecast_plot_left) ? (forecast_plot_right - forecast_plot_left) : 0;
  const lv_coord_t forecast_col_total_w = forecast_plot_w - (kForecastColGap * (kCols - 1));
  const lv_coord_t forecast_col_base_w = forecast_col_total_w / kCols;
  const lv_coord_t forecast_col_extra = forecast_col_total_w - (forecast_col_base_w * kCols);
  const lv_coord_t forecast_chart_x = 0;
  const lv_coord_t forecast_chart_w = forecast_total_w;

  lv_obj_t* forecast_row = lv_obj_create(card);
  ctx->forecast_row = forecast_row;
  lv_obj_remove_style_all(forecast_row);
  lv_obj_set_size(forecast_row, forecast_total_w, kForecastRowHeight);
  lv_obj_set_style_bg_opa(forecast_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(forecast_row, 0, 0);
  lv_obj_remove_flag(forecast_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(forecast_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_pos(forecast_row, 0, kForecastRowTop);

  lv_obj_t* temp_chart = lv_chart_create(forecast_row);
  ctx->forecast_temp_chart = temp_chart;
  lv_obj_set_size(temp_chart, forecast_chart_w, kForecastTempChartHeight);
  lv_obj_set_pos(temp_chart, forecast_chart_x, kForecastTempChartTop);
  lv_obj_set_style_bg_opa(temp_chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(temp_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(temp_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(temp_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(temp_chart, kForecastChartSideInset, LV_PART_MAIN);
  lv_obj_set_style_pad_right(temp_chart, kForecastChartSideInset, LV_PART_MAIN);
  lv_obj_set_style_line_width(temp_chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_line_opa(temp_chart, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(temp_chart, true, LV_PART_ITEMS);
  lv_obj_set_style_size(temp_chart, 0, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(temp_chart, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(temp_chart, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(temp_chart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_remove_flag(temp_chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(temp_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_chart_set_div_line_count(temp_chart, 0, 0);
  lv_chart_set_type(temp_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(temp_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(temp_chart, kForecastTempPointCount);
  ctx->forecast_temp_series =
      lv_chart_add_series(temp_chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);

  for (int i = 1; i < kCols; ++i) {
    lv_obj_t* divider = lv_obj_create(forecast_row);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(
        divider, 1, kForecastRowHeight - popup_layout::scale480(12));
    lv_obj_set_style_bg_color(divider, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_10, 0);
    lv_obj_set_style_radius(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_coord_t divider_x =
        forecast_plot_left +
        (i * forecast_col_base_w) +
        ((i < forecast_col_extra) ? i : forecast_col_extra) +
        (i * kForecastColGap);
    lv_obj_set_pos(divider, divider_x, popup_layout::scale480(10));
    lv_obj_add_flag(divider, LV_OBJ_FLAG_HIDDEN);
    ctx->forecast_dividers[i - 1] = divider;
  }

  lv_obj_t* precip_base = lv_obj_create(forecast_row);
  ctx->forecast_precip_base = precip_base;
  lv_obj_remove_style_all(precip_base);
  lv_obj_set_size(precip_base, forecast_plot_w, 1);
  lv_obj_set_style_bg_color(precip_base, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(precip_base, LV_OPA_20, 0);
  lv_obj_set_style_radius(precip_base, 0, 0);
  lv_obj_remove_flag(precip_base, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(precip_base, forecast_plot_left, kForecastPrecipChartTop + kForecastPrecipChartHeight);
  lv_obj_add_flag(precip_base, LV_OBJ_FLAG_HIDDEN);

  auto make_forecast_guide_line = [&]() -> lv_obj_t* {
    lv_obj_t* line = lv_obj_create(forecast_row);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 0, 1);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
  };

  ctx->forecast_y_max_line = make_forecast_guide_line();
  ctx->forecast_y_min_line = make_forecast_guide_line();

  for (int i = 0; i < kCols; ++i) {
    lv_obj_t* vline = lv_obj_create(forecast_row);
    lv_obj_remove_style_all(vline);
    lv_obj_set_size(vline, 1, 0);
    lv_obj_set_style_bg_color(vline, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(vline, LV_OPA_30, 0);
    lv_obj_remove_flag(vline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(vline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(vline, LV_OBJ_FLAG_HIDDEN);
    ctx->forecast_time_lines[i] = vline;
  }

  lv_obj_move_foreground(icon);
  lv_obj_move_foreground(location);
  lv_obj_move_foreground(close_btn);

  lv_coord_t forecast_col_x = forecast_plot_left;
  for (int i = 0; i < kCols; ++i) {
    const lv_coord_t forecast_col_w = forecast_col_base_w + ((i < forecast_col_extra) ? 1 : 0);
    lv_obj_t* col = lv_obj_create(forecast_row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, forecast_col_w, kForecastColumnTouchHeight);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(col, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(col, popup_layout::scale480(4));
    lv_obj_set_pos(col, forecast_col_x, 0);
    lv_obj_add_event_cb(col, on_day_column_click, LV_EVENT_CLICKED, ctx);
    ctx->forecast[i].column = col;

    lv_obj_t* day = lv_label_create(col);
    set_label_style(day, lv_color_white(), popup_layout::font20());
    lv_label_set_long_mode(day, LV_LABEL_LONG_DOT);
    lv_obj_set_width(day, LV_PCT(100));
    lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(day, "");
    lv_obj_add_flag(day, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(day, 0, kForecastDayTop);

    lv_obj_t* icon_day = lv_label_create(col);
    set_label_style(icon_day, lv_color_white(), FONT_MDI_ICONS);
    lv_label_set_text(icon_day, "");
    lv_obj_add_flag(icon_day, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(icon_day, LV_ALIGN_TOP_MID, 0, kForecastIconTop);

    lv_obj_t* high = lv_label_create(col);
    set_label_style(high, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(high, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(high, "");
    lv_obj_add_flag(high, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(high, 0, kForecastTempChartTop);

    lv_obj_t* high_unit = lv_label_create(col);
    set_label_style(high_unit, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(high_unit, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(high_unit, "");
    lv_obj_add_flag(high_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(high_unit, 0, kForecastTempChartTop);

    lv_obj_t* low = lv_label_create(col);
    set_label_style(low, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(low, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(low, "");
    lv_obj_add_flag(low, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(low, 0,
                   kForecastTempChartTop + popup_layout::scale480(24));

    lv_obj_t* low_unit = lv_label_create(col);
    set_label_style(low_unit, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(low_unit, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(low_unit, "");
    lv_obj_add_flag(low_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(low_unit, 0,
                   kForecastTempChartTop + popup_layout::scale480(24));

    lv_obj_t* precip_bar = lv_obj_create(col);
    lv_obj_remove_style_all(precip_bar);
    lv_obj_set_size(precip_bar, kForecastBarWidth, 0);
    lv_obj_set_style_bg_color(precip_bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(precip_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(precip_bar, 0, 0);
    lv_obj_add_flag(precip_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(precip_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* amount = lv_label_create(col);
    set_label_style(amount, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(amount, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(amount, "");
    lv_obj_add_flag(amount, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(amount, 0, kForecastAmountTop);

    lv_obj_t* amount_unit = lv_label_create(col);
    set_label_style(amount_unit, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(amount_unit, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(amount_unit, "");
    lv_obj_add_flag(amount_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(amount_unit, 0, kForecastAmountTop);

    lv_obj_t* probability = lv_label_create(col);
    set_label_style(probability, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(probability, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(probability, "");
    lv_obj_add_flag(probability, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(probability, 0, kForecastProbabilityTop);

    lv_obj_t* probability_unit = lv_label_create(col);
    set_label_style(probability_unit, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(probability_unit, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(probability_unit, "");
    lv_obj_add_flag(probability_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(probability_unit, 0, kForecastProbabilityTop);

    ctx->forecast[i].day_label = day;
    ctx->forecast[i].icon_label = icon_day;
    ctx->forecast[i].high_label = high;
    ctx->forecast[i].high_unit_label = high_unit;
    ctx->forecast[i].low_label = low;
    ctx->forecast[i].low_unit_label = low_unit;
    ctx->forecast[i].precip_bar = precip_bar;
    ctx->forecast[i].precip_amount_label = amount;
    ctx->forecast[i].precip_amount_unit_label = amount_unit;
    ctx->forecast[i].precip_probability_label = probability;
    ctx->forecast[i].precip_probability_unit_label = probability_unit;

    forecast_col_x += forecast_col_w + kForecastColGap;
  }

  lv_obj_t* detail_wrap = lv_obj_create(card);
  ctx->detail_wrap = detail_wrap;
  lv_obj_remove_style_all(detail_wrap);
  lv_obj_set_size(detail_wrap, forecast_total_w, kForecastRowHeight);
  lv_obj_set_style_bg_opa(detail_wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(detail_wrap, 0, 0);
  lv_obj_remove_flag(detail_wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(detail_wrap, 0, kDetailRowTop);
  lv_obj_add_flag(detail_wrap, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* detail_title_pill = lv_obj_create(card);
  ctx->detail_title_pill = detail_title_pill;
  lv_obj_remove_style_all(detail_title_pill);
  lv_obj_set_size(detail_title_pill, kFooterDatePillWidth, kFooterButtonHeight);
  lv_obj_set_style_bg_color(detail_title_pill, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(detail_title_pill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(detail_title_pill, 0, 0);
  lv_obj_set_style_border_opa(detail_title_pill, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(detail_title_pill, kFooterButtonRadius, 0);
  lv_obj_set_style_pad_all(detail_title_pill, 0, 0);
  lv_obj_clear_flag(detail_title_pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(detail_title_pill, LV_ALIGN_BOTTOM_LEFT, kFooterInsetX, kFooterOffsetY);
  lv_obj_add_flag(detail_title_pill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* detail_title = lv_label_create(detail_title_pill);
  ctx->detail_title_label = detail_title;
  set_label_style(detail_title, lv_color_hex(ctx->bg_color), FONT_UNIT);
  lv_label_set_long_mode(detail_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(detail_title, kFooterDatePillWidth - 24);
  lv_obj_set_style_text_align(detail_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(detail_title, "");
  lv_obj_center(detail_title);

  auto make_detail_nav_button = [&](const char* mdi_icon, lv_align_t align) -> lv_obj_t* {
    lv_obj_t* btn = lv_button_create(card);
    lv_obj_set_size(btn, kDetailNavButtonSize, kDetailNavButtonSize);
    lv_obj_set_style_radius(btn, kFooterButtonRadius, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, kFooterIndicatorOpa, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_anim_time(btn, 0, 0);
    lv_obj_set_style_anim_time(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(btn, 0, 0);
    lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(btn, 0, 0);
    lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(btn, 0, 0);
    lv_obj_set_style_translate_y(btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    const int x_ofs = (align == LV_ALIGN_TOP_LEFT) ? kFooterPrevButtonX : kFooterNextButtonX;
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, x_ofs, kFooterOffsetY);
    lv_obj_set_ext_click_area(btn, 12);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* label = lv_label_create(btn);
    set_label_style(label, lv_color_white(), FONT_MDI_ICONS);
    lv_label_set_text(label, getMdiChar(mdi_icon).c_str());
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, on_detail_nav_click, LV_EVENT_CLICKED, ctx);
    return btn;
  };

  ctx->detail_prev_btn = make_detail_nav_button("chevron-left", LV_ALIGN_TOP_LEFT);
  ctx->detail_next_btn = make_detail_nav_button("chevron-right", LV_ALIGN_TOP_RIGHT);

  lv_obj_t* chart_wrap = lv_obj_create(detail_wrap);
  lv_obj_remove_style_all(chart_wrap);
  lv_obj_set_size(chart_wrap, forecast_total_w, kDetailChartWrapHeight);
  lv_obj_set_pos(chart_wrap, 0, kDetailChartWrapTop);
  lv_obj_set_style_bg_opa(chart_wrap, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(chart_wrap, LV_OBJ_FLAG_SCROLLABLE);
  const lv_color_t popup_bg_color = lv_obj_get_style_bg_color(card, LV_PART_MAIN);
  lv_obj_t* past_overlay = lv_obj_create(chart_wrap);
  ctx->detail_past_overlay = past_overlay;
  lv_obj_remove_style_all(past_overlay);
  lv_obj_set_size(past_overlay, 0, kDetailDisabledOverlayHeight);
  lv_obj_set_pos(past_overlay, 0, kDetailDisabledOverlayTop);
  lv_obj_set_style_bg_opa(past_overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(past_overlay, 0, 0);
  lv_obj_remove_flag(past_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(past_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(past_overlay, LV_OBJ_FLAG_HIDDEN);

  auto build_detail_overlay = [&](lv_obj_t* parent) {
    lv_obj_t* block = lv_obj_create(parent);
    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(block, 0, 0);
    lv_obj_set_style_bg_color(block, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(block, 32, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_radius(block, kDetailPastOverlayRadius, 0);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);

    auto add_separator = [&](lv_coord_t y) {
      lv_obj_t* sep = lv_obj_create(parent);
      lv_obj_remove_style_all(sep);
      lv_obj_set_size(sep, LV_PCT(100), kDetailDisabledBlockGap);
      lv_obj_set_pos(sep, 0, y);
      lv_obj_set_style_bg_color(sep, popup_bg_color, 0);
      lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(sep, 0, 0);
      lv_obj_set_style_radius(sep, 0, 0);
      lv_obj_remove_flag(sep, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
      if (ctx->detail_disabled_separator_count <
          static_cast<int>(sizeof(ctx->detail_disabled_separators) /
                           sizeof(ctx->detail_disabled_separators[0]))) {
        ctx->detail_disabled_separators[ctx->detail_disabled_separator_count++] = sep;
      }
    };

    add_separator(kDetailDisabledSeparator1Top);
    add_separator(kDetailDisabledSeparator2Top);
  };

  build_detail_overlay(past_overlay);

  lv_obj_t* end_overlay = lv_obj_create(chart_wrap);
  ctx->detail_end_overlay = end_overlay;
  lv_obj_remove_style_all(end_overlay);
  lv_obj_set_size(end_overlay, 0, kDetailDisabledOverlayHeight);
  lv_obj_set_pos(end_overlay, 0, kDetailDisabledOverlayTop);
  lv_obj_set_style_bg_opa(end_overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(end_overlay, 0, 0);
  lv_obj_remove_flag(end_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(end_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(end_overlay, LV_OBJ_FLAG_HIDDEN);

  build_detail_overlay(end_overlay);

  constexpr int kLineOverlap = 6;
  const int line_start = kDetailYAxisWidth - kLineOverlap;
  const int line_width = forecast_total_w - line_start;

  lv_obj_t* y_max = lv_label_create(chart_wrap);
  ctx->detail_y_max_label = y_max;
  set_label_style(y_max, lv_color_white(), popup_layout::font20());
  lv_obj_set_style_text_align(y_max, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(
      y_max, kDetailYAxisWidth - popup_layout::scale480(10));
  lv_label_set_text(y_max, "");
  lv_obj_set_pos(y_max, 0, 0);
  lv_obj_add_flag(y_max, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* y_min = lv_label_create(chart_wrap);
  ctx->detail_y_min_label = y_min;
  set_label_style(y_min, lv_color_white(), popup_layout::font20());
  lv_obj_set_style_text_align(y_min, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(
      y_min, kDetailYAxisWidth - popup_layout::scale480(10));
  lv_label_set_text(y_min, "");
  lv_obj_set_pos(y_min, 0, kDetailTempChartHeight);
  lv_obj_add_flag(y_min, LV_OBJ_FLAG_HIDDEN);

  auto make_guide_line = [&](int16_t y_pos) -> lv_obj_t* {
    lv_obj_t* line = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, line_width, 1);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, line_start, y_pos);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
  };
  ctx->detail_y_max_line = make_guide_line(kDetailLabelOverhang);
  ctx->detail_y_min_line = make_guide_line(kDetailLabelOverhang + kDetailTempChartHeight - 1);

  for (int i = 0; i < kDetailMarkerCount; ++i) {
    lv_obj_t* icon_line = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(icon_line);
    lv_obj_set_size(icon_line, 1, 0);
    lv_obj_set_style_bg_color(icon_line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(icon_line, LV_OPA_30, 0);
    lv_obj_remove_flag(icon_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(icon_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon_line, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_icon_guides[i] = icon_line;

    lv_obj_t* vline = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(vline);
    lv_obj_set_size(vline, 1, 0);
    lv_obj_set_style_bg_color(vline, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(vline, LV_OPA_30, 0);
    lv_obj_remove_flag(vline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(vline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(vline, 0, kDetailTempChartTop);
    lv_obj_add_flag(vline, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_time_lines[i] = vline;

    lv_obj_t* tlbl = lv_label_create(chart_wrap);
    set_label_style(tlbl, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(tlbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(tlbl, "");
    lv_obj_set_pos(tlbl, 0, kDetailTimeLabelY);
    lv_obj_add_flag(tlbl, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_time_labels[i] = tlbl;

    lv_obj_t* ilbl = lv_label_create(chart_wrap);
    set_label_style(ilbl, lv_color_white(), FONT_MDI_ICONS);
    lv_obj_set_style_text_align(ilbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ilbl, "");
    lv_obj_set_pos(ilbl, 0, kDetailIconY);
    lv_obj_add_flag(ilbl, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_icon_labels[i] = ilbl;

    lv_obj_t* dot = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, kDetailTempMarkerSize, kDetailTempMarkerSize);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_temp_point_dots[i] = dot;

    lv_obj_t* vlbl = lv_label_create(chart_wrap);
    set_label_style(vlbl, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(vlbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_color(vlbl, lv_color_hex(ctx->bg_color ? ctx->bg_color : 0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(vlbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(vlbl, popup_layout::scale480(6), 0);
    lv_obj_set_style_pad_left(vlbl, popup_layout::scale480(4), 0);
    lv_obj_set_style_pad_right(vlbl, 0, 0);
    lv_obj_set_style_pad_top(vlbl, popup_layout::scale480(1), 0);
    lv_obj_set_style_pad_bottom(vlbl, popup_layout::scale480(1), 0);
    lv_label_set_text(vlbl, "");
    lv_obj_add_flag(vlbl, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_temp_value_labels[i] = vlbl;

    lv_obj_t* vu = lv_label_create(chart_wrap);
    set_label_style(vu, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(vu, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_color(vu, lv_color_hex(ctx->bg_color ? ctx->bg_color : 0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(vu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(vu, popup_layout::scale480(6), 0);
    lv_obj_set_style_pad_left(vu, 0, 0);
    lv_obj_set_style_pad_right(vu, popup_layout::scale480(4), 0);
    lv_obj_set_style_pad_top(vu, popup_layout::scale480(1), 0);
    lv_obj_set_style_pad_bottom(vu, popup_layout::scale480(1), 0);
    lv_label_set_text(vu, "");
    lv_obj_add_flag(vu, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_temp_unit_labels[i] = vu;

    lv_obj_t* albl = lv_label_create(chart_wrap);
    set_label_style(albl, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(albl, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(albl, "");
    lv_obj_set_pos(albl, 0, kDetailAmountTop);
    lv_obj_add_flag(albl, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_precip_amount_labels[i] = albl;

    lv_obj_t* au = lv_label_create(chart_wrap);
    set_label_style(au, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(au, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(au, "");
    lv_obj_set_pos(au, 0, kDetailAmountTop);
    lv_obj_add_flag(au, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_precip_amount_unit_labels[i] = au;

    lv_obj_t* plbl = lv_label_create(chart_wrap);
    set_label_style(plbl, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(plbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(plbl, "");
    lv_obj_set_pos(plbl, 0, kDetailProbabilityTop);
    lv_obj_add_flag(plbl, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_probability_labels[i] = plbl;

    lv_obj_t* pu = lv_label_create(chart_wrap);
    set_label_style(pu, lv_color_white(), weather_unit_font());
    lv_obj_set_style_text_align(pu, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(pu, "");
    lv_obj_set_pos(pu, 0, kDetailProbabilityTop);
    lv_obj_add_flag(pu, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_probability_unit_labels[i] = pu;

    lv_obj_t* precip_line = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(precip_line);
    lv_obj_set_size(precip_line, 1, 0);
    lv_obj_set_style_bg_color(precip_line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(precip_line, LV_OPA_30, 0);
    lv_obj_remove_flag(precip_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(precip_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(precip_line, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_precip_guides[i] = precip_line;
  }

  lv_obj_t* detail_temp_chart = lv_chart_create(chart_wrap);
  ctx->detail_temp_chart = detail_temp_chart;
  lv_obj_set_size(detail_temp_chart, LV_PCT(100), kDetailTempChartHeight);
  lv_obj_set_pos(detail_temp_chart, 0, kDetailTempChartTop);
  lv_obj_set_style_bg_opa(detail_temp_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(detail_temp_chart, 0, 0);
  lv_obj_set_style_pad_left(detail_temp_chart, kDetailChartLeftInset, 0);
  lv_obj_set_style_pad_right(detail_temp_chart, kDetailChartRightInset, 0);
  lv_obj_set_style_pad_top(detail_temp_chart, 0, 0);
  lv_obj_set_style_pad_bottom(detail_temp_chart, 0, 0);
  lv_obj_set_style_line_width(detail_temp_chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_line_opa(detail_temp_chart, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_line_color(detail_temp_chart, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(detail_temp_chart, true, LV_PART_ITEMS);
  lv_obj_set_style_size(detail_temp_chart, 6, 6, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(detail_temp_chart, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(detail_temp_chart, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(detail_temp_chart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_remove_flag(detail_temp_chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(detail_temp_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_chart_set_div_line_count(detail_temp_chart, 0, 0);
  lv_chart_set_type(detail_temp_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(detail_temp_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(detail_temp_chart, kDetailChartPointCount);
  ctx->detail_temp_series =
      lv_chart_add_series(detail_temp_chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);

  lv_obj_t* current_dot = lv_obj_create(chart_wrap);
  ctx->detail_current_dot = current_dot;
  lv_obj_remove_style_all(current_dot);
  lv_obj_set_size(current_dot, kDetailCurrentHourDotSize, kDetailCurrentHourDotSize);
  lv_obj_set_style_bg_color(current_dot, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(current_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(current_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(current_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(current_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(current_dot, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_time_line = lv_obj_create(chart_wrap);
  ctx->detail_now_time_line = now_time_line;
  lv_obj_remove_style_all(now_time_line);
  lv_obj_set_size(now_time_line, kDetailNowGuideWidth, 0);
  lv_obj_set_style_bg_color(now_time_line, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(now_time_line, kDetailNowGuideOpa, 0);
  lv_obj_remove_flag(now_time_line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(now_time_line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(now_time_line, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_precip_guide = lv_obj_create(chart_wrap);
  ctx->detail_now_precip_guide = now_precip_guide;
  lv_obj_remove_style_all(now_precip_guide);
  lv_obj_set_size(now_precip_guide, kDetailNowGuideWidth, 0);
  lv_obj_set_style_bg_color(now_precip_guide, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(now_precip_guide, kDetailNowGuideOpa, 0);
  lv_obj_remove_flag(now_precip_guide, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(now_precip_guide, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(now_precip_guide, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_time = lv_label_create(chart_wrap);
  ctx->detail_now_time_label = now_time;
  set_label_style(now_time, lv_color_white(), popup_layout::font20());
  lv_obj_set_style_text_align(now_time, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(now_time, "");
  lv_obj_set_pos(now_time, 0, kDetailTimeLabelY);
  lv_obj_add_flag(now_time, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_icon = lv_label_create(chart_wrap);
  ctx->detail_now_icon_label = now_icon;
  set_label_style(now_icon, lv_color_white(), FONT_MDI_ICONS);
  lv_obj_set_style_text_align(now_icon, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(now_icon, "");
  lv_obj_set_pos(now_icon, 0, kDetailIconY);
  lv_obj_add_flag(now_icon, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_temp = lv_label_create(chart_wrap);
  ctx->detail_now_temp_value_label = now_temp;
  set_label_style(now_temp, lv_color_white(), popup_layout::font20());
  lv_obj_set_style_text_align(now_temp, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_bg_color(now_temp, lv_color_hex(ctx->bg_color ? ctx->bg_color : 0x2A2A2A), 0);
  lv_obj_set_style_bg_opa(now_temp, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(now_temp, 6, 0);
  lv_obj_set_style_pad_left(now_temp, 4, 0);
  lv_obj_set_style_pad_right(now_temp, 0, 0);
  lv_obj_set_style_pad_top(now_temp, 1, 0);
  lv_obj_set_style_pad_bottom(now_temp, 1, 0);
  lv_label_set_text(now_temp, "");
  lv_obj_add_flag(now_temp, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_temp_unit = lv_label_create(chart_wrap);
  ctx->detail_now_temp_unit_label = now_temp_unit;
  set_label_style(now_temp_unit, lv_color_white(), weather_unit_font());
  lv_obj_set_style_text_align(now_temp_unit, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_bg_color(now_temp_unit, lv_color_hex(ctx->bg_color ? ctx->bg_color : 0x2A2A2A), 0);
  lv_obj_set_style_bg_opa(now_temp_unit, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(now_temp_unit, 6, 0);
  lv_obj_set_style_pad_left(now_temp_unit, 0, 0);
  lv_obj_set_style_pad_right(now_temp_unit, 4, 0);
  lv_obj_set_style_pad_top(now_temp_unit, 1, 0);
  lv_obj_set_style_pad_bottom(now_temp_unit, 1, 0);
  lv_label_set_text(now_temp_unit, "");
  lv_obj_add_flag(now_temp_unit, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_amount = lv_label_create(chart_wrap);
  ctx->detail_now_precip_amount_label = now_amount;
  set_label_style(now_amount, lv_color_white(), popup_layout::font20());
  lv_obj_set_style_text_align(now_amount, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(now_amount, "");
  lv_obj_set_pos(now_amount, 0, kDetailAmountTop);
  lv_obj_add_flag(now_amount, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_amount_unit = lv_label_create(chart_wrap);
  ctx->detail_now_precip_amount_unit_label = now_amount_unit;
  set_label_style(now_amount_unit, lv_color_white(), weather_unit_font());
  lv_obj_set_style_text_align(now_amount_unit, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(now_amount_unit, "");
  lv_obj_set_pos(now_amount_unit, 0, kDetailAmountTop);
  lv_obj_add_flag(now_amount_unit, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_probability = lv_label_create(chart_wrap);
  ctx->detail_now_probability_label = now_probability;
  set_label_style(now_probability, lv_color_white(), popup_layout::font20());
  lv_obj_set_style_text_align(now_probability, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(now_probability, "");
  lv_obj_set_pos(now_probability, 0, kDetailProbabilityTop);
  lv_obj_add_flag(now_probability, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* now_prob_unit = lv_label_create(chart_wrap);
  ctx->detail_now_probability_unit_label = now_prob_unit;
  set_label_style(now_prob_unit, lv_color_white(), weather_unit_font());
  lv_obj_set_style_text_align(now_prob_unit, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(now_prob_unit, "");
  lv_obj_set_pos(now_prob_unit, 0, kDetailProbabilityTop);
  lv_obj_add_flag(now_prob_unit, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* detail_precip_chart = lv_chart_create(chart_wrap);
  ctx->detail_precip_chart = detail_precip_chart;
  lv_obj_set_size(detail_precip_chart, LV_PCT(100), kDetailPrecipChartHeight);
  lv_obj_set_pos(detail_precip_chart, 0, kDetailPrecipChartTop);
  lv_obj_set_style_bg_opa(detail_precip_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(detail_precip_chart, 0, 0);
  lv_obj_set_style_pad_left(detail_precip_chart, kDetailChartLeftInset, 0);
  lv_obj_set_style_pad_right(detail_precip_chart, kDetailChartRightInset, 0);
  lv_obj_set_style_pad_top(detail_precip_chart, 0, 0);
  lv_obj_set_style_pad_bottom(detail_precip_chart, 0, 0);
  lv_obj_set_style_bg_color(detail_precip_chart, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(detail_precip_chart, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_radius(detail_precip_chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_line_width(detail_precip_chart, 0, LV_PART_ITEMS);
  lv_obj_set_style_size(detail_precip_chart, 4, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(detail_precip_chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(detail_precip_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(detail_precip_chart, LV_OBJ_FLAG_HIDDEN);
  lv_chart_set_div_line_count(detail_precip_chart, 0, 0);
  lv_chart_set_type(detail_precip_chart, LV_CHART_TYPE_BAR);
  lv_chart_set_update_mode(detail_precip_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(detail_precip_chart, kDetailChartPointCount);
  ctx->detail_precip_series =
      lv_chart_add_series(detail_precip_chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);

  for (int i = 0; i < kDetailChartPointCount; ++i) {
    lv_obj_t* bar = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, kDetailPrecipBarWidth, 0);
    lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    ctx->detail_precip_bars[i] = bar;
  }

  for (int i = 0; i < kDetailMarkerCount; ++i) {
    if (ctx->detail_time_labels[i]) lv_obj_move_foreground(ctx->detail_time_labels[i]);
    if (ctx->detail_icon_labels[i]) lv_obj_move_foreground(ctx->detail_icon_labels[i]);
    if (ctx->detail_temp_point_dots[i]) lv_obj_move_foreground(ctx->detail_temp_point_dots[i]);
    if (ctx->detail_temp_value_labels[i]) lv_obj_move_foreground(ctx->detail_temp_value_labels[i]);
    if (ctx->detail_precip_amount_labels[i]) lv_obj_move_foreground(ctx->detail_precip_amount_labels[i]);
    if (ctx->detail_precip_amount_unit_labels[i]) lv_obj_move_foreground(ctx->detail_precip_amount_unit_labels[i]);
    if (ctx->detail_probability_labels[i]) lv_obj_move_foreground(ctx->detail_probability_labels[i]);
    if (ctx->detail_probability_unit_labels[i]) lv_obj_move_foreground(ctx->detail_probability_unit_labels[i]);
  }
  if (ctx->detail_now_time_label) lv_obj_move_foreground(ctx->detail_now_time_label);
  if (ctx->detail_now_icon_label) lv_obj_move_foreground(ctx->detail_now_icon_label);
  if (ctx->detail_now_temp_value_label) lv_obj_move_foreground(ctx->detail_now_temp_value_label);
  if (ctx->detail_now_precip_amount_label) lv_obj_move_foreground(ctx->detail_now_precip_amount_label);
  if (ctx->detail_now_precip_amount_unit_label) lv_obj_move_foreground(ctx->detail_now_precip_amount_unit_label);
  if (ctx->detail_now_probability_label) lv_obj_move_foreground(ctx->detail_now_probability_label);
  if (ctx->detail_now_probability_unit_label) lv_obj_move_foreground(ctx->detail_now_probability_unit_label);
  if (ctx->detail_past_overlay) lv_obj_move_foreground(ctx->detail_past_overlay);

  clear_forecast_chart(ctx);
  clear_detail_view(ctx);
  show_week_view(ctx);

  apply_init_to_context(ctx, init);

  lv_obj_move_foreground(icon);
  lv_obj_move_foreground(location);
  lv_obj_move_foreground(mode_row);
  lv_obj_move_foreground(value_row);
  if (ctx->week_range_pill) lv_obj_move_foreground(ctx->week_range_pill);
  if (ctx->header_today_btn) lv_obj_move_foreground(ctx->header_today_btn);
  if (ctx->header_week_btn) lv_obj_move_foreground(ctx->header_week_btn);
  if (ctx->detail_title_pill) lv_obj_move_foreground(ctx->detail_title_pill);
  if (ctx->detail_prev_btn) lv_obj_move_foreground(ctx->detail_prev_btn);
  if (ctx->detail_next_btn) lv_obj_move_foreground(ctx->detail_next_btn);
  lv_obj_move_foreground(close_btn);
  lv_obj_add_event_cb(overlay, on_overlay_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);

  ctx->detail_title_timer = lv_timer_create(detail_title_tick_cb, 10000, ctx);
}

}  // namespace

static bool weather_popup_rendered_payload_matches(
    const char* entity_id, uint32_t cached_hash, size_t cached_length) {
  if (!entity_id || !*entity_id || !g_weather_popup_ctx ||
      !g_weather_popup_ctx->overlay || !g_weather_popup_ctx->card ||
      !g_weather_popup_ctx->has_rendered_data) {
    return false;
  }

  return g_weather_popup_ctx->rendered_entity_id.equalsIgnoreCase(entity_id) &&
         g_weather_popup_ctx->rendered_payload_hash == cached_hash &&
         g_weather_popup_ctx->rendered_payload_length == cached_length &&
         g_weather_popup_ctx->rendered_language.equalsIgnoreCase(
             i18n::normalize_language_code(configManager.getConfig().language));
}

bool weather_popup_has_current_cached_payload(const char* entity_id) {
  uint32_t cached_hash = 0;
  size_t cached_length = 0;
  return entity_id && *entity_id &&
         tiles_get_cached_entity_payload_signature(
             entity_id, cached_hash, cached_length) &&
         weather_popup_rendered_payload_matches(
             entity_id, cached_hash, cached_length);
}

static void finish_weather_popup_open() {
  if (g_weather_popup_ctx && popup_open_pending(g_weather_popup_ctx->card)) return;
  if (!g_weather_open_pending ||
      !g_weather_popup_ctx || !is_popup_visible(g_weather_popup_ctx)) return;
  WeatherPopupInit init = std::move(g_pending_weather_init);
  g_pending_weather_init = WeatherPopupInit{};
  g_weather_open_pending = false;
  g_weather_body.restore();

  const bool keep_pending_parse =
      (g_pending_weather.valid &&
       g_pending_weather.entity_id.equalsIgnoreCase(init.entity_id)) ||
      (g_pending_weather.parse_hourly_pending &&
       g_pending_weather.build_entity_id.equalsIgnoreCase(init.entity_id));
  const bool keep_pending_build =
      g_pending_weather.build_ui_pending &&
      g_pending_weather.build_entity_id.equalsIgnoreCase(init.entity_id);
  const bool matching_refresh_pending = keep_pending_parse || keep_pending_build;

  bool same_rendered_entity = false;
  if (g_weather_popup_ctx && g_weather_popup_ctx->overlay && g_weather_popup_ctx->card) {
    same_rendered_entity =
        g_weather_popup_ctx->has_rendered_data &&
        g_weather_popup_ctx->rendered_entity_id.equalsIgnoreCase(init.entity_id);
    if (!same_rendered_entity) {
      // Clear the many child widgets while their parent is hidden. This avoids
      // exposing stale data from another entity and suppresses invalidation.
      lv_obj_add_flag(g_weather_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
      if (!matching_refresh_pending) {
        reset_weather_popup_content(g_weather_popup_ctx);
        g_weather_popup_ctx->has_rendered_data = false;
        g_weather_popup_ctx->rendered_entity_id.remove(0);
        g_weather_popup_ctx->rendered_payload_hash = 0;
        g_weather_popup_ctx->rendered_payload_length = 0;
        g_weather_popup_ctx->rendered_language.remove(0);
      }
    }
    apply_init_to_context(g_weather_popup_ctx, init);
    if (!same_rendered_entity && matching_refresh_pending) {
      hide_weather_model_widgets(g_weather_popup_ctx);
    }
    lv_obj_clear_flag(g_weather_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_weather_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  } else {
    WeatherPopupContext* ctx = new WeatherPopupContext();
    g_weather_popup_ctx = ctx;
    build_popup_ui(ctx, init);
  }

  if (!matching_refresh_pending) {
    reset_pending_weather_update();
  }
  g_pending_weather.pending_day_nav = -1;

  // Apply header immediately (icon, condition, temp) so popup appears populated
  uint32_t cached_hash = 0;
  size_t cached_length = 0;
  if (tiles_get_cached_entity_payload_signature(
          init.entity_id.c_str(), cached_hash, cached_length)) {
    const bool rendered_payload_is_current =
        same_rendered_entity &&
        weather_popup_rendered_payload_matches(
            init.entity_id.c_str(), cached_hash, cached_length);
    if (rendered_payload_is_current) {
      Serial.printf("[WeatherPopup] Using cached payload: %s (%u bytes)\n",
                    init.entity_id.c_str(),
                    static_cast<unsigned>(cached_length));
    } else {
      String cached;
      if (!tiles_get_cached_entity_payload(init.entity_id.c_str(), cached)) {
        request_weather_for_context(g_weather_popup_ctx);
        return;
      }
      if (matching_refresh_pending) {
        // Parsing/building may have started while the popup was still hidden.
        // Keep that work instead of discarding it and parsing the same payload
        // again after an early user tap.
        apply_weather_header(g_weather_popup_ctx, cached);
        Serial.printf("[WeatherPopup] Continuing in-flight preparation: %s\n",
                      init.entity_id.c_str());
      } else {
        apply_weather_header(g_weather_popup_ctx, cached);
        // Keep the rendered snapshot visible until fresher content is ready.
        queue_weather_popup_payload(init.entity_id.c_str(), cached.c_str());
      }
    }
  } else {
    // No cache — request fresh data via MQTT
    request_weather_for_context(g_weather_popup_ctx);
  }
}

void show_weather_popup(const WeatherPopupInit& init) {
  hide_pin_popup();
  hide_camera_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;
  hide_light_popup();
  hide_sensor_popup();
  hide_energy_popup();
  hide_media_popup();
  if (!g_weather_popup_ctx) {
    g_weather_popup_ctx = new WeatherPopupContext();
    build_popup_ui(g_weather_popup_ctx, init);
  }
  auto* ctx = g_weather_popup_ctx;
  if (!ctx->has_rendered_data || !ctx->rendered_entity_id.equalsIgnoreCase(init.entity_id))
    g_weather_body.hide(ctx->card, ctx->location_label, ctx->icon_label, ctx->close_button);
  if (!ctx->entity_id.equalsIgnoreCase(init.entity_id)) {
    lv_label_set_text(ctx->icon_label, "");
    lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  }
  ctx->entity_id = init.entity_id;
  ctx->title = init.title;
  ctx->bg_color = init.bg_color;
  lv_obj_set_style_bg_color(ctx->card, lv_color_hex(init.bg_color ? init.bg_color : 0x2A2A2A), 0);
  hometiles_title::set(ctx->location_label, init.title.c_str());
  popup_layout::alignHeader(ctx->card, ctx->location_label, ctx->icon_label);
  g_pending_weather_init = init;
  g_weather_open_pending = true;
  defer_popup_content(g_weather_popup_ctx->card, finish_weather_popup_open);
  lv_obj_clear_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_invalidate(ctx->card);
  viewNavigationPopupShown(ctx->card, init.entity_id.c_str());
  show_popup_shell(g_weather_popup_ctx->overlay, g_weather_popup_ctx->card, g_weather_popup_ctx->location_label, g_weather_popup_ctx->icon_label, g_weather_popup_ctx->close_button);
}

void preload_weather_popup() {
  if (g_weather_popup_ctx && g_weather_popup_ctx->overlay && g_weather_popup_ctx->card) return;
  WeatherPopupInit init;
  init.entity_id = "__preload__";
  init.title = "";
  init.bg_color = 0;
  show_weather_popup(init);
  hide_weather_popup();
}

void hide_weather_popup() {

  g_weather_open_pending = false;
  g_pending_weather_init = WeatherPopupInit{};
  g_weather_body.restore();
  if (!g_weather_popup_ctx || !g_weather_popup_ctx->card || !g_weather_popup_ctx->overlay) return;
  hide_popup_shell(g_weather_popup_ctx->card);
  cancel_popup_open(g_weather_popup_ctx->card);
  lv_obj_add_flag(g_weather_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_weather_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void queue_weather_popup_payload(const char* entity_id, const char* payload) {
  if (!entity_id || !*entity_id || !payload) return;

  // A visible popup only consumes updates for its own entity. Other weather
  // tiles keep their normal tile cache without replacing in-flight popup work.
  if (g_weather_popup_ctx && is_popup_visible(g_weather_popup_ctx) &&
      !g_weather_popup_ctx->entity_id.equalsIgnoreCase(entity_id)) {
    return;
  }

  size_t payload_length = 0;
  const uint32_t payload_hash = hash_weather_payload(payload, &payload_length);
  if (g_weather_popup_ctx &&
      g_weather_popup_ctx->has_rendered_data &&
      g_weather_popup_ctx->rendered_entity_id.equalsIgnoreCase(entity_id) &&
      g_weather_popup_ctx->rendered_payload_hash == payload_hash &&
      g_weather_popup_ctx->rendered_payload_length == payload_length &&
      g_weather_popup_ctx->rendered_language.equalsIgnoreCase(
          i18n::normalize_language_code(configManager.getConfig().language))) {
    return;
  }
  if (g_pending_weather.valid &&
      g_pending_weather.entity_id.equalsIgnoreCase(entity_id) &&
      g_pending_weather.payload_hash == payload_hash &&
      g_pending_weather.payload_length == payload_length) {
    return;
  }
  if ((g_pending_weather.parse_hourly_pending ||
       g_pending_weather.build_ui_pending) &&
      g_pending_weather.build_entity_id.equalsIgnoreCase(entity_id) &&
      g_pending_weather.build_payload_hash == payload_hash &&
      g_pending_weather.build_payload_length == payload_length) {
    return;
  }

  String active_work_entity;
  if (g_pending_weather.valid) {
    active_work_entity = g_pending_weather.entity_id;
  } else if (g_pending_weather.parse_hourly_pending ||
             g_pending_weather.build_ui_pending) {
    active_work_entity = g_pending_weather.build_entity_id;
  }
  if (active_work_entity.length() &&
      !active_work_entity.equalsIgnoreCase(entity_id)) {
    // View state belongs to one entity. Never carry a selected day or deferred
    // navigation from cancelled entity A into replacement entity B.
    g_pending_weather.previous_selected_date.remove(0);
    g_pending_weather.previous_mode = WeatherPopupViewMode::Week;
    g_pending_weather.previous_view_captured = false;
    g_pending_weather.pending_day_nav = -1;
  }

  // Only one popup model exists. A newer state for that entity supersedes any
  // unfinished parse/build and starts from a clean bounded work state.
  cancel_weather_refresh_work();
  g_pending_weather.entity_id = entity_id;
  g_pending_weather.payload = payload;
  g_pending_weather.payload_hash = payload_hash;
  g_pending_weather.payload_length = payload_length;
  g_pending_weather.valid = true;
}

void process_weather_popup_queue() {
  if (!g_weather_popup_ctx || !g_weather_popup_ctx->card) {
    reset_pending_weather_update();
    return;
  }
  if (PopupFirstFrame::any_pending()) return;
  if (g_weather_open_pending) {
    finish_weather_popup_open();
    return;
  }

  // The very first payload is prepared immediately, so even a user who opens
  // the weather tile right after startup can take over the in-flight work.
  // Later hidden refreshes wait for the touch performance window to end and
  // therefore cannot interrupt active operation elsewhere in the UI.
  if (!is_popup_visible(g_weather_popup_ctx) &&
      g_weather_popup_ctx->has_rendered_data &&
      powerManager.isHighPerformance()) {
    return;
  }

  // A newer state always supersedes unfinished work for this popup entity.
  if (g_pending_weather.valid) {
    g_pending_weather.valid = false;
    const bool visible = is_popup_visible(g_weather_popup_ctx);
    const bool entity_matches =
        g_weather_popup_ctx->entity_id.equalsIgnoreCase(g_pending_weather.entity_id);
    if (entity_matches || !visible) {
      if (g_weather_popup_ctx->has_rendered_data &&
          !g_weather_popup_ctx->rendered_entity_id.equalsIgnoreCase(
              g_pending_weather.entity_id)) {
        // Parsing another entity replaces the shared data model before its
        // widgets are rebuilt. Invalidate the old model identity immediately,
        // so reopening the old entity cannot reuse partially replaced data.
        g_weather_popup_ctx->has_rendered_data = false;
        g_weather_popup_ctx->rendered_entity_id.remove(0);
        g_weather_popup_ctx->rendered_payload_hash = 0;
        g_weather_popup_ctx->rendered_payload_length = 0;
        g_weather_popup_ctx->rendered_language.remove(0);
      }
      if (!entity_matches) {
        // The preloaded popup has no entity yet. While hidden it can adopt the
        // latest weather entity and be ready before the first tap.
        g_weather_popup_ctx->entity_id = g_pending_weather.entity_id;
        g_weather_popup_ctx->title.remove(0);
      }

      if (!g_pending_weather.previous_view_captured) {
        g_pending_weather.previous_selected_date.remove(0);
        if (g_weather_popup_ctx->has_rendered_data &&
            g_weather_popup_ctx->rendered_entity_id.equalsIgnoreCase(
                g_pending_weather.entity_id) &&
            g_weather_popup_ctx->selected_day_index >= 0 &&
            g_weather_popup_ctx->selected_day_index < kCols) {
          g_pending_weather.previous_selected_date =
              g_weather_popup_ctx
                  ->forecast_data[g_weather_popup_ctx->selected_day_index]
                  .date_local;
        }
        g_pending_weather.previous_mode =
            (g_weather_popup_ctx->has_rendered_data &&
             g_weather_popup_ctx->rendered_entity_id.equalsIgnoreCase(
                 g_pending_weather.entity_id))
                ? g_weather_popup_ctx->view_mode
                : WeatherPopupViewMode::Week;
        g_pending_weather.previous_view_captured = true;
      }

      g_pending_weather.build_entity_id = g_pending_weather.entity_id;
      g_pending_weather.build_payload_hash = g_pending_weather.payload_hash;
      g_pending_weather.build_payload_length = g_pending_weather.payload_length;
      g_pending_weather.parse_started_ms = millis();
      g_pending_weather.hourly_cursor = 0;
      g_pending_weather.hourly_objects_seen = 0;
      g_pending_weather.hourly_count = 0;
      parse_weather_base_data(g_weather_popup_ctx, g_pending_weather.payload,
                              g_pending_weather.hourly_cursor);

      g_pending_weather.parse_hourly_pending =
          g_pending_weather.hourly_cursor >= 0;
      if (!g_pending_weather.parse_hourly_pending ||
          parse_weather_hourly_batch(g_weather_popup_ctx)) {
        g_pending_weather.parse_hourly_pending = false;
        g_pending_weather.build_ui_pending = true;
        Serial.printf(
            "[WeatherPopup] Data parsed in %u ms, %u bytes (%s)\n",
            static_cast<unsigned>(millis() -
                                  g_pending_weather.parse_started_ms),
            static_cast<unsigned>(g_pending_weather.build_payload_length),
            visible ? "visible" : "hidden");
      }
    } else {
      // An update for another weather tile must not replace the popup the user
      // currently has open.
      g_pending_weather.payload.remove(0);
      cancel_weather_refresh_work();
      g_pending_weather.previous_view_captured = false;
    }
    return;
  }

  // Phase 1b: S3 parses only a small number of hourly objects per UI cycle.
  // Other profiles have no per-cycle input limit and therefore retain their
  // previous one-pass parse behavior.
  if (g_pending_weather.parse_hourly_pending) {
    if (parse_weather_hourly_batch(g_weather_popup_ctx)) {
      g_pending_weather.parse_hourly_pending = false;
      g_pending_weather.build_ui_pending = true;
      Serial.printf(
          "[WeatherPopup] Data parsed in %u ms, %u bytes (%s)\n",
          static_cast<unsigned>(millis() -
                                g_pending_weather.parse_started_ms),
          static_cast<unsigned>(g_pending_weather.build_payload_length),
          is_popup_visible(g_weather_popup_ctx) ? "visible" : "hidden");
    }
    return;
  }

  // Phase 2: Build LVGL objects only after the complete model is available.
  if (g_pending_weather.build_ui_pending) {
    g_pending_weather.build_ui_pending = false;
    if (g_weather_popup_ctx->entity_id.equalsIgnoreCase(
            g_pending_weather.build_entity_id)) {
      const uint32_t started_ms = millis();
      // Parsing can span several S3 frames. Reapply the lightweight header at
      // completion so a language change during that interval is reflected in
      // the header and the graph before rendered_language is committed.
      apply_weather_header(g_weather_popup_ctx, g_pending_weather.payload);
      const WeatherPopupViewMode build_mode =
          g_pending_weather.pending_day_nav >= 0
              ? WeatherPopupViewMode::Week
              : g_pending_weather.previous_mode;
      build_weather_ui(g_weather_popup_ctx,
                       g_pending_weather.previous_selected_date, build_mode);
      g_weather_popup_ctx->rendered_entity_id =
          g_pending_weather.build_entity_id;
      g_weather_popup_ctx->rendered_payload_hash =
          g_pending_weather.build_payload_hash;
      g_weather_popup_ctx->rendered_payload_length =
          g_pending_weather.build_payload_length;
      g_weather_popup_ctx->has_rendered_data = true;
      g_weather_popup_ctx->rendered_language =
          i18n::normalize_language_code(configManager.getConfig().language);
      Serial.printf("[WeatherPopup] UI built in %u ms (%s)\n",
                    static_cast<unsigned>(millis() - started_ms),
                    is_popup_visible(g_weather_popup_ctx) ? "visible"
                                                          : "hidden");
    }
    g_pending_weather.build_entity_id.remove(0);
    g_pending_weather.build_payload_hash = 0;
    g_pending_weather.build_payload_length = 0;
    g_pending_weather.payload.remove(0);
    g_pending_weather.parse_started_ms = 0;
    g_pending_weather.previous_selected_date.remove(0);
    g_pending_weather.previous_mode = WeatherPopupViewMode::Week;
    g_pending_weather.previous_view_captured = false;
    return;
  }

  // Day navigation is deferred until parse and graph construction are done,
  // so callbacks never read a partially replaced hourly model.
  if (g_pending_weather.pending_day_nav >= 0) {
    const int target_day = g_pending_weather.pending_day_nav;
    g_pending_weather.pending_day_nav = -1;
    if (is_popup_visible(g_weather_popup_ctx)) {
      show_day_view(g_weather_popup_ctx, target_day);
    }
  }
}

void weather_popup_refresh_language() {
  if (!g_weather_popup_ctx || !g_weather_popup_ctx->card ||
      !g_weather_popup_ctx->has_rendered_data) {
    return;
  }
  if (weather_refresh_in_progress(g_weather_popup_ctx)) return;

  const char* language =
      i18n::normalize_language_code(configManager.getConfig().language);
  if (g_weather_popup_ctx->rendered_language.equalsIgnoreCase(language)) {
    return;
  }

  String selected_date;
  if (g_weather_popup_ctx->selected_day_index >= 0 &&
      g_weather_popup_ctx->selected_day_index < kCols) {
    selected_date =
        g_weather_popup_ctx
            ->forecast_data[g_weather_popup_ctx->selected_day_index]
            .date_local;
  }
  const WeatherPopupViewMode previous_mode =
      g_weather_popup_ctx->view_mode;

  String cached;
  if (tiles_get_cached_entity_payload(
          g_weather_popup_ctx->entity_id.c_str(), cached)) {
    apply_weather_header(g_weather_popup_ctx, cached);
  }
  build_weather_ui(g_weather_popup_ctx, selected_date, previous_mode);
  g_weather_popup_ctx->rendered_language = language;
  lv_obj_invalidate(g_weather_popup_ctx->card);
}
