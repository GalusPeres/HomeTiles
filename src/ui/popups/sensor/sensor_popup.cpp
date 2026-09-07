#include "src/types/value/value_control.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/io/hardware_io.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/config/tile_config.h"
#include "src/types/clock/clock_format.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <math.h>
#include <mbedtls/sha256.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

namespace {

constexpr int kCardMargin = popup_layout::kCardMargin;
constexpr int kCardWidth =
    (SCREEN_WIDTH > SCREEN_HEIGHT)
        ? (SCREEN_HEIGHT - (kCardMargin * 2))
        : (SCREEN_WIDTH - (kCardMargin * 2));
constexpr int kCardHeight = SCREEN_HEIGHT - (kCardMargin * 2);
constexpr int kCardPad = popup_layout::kCardPad;
constexpr int kHeaderPadTop = popup_layout::scale(4);
constexpr int kHeaderIconOffsetX = popup_layout::scale(4);
constexpr int kHeaderIconOffsetY = -popup_layout::scale(8);
constexpr int kChartHeight = popup_layout::contentScale(325);
#if defined(DEVICE_LAYOUT_1024X600)
constexpr int kTimeAxisHeight = 24;  // full UI16 line height plus bottom breathing room
#elif defined(DEVICE_LAYOUT_480X480)
constexpr int kTimeAxisHeight = 20;  // full native UI14 line plus breathing room
#else
constexpr int kTimeAxisHeight = popup_layout::scale(20);  // space for time labels below chart
#endif
constexpr int kTimeAxisMarkerCount = 8;
constexpr int kChartLineWidth = popup_layout::scale(4);
constexpr uint16_t kHistoryHours24h = 24;
constexpr uint16_t kHistoryPeriodMinutes24h = 5;
constexpr uint16_t kHistoryPoints24h = 288;
constexpr uint16_t kHistoryHours7d = 168;
constexpr uint16_t kHistoryPeriodMinutes7d = 60;
constexpr uint16_t kHistoryPoints7d = 168;
constexpr int kRangeButtonWidth = popup_layout::scale(92);
constexpr int kRangeButtonHeight = popup_layout::kNavHeight;
constexpr int kRangeButtonGap = popup_layout::scale(10);
constexpr size_t kBinaryMaxSegments = 96;
constexpr size_t kBinaryMaxTimelineBins = 768;
constexpr size_t kBinaryMaxActivityEntries = 96;
constexpr size_t kStateHistoryMaxPaletteEntries = 16;
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kBinaryTimelineHeight = 22;
constexpr int kBinaryActivityRowHeight = 42;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kBinaryTimelineHeight = 24;
constexpr int kBinaryActivityRowHeight = 42;
#else
constexpr int kBinaryTimelineHeight = 30;
constexpr int kBinaryActivityRowHeight = 50;
#endif
// Bound the reusable rows by the available body, including one partial row
// during scrolling. Taller viewports must not stop rendering after five rows.
constexpr int kBinaryActivityViewportLimit = std::max(
    popup_layout::kBodyHeight,
    popup_layout::kNavY - popup_layout::kValueY - popup_layout::kValueHeight -
        2 * popup_layout::kCardPad);
constexpr int kBinaryActivityPoolRows =
    (kBinaryActivityViewportLimit + kBinaryActivityRowHeight - 1) /
        kBinaryActivityRowHeight + 1;
constexpr uint32_t kBinaryActiveColor = 0xFFC107;
constexpr uint32_t kBinaryInactiveColor = 0x8B8E96;
constexpr uint32_t kBinaryUnknownColor = 0x555861;
constexpr uint32_t kBinaryUnavailableColor = 0x34363C;
constexpr uint32_t kStateHistoryRefreshMinMs24h = 60000;
constexpr uint32_t kStateHistoryRefreshMinMs7d = 300000;
constexpr uint32_t kStateHistoryColors[] = {
    0x4C74D9, 0xF6C148, 0x43A047, 0xAB47BC,
    0x26A69A, 0xEF6C63, 0x5C6BC0, 0xEC8F3A,
    0x66BB6A, 0x8D6E63, 0x29B6F6, 0xD45A92,
};
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kContentLiftY = 6;
#else
constexpr int kContentLiftY = 0;
#endif

enum class SensorHistoryRange : uint8_t {
  Day24,
  Day7,
};

struct HistoryRangeConfig {
  uint16_t hours;
  uint16_t period_minutes;
  uint16_t points;
};

struct SensorPopupContext {
  String entity_id;
  String unit;
  bool lock_unit = false;
  uint8_t decimals = 0xFF;
  uint32_t bg_color = 0;
  SensorHistoryRange history_range = SensorHistoryRange::Day24;
  SensorHistoryRange editable_requested_range = SensorHistoryRange::Day24;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* value_label = nullptr;
  lv_obj_t* value_box = nullptr;
  lv_obj_t* control_row = nullptr;
  EditableControl* control = nullptr;
  bool editable = false;
  String editable_kind, editable_state, editable_history_id;
  uint32_t editable_generation = 0;
  bool editable_available = false;
  lv_obj_t* body_box = nullptr;
  lv_obj_t* range_row = nullptr;
  lv_obj_t* range_day_btn = nullptr;
  lv_obj_t* range_week_btn = nullptr;
  lv_obj_t* chart = nullptr;
  int chart_height = kChartHeight;
  lv_chart_series_t* series = nullptr;
  lv_obj_t* y_max_label = nullptr;
  lv_obj_t* y_min_label = nullptr;
  lv_obj_t* y_max_line = nullptr;
  lv_obj_t* y_min_line = nullptr;
  lv_obj_t* time_labels[kTimeAxisMarkerCount] = {};
  lv_obj_t* time_lines[kTimeAxisMarkerCount] = {};
  lv_obj_t* chart_wrap = nullptr;
  uint16_t point_count = kHistoryPoints24h;
  bool binary_mode = false;
  bool state_history_mode = false;
  bool binary_available = true;
  bool binary_icon_override = false;
  String binary_state;
  String state_history_value;
  String binary_device_class;
  uint64_t binary_last_changed = 0;
  lv_obj_t* binary_body = nullptr;
  lv_obj_t* binary_history_title = nullptr;
  lv_obj_t* binary_timeline = nullptr;
  lv_obj_t* binary_history_status = nullptr;
  lv_obj_t* binary_activity_title = nullptr;
  lv_obj_t* binary_activity_date = nullptr;
  lv_obj_t* binary_activity_status = nullptr;
  lv_obj_t* binary_activity_viewport = nullptr;
  lv_obj_t* binary_activity_spacer = nullptr;
  lv_obj_t* binary_activity_rows[kBinaryActivityPoolRows] = {};
  lv_obj_t* binary_activity_lines[kBinaryActivityPoolRows] = {};
  lv_obj_t* binary_activity_dots[kBinaryActivityPoolRows] = {};
  lv_obj_t* binary_activity_states[kBinaryActivityPoolRows] = {};
  lv_obj_t* binary_activity_times[kBinaryActivityPoolRows] = {};
  lv_obj_t* binary_activity_dividers[kBinaryActivityPoolRows] = {};
  lv_obj_t* binary_time_labels[kTimeAxisMarkerCount] = {};
  uint64_t binary_range_start = 0;
  uint64_t binary_range_end = 0;
  uint32_t state_history_last_request_ms = 0;
  bool state_history_refresh_pending = false;
  struct BinarySegment {
    uint64_t start = 0;
    uint64_t end = 0;
    uint8_t state = 2;
    String value;
  };
  std::vector<BinarySegment> binary_segments;
  std::vector<uint8_t> binary_timeline_bins;
  std::vector<String> state_history_palette;
  bool state_history_palette_complete = true;
  struct BinaryActivityEntry {
    uint64_t timestamp = 0;
    uint8_t state = 2;
    String value;
  };
  std::vector<BinaryActivityEntry> binary_activity;
  size_t binary_activity_first_row = kBinaryMaxActivityEntries;
  size_t binary_activity_row_indices[kBinaryActivityPoolRows] = {};
  uint32_t binary_activity_date_key = 0;
};

struct PendingValueUpdate {
  String entity_id;
  String value;
  String unit;
  uint8_t decimals = 0xFF;
  bool valid = false;
};

struct PendingHistoryUpdate {
  String entity_id;
  String payload;
  bool valid = false;
};

struct PendingBinaryStateUpdate {
  String entity_id;
  String state;
  String device_class;
  String icon_name;
  uint64_t last_changed = 0;
  bool available = true;
  bool valid = false;
};

static SensorPopupContext* g_sensor_popup_ctx = nullptr;
static PendingValueUpdate g_pending_value;
static PendingHistoryUpdate g_pending_history;
static PendingBinaryStateUpdate g_pending_binary_state;
static bool g_pending_icon_refresh = false;

static void ensure_binary_view(SensorPopupContext* ctx);
static void layout_editable_history(SensorPopupContext* ctx);
static void clear_binary_history(SensorPopupContext* ctx);
static void refresh_binary_activity_rows(SensorPopupContext* ctx,
                                         bool force = false);
static void update_binary_state(SensorPopupContext* ctx,
                                const String& state,
                                bool available,
                                const String& device_class,
                                uint64_t last_changed,
                                const String& icon_name);
static String normalize_state_live_value(const String& value);
static String normalize_state_history_value(const String& value);

static HistoryRangeConfig get_history_range_config(SensorHistoryRange range) {
  switch (range) {
    case SensorHistoryRange::Day7:
      return {kHistoryHours7d, kHistoryPeriodMinutes7d, kHistoryPoints7d};
    case SensorHistoryRange::Day24:
    default:
      return {kHistoryHours24h, kHistoryPeriodMinutes24h, kHistoryPoints24h};
  }
}

static const char* get_weekday_abbrev(uint8_t wday) {
  if (wday > 6) wday = 0;
  return i18n::locale(
             configManager.getConfig().language).weather_weekdays_short[wday];
}

static const lv_font_t* get_value_font() {
#if defined(DEVICE_LAYOUT_480X480)
  return &ui_font_24;
#else
  return popup_layout::font32();
#endif
}

static void set_label_style(lv_obj_t* lbl, lv_color_t color, const lv_font_t* font) {
  if (!lbl) return;
  lv_obj_set_style_text_color(lbl, color, 0);
  if (font) {
    lv_obj_set_style_text_font(lbl, font, 0);
  }
}

static bool is_popup_visible(SensorPopupContext* ctx) {
  if (!ctx || !ctx->card) return false;
  return !lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
}

static bool apply_decimals(String& value, uint8_t decimals) {
  const char* language = configManager.getConfig().language;
  if (decimals == 0xFF) {
    String localized = i18n::localize_numeric_text(language, value);
    const bool changed = localized != value;
    value = localized;
    return changed;
  }
  String normalized = value;
  normalized.replace(",", ".");
  char* end = nullptr;
  float f = strtof(normalized.c_str(), &end);
  if (!end || end == normalized.c_str()) return false;
  if (isnan(f) || isinf(f)) return false;
  uint8_t d = decimals > 6 ? 6 : decimals;
  value = i18n::format_number(language, f, d);
  return true;
}

static void update_value_label(SensorPopupContext* ctx, const String& value, const String& unit) {
  if (!ctx || !ctx->value_label) return;
  const bool categorical_state = ctx->state_history_mode && !ctx->binary_mode;
  String display = categorical_state ? normalize_state_live_value(value)
                                     : value;
  if (!categorical_state) display.trim();
  String lower = display;
  lower.toLowerCase();
  if (lower == "unavailable" || lower == "unknown" || lower == "none" || lower == "null") {
    display = "--";
  }
  if (display.isEmpty()) {
    display = "--";
  }
  if (!categorical_state && display.length() > 0 && display != "--" &&
      !display.equalsIgnoreCase("unavailable")) {
    apply_decimals(display, ctx->decimals);
  }
  String display_unit = unit;
  display_unit.trim();
  if (!display_unit.isEmpty() && display != "--") {
    display += " ";
    display += display_unit;
  }
  lv_label_set_text(ctx->value_label, display.c_str());
  ctx->unit = display_unit;
}

static void style_range_button(lv_obj_t* btn, bool active) {
  if (!btn) return;
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

  lv_obj_t* label = lv_obj_get_child(btn, 0);
  if (label) {
    lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
    lv_obj_set_style_text_font(label, popup_layout::font24(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), 0);
    lv_obj_set_style_text_color(label, active ? active_text_color : lv_color_white(), LV_STATE_PRESSED);
  }
}

static void update_range_buttons(SensorPopupContext* ctx) {
  if (!ctx) return;
  const auto selected = ctx->editable ? ctx->editable_requested_range : ctx->history_range;
  style_range_button(ctx->range_day_btn, selected == SensorHistoryRange::Day24);
  style_range_button(ctx->range_week_btn, selected == SensorHistoryRange::Day7);
}

static void set_range_buttons_visible(SensorPopupContext* ctx, bool visible) {
  if (!ctx || !ctx->range_row) return;
  if (visible) {
    lv_obj_clear_flag(ctx->range_row, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ctx->range_row, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool extract_numeric(JsonVariant v, float& out) {
  if (v.isNull()) return false;

  if (v.is<const char*>()) {
    const char* text = v.as<const char*>();
    if (!text || !*text) return false;
    char* end = nullptr;
    out = strtof(text, &end);
    if (end == text) return false;
    return isfinite(out);
  }

  if (v.is<float>() || v.is<double>() || v.is<int>() || v.is<long>() ||
      v.is<unsigned long>() || v.is<long long>() || v.is<unsigned long long>()) {
    out = v.as<float>();
    return isfinite(out);
  }

  return false;
}

static void align_header_row(lv_obj_t* card, lv_obj_t* title_label, lv_obj_t* icon_label) {
  if (!card) return;
  lv_obj_update_layout(card);
  lv_coord_t header_center_y =
      popup_layout::kHeaderCenterY - lv_obj_get_style_pad_top(card, LV_PART_MAIN);
  if (header_center_y < 0) header_center_y = 0;
  if (icon_label) {
    lv_coord_t icon_y = header_center_y - (lv_obj_get_height(icon_label) / 2);
    if (icon_y < 0) icon_y = 0;
    lv_obj_align(icon_label, LV_ALIGN_TOP_LEFT,
                 popup_layout::kHeaderIconX, icon_y);
  }
  if (title_label) {
    lv_coord_t title_y = header_center_y - (lv_obj_get_height(title_label) / 2);
    if (title_y < 0) title_y = 0;
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT,
                 popup_layout::kHeaderTitleX, title_y);
  }
}

static void apply_init_to_context(SensorPopupContext* ctx, const SensorPopupInit& init) {
  if (!ctx) return;
  editable_control_close(ctx->control);
  ctx->editable = init.editable;
  ctx->editable_kind = init.editable ? parse_editable_value(haBridgeConfig.findEditableValue(init.entity_id)).kind : String();
  ctx->editable_state = init.value;
  ctx->editable_generation = 0;
  ctx->editable_history_id = "";
  ctx->editable_requested_range = SensorHistoryRange::Day24;
  ctx->entity_id = init.entity_id;
  ctx->lock_unit = init.lock_unit;
  ctx->decimals = init.decimals;
  ctx->bg_color = init.bg_color;
  ctx->binary_mode = init.binary_mode;
  ctx->state_history_mode = init.editable || init.binary_mode || init.state_history_mode;
  ctx->binary_available = init.binary_available;
  ctx->binary_icon_override = init.binary_icon_override;
  ctx->binary_device_class = init.binary_device_class;
  ctx->binary_last_changed = init.binary_last_changed;
  if (ctx->card) {
    uint32_t color = ctx->bg_color ? ctx->bg_color : 0x2A2A2A;
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(color), 0);
  }
  if (ctx->title_label) {
    hometiles_title::set(ctx->title_label, init.title.c_str());
  }
  if (ctx->icon_label) {
    String icon_name = init.icon_name;
    icon_name.trim();
    if (!icon_name.length() || isMdiIconDisabled(icon_name)) {
      lv_label_set_text(ctx->icon_label, "");
      lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      String icon_char = getMdiChar(icon_name);
      if (!icon_char.isEmpty()) {
        lv_label_set_text(ctx->icon_label, icon_char.c_str());
        lv_obj_clear_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(ctx->icon_label, "");
        lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  if (ctx->icon_label) {
    lv_obj_set_style_text_color(ctx->icon_label, lv_color_white(), 0);
  }
  align_header_row(ctx->card, ctx->title_label, ctx->icon_label);
  if (ctx->state_history_mode) {
    ensure_binary_view(ctx);
    if (ctx->chart_wrap) lv_obj_add_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    if (ctx->binary_body) lv_obj_clear_flag(ctx->binary_body, LV_OBJ_FLAG_HIDDEN);
    if (ctx->range_day_btn) {
      lv_obj_t* label = lv_obj_get_child(ctx->range_day_btn, 0);
      if (label) {
        lv_label_set_text(label, i18n::binary_sensor_label(
                                     configManager.getConfig().language, 6));
      }
    }
    if (ctx->range_week_btn) {
      lv_obj_t* label = lv_obj_get_child(ctx->range_week_btn, 0);
      if (label) {
        lv_label_set_text(label, i18n::binary_sensor_label(
                                     configManager.getConfig().language, 7));
      }
    }
    if (ctx->binary_mode) {
      update_binary_state(ctx, init.value, init.binary_available,
                          init.binary_device_class, init.binary_last_changed,
                          init.icon_name);
    } else {
      ctx->state_history_value = normalize_state_live_value(init.value);
      update_value_label(ctx, init.value, init.unit);
    }
  } else {
    if (ctx->chart_wrap) lv_obj_clear_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    if (ctx->binary_body) lv_obj_add_flag(ctx->binary_body, LV_OBJ_FLAG_HIDDEN);
    if (ctx->range_day_btn) {
      lv_obj_t* label = lv_obj_get_child(ctx->range_day_btn, 0);
      if (label) lv_label_set_text(label, "24H");
    }
    if (ctx->range_week_btn) {
      lv_obj_t* label = lv_obj_get_child(ctx->range_week_btn, 0);
      if (label) lv_label_set_text(label, "7D");
    }
    update_value_label(ctx, init.value, init.unit);
  }
  layout_editable_history(ctx);
  if (ctx->editable) {
    lv_obj_add_flag(ctx->value_box, LV_OBJ_FLAG_HIDDEN);
    editable_control_open(ctx->control, ctx->entity_id);
  } else {
    lv_obj_remove_flag(ctx->value_box, LV_OBJ_FLAG_HIDDEN);
  }
}

// Measure text without changing label geometry or forcing a screen-wide layout.
static lv_coord_t measure_label_text_width(lv_obj_t* label) {
  if (!label) return 0;
  const char* txt = lv_label_get_text(label);
  if (!txt || !*txt) return 0;
  lv_point_t size;
  lv_text_get_size(&size, txt, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                   lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                   lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x + lv_obj_get_style_pad_left(label, LV_PART_MAIN) +
         lv_obj_get_style_pad_right(label, LV_PART_MAIN);
}

static bool get_valid_local_time(struct tm& out) {
  if (!getLocalTime(&out, 0)) return false;
  const int year = out.tm_year + 1900;
  const int month = out.tm_mon + 1;
  const int day = out.tm_mday;
  if (year < 2024 || year > 2100) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > 31) return false;
  return true;
}

static String format_time_axis_label(int hour24) {
  int normalized = hour24 % 24;
  if (normalized < 0) normalized += 24;
  const DeviceConfig& cfg = configManager.getConfig();
  const uint8_t time_format =
      clock_tile::resolve_time_format(clock_tile::TIME_FORMAT_AUTO, cfg.global_time_format, cfg.language);
  if (time_format == clock_tile::TIME_FORMAT_24H) {
    return String(normalized) + i18n::locale(cfg.language).hour_axis_suffix;
  }

  int hour12 = normalized % 12;
  if (hour12 == 0) hour12 = 12;
  return String(hour12) + (normalized < 12 ? " AM" : " PM");
}

static String format_day_axis_label(time_t ts) {
  struct tm timeinfo;
#ifdef _WIN32
  localtime_s(&timeinfo, &ts);
#else
  localtime_r(&ts, &timeinfo);
#endif
  return String(get_weekday_abbrev(static_cast<uint8_t>(timeinfo.tm_wday)));
}

static bool get_day7_axis_base(time_t& now_local, time_t& range_start, struct tm& today_midnight_tm) {
  struct tm now_tm;
  if (!get_valid_local_time(now_tm)) return false;

  now_local = mktime(&now_tm);
  if (now_local <= 0) return false;

  range_start = now_local - static_cast<time_t>(kHistoryHours7d) * 60 * 60;
  today_midnight_tm = now_tm;
  today_midnight_tm.tm_hour = 0;
  today_midnight_tm.tm_min = 0;
  today_midnight_tm.tm_sec = 0;
  return true;
}

static time_t next_local_midnight_after(time_t ts) {
  struct tm timeinfo;
#ifdef _WIN32
  localtime_s(&timeinfo, &ts);
#else
  localtime_r(&ts, &timeinfo);
#endif
  timeinfo.tm_hour = 0;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  timeinfo.tm_mday += 1;
  return mktime(&timeinfo);
}

static time_t next_local_6h_boundary_after(time_t ts) {
  struct tm timeinfo;
#ifdef _WIN32
  localtime_s(&timeinfo, &ts);
#else
  localtime_r(&ts, &timeinfo);
#endif

  const int current_hour = timeinfo.tm_hour;
  int next_hour = ((current_hour / 6) + 1) * 6;
  if (next_hour >= 24) {
    next_hour -= 24;
    timeinfo.tm_mday += 1;
  }

  timeinfo.tm_hour = next_hour;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  return mktime(&timeinfo);
}

static int calc_day24_axis(String out_labels[], float out_frac[], int max_markers) {
  struct tm now_tm;
  if (!get_valid_local_time(now_tm)) return 0;

  time_t now_local = mktime(&now_tm);
  if (now_local <= 0) return 0;

  const time_t range_start = now_local - static_cast<time_t>(kHistoryHours24h) * 60 * 60;
  const float span = static_cast<float>(now_local - range_start);
  if (span <= 0.0f) return 0;

  int count = 0;
  time_t boundary = next_local_6h_boundary_after(range_start);
  while (boundary < now_local && count < max_markers) {
    struct tm boundary_tm;
#ifdef _WIN32
    localtime_s(&boundary_tm, &boundary);
#else
    localtime_r(&boundary, &boundary_tm);
#endif
    out_labels[count] = format_time_axis_label(boundary_tm.tm_hour);
    out_frac[count] = static_cast<float>(boundary - range_start) / span;
    ++count;
    boundary = next_local_6h_boundary_after(boundary);
  }

  return count;
}

static int calc_day7_label_axis(String out_labels[], float out_frac[], int max_markers) {
  time_t now_local = 0;
  time_t range_start = 0;
  struct tm today_midnight_tm;
  if (!get_day7_axis_base(now_local, range_start, today_midnight_tm)) return 0;

  const float span = static_cast<float>(now_local - range_start);
  if (span <= 0.0f) return 0;

  int count = 0;
  time_t segment_start = range_start;
  while (segment_start < now_local && count < max_markers) {
    time_t segment_end = next_local_midnight_after(segment_start);
    if (segment_end > now_local) segment_end = now_local;
    if (segment_end <= segment_start) break;

    const float center = static_cast<float>(segment_start - range_start) +
                         (static_cast<float>(segment_end - segment_start) * 0.5f);
    out_labels[count] = format_day_axis_label(segment_start);
    out_frac[count] = center / span;
    ++count;
    segment_start = segment_end;
  }

  return count;
}

static int calc_day7_boundary_axis(float out_frac[], int max_markers) {
  time_t now_local = 0;
  time_t range_start = 0;
  struct tm today_midnight_tm;
  if (!get_day7_axis_base(now_local, range_start, today_midnight_tm)) return 0;

  const float span = static_cast<float>(now_local - range_start);
  if (span <= 0.0f) return 0;

  int count = 0;
  time_t boundary = next_local_midnight_after(range_start);
  while (boundary < now_local && count < max_markers) {
    if (boundary > range_start) {
      out_frac[count] = static_cast<float>(boundary - range_start) / span;
      ++count;
    }
    boundary = next_local_midnight_after(boundary);
  }

  return count;
}

// Calculate time axis marker positions and labels.
static int calc_time_axis(const SensorPopupContext* ctx, String out_labels[], float out_frac[], int max_markers) {
  if (!ctx || max_markers < 1) return 0;

  if (ctx->history_range == SensorHistoryRange::Day7) {
    return calc_day7_label_axis(out_labels, out_frac, max_markers);
  }

  return calc_day24_axis(out_labels, out_frac, max_markers);
}

// Recalculate Y-axis layout based on actual label text widths.
// Measures max/min labels, repositions guide lines and chart padding.
static void update_y_axis_layout(SensorPopupContext* ctx) {
  if (!ctx || !ctx->chart || !ctx->chart_wrap) return;
  constexpr int kLineOverlap = 6;
  constexpr int kLabelGap = 16;   // gap between label right edge and guide line start
  constexpr int kMinAxisW = 10;   // minimum width even if labels empty
  constexpr int kTimeAxisEdgeGap = 6;
  const int kAvailW = kCardWidth - (kCardPad * 2);

  // Measure actual rendered text widths
  lv_coord_t max_w = measure_label_text_width(ctx->y_max_label);
  lv_coord_t min_w = measure_label_text_width(ctx->y_min_label);

  lv_coord_t text_w = LV_MAX(max_w, min_w);
  if (text_w < kMinAxisW) text_w = kMinAxisW;
  int label_w = text_w + 2;  // text width + small safety margin
  int axis_w = label_w + kLabelGap;  // label + visible gap before lines

  // Reposition labels; their width must fit the full text.
  if (ctx->y_max_label) {
    lv_obj_set_width(ctx->y_max_label, label_w);
  }
  if (ctx->y_min_label) {
    lv_obj_set_width(ctx->y_min_label, label_w);
  }

  // Chart drawing starts directly after label area
  int chart_left = axis_w;

  // Prepare time axis labels first so we can reserve enough room on the right.
  constexpr int kLabelOverhang = 12;

  String labels[kTimeAxisMarkerCount];
  float fracs[kTimeAxisMarkerCount];
  int n = calc_time_axis(ctx, labels, fracs, kTimeAxisMarkerCount);
  lv_coord_t max_time_label_w = 0;
  lv_coord_t time_label_widths[kTimeAxisMarkerCount] = {};

  for (int i = 0; i < kTimeAxisMarkerCount; ++i) {
    if (i < n && ctx->time_labels[i]) {
      lv_label_set_text(ctx->time_labels[i], labels[i].c_str());
      lv_coord_t lbl_w = measure_label_text_width(ctx->time_labels[i]);
      time_label_widths[i] = lbl_w;
      if (lbl_w > max_time_label_w) max_time_label_w = lbl_w;
    }
  }

  int chart_right_reserve = (max_time_label_w > 0) ? (max_time_label_w / 2 + kTimeAxisEdgeGap) : 0;

  // Reposition guide lines (keep Y unchanged, only adjust X and width)
  int line_start = axis_w - kLineOverlap;
  int line_w = kAvailW - line_start - chart_right_reserve;
  if (line_w < 10) line_w = 10;
  if (ctx->y_max_line) {
    lv_obj_set_width(ctx->y_max_line, line_w);
    lv_obj_set_x(ctx->y_max_line, line_start);
  }
  if (ctx->y_min_line) {
    lv_obj_set_width(ctx->y_min_line, line_w);
    lv_obj_set_x(ctx->y_min_line, line_start);
  }

  // Adjust chart padding so the graph itself ends slightly earlier on the right.
  lv_obj_set_style_pad_left(ctx->chart, chart_left, 0);
  lv_obj_set_style_pad_right(ctx->chart, chart_right_reserve, 0);

  // Reposition time axis markers based on the reduced chart area.
  int chart_draw_w = kAvailW - chart_left - chart_right_reserve;
  if (chart_draw_w < 10) return;

  const bool is_week_range = ctx->history_range == SensorHistoryRange::Day7;
  float week_boundary_fracs[kTimeAxisMarkerCount] = {};
  int week_boundary_count = 0;
  if (is_week_range) {
    week_boundary_count = calc_day7_boundary_axis(week_boundary_fracs, kTimeAxisMarkerCount);
  }

  for (int i = 0; i < kTimeAxisMarkerCount; ++i) {
    if (i < n) {
      int label_x_anchor = chart_left + static_cast<int>(fracs[i] * chart_draw_w);
      int line_x = label_x_anchor;
      if (is_week_range) {
        if (i < week_boundary_count) {
          line_x = chart_left + static_cast<int>(week_boundary_fracs[i] * chart_draw_w);
        }
      }
      if (ctx->time_lines[i]) {
        if (is_week_range && i >= week_boundary_count) {
          lv_obj_add_flag(ctx->time_lines[i], LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_set_x(ctx->time_lines[i], line_x);
          lv_obj_clear_flag(ctx->time_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
      }
      if (ctx->time_labels[i]) {
        lv_coord_t lbl_w = time_label_widths[i];
        int label_x = label_x_anchor - lbl_w / 2;
        int min_x = chart_left - (lbl_w / 2);
        if (min_x < 0) min_x = 0;
        int max_x = kAvailW - lbl_w;
        if (max_x < min_x) max_x = min_x;
        if (label_x < min_x) label_x = min_x;
        if (label_x > max_x) label_x = max_x;
        lv_obj_set_pos(
            ctx->time_labels[i], label_x,
            kLabelOverhang + ctx->chart_height + popup_layout::scale480(8));
        lv_obj_clear_flag(ctx->time_labels[i], LV_OBJ_FLAG_HIDDEN);
      }
    } else {
      if (ctx->time_lines[i]) lv_obj_add_flag(ctx->time_lines[i], LV_OBJ_FLAG_HIDDEN);
      if (ctx->time_labels[i]) lv_obj_add_flag(ctx->time_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void clear_chart(SensorPopupContext* ctx, uint16_t points) {
  if (!ctx || !ctx->chart || !ctx->series) return;
  ctx->point_count = points;
  lv_chart_set_point_count(ctx->chart, points);
  lv_chart_set_all_value(ctx->chart, ctx->series, LV_CHART_POINT_NONE);
  if (ctx->y_max_label) lv_label_set_text(ctx->y_max_label, "");
  if (ctx->y_min_label) lv_label_set_text(ctx->y_min_label, "");
  if (ctx->y_max_line) lv_obj_add_flag(ctx->y_max_line, LV_OBJ_FLAG_HIDDEN);
  if (ctx->y_min_line) lv_obj_add_flag(ctx->y_min_line, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < kTimeAxisMarkerCount; ++i) {
    if (ctx->time_lines[i]) lv_obj_add_flag(ctx->time_lines[i], LV_OBJ_FLAG_HIDDEN);
    if (ctx->time_labels[i]) lv_obj_add_flag(ctx->time_labels[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static uint64_t extract_epoch(JsonVariantConst value) {
  if (value.isNull()) return 0;
  if (value.is<uint64_t>()) return value.as<uint64_t>();
  if (value.is<unsigned long>()) return value.as<unsigned long>();
  if (value.is<long long>()) {
    const long long parsed = value.as<long long>();
    return parsed > 0 ? static_cast<uint64_t>(parsed) : 0;
  }
  if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    if (!text || !*text) return 0;
    char* end = nullptr;
    const unsigned long long parsed = strtoull(text, &end, 10);
    return end && end != text ? static_cast<uint64_t>(parsed) : 0;
  }
  return 0;
}

static uint8_t binary_state_code(const String& state_value,
                                 bool available = true) {
  String state = state_value;
  state.trim();
  state.toLowerCase();
  if (!state.length()) return 2;
  if (!available) return 3;
  if (state == "on") return 1;
  if (state == "off") return 0;
  if (state == "unavailable") return 3;
  return 2;
}

static uint32_t binary_state_color(uint8_t state) {
  switch (state) {
    case 1: return kBinaryActiveColor;
    case 0: return kBinaryInactiveColor;
    case 3: return kBinaryUnavailableColor;
    default: return kBinaryUnknownColor;
  }
}

static int binary_state_priority(uint8_t state) {
  switch (state) {
    case 1: return 4;
    case 3: return 3;
    case 2: return 2;
    default: return 1;
  }
}

static const char* binary_state_identifier(uint8_t state) {
  switch (state) {
    case 1: return "on";
    case 0: return "off";
    case 3: return "unavailable";
    default: return "unknown";
  }
}

static size_t bounded_utf8_prefix_length(const String& value,
                                         size_t max_bytes) {
  size_t offset = 0;
  const size_t length = value.length();
  while (offset < length && offset < max_bytes) {
    const uint8_t lead = static_cast<uint8_t>(value.charAt(offset));
    size_t sequence_length = 1;
    if ((lead & 0xE0U) == 0xC0U) {
      sequence_length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      sequence_length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      sequence_length = 4;
    }
    if (offset + sequence_length > length ||
        offset + sequence_length > max_bytes) {
      break;
    }
    offset += sequence_length;
  }
  return offset;
}

static String normalize_state_live_value(const String& value) {
  String normalized = value;
  normalized.trim();
  constexpr size_t kMaxLiveStateBytes = 255;
  if (normalized.length() > kMaxLiveStateBytes) {
    normalized.remove(
        bounded_utf8_prefix_length(normalized, kMaxLiveStateBytes));
  }
  return normalized;
}

static String normalize_state_history_value(const String& value) {
  String normalized = normalize_state_live_value(value);
  if (!normalized.length()) return "unknown";
  if (normalized.equalsIgnoreCase("unknown")) return "unknown";
  if (normalized.equalsIgnoreCase("unavailable")) return "unavailable";

  constexpr size_t kMaxHistoryStateBytes = 32;
  constexpr size_t kDigestHexChars = 8;
  constexpr size_t kSuffixBytes = 1 + kDigestHexChars;
  if (normalized.length() <= kMaxHistoryStateBytes) return normalized;

  uint8_t digest[32] = {};
  if (mbedtls_sha256(
          reinterpret_cast<const uint8_t*>(normalized.c_str()),
          normalized.length(), digest, 0) != 0) {
    return "unknown";
  }
  const size_t prefix_length = bounded_utf8_prefix_length(
      normalized, kMaxHistoryStateBytes - kSuffixBytes);
  normalized.remove(prefix_length);
  char suffix[kSuffixBytes + 1] = {};
  snprintf(suffix, sizeof(suffix), "~%02x%02x%02x%02x", digest[0], digest[1],
           digest[2], digest[3]);
  normalized += suffix;
  return normalized;
}

static String format_state_history_label(const String& value) {
  String label = normalize_state_history_value(value);
  if (!label.length()) return "--";
  if (label.equalsIgnoreCase("unknown") ||
      label.equalsIgnoreCase("unavailable")) {
    return i18n::binary_sensor_state_label(
        configManager.getConfig().language, label, "");
  }
  label.replace("_", " ");
  const char first = label.charAt(0);
  if (first >= 'a' && first <= 'z') {
    label.setCharAt(0, static_cast<char>(first - ('a' - 'A')));
  }
  return label;
}

static uint32_t state_history_color(const SensorPopupContext* ctx,
                                    const String& value) {
  (void)ctx;
  const String normalized = normalize_state_history_value(value);
  if (normalized.equalsIgnoreCase("unavailable")) {
    return kBinaryUnavailableColor;
  }
  if (!normalized.length() || normalized.equalsIgnoreCase("unknown")) {
    return kBinaryUnknownColor;
  }
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < normalized.length(); ++index) {
    hash ^= static_cast<uint8_t>(normalized.charAt(index));
    hash *= 16777619U;
  }
  return kStateHistoryColors[
      hash % (sizeof(kStateHistoryColors) / sizeof(kStateHistoryColors[0]))];
}

static uint32_t local_date_key(uint64_t epoch_seconds, struct tm* out = nullptr) {
  if (!epoch_seconds) return 0;
  const time_t timestamp = static_cast<time_t>(epoch_seconds);
  struct tm timeinfo;
#ifdef _WIN32
  localtime_s(&timeinfo, &timestamp);
#else
  localtime_r(&timestamp, &timeinfo);
#endif
  if (out) *out = timeinfo;
  return static_cast<uint32_t>((timeinfo.tm_year + 1900) * 10000 +
                               (timeinfo.tm_mon + 1) * 100 +
                               timeinfo.tm_mday);
}

static String format_state_history_date(uint64_t epoch_seconds) {
  struct tm timeinfo;
  const uint32_t date_key = local_date_key(epoch_seconds, &timeinfo);
  if (!date_key) return "--";

  const DeviceConfig& cfg = configManager.getConfig();
  const auto& locale = i18n::locale(cfg.language);
  struct tm now_info;
  const time_t now = time(nullptr);
#ifdef _WIN32
  localtime_s(&now_info, &now);
#else
  localtime_r(&now, &now_info);
#endif
  const uint32_t today_key =
      static_cast<uint32_t>((now_info.tm_year + 1900) * 10000 +
                            (now_info.tm_mon + 1) * 100 + now_info.tm_mday);
  String result = date_key == today_key
                      ? String(locale.weather_today)
                      : String(clock_tile::weekday_name(timeinfo.tm_wday,
                                                        cfg.language));
  result += " · ";

  char date_buffer[16];
  const uint8_t date_format = clock_tile::resolve_date_format(
      clock_tile::DATE_FORMAT_AUTO, cfg.global_date_format, cfg.language);
  if (date_format == clock_tile::DATE_FORMAT_MDY) {
    snprintf(date_buffer, sizeof(date_buffer), "%02d/%02d/%04d",
             timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900);
  } else if (date_format == clock_tile::DATE_FORMAT_YMD) {
    snprintf(date_buffer, sizeof(date_buffer), "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  } else {
    snprintf(date_buffer, sizeof(date_buffer), "%02d.%02d.%04d",
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  }
  result += date_buffer;
  return result;
}

static String format_binary_activity_time(uint64_t epoch_seconds,
                                          bool include_day) {
  if (!epoch_seconds) return "--";
  const time_t timestamp = static_cast<time_t>(epoch_seconds);
  struct tm timeinfo;
#ifdef _WIN32
  localtime_s(&timeinfo, &timestamp);
#else
  localtime_r(&timestamp, &timeinfo);
#endif

  const DeviceConfig& cfg = configManager.getConfig();
  const uint8_t time_format = clock_tile::resolve_time_format(
      clock_tile::TIME_FORMAT_AUTO, cfg.global_time_format, cfg.language);
  char time_buffer[20];
  if (time_format == clock_tile::TIME_FORMAT_24H) {
    snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    int hour = timeinfo.tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(time_buffer, sizeof(time_buffer), "%d:%02d:%02d %s", hour,
             timeinfo.tm_min, timeinfo.tm_sec,
             timeinfo.tm_hour < 12 ? "AM" : "PM");
  }
  if (!include_day) return String(time_buffer);
  String result = get_weekday_abbrev(static_cast<uint8_t>(timeinfo.tm_wday));
  result += " ";
  result += time_buffer;
  return result;
}

static void on_binary_timeline_draw(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
  SensorPopupContext* ctx = static_cast<SensorPopupContext*>(
      lv_event_get_user_data(event));
  if (!ctx || !ctx->binary_timeline ||
      (ctx->binary_timeline_bins.empty() && ctx->binary_segments.empty()) ||
      ctx->binary_range_end <= ctx->binary_range_start) {
    return;
  }
  lv_layer_t* layer = lv_event_get_layer(event);
  if (!layer) return;
  lv_area_t area;
  lv_obj_get_coords(ctx->binary_timeline, &area);
  const uint64_t span = ctx->binary_range_end - ctx->binary_range_start;
  const lv_coord_t width = lv_area_get_width(&area);
  if (!span || width < 3) return;

  auto draw_section = [&](lv_coord_t x1, lv_coord_t x2, uint32_t color) {
    if (x1 < area.x1) x1 = area.x1;
    if (x2 > area.x2) x2 = area.x2;
    if (x2 < x1) x2 = x1;

    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.base.layer = layer;
    fill.bg_color = lv_color_hex(color);
    fill.bg_opa = LV_OPA_COVER;
    fill.border_opa = LV_OPA_TRANSP;
    fill.radius = 0;
    lv_area_t section_area = {x1, area.y1, x2, area.y2};
    lv_draw_rect(layer, &fill, &section_area);
  };

  if (!ctx->binary_timeline_bins.empty()) {
    const size_t count = ctx->binary_timeline_bins.size();
    auto state_for_pixel = [&](lv_coord_t pixel) {
      size_t first =
          (static_cast<size_t>(pixel) * count) /
          static_cast<size_t>(width);
      size_t last =
          ((static_cast<size_t>(pixel + 1) * count) +
           static_cast<size_t>(width) - 1U) /
          static_cast<size_t>(width);
      if (last <= first) last = first + 1U;
      if (last > count) last = count;
      uint8_t selected = ctx->binary_timeline_bins[first];
      if (!ctx->binary_mode) {
        return ctx->binary_timeline_bins[last - 1U];
      }
      int selected_priority = binary_state_priority(selected);
      for (size_t index = first + 1U; index < last; ++index) {
        const uint8_t candidate = ctx->binary_timeline_bins[index];
        const int candidate_priority = binary_state_priority(candidate);
        if (candidate_priority > selected_priority) {
          selected = candidate;
          selected_priority = candidate_priority;
        }
      }
      return selected;
    };

    lv_coord_t run_start = 0;
    uint8_t run_state = state_for_pixel(0);
    for (lv_coord_t pixel = 1; pixel <= width; ++pixel) {
      const uint8_t next_state =
          pixel < width ? state_for_pixel(pixel) : run_state;
      if (pixel < width && next_state == run_state) continue;
      const uint32_t color =
          ctx->binary_mode
              ? binary_state_color(run_state)
              : (run_state < ctx->state_history_palette.size()
                     ? state_history_color(
                           ctx, ctx->state_history_palette[run_state])
                     : kBinaryUnknownColor);
      draw_section(static_cast<lv_coord_t>(area.x1 + run_start),
                   static_cast<lv_coord_t>(area.x1 + pixel - 1), color);
      run_start = pixel;
      run_state = next_state;
    }
    return;
  }

  for (const SensorPopupContext::BinarySegment& segment :
       ctx->binary_segments) {
    const uint64_t start = std::max(segment.start, ctx->binary_range_start);
    const uint64_t end = std::min(segment.end, ctx->binary_range_end);
    if (end <= start) continue;
    lv_coord_t x1 = static_cast<lv_coord_t>(
        area.x1 + ((start - ctx->binary_range_start) * width) / span);
    lv_coord_t x2 = static_cast<lv_coord_t>(
        area.x1 + ((end - ctx->binary_range_start) * width) / span - 1);
    draw_section(x1, x2,
                 ctx->binary_mode ? binary_state_color(segment.state)
                                  : state_history_color(ctx, segment.value));
  }
}

static void update_binary_time_axis(SensorPopupContext* ctx) {
  if (!ctx) return;
  // Reused popups also call this when clearing/loading history. A Number
  // uses the chart's axis; temporal editors have no timeline at all.
  if (ctx->editable && ctx->editable_kind != "select") {
    for (auto* label : ctx->binary_time_labels) {
      if (label) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }
  String labels[kTimeAxisMarkerCount];
  float fractions[kTimeAxisMarkerCount] = {};
  const int count = calc_time_axis(ctx, labels, fractions,
                                   kTimeAxisMarkerCount);
  lv_obj_update_layout(ctx->binary_body);
  const int width = lv_obj_get_width(ctx->binary_timeline);
  for (int index = 0; index < kTimeAxisMarkerCount; ++index) {
    lv_obj_t* label = ctx->binary_time_labels[index];
    if (!label) continue;
    if (index >= count || width <= 0) {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_label_set_text(label, labels[index].c_str());
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    const int label_width = measure_label_text_width(label);
    int x = static_cast<int>(fractions[index] * width) - (label_width / 2);
    if (x < 0) x = 0;
    if (x + label_width > width) x = width - label_width;
    if (x < 0) x = 0;
    lv_obj_set_x(label, x);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void refresh_binary_labels(SensorPopupContext* ctx) {
  if (!ctx) return;
  const char* language = configManager.getConfig().language;
  if (ctx->binary_history_title) {
    lv_label_set_text(ctx->binary_history_title,
                      i18n::binary_sensor_label(language, 2));
  }
  if (ctx->binary_activity_title) {
    lv_label_set_text(ctx->binary_activity_title,
                      i18n::binary_sensor_label(language, 3));
  }
}

static void refresh_binary_activity_rows(SensorPopupContext* ctx,
                                         bool force) {
  if (!ctx || !ctx->binary_activity_viewport) return;

  const size_t activity_count = ctx->binary_activity.size();
  int32_t scroll_y = lv_obj_get_scroll_y(ctx->binary_activity_viewport);
  if (scroll_y < 0) scroll_y = 0;
  size_t first_row = static_cast<size_t>(scroll_y / kBinaryActivityRowHeight);
  if (activity_count && first_row >= activity_count) {
    first_row = activity_count - 1;
  }
  if (!force && first_row == ctx->binary_activity_first_row) return;
  ctx->binary_activity_first_row = first_row;

  if (ctx->binary_activity_date) {
    const uint32_t date_key =
        activity_count
            ? local_date_key(ctx->binary_activity[first_row].timestamp)
            : 0;
    if (!date_key) {
      ctx->binary_activity_date_key = 0;
      lv_obj_add_flag(ctx->binary_activity_date, LV_OBJ_FLAG_HIDDEN);
    } else {
      if (force || date_key != ctx->binary_activity_date_key) {
        const String date = format_state_history_date(
            ctx->binary_activity[first_row].timestamp);
        lv_label_set_text(ctx->binary_activity_date, date.c_str());
        ctx->binary_activity_date_key = date_key;
      }
      lv_obj_clear_flag(ctx->binary_activity_date, LV_OBJ_FLAG_HIDDEN);
    }
  }

  const int dot_center_y = kBinaryActivityRowHeight / 2;
  bool active_pool[kBinaryActivityPoolRows] = {};
  const size_t last_row = std::min(
      activity_count, first_row + static_cast<size_t>(kBinaryActivityPoolRows));
  for (size_t activity_index = first_row; activity_index < last_row;
       ++activity_index) {
    const int pool_index = static_cast<int>(
        activity_index % static_cast<size_t>(kBinaryActivityPoolRows));
    active_pool[pool_index] = true;
    lv_obj_t* row = ctx->binary_activity_rows[pool_index];
    if (!row) continue;
    const SensorPopupContext::BinaryActivityEntry& entry =
        ctx->binary_activity[activity_index];
    if (force ||
        ctx->binary_activity_row_indices[pool_index] != activity_index) {
      ctx->binary_activity_row_indices[pool_index] = activity_index;
      lv_obj_set_y(row, static_cast<int32_t>(activity_index) *
                            kBinaryActivityRowHeight);
      const uint32_t color =
          ctx->binary_mode ? binary_state_color(entry.state)
                           : state_history_color(ctx, entry.value);
      lv_obj_set_style_bg_color(ctx->binary_activity_dots[pool_index],
                                lv_color_hex(color), 0);
      const String state_label =
          ctx->binary_mode
              ? String(i18n::binary_sensor_state_label(
                    configManager.getConfig().language,
                    binary_state_identifier(entry.state),
                    ctx->binary_device_class))
              : format_state_history_label(entry.value);
      lv_label_set_text(ctx->binary_activity_states[pool_index],
                        state_label.c_str());
      const String time_label = format_binary_activity_time(
          entry.timestamp, ctx->history_range == SensorHistoryRange::Day7);
      lv_label_set_text(ctx->binary_activity_times[pool_index],
                        time_label.c_str());

      lv_obj_t* line = ctx->binary_activity_lines[pool_index];
      if (line) {
        if (activity_count < 2) {
          lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
        } else {
          const int line_top = activity_index == 0 ? dot_center_y : 0;
          const int line_bottom = activity_index + 1 == activity_count
                                      ? dot_center_y + 1
                                      : kBinaryActivityRowHeight;
          lv_obj_set_y(line, line_top);
          lv_obj_set_height(line, std::max(1, line_bottom - line_top));
          lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
        }
      }
      lv_obj_t* divider = ctx->binary_activity_dividers[pool_index];
      if (divider) {
        if (activity_index + 1 < activity_count) {
          lv_obj_clear_flag(divider, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(divider, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
  }

  for (int pool_index = 0; pool_index < kBinaryActivityPoolRows;
       ++pool_index) {
    if (active_pool[pool_index]) continue;
    ctx->binary_activity_row_indices[pool_index] =
        kBinaryMaxActivityEntries;
    if (ctx->binary_activity_rows[pool_index]) {
      lv_obj_add_flag(ctx->binary_activity_rows[pool_index],
                      LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void on_binary_activity_scroll(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_SCROLL) return;
  SensorPopupContext* ctx = static_cast<SensorPopupContext*>(
      lv_event_get_user_data(event));
  refresh_binary_activity_rows(ctx);
}

static uint8_t state_history_palette_index(SensorPopupContext* ctx,
                                           const String& value,
                                           bool add_if_missing) {
  if (!ctx) return 0;
  const String normalized = normalize_state_history_value(value);
  for (size_t index = 0; index < ctx->state_history_palette.size(); ++index) {
    if (ctx->state_history_palette[index] == normalized) {
      return static_cast<uint8_t>(index);
    }
  }
  if (add_if_missing &&
      ctx->state_history_palette.size() < kStateHistoryMaxPaletteEntries) {
    ctx->state_history_palette.push_back(normalized);
    return static_cast<uint8_t>(ctx->state_history_palette.size() - 1U);
  }
  for (size_t index = 0; index < ctx->state_history_palette.size(); ++index) {
    if (ctx->state_history_palette[index].equalsIgnoreCase("unknown")) {
      return static_cast<uint8_t>(index);
    }
  }
  return 0;
}

static void prepend_state_history_activity(SensorPopupContext* ctx,
                                           uint64_t timestamp,
                                           uint8_t state,
                                           const String& value) {
  if (!ctx || !timestamp) return;
  const String normalized = ctx->binary_mode
                                ? String(binary_state_identifier(state))
                                : normalize_state_history_value(value);
  if (!ctx->binary_activity.empty() &&
      ctx->binary_activity.front().timestamp == timestamp &&
      ctx->binary_activity.front().state == state &&
      ctx->binary_activity.front().value == normalized) {
    return;
  }

  int32_t previous_scroll_y = 0;
  if (ctx->binary_activity_viewport) {
    previous_scroll_y = lv_obj_get_scroll_y(ctx->binary_activity_viewport);
    if (previous_scroll_y < 0) previous_scroll_y = 0;
  }
  SensorPopupContext::BinaryActivityEntry entry;
  entry.timestamp = timestamp;
  entry.state = state;
  entry.value = normalized;
  ctx->binary_activity.insert(ctx->binary_activity.begin(), entry);
  if (ctx->binary_activity.size() > kBinaryMaxActivityEntries) {
    ctx->binary_activity.resize(kBinaryMaxActivityEntries);
  }

  if (!ctx->binary_timeline_bins.empty()) {
    const uint8_t timeline_state =
        ctx->binary_mode
            ? state
            : state_history_palette_index(ctx, normalized, true);
    size_t first_bin = ctx->binary_timeline_bins.size() - 1U;
    if (ctx->binary_range_end > ctx->binary_range_start &&
        timestamp <= ctx->binary_range_start) {
      first_bin = 0;
    } else if (ctx->binary_range_end > ctx->binary_range_start &&
               timestamp < ctx->binary_range_end) {
      first_bin = static_cast<size_t>(
          ((timestamp - ctx->binary_range_start) *
           ctx->binary_timeline_bins.size()) /
          (ctx->binary_range_end - ctx->binary_range_start));
      if (first_bin >= ctx->binary_timeline_bins.size()) {
        first_bin = ctx->binary_timeline_bins.size() - 1U;
      }
    }
    if (!ctx->binary_mode ||
        binary_state_priority(timeline_state) > binary_state_priority(
            ctx->binary_timeline_bins[first_bin])) {
      ctx->binary_timeline_bins[first_bin] = timeline_state;
    }
    if (first_bin + 1U < ctx->binary_timeline_bins.size()) {
      std::fill(ctx->binary_timeline_bins.begin() + first_bin + 1U,
                ctx->binary_timeline_bins.end(), timeline_state);
    }
    if (ctx->binary_timeline) lv_obj_invalidate(ctx->binary_timeline);
  }

  if (ctx->binary_activity_status) {
    lv_obj_add_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);
  }

  ctx->binary_activity_first_row = kBinaryMaxActivityEntries;
  ctx->binary_activity_date_key = 0;
  for (int index = 0; index < kBinaryActivityPoolRows; ++index) {
    ctx->binary_activity_row_indices[index] = kBinaryMaxActivityEntries;
  }
  if (ctx->binary_activity_viewport) {
    const int viewport_height = std::max(
        kBinaryActivityRowHeight,
        static_cast<int>(lv_obj_get_height(ctx->binary_activity_viewport)));
    const int content_height = std::max(
        viewport_height,
        static_cast<int>(ctx->binary_activity.size()) *
            kBinaryActivityRowHeight);
    if (ctx->binary_activity_spacer) {
      lv_obj_set_height(ctx->binary_activity_spacer, content_height);
    }
    lv_obj_update_layout(ctx->binary_activity_viewport);
    const int32_t target_scroll_y =
        previous_scroll_y < (kBinaryActivityRowHeight / 2)
            ? 0
            : previous_scroll_y + kBinaryActivityRowHeight;
    lv_obj_scroll_to_y(ctx->binary_activity_viewport, target_scroll_y,
                       LV_ANIM_OFF);
  }
  refresh_binary_activity_rows(ctx, true);
}

static void clear_binary_history(SensorPopupContext* ctx) {
  if (!ctx) return;
  ctx->binary_segments.clear();
  ctx->binary_timeline_bins.clear();
  ctx->state_history_palette.clear();
  ctx->state_history_palette_complete = true;
  ctx->binary_activity.clear();
  ctx->binary_activity_first_row = kBinaryMaxActivityEntries;
  ctx->binary_activity_date_key = 0;
  ctx->binary_range_start = 0;
  ctx->binary_range_end = 0;
  if (ctx->binary_timeline) lv_obj_invalidate(ctx->binary_timeline);
  for (int index = 0; index < kTimeAxisMarkerCount; ++index) {
    if (ctx->binary_time_labels[index]) {
      lv_obj_add_flag(ctx->binary_time_labels[index], LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (int index = 0; index < kBinaryActivityPoolRows; ++index) {
    ctx->binary_activity_row_indices[index] = kBinaryMaxActivityEntries;
    if (ctx->binary_activity_rows[index]) {
      lv_obj_add_flag(ctx->binary_activity_rows[index], LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->binary_activity_date) {
    lv_obj_add_flag(ctx->binary_activity_date, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->binary_activity_viewport) {
    lv_obj_scroll_to_y(ctx->binary_activity_viewport, 0, LV_ANIM_OFF);
    if (ctx->binary_activity_spacer) {
      const int viewport_height = std::max(
          kBinaryActivityRowHeight,
          static_cast<int>(lv_obj_get_height(ctx->binary_activity_viewport)));
      lv_obj_set_height(ctx->binary_activity_spacer, viewport_height);
    }
    refresh_binary_activity_rows(ctx, true);
  }
  if (ctx->binary_activity_status) {
    lv_obj_add_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->binary_history_status) {
    lv_label_set_text(
        ctx->binary_history_status,
        i18n::strings(configManager.getConfig().language).loading);
    lv_obj_clear_flag(ctx->binary_history_status, LV_OBJ_FLAG_HIDDEN);
  }
  set_range_buttons_visible(ctx, true);
  update_binary_time_axis(ctx);
}

static void update_binary_icon(SensorPopupContext* ctx,
                               const String& icon_name_value) {
  if (!ctx || !ctx->icon_label || ctx->binary_icon_override) return;
  String icon_name = normalizeMdiIconName(icon_name_value);
  if (!icon_name.length()) return;
  const String icon = getMdiChar(icon_name);
  if (!icon.length()) return;
  lv_label_set_text(ctx->icon_label, icon.c_str());
  lv_obj_clear_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  align_header_row(ctx->card, ctx->title_label, ctx->icon_label);
}

static void refresh_editable_popup_icon(SensorPopupContext* ctx) {
  if (!ctx || !ctx->editable || !ctx->icon_label || ctx->binary_icon_override) return;
  const String name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(ctx->entity_id));
  const String glyph = name.length() ? getMdiChar(name) : String();
  lv_label_set_text(ctx->icon_label, glyph.c_str());
  if (glyph.length()) lv_obj_remove_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  align_header_row(ctx->card, ctx->title_label, ctx->icon_label);
}

static void update_binary_state(SensorPopupContext* ctx,
                                const String& state_value,
                                bool available,
                                const String& device_class,
                                uint64_t last_changed,
                                const String& icon_name) {
  if (!ctx) return;
  String state = state_value;
  state.trim();
  state.toLowerCase();
  const bool missing = !state.length();
  if (missing) {
    available = false;
  } else if (!available || state == "unavailable") {
    state = "unavailable";
    available = false;
  } else if (state != "on" && state != "off" && state != "unknown") {
    state = "unknown";
  }
  ctx->binary_state = state;
  ctx->binary_available = available;
  ctx->binary_device_class = device_class;
  ctx->binary_last_changed = last_changed;
  if (ctx->value_label) {
    if (missing) {
      lv_label_set_text(ctx->value_label, "--");
    } else {
      lv_label_set_text(
          ctx->value_label,
          i18n::binary_sensor_state_label(configManager.getConfig().language,
                                          state, device_class));
    }
  }
  if (ctx->icon_label) {
    lv_obj_set_style_text_color(
        ctx->icon_label,
        lv_color_hex(binary_state_color(binary_state_code(state, available))),
        0);
  }
  update_binary_icon(ctx, icon_name);
}

static void ensure_binary_view(SensorPopupContext* ctx) {
  if (!ctx || ctx->binary_body || !ctx->body_box) {
    if (ctx && ctx->binary_body) refresh_binary_labels(ctx);
    return;
  }

  lv_obj_t* body = lv_obj_create(ctx->body_box);
  ctx->binary_body = body;
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_center(body);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  const int section_font_height = popup_layout::scale(28);
  const int timeline_y = section_font_height + popup_layout::scale(14);
  const int axis_y = timeline_y + kBinaryTimelineHeight +
                     popup_layout::scale(6);
  const int activity_title_y =
      axis_y + lv_font_get_line_height(popup_layout::font20()) +
      popup_layout::scale(12);
  const int activity_date_y = activity_title_y + section_font_height +
                              popup_layout::scale(4);
  const int activity_rows_y =
      activity_date_y + lv_font_get_line_height(popup_layout::font20()) +
      popup_layout::scale(6);
  const int activity_view_height = std::max(
      kBinaryActivityRowHeight,
      popup_layout::kBodyHeight - activity_rows_y);

  ctx->binary_history_title = lv_label_create(body);
  set_label_style(ctx->binary_history_title, lv_color_white(),
                  popup_layout::font24());
  lv_obj_set_pos(ctx->binary_history_title, 0, 0);

  ctx->binary_timeline = lv_obj_create(body);
  lv_obj_remove_style_all(ctx->binary_timeline);
  lv_obj_set_size(ctx->binary_timeline, LV_PCT(100), kBinaryTimelineHeight);
  lv_obj_set_pos(ctx->binary_timeline, 0, timeline_y);
  lv_obj_set_style_bg_color(ctx->binary_timeline,
                            lv_color_hex(kBinaryUnavailableColor), 0);
  lv_obj_set_style_bg_opa(ctx->binary_timeline, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ctx->binary_timeline, 0, 0);
  lv_obj_set_style_clip_corner(ctx->binary_timeline, false, 0);
  lv_obj_set_style_border_width(ctx->binary_timeline, 0, 0);
  lv_obj_set_style_outline_width(ctx->binary_timeline, 0, 0);
  lv_obj_set_style_pad_all(ctx->binary_timeline, 0, 0);
  lv_obj_clear_flag(ctx->binary_timeline, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ctx->binary_timeline, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ctx->binary_timeline, on_binary_timeline_draw,
                      LV_EVENT_DRAW_MAIN, ctx);

  ctx->binary_history_status = lv_label_create(body);
  set_label_style(ctx->binary_history_status, lv_color_white(),
                  popup_layout::font20());
  lv_obj_set_style_text_opa(ctx->binary_history_status, LV_OPA_70, 0);
  lv_obj_set_width(ctx->binary_history_status, LV_PCT(100));
  lv_obj_set_style_text_align(ctx->binary_history_status,
                              LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_y(ctx->binary_history_status,
               timeline_y + (kBinaryTimelineHeight / 2) -
                   popup_layout::scale(10));

  for (int index = 0; index < kTimeAxisMarkerCount; ++index) {
    lv_obj_t* label = lv_label_create(body);
    ctx->binary_time_labels[index] = label;
    set_label_style(label, lv_color_white(), popup_layout::font20());
    lv_label_set_text(label, "");
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_y(label, axis_y);
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
  }

  ctx->binary_activity_title = lv_label_create(body);
  set_label_style(ctx->binary_activity_title, lv_color_white(),
                  popup_layout::font24());
  lv_obj_set_pos(ctx->binary_activity_title, 0, activity_title_y);

  ctx->binary_activity_date = lv_label_create(body);
  set_label_style(ctx->binary_activity_date, lv_color_white(),
                  popup_layout::font20());
  lv_obj_set_pos(ctx->binary_activity_date, 0, activity_date_y);
  lv_obj_set_width(ctx->binary_activity_date, LV_PCT(100));
  lv_label_set_long_mode(ctx->binary_activity_date, LV_LABEL_LONG_DOT);
  lv_obj_add_flag(ctx->binary_activity_date, LV_OBJ_FLAG_HIDDEN);

  ctx->binary_activity_status = lv_label_create(body);
  set_label_style(ctx->binary_activity_status, lv_color_white(),
                  popup_layout::font20());
  lv_obj_set_style_text_opa(ctx->binary_activity_status, LV_OPA_60, 0);
  lv_obj_set_width(ctx->binary_activity_status, LV_PCT(100));
  lv_obj_set_style_text_align(ctx->binary_activity_status,
                              LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_y(ctx->binary_activity_status,
               activity_rows_y + popup_layout::scale(8));
  lv_obj_add_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);

  const int dot_size = popup_layout::scale(10);
  const int dot_x = popup_layout::scale(12);
  const int state_x = popup_layout::scale(28);
  lv_obj_t* viewport = lv_obj_create(body);
  ctx->binary_activity_viewport = viewport;
  lv_obj_remove_style_all(viewport);
  lv_obj_set_size(viewport, LV_PCT(100), activity_view_height);
  lv_obj_set_pos(viewport, 0, activity_rows_y);
  lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(viewport, lv_color_hex(kBinaryInactiveColor),
                            LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(viewport, LV_OPA_50, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(viewport, popup_layout::scale(5),
                         LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(viewport, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
  lv_obj_set_scroll_dir(viewport, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_flag(
      viewport,
      static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE |
                                 LV_OBJ_FLAG_SCROLL_ELASTIC |
                                 LV_OBJ_FLAG_SCROLL_MOMENTUM));
  lv_obj_add_event_cb(viewport, on_binary_activity_scroll, LV_EVENT_SCROLL,
                      ctx);

  lv_obj_t* spacer = lv_obj_create(viewport);
  ctx->binary_activity_spacer = spacer;
  lv_obj_remove_style_all(spacer);
  lv_obj_set_size(spacer, 1, activity_view_height);
  lv_obj_set_pos(spacer, 0, 0);
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

  for (int index = 0; index < kBinaryActivityPoolRows; ++index) {
    ctx->binary_activity_row_indices[index] = kBinaryMaxActivityEntries;
    lv_obj_t* row = lv_obj_create(viewport);
    ctx->binary_activity_rows[index] = row;
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kBinaryActivityRowHeight);
    lv_obj_set_pos(row, 0, index * kBinaryActivityRowHeight);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* line = lv_obj_create(row);
    ctx->binary_activity_lines[index] = line;
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 1, kBinaryActivityRowHeight);
    lv_obj_set_pos(line, dot_x + (dot_size / 2), 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(kBinaryInactiveColor), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dot = lv_obj_create(row);
    ctx->binary_activity_dots[index] = dot;
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, dot_size, dot_size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, dot_x, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* state_label = lv_label_create(row);
    ctx->binary_activity_states[index] = state_label;
    set_label_style(state_label, lv_color_white(), popup_layout::font20());
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(state_label, LV_PCT(50));
    lv_obj_align(state_label, LV_ALIGN_LEFT_MID, state_x, 0);

    lv_obj_t* time_label = lv_label_create(row);
    ctx->binary_activity_times[index] = time_label;
    set_label_style(time_label, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_opa(time_label, LV_OPA_60, 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(time_label, LV_PCT(42));
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID,
                 -popup_layout::scale(12), 0);

    lv_obj_t* divider = lv_obj_create(row);
    ctx->binary_activity_dividers[index] = divider;
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, LV_PCT(94), 1);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_RIGHT,
                 -popup_layout::scale(12), 0);
    lv_obj_set_style_bg_color(divider, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_10, 0);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_obj_update_layout(body);
  refresh_binary_labels(ctx);
  clear_binary_history(ctx);
}

static int binary_timeline_hex_nibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

static bool decode_binary_timeline(JsonVariantConst points_value,
                                   JsonVariantConst encoding_value,
                                   JsonVariantConst data_value,
                                   std::vector<uint8_t>& output) {
  output.clear();
  if (!points_value.is<uint16_t>() || !encoding_value.is<const char*>() ||
      !data_value.is<const char*>()) {
    return false;
  }
  const uint16_t points = points_value.as<uint16_t>();
  const char* encoding = encoding_value.as<const char*>();
  const char* data = data_value.as<const char*>();
  if (!points || points > kBinaryMaxTimelineBins || !encoding || !data ||
      strcmp(encoding, "2bit-hex") != 0) {
    return false;
  }
  const size_t encoded_bytes = (static_cast<size_t>(points) + 3U) / 4U;
  const size_t expected_chars = encoded_bytes * 2U;
  if (strlen(data) != expected_chars) return false;

  output.reserve(points);
  for (size_t byte_index = 0; byte_index < encoded_bytes; ++byte_index) {
    const int high = binary_timeline_hex_nibble(data[byte_index * 2U]);
    const int low = binary_timeline_hex_nibble(data[byte_index * 2U + 1U]);
    if (high < 0 || low < 0) {
      output.clear();
      return false;
    }
    const uint8_t packed = static_cast<uint8_t>((high << 4) | low);
    for (int shift = 6; shift >= 0 && output.size() < points; shift -= 2) {
      output.push_back(static_cast<uint8_t>((packed >> shift) & 0x03U));
    }
  }
  return output.size() == points;
}

static bool decode_state_timeline(JsonVariantConst points_value,
                                  JsonVariantConst encoding_value,
                                  JsonVariantConst data_value,
                                  const std::vector<String>& palette,
                                  std::vector<uint8_t>& output) {
  output.clear();
  if (!points_value.is<uint16_t>() || !encoding_value.is<const char*>() ||
      !data_value.is<const char*>() || palette.empty() ||
      palette.size() > kStateHistoryMaxPaletteEntries) {
    return false;
  }
  const uint16_t points = points_value.as<uint16_t>();
  const char* encoding = encoding_value.as<const char*>();
  const char* data = data_value.as<const char*>();
  if (!points || points > kBinaryMaxTimelineBins || !encoding || !data ||
      strcmp(encoding, "palette4-hex") != 0) {
    return false;
  }

  const size_t expected_chars =
      ((static_cast<size_t>(points) + 1U) / 2U) * 2U;
  if (strlen(data) != expected_chars) return false;

  output.reserve(points);
  for (size_t index = 0; index < static_cast<size_t>(points); ++index) {
    const int code = binary_timeline_hex_nibble(data[index]);
    if (code < 0 || static_cast<size_t>(code) >= palette.size()) {
      output.clear();
      return false;
    }
    output.push_back(static_cast<uint8_t>(code));
  }
  return output.size() == points;
}

static void apply_binary_history_payload(SensorPopupContext* ctx,
                                         DynamicJsonDocument& doc) {
  if (!ctx) return;
  const HistoryRangeConfig range_cfg =
      get_history_range_config(ctx->history_range);
  const uint16_t payload_hours = doc["hours"] | range_cfg.hours;
  if (payload_hours != range_cfg.hours) return;

  const JsonVariantConst device_class_variant = doc["device_class"];
  const String device_class =
      doc.containsKey("device_class") && device_class_variant.isNull()
          ? String()
          : String(device_class_variant |
                   ctx->binary_device_class.c_str());
  const JsonVariantConst current_variant = doc["current"];
  const String current =
      doc.containsKey("current") && current_variant.isNull()
          ? String()
          : String(current_variant | ctx->binary_state.c_str());
  const JsonVariantConst available_variant = doc["available"];
  const bool available =
      doc.containsKey("available") && available_variant.isNull()
          ? false
          : (available_variant | ctx->binary_available);
  const JsonVariantConst last_changed_variant = doc["last_changed"];
  const bool clear_last_changed =
      doc.containsKey("last_changed") && last_changed_variant.isNull();
  const uint64_t last_changed = extract_epoch(last_changed_variant);
  update_binary_state(ctx, current, available, device_class,
                      clear_last_changed
                          ? 0
                          : (last_changed ? last_changed
                                          : ctx->binary_last_changed),
                      "");

  const bool history_available =
      doc.containsKey("error") ? false : (doc["history_available"] | true);
  ctx->binary_segments.clear();
  ctx->binary_timeline_bins.clear();
  ctx->binary_range_start = extract_epoch(doc["range_start"]);
  ctx->binary_range_end = extract_epoch(doc["range_end"]);
  if (!ctx->binary_range_end) {
    ctx->binary_range_end = static_cast<uint64_t>(time(nullptr));
  }
  if (!ctx->binary_range_start && ctx->binary_range_end) {
    ctx->binary_range_start =
        ctx->binary_range_end - (static_cast<uint64_t>(payload_hours) * 3600ULL);
  }

  if (history_available) {
    decode_binary_timeline(doc["timeline_points"],
                           doc["timeline_encoding"],
                           doc["timeline_data"],
                           ctx->binary_timeline_bins);
  }

  JsonArrayConst segments = doc["segments"].as<JsonArrayConst>();
  if (history_available && !segments.isNull()) {
    ctx->binary_segments.reserve(
        std::min(segments.size(), kBinaryMaxSegments));
    for (JsonObjectConst item : segments) {
      if (ctx->binary_segments.size() >= kBinaryMaxSegments) break;
      SensorPopupContext::BinarySegment segment;
      segment.start = extract_epoch(item["start"]);
      segment.end = extract_epoch(item["end"]);
      String state = String(item["state"] | "unknown");
      segment.state = binary_state_code(state);
      segment.value = binary_state_identifier(segment.state);
      if (!segment.start || !segment.end || segment.end <= segment.start) {
        continue;
      }
      ctx->binary_segments.push_back(segment);
    }
    std::sort(ctx->binary_segments.begin(), ctx->binary_segments.end(),
              [](const SensorPopupContext::BinarySegment& left,
                 const SensorPopupContext::BinarySegment& right) {
                return left.start < right.start;
              });
  }

  JsonArrayConst activity = doc["activity"].as<JsonArrayConst>();
  ctx->binary_activity.clear();
  if (history_available && !activity.isNull()) {
    ctx->binary_activity.reserve(
        std::min(activity.size(), kBinaryMaxActivityEntries));
    for (int source_index = static_cast<int>(activity.size()) - 1;
         source_index >= 0 &&
         ctx->binary_activity.size() < kBinaryMaxActivityEntries;
         --source_index) {
      JsonObjectConst item = activity[source_index].as<JsonObjectConst>();
      const uint64_t timestamp = extract_epoch(item["timestamp"]);
      String state = String(item["state"] | "unknown");
      if (!timestamp) continue;
      SensorPopupContext::BinaryActivityEntry entry;
      entry.timestamp = timestamp;
      entry.state = binary_state_code(state);
      entry.value = binary_state_identifier(entry.state);
      ctx->binary_activity.push_back(entry);
    }
  }

  ctx->binary_activity_first_row = kBinaryMaxActivityEntries;
  if (ctx->binary_activity_viewport) {
    const int viewport_height = std::max(
        kBinaryActivityRowHeight,
        static_cast<int>(lv_obj_get_height(ctx->binary_activity_viewport)));
    const int content_height = std::max(
        viewport_height,
        static_cast<int>(ctx->binary_activity.size()) *
            kBinaryActivityRowHeight);
    if (ctx->binary_activity_spacer) {
      lv_obj_set_height(ctx->binary_activity_spacer, content_height);
    }
    lv_obj_update_layout(ctx->binary_activity_viewport);
    lv_obj_scroll_to_y(ctx->binary_activity_viewport, 0, LV_ANIM_OFF);
  }
  refresh_binary_activity_rows(ctx, true);

  if (ctx->binary_activity_status) {
    if (history_available && ctx->binary_activity.empty()) {
      lv_label_set_text(
          ctx->binary_activity_status,
          i18n::binary_sensor_label(configManager.getConfig().language, 5));
      lv_obj_clear_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (ctx->binary_history_status) {
    if (!history_available) {
      lv_label_set_text(
          ctx->binary_history_status,
          i18n::binary_sensor_label(configManager.getConfig().language, 4));
      lv_obj_clear_flag(ctx->binary_history_status, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->binary_history_status, LV_OBJ_FLAG_HIDDEN);
    }
  }
  set_range_buttons_visible(ctx, true);
  update_binary_time_axis(ctx);
  if (ctx->binary_timeline) lv_obj_invalidate(ctx->binary_timeline);
}


static void resize_editable_chart(SensorPopupContext* ctx, int height) {
  if (!ctx->chart_wrap || !ctx->chart) return;
  ctx->chart_height = height;
  constexpr int overhang = popup_layout::contentScale(12);
  lv_obj_set_height(ctx->chart_wrap, height + 2 * overhang + kTimeAxisHeight);
  lv_obj_set_height(ctx->chart, height);
  if (ctx->y_min_label) lv_obj_set_y(ctx->y_min_label, height);
  if (ctx->y_min_line) lv_obj_set_y(ctx->y_min_line, overhang + height - 1);
  for (int i = 0; i < kTimeAxisMarkerCount; ++i) {
    if (ctx->time_lines[i]) lv_obj_set_height(ctx->time_lines[i], height);
    if (ctx->time_labels[i]) lv_obj_set_y(ctx->time_labels[i], overhang + height + popup_layout::scale480(8));
  }
}

static int editable_control_top(const SensorPopupContext* ctx) {
  // Include the close button's invisible touch extension and wrapped titles.
  int header_bottom = std::max(popup_layout::kHeaderHeight,
      popup_layout::kCloseButtonOffsetY + popup_layout::kCloseButtonSize +
          popup_layout::kCloseButtonClickArea);
  for (auto* label : {ctx->title_label, ctx->icon_label}) {
    if (label && !lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) {
      header_bottom = std::max<int>(header_bottom,
          lv_obj_get_y(label) + lv_obj_get_height(label));
    }
  }
  return std::max(popup_layout::kValueY, header_bottom + popup_layout::scale(6));
}

static void layout_editable_history(SensorPopupContext* ctx) {
  if (!ctx || !ctx->body_box) return;
  const bool numeric = ctx->editable && ctx->editable_kind == "number";
  const bool temporal = ctx->editable && ctx->editable_kind != "select" && !numeric;
  const int section_height = popup_layout::scale(28);
  const int timeline_y = section_height + popup_layout::scale(14);
  const int axis_y = timeline_y + kBinaryTimelineHeight + popup_layout::scale(6);
  const int default_activity_y = axis_y + lv_font_get_line_height(popup_layout::font20()) + popup_layout::scale(12);
  const int control_y = ctx->editable ? editable_control_top(ctx) : popup_layout::kValueY;
  if (ctx->editable && ctx->control_row) lv_obj_set_y(ctx->control_row, control_y);
  const int body_y = ctx->editable ? control_y + editable_control_height(ctx->editable_kind) +
                                   popup_layout::scale(8) : popup_layout::kBodyY;
  // Both children are positioned inside the padded card. Reserve the actual
  // footer bounds, so the graph and Activity never require an outer scroll.
  const int content_lift = ctx->editable ? 0 : kContentLiftY;
  const int body_height = ctx->editable ? popup_layout::kNavY - 2 * popup_layout::kCardPad -
                                         popup_layout::scale(12) - body_y + content_lift : popup_layout::kBodyHeight;
  lv_obj_set_height(ctx->body_box, body_height);
  lv_obj_align(ctx->body_box, LV_ALIGN_TOP_MID, 0, body_y - content_lift);
  lv_obj_scroll_to_y(ctx->body_box, 0, LV_ANIM_OFF);
  lv_obj_remove_flag(ctx->body_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(ctx->body_box, LV_SCROLLBAR_MODE_OFF);
  if (ctx->editable) lv_obj_remove_flag(ctx->body_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  else lv_obj_add_flag(ctx->body_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  const int activity_heading = section_height + popup_layout::scale(4) +
                               lv_font_get_line_height(popup_layout::font20()) + popup_layout::scale(6);
  const int chart_extra = popup_layout::contentScale(24) + kTimeAxisHeight + popup_layout::scale(12);
  const int reserved_rows = SCREEN_HEIGHT <= 600 ? 2 : 3;
  // Reclaimed control spacing belongs to Activity, not a taller graph.
  const int chart_limit = popup_layout::contentScale(SCREEN_HEIGHT <= 600 ? 60 : 90);
  const int chart_height = numeric ? std::max(40, std::min(chart_limit,
      body_height - timeline_y - chart_extra - activity_heading - reserved_rows * kBinaryActivityRowHeight)) : kChartHeight;
  resize_editable_chart(ctx, chart_height);
  if (numeric) {
    lv_obj_align(ctx->chart_wrap, LV_ALIGN_TOP_MID, 0, timeline_y);
    lv_obj_remove_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->chart_wrap);
  } else {
    lv_obj_center(ctx->chart_wrap);
    if (ctx->state_history_mode) lv_obj_add_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
  }
  if (!ctx->binary_body) return;
  auto show = [](lv_obj_t* obj, bool visible) {
    if (!obj) return;
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  };
  const int activity_y = temporal ? popup_layout::scale(8) : numeric ? timeline_y + chart_height + chart_extra : default_activity_y;
  const int date_y = activity_y + section_height + popup_layout::scale(4);
  const int rows_y = activity_y + activity_heading;
  const int viewport_height = std::max(kBinaryActivityRowHeight, body_height - rows_y);
  lv_obj_set_size(ctx->binary_body, LV_PCT(100), body_height);
  lv_obj_align(ctx->binary_body, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_y(ctx->binary_activity_title, activity_y);
  lv_obj_set_y(ctx->binary_activity_date, date_y);
  lv_obj_set_y(ctx->binary_activity_viewport, rows_y);
  lv_obj_set_height(ctx->binary_activity_viewport, viewport_height);
  lv_obj_set_y(ctx->binary_activity_status, rows_y);
  show(ctx->binary_history_title, !temporal);
  show(ctx->binary_timeline, !numeric && !temporal);
  // Categorical history retains its compact bar and axis spacing, leaving
  // the space that a numeric graph would occupy available for Activity.
  lv_obj_set_y(ctx->binary_timeline, timeline_y);
  for (auto* time_label : ctx->binary_time_labels) {
    if (time_label) lv_obj_set_y(time_label, axis_y);
  }
  if (numeric || temporal) {
    for (auto* time_label : ctx->binary_time_labels) show(time_label, false);
  } else {
    // Only the current range's axis calculation may expose labels. Showing the
    // entire pool resurrects default text and leftover seven-day markers.
    update_binary_time_axis(ctx);
  }
  lv_obj_set_y(ctx->binary_history_status, temporal ? rows_y : timeline_y);
  lv_obj_update_layout(ctx->body_box);
}

static void apply_state_history_payload(SensorPopupContext* ctx,
                                        DynamicJsonDocument& doc) {
  if (!ctx) return;
  const HistoryRangeConfig range_cfg =
      get_history_range_config(ctx->history_range);
  const uint16_t payload_hours = doc["hours"] | range_cfg.hours;
  if (payload_hours != range_cfg.hours) return;

  ctx->binary_mode = false;
  ctx->state_history_mode = true;
  ensure_binary_view(ctx);
  // Editable Numbers share Activity parsing with categorical histories but
  // retain their numeric graph. Visibility must not depend on a later layout.
  if (ctx->chart_wrap) {
    if (ctx->editable && ctx->editable_kind == "number") {
      lv_obj_remove_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->chart_wrap, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->binary_body) lv_obj_clear_flag(ctx->binary_body, LV_OBJ_FLAG_HIDDEN);

  if (!ctx->editable && doc.containsKey("current")) {
    const JsonVariantConst current_variant = doc["current"];
    ctx->state_history_value =
        current_variant.isNull()
            ? String()
            : normalize_state_live_value(
                  String(current_variant.as<const char*>()));
    update_value_label(ctx, ctx->state_history_value, ctx->unit);
  }

  const bool history_available =
      doc.containsKey("error") ? false : (doc["history_available"] | true);
  ctx->binary_segments.clear();
  ctx->binary_timeline_bins.clear();
  ctx->state_history_palette.clear();
  ctx->state_history_palette_complete = doc["palette_complete"] | true;
  if (!ctx->state_history_palette_complete) {
    Serial.printf(
        "[StateHistory] Palette limit reached for %s; overflow is shown as unknown\n",
        ctx->entity_id.c_str());
  }
  ctx->binary_range_start = extract_epoch(doc["range_start"]);
  ctx->binary_range_end = extract_epoch(doc["range_end"]);
  if (!ctx->binary_range_end) {
    ctx->binary_range_end = static_cast<uint64_t>(time(nullptr));
  }
  if (!ctx->binary_range_start && ctx->binary_range_end) {
    ctx->binary_range_start =
        ctx->binary_range_end - (static_cast<uint64_t>(payload_hours) * 3600ULL);
  }

  const JsonArrayConst palette = doc["palette"].as<JsonArrayConst>();
  if (history_available && !palette.isNull() &&
      palette.size() <= kStateHistoryMaxPaletteEntries) {
    ctx->state_history_palette.reserve(palette.size());
    for (JsonVariantConst item : palette) {
      if (!item.is<const char*>()) {
        ctx->state_history_palette.clear();
        break;
      }
      ctx->state_history_palette.push_back(
          normalize_state_history_value(String(item.as<const char*>())));
    }
  }
  if (history_available) {
    decode_state_timeline(doc["timeline_points"],
                          doc["timeline_encoding"],
                          doc["timeline_data"],
                          ctx->state_history_palette,
                          ctx->binary_timeline_bins);
  }

  const JsonArrayConst segments = doc["segments"].as<JsonArrayConst>();
  if (history_available && !segments.isNull()) {
    ctx->binary_segments.reserve(
        std::min(segments.size(), kBinaryMaxSegments));
    for (JsonObjectConst item : segments) {
      if (ctx->binary_segments.size() >= kBinaryMaxSegments) break;
      SensorPopupContext::BinarySegment segment;
      segment.start = extract_epoch(item["start"]);
      segment.end = extract_epoch(item["end"]);
      segment.value = normalize_state_history_value(
          String(item["state"] | "unknown"));
      if (!segment.start || !segment.end || segment.end <= segment.start) {
        continue;
      }
      ctx->binary_segments.push_back(segment);
    }
    std::sort(ctx->binary_segments.begin(), ctx->binary_segments.end(),
              [](const SensorPopupContext::BinarySegment& left,
                 const SensorPopupContext::BinarySegment& right) {
                return left.start < right.start;
              });
  }

  const JsonArrayConst activity = doc["activity"].as<JsonArrayConst>();
  ctx->binary_activity.clear();
  if (history_available && !activity.isNull()) {
    ctx->binary_activity.reserve(
        std::min(activity.size(), kBinaryMaxActivityEntries));
    for (int source_index = static_cast<int>(activity.size()) - 1;
         source_index >= 0 &&
         ctx->binary_activity.size() < kBinaryMaxActivityEntries;
         --source_index) {
      const JsonObjectConst item = activity[source_index].as<JsonObjectConst>();
      const uint64_t timestamp = extract_epoch(item["timestamp"]);
      if (!timestamp) continue;
      SensorPopupContext::BinaryActivityEntry entry;
      entry.timestamp = timestamp;
      entry.value = normalize_state_history_value(
          String(item["state"] | "unknown"));
      ctx->binary_activity.push_back(entry);
    }
  }

  ctx->binary_activity_first_row = kBinaryMaxActivityEntries;
  ctx->binary_activity_date_key = 0;
  for (int index = 0; index < kBinaryActivityPoolRows; ++index) {
    ctx->binary_activity_row_indices[index] = kBinaryMaxActivityEntries;
  }
  if (ctx->binary_activity_viewport) {
    const int viewport_height = std::max(
        kBinaryActivityRowHeight,
        static_cast<int>(lv_obj_get_height(ctx->binary_activity_viewport)));
    const int content_height = std::max(
        viewport_height,
        static_cast<int>(ctx->binary_activity.size()) *
            kBinaryActivityRowHeight);
    if (ctx->binary_activity_spacer) {
      lv_obj_set_height(ctx->binary_activity_spacer, content_height);
    }
    lv_obj_update_layout(ctx->binary_activity_viewport);
    lv_obj_scroll_to_y(ctx->binary_activity_viewport, 0, LV_ANIM_OFF);
  }
  refresh_binary_activity_rows(ctx, true);

  if (ctx->binary_activity_status) {
    if (history_available && ctx->binary_activity.empty()) {
      lv_label_set_text(
          ctx->binary_activity_status,
          i18n::binary_sensor_label(configManager.getConfig().language, 5));
      lv_obj_clear_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->binary_activity_status, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (ctx->binary_history_status) {
    if (!history_available) {
      lv_label_set_text(
          ctx->binary_history_status,
          i18n::binary_sensor_label(configManager.getConfig().language, 4));
      lv_obj_clear_flag(ctx->binary_history_status, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->binary_history_status, LV_OBJ_FLAG_HIDDEN);
    }
  }
  set_range_buttons_visible(ctx, true);
  if (!ctx->editable || ctx->editable_kind == "select") update_binary_time_axis(ctx);
  if (ctx->binary_timeline) lv_obj_invalidate(ctx->binary_timeline);
}

static bool accept_editable_history_range(SensorPopupContext* ctx, DynamicJsonDocument& doc) {
  if (!ctx->editable_history_id.length() || ctx->editable_history_id != (doc["request_id"] | "")) return false;
  const auto requested = get_history_range_config(ctx->editable_requested_range);
  if ((doc["hours"] | 0) != requested.hours) return false;
  // Keep the displayed range and its data intact until the matching reply is
  // ready. Slow Recorder responses must not blank or resize the popup.
  ctx->history_range = ctx->editable_requested_range;
  update_range_buttons(ctx);
  return true;
}

static void apply_history_payload(SensorPopupContext* ctx, const char* payload) {
  if (!ctx || !payload || !*payload) return;

  const uint32_t apply_started_ms = millis();
  // Discrete responses are bounded to 96 timeline segments and 96 activity
  // entries. Numeric histories continue to use the same transient document.
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[SensorPopup] History JSON error: %s\n", err.c_str());
    return;
  }
  const uint32_t parsed_ms = millis();

  const char* entity = doc["entity_id"] | "";
  if (!ctx->entity_id.equalsIgnoreCase(entity)) {
    return;
  }

  if (ctx->editable) {
    if (!accept_editable_history_range(ctx, doc)) return;
    apply_state_history_payload(ctx, doc);
    // Editor geometry is established on open/kind changes. A history response
    // changes data and axes, not the control, graph or Activity allocation.
    if (ctx->editable_kind != "number") return;
  }
  const char* kind = doc["kind"] | "";
  if (strcmp(kind, "binary") == 0) {
    if (!ctx->binary_mode) return;
    apply_binary_history_payload(ctx, doc);
    return;
  }
  if (strcmp(kind, "state") == 0) {
    if (ctx->binary_mode || ctx->editable) return;
    apply_state_history_payload(ctx, doc);
    return;
  }
  if (ctx->state_history_mode && !ctx->editable) return;

  if (!ctx->lock_unit && doc.containsKey("unit")) {
    String unit = String(doc["unit"].as<const char*>());
    unit.trim();
    if (!unit.isEmpty()) {
      ctx->unit = unit;
    }
  }

  const HistoryRangeConfig range_cfg = get_history_range_config(ctx->history_range);
  const uint16_t payload_hours = doc["hours"] | range_cfg.hours;
  const uint16_t payload_period_minutes = doc["period_minutes"] | range_cfg.period_minutes;
  if (payload_hours != range_cfg.hours || payload_period_minutes != range_cfg.period_minutes) {
    return;
  }

  if (!ctx->editable && doc.containsKey("current")) {
    String current = String(doc["current"].as<const char*>());
    update_value_label(ctx, current, ctx->unit);
  }

  JsonArray values = doc["values"].as<JsonArray>();
  if (values.isNull()) {
    return;
  }

  size_t count = values.size();
  if (ctx->editable && count > 288) return;
  if (count == 0) {
    set_range_buttons_visible(ctx, ctx->editable);
    clear_chart(ctx, range_cfg.points);
    return;
  }

  uint16_t points = static_cast<uint16_t>(count);

  std::vector<float> plot_values(count, NAN);
  for (size_t i = 0; i < count; ++i) {
    JsonVariant v = values[i];
    float val = 0.0f;
    if (extract_numeric(v, val) && (!ctx->editable || fabsf(val) < 100000000.0f)) {
      plot_values[i] = val;
    }
  }

  // Fill gaps as HA does: carry the last valid value into missing buckets,
  // including filling the prefix with the first valid value.
  size_t first_numeric = count;
  for (size_t i = 0; i < count; ++i) {
    if (isfinite(plot_values[i])) {
      first_numeric = i;
      break;
    }
  }
  if (!ctx->editable && first_numeric < count) {
    float last = plot_values[first_numeric];
    for (size_t i = 0; i < first_numeric; ++i) {
      plot_values[i] = last;
    }
    for (size_t i = first_numeric + 1; i < count; ++i) {
      if (!isfinite(plot_values[i])) {
        plot_values[i] = last;
      } else {
        last = plot_values[i];
      }
    }
  }

  bool has_range = false;
  float min_v = 0.0f;
  float max_v = 0.0f;
  size_t numeric_count = 0;

  for (float val : plot_values) {
    if (!isfinite(val)) continue;
    ++numeric_count;
    if (!has_range) {
      min_v = max_v = val;
      has_range = true;
    } else {
      if (val < min_v) min_v = val;
      if (val > max_v) max_v = val;
    }
  }

  set_range_buttons_visible(ctx, ctx->editable || numeric_count > 1);

  int scale = 1;
  if (has_range) {
    float span = max_v - min_v;
    if (span <= 10.0f) {
      scale = 100;
    } else if (span <= 100.0f) {
      scale = 10;
    }
    float max_abs = fmaxf(fabsf(min_v), fabsf(max_v));
    while (scale > 1 && (max_abs * scale) > 30000.0f) {
      scale /= 10;
    }
  }

  ctx->point_count = points;
  lv_chart_set_point_count(ctx->chart, points);
  int32_t* chart_values = lv_chart_get_series_y_array(ctx->chart, ctx->series);
  if (!chart_values) {
    Serial.println("[SensorPopup] History chart buffer unavailable");
    clear_chart(ctx, points);
    return;
  }
  lv_chart_set_x_start_point(ctx->chart, ctx->series, 0);

  // Write the complete LVGL series buffer in one pass. Calling
  // lv_chart_set_value_by_id() for every sample invalidates and recalculates
  // a chart area up to 288 times, blocking the UI for roughly half a second.
  for (size_t i = 0; i < count; ++i) {
    float val = plot_values[i];
    if (!isfinite(val)) {
      chart_values[i] = LV_CHART_POINT_NONE;
      continue;
    }
    chart_values[i] = static_cast<int32_t>(lroundf(val * scale));
  }

  // A single sample has no line yet, so make its point visible.
  if (numeric_count <= 1) {
    lv_obj_set_style_size(ctx->chart, 8, 8, LV_PART_INDICATOR);
  } else {
    lv_obj_set_style_size(ctx->chart, 0, 0, LV_PART_INDICATOR);
  }

  if (has_range && ctx->chart) {
    if (min_v == max_v) {
      min_v -= 1.0f;
      max_v += 1.0f;
    }
    lv_chart_set_range(
      ctx->chart,
      LV_CHART_AXIS_PRIMARY_Y,
      static_cast<lv_coord_t>(floorf(min_v * scale)),
      static_cast<lv_coord_t>(ceilf(max_v * scale))
    );
  }
  const uint32_t chart_ready_ms = millis();

  // Update Y-axis labels with min/max values
  if (has_range) {
    auto format_y = [](float val, uint8_t decimals) -> String {
      if (decimals != 0xFF && decimals <= 6) {
        return i18n::format_number(
            configManager.getConfig().language, val, decimals);
      }
      // Auto: use integer if close to whole number, else 1 decimal
      if (fabsf(val - roundf(val)) < 0.05f) {
        return i18n::format_number(
            configManager.getConfig().language, val, 0);
      }
      return i18n::format_number(
          configManager.getConfig().language, val, 1);
    };
    String unit_suffix = "";
    if (ctx->unit.length()) {
      unit_suffix = " " + ctx->unit;
    }
    if (ctx->y_max_label) {
      lv_label_set_text(ctx->y_max_label, (format_y(max_v, ctx->decimals) + unit_suffix).c_str());
    }
    if (ctx->y_min_label) {
      lv_label_set_text(ctx->y_min_label, (format_y(min_v, ctx->decimals) + unit_suffix).c_str());
    }
    update_y_axis_layout(ctx);
    if (ctx->y_max_line) lv_obj_clear_flag(ctx->y_max_line, LV_OBJ_FLAG_HIDDEN);
    if (ctx->y_min_line) lv_obj_clear_flag(ctx->y_min_line, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (ctx->y_max_label) lv_label_set_text(ctx->y_max_label, "");
    if (ctx->y_min_label) lv_label_set_text(ctx->y_min_label, "");
    if (ctx->y_max_line) lv_obj_add_flag(ctx->y_max_line, LV_OBJ_FLAG_HIDDEN);
    if (ctx->y_min_line) lv_obj_add_flag(ctx->y_min_line, LV_OBJ_FLAG_HIDDEN);
    // No valid range: hide the time axis too.
    for (int i = 0; i < kTimeAxisMarkerCount; ++i) {
      if (ctx->time_lines[i]) lv_obj_add_flag(ctx->time_lines[i], LV_OBJ_FLAG_HIDDEN);
      if (ctx->time_labels[i]) lv_obj_add_flag(ctx->time_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  // One refresh is sufficient after the complete buffer, scale and labels
  // have been prepared.
  lv_chart_refresh(ctx->chart);

  const uint32_t apply_finished_ms = millis();
  const uint32_t total_ms = apply_finished_ms - apply_started_ms;
  if (total_ms >= 20) {
    Serial.printf(
      "[SensorPopup] History applied: points=%u parse=%lu ms chart=%lu ms layout=%lu ms total=%lu ms\n",
      static_cast<unsigned>(points),
      static_cast<unsigned long>(parsed_ms - apply_started_ms),
      static_cast<unsigned long>(chart_ready_ms - parsed_ms),
      static_cast<unsigned long>(apply_finished_ms - chart_ready_ms),
      static_cast<unsigned long>(total_ms)
    );
  }
}

static bool should_request_history(const String& entity_id) {
  return entity_id.length() && !entity_id.startsWith("__") &&
         !hardwareIo.isLocalEntityId(entity_id.c_str());
}

static void request_history_for_context(SensorPopupContext* ctx) {
  if (!ctx || !should_request_history(ctx->entity_id)) return;
  const HistoryRangeConfig cfg = get_history_range_config(
      ctx->editable ? ctx->editable_requested_range : ctx->history_range);
  if (ctx->editable) {
    ctx->editable_history_id = editable_request_history(ctx->entity_id, cfg.hours);
    ctx->state_history_refresh_pending = false;
    ctx->state_history_last_request_ms = millis();
    return;
  }
  if (ctx->state_history_mode) {
    ctx->state_history_refresh_pending = false;
    ctx->state_history_last_request_ms = millis();
    if (ctx->binary_mode) {
      mqttPublishBinaryHistoryRequest(ctx->entity_id.c_str(), cfg.hours,
                                      kBinaryMaxSegments);
    } else {
      mqttPublishStateHistoryRequest(ctx->entity_id.c_str(), cfg.hours,
                                     kBinaryMaxSegments);
    }
    return;
  }
  mqttPublishHistoryRequest(ctx->entity_id.c_str(), cfg.hours, cfg.period_minutes, cfg.points);
}

static uint32_t state_history_refresh_interval_ms(
    const SensorPopupContext* ctx) {
  return ctx && ctx->history_range == SensorHistoryRange::Day7
             ? kStateHistoryRefreshMinMs7d
             : kStateHistoryRefreshMinMs24h;
}

static void schedule_state_history_refresh(SensorPopupContext* ctx) {
  if (!ctx || !ctx->state_history_mode) return;
  const uint32_t now_ms = millis();
  if (ctx->state_history_last_request_ms == 0 ||
      static_cast<uint32_t>(now_ms - ctx->state_history_last_request_ms) >=
          state_history_refresh_interval_ms(ctx)) {
    request_history_for_context(ctx);
    return;
  }
  ctx->state_history_refresh_pending = true;
}

static void on_overlay_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)e;
}

static void set_sensor_popup_visible(SensorPopupContext* ctx, bool visible) {
  if (!ctx || !ctx->overlay || !ctx->card) return;
  if (visible) {
    // LVGL can skip covered grid tiles only when the opaque card participates
    // in the active screen's cover search, just like the Settings popup.
    lv_obj_set_parent(ctx->overlay, lv_screen_active());
    lv_obj_set_pos(ctx->overlay, 0, 0);
    lv_obj_remove_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ctx->overlay);
  } else {
    lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
    // Park the reusable shell off the screen so hidden popups survive screen
    // replacement; the existing delete callback still owns final cleanup.
    lv_obj_set_parent(ctx->overlay, lv_layer_top());
  }
}

static void on_close_click(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  SensorPopupContext* ctx = static_cast<SensorPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->overlay || !ctx->card) return;
  editable_control_close(ctx->control);
  set_sensor_popup_visible(ctx, false);
}

static void on_overlay_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  SensorPopupContext* ctx = static_cast<SensorPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  editable_control_delete(ctx->control); ctx->control = nullptr;
  if (g_sensor_popup_ctx == ctx) {
    g_sensor_popup_ctx = nullptr;
  }
  delete ctx;
}

static void on_range_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  SensorPopupContext* ctx = static_cast<SensorPopupContext*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (!ctx || !target) return;

  SensorHistoryRange next_range =
      (target == ctx->range_week_btn) ? SensorHistoryRange::Day7 : SensorHistoryRange::Day24;
  if (ctx->editable) {
    if (ctx->editable_requested_range == next_range) return;
    ctx->editable_requested_range = next_range;
    update_range_buttons(ctx);
    request_history_for_context(ctx);
    return;
  }
  if (ctx->history_range == next_range) return;

  ctx->history_range = next_range;
  update_range_buttons(ctx);
  if (ctx->state_history_mode) {
    clear_binary_history(ctx);
    if (ctx->editable) clear_chart(ctx, get_history_range_config(ctx->history_range).points);
  } else {
    clear_chart(ctx, get_history_range_config(ctx->history_range).points);
  }
  request_history_for_context(ctx);
}

static void build_popup_ui(SensorPopupContext* ctx, const SensorPopupInit& init) {
  lv_obj_t* overlay = lv_obj_create(lv_layer_top());
  ctx->overlay = overlay;
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* card = lv_obj_create(overlay);
  ctx->card = card;
  lv_obj_set_size(card, kCardWidth, kCardHeight);
  lv_obj_center(card);
  uint32_t card_color = init.bg_color ? init.bg_color : 0x2A2A2A;
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, popup_layout::kCardRadius, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  ui_surface_style::apply_global_tile_border(card);
  lv_obj_set_style_pad_all(card, kCardPad, 0);
  lv_obj_set_style_shadow_width(card, popup_layout::scale480(28), 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, popup_layout::scale480(2), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(card);
  ctx->title_label = title;
  set_label_style(title, lv_color_white(), popup_layout::headerTitleFont());
  hometiles_title::set(title, init.title.c_str());
  lv_obj_set_width(title, LV_PCT(38));
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 78, 10);

  lv_obj_t* icon = lv_label_create(card);
  ctx->icon_label = icon;
  lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(icon);
  lv_obj_set_style_text_color(icon, lv_color_white(), 0);

  lv_obj_t* close_btn =
      popup_layout::createCloseButton(card, on_close_click, ctx);

  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 8, 0);

  lv_obj_t* range_row = lv_obj_create(card);
  ctx->range_row = range_row;
  lv_obj_remove_style_all(range_row);
  lv_obj_set_size(range_row, (kRangeButtonWidth * 2) + kRangeButtonGap, popup_layout::kNavHeight);
  lv_obj_align(range_row, LV_ALIGN_BOTTOM_MID, 0, -popup_layout::kNavBottomInset);
  lv_obj_set_style_bg_opa(range_row, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(range_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(range_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(range_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(range_row, kRangeButtonGap, 0);
  lv_obj_clear_flag(range_row, LV_OBJ_FLAG_CLICKABLE);

  auto make_range_button = [&](const char* text) -> lv_obj_t* {
    lv_obj_t* btn = lv_button_create(range_row);
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
    return btn;
  };

  ctx->range_day_btn = make_range_button("24H");
  ctx->range_week_btn = make_range_button("7D");
  lv_obj_add_event_cb(ctx->range_day_btn, on_range_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(ctx->range_week_btn, on_range_click, LV_EVENT_CLICKED, ctx);
  update_range_buttons(ctx);
  set_range_buttons_visible(ctx, false);

  lv_obj_t* value_box = lv_obj_create(card);
  ctx->value_box = value_box;
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
  set_label_style(value, lv_color_white(), get_value_font());
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_translate_y(value, popup_layout::kLargeValueTextOffsetY, 0);
  lv_obj_set_width(value, LV_PCT(100));

  ctx->control_row = lv_obj_create(card);
  lv_obj_remove_style_all(ctx->control_row);
  lv_obj_set_size(ctx->control_row, LV_PCT(100), popup_layout::kValueHeight);
  lv_obj_align(ctx->control_row, LV_ALIGN_TOP_MID, 0, popup_layout::kValueY);
  lv_obj_remove_flag(ctx->control_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->control_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  ctx->control = editable_control_create(ctx->control_row, card);

  lv_obj_t* body_box = lv_obj_create(card);
  ctx->body_box = body_box;
  lv_obj_remove_style_all(body_box);
  lv_obj_set_size(body_box, LV_PCT(100), popup_layout::kBodyHeight);
  lv_obj_align(
      body_box, LV_ALIGN_TOP_MID, 0,
      popup_layout::kBodyY - kContentLiftY);
  lv_obj_set_style_bg_opa(body_box, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(body_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_clear_flag(body_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(body_box, LV_OBJ_FLAG_SCROLLABLE);

  // Chart wrapper: Y-axis labels on the left, chart on the right, time labels below.
  // Extra vertical space (kLabelOverhang) at top/bottom so labels don't get clipped.
  constexpr int kLabelOverhang = popup_layout::contentScale(12);
  lv_obj_t* chart_wrap = lv_obj_create(body_box);
  ctx->chart_wrap = chart_wrap;
  lv_obj_remove_style_all(chart_wrap);
  lv_obj_set_size(chart_wrap, LV_PCT(100), kChartHeight + 2 * kLabelOverhang + kTimeAxisHeight);
  lv_obj_center(chart_wrap);
  lv_obj_set_style_bg_opa(chart_wrap, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(chart_wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(chart_wrap, LV_OBJ_FLAG_SCROLLABLE);

  // Layout: [labels] [chart fills rest]
  constexpr int kYAxisWidth = popup_layout::scale(48);
  const int kChartLeft = kYAxisWidth;
  const lv_font_t* y_font = popup_layout::font20();

  // Max label: vertically centered on top guide line (at y = kLabelOverhang)
  lv_obj_t* y_max = lv_label_create(chart_wrap);
  ctx->y_max_label = y_max;
  set_label_style(y_max, lv_color_white(), y_font);
  lv_obj_set_style_text_align(y_max, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(y_max, kYAxisWidth - 10);
  lv_label_set_text(y_max, "");
  lv_obj_set_pos(y_max, 0, 0);  // font center ~kLabelOverhang = on top guide line

  // Min label: vertically centered on bottom guide line (at y = kLabelOverhang + kChartHeight - 1)
  lv_obj_t* y_min = lv_label_create(chart_wrap);
  ctx->y_min_label = y_min;
  set_label_style(y_min, lv_color_white(), y_font);
  lv_obj_set_style_text_align(y_min, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(y_min, kYAxisWidth - 10);
  lv_label_set_text(y_min, "");
  lv_obj_set_pos(y_min, 0, kChartHeight);  // font center ~kChartHeight+kLabelOverhang = on bottom guide line

  // Horizontal guides at max/min extend 6 px left of the Y-axis for label alignment.
  constexpr int kLineOverlap = popup_layout::scale(6);
  const int kLineStart = kYAxisWidth - kLineOverlap;
  const int kLineWidth = kCardWidth - (kCardPad * 2) - kLineStart;
  auto make_guide_line = [&](int16_t y_pos) -> lv_obj_t* {
    lv_obj_t* line = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, kLineWidth, 1);
    lv_obj_set_style_bg_color(line, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(line, kLineStart, y_pos);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
  };
  ctx->y_max_line = make_guide_line(kLabelOverhang);
  ctx->y_min_line = make_guide_line(kLabelOverhang + kChartHeight - 1);

  // Vertical time marker lines (thin, same opacity as horizontal guides)
  // Created hidden; positioned and shown in apply_history_payload.
  for (int i = 0; i < kTimeAxisMarkerCount; ++i) {
    lv_obj_t* vline = lv_obj_create(chart_wrap);
    lv_obj_remove_style_all(vline);
    lv_obj_set_size(vline, 1, kChartHeight);
    lv_obj_set_style_bg_color(vline, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(vline, LV_OPA_30, 0);
    lv_obj_remove_flag(vline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(vline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(vline, 0, kLabelOverhang);
    lv_obj_add_flag(vline, LV_OBJ_FLAG_HIDDEN);
    ctx->time_lines[i] = vline;

    lv_obj_t* tlbl = lv_label_create(chart_wrap);
    set_label_style(tlbl, lv_color_white(), popup_layout::font20());
    lv_obj_set_style_text_align(tlbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(tlbl, LV_SIZE_CONTENT);
    lv_label_set_text(tlbl, "");
    lv_obj_set_pos(tlbl, 0, kLabelOverhang + kChartHeight + 2);
    lv_obj_add_flag(tlbl, LV_OBJ_FLAG_HIDDEN);
    ctx->time_labels[i] = tlbl;
  }

  lv_obj_t* chart = lv_chart_create(chart_wrap);
  ctx->chart = chart;
  lv_obj_set_size(chart, LV_PCT(100), kChartHeight);
  lv_obj_set_style_pad_left(chart, kChartLeft, 0);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_top(chart, 0, 0);
  lv_obj_set_style_pad_right(chart, 0, 0);
  lv_obj_set_style_pad_bottom(chart, 0, 0);
  lv_obj_set_pos(chart, 0, kLabelOverhang);
  lv_chart_set_div_line_count(chart, 0, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_line_width(chart, kChartLineWidth, LV_PART_ITEMS);
  lv_obj_set_style_line_color(chart, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

  ctx->series = lv_chart_add_series(chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);
  clear_chart(ctx, get_history_range_config(ctx->history_range).points);

  lv_obj_move_foreground(icon);
  lv_obj_move_foreground(title);
  lv_obj_move_foreground(range_row);
  lv_obj_move_foreground(close_btn);

  apply_init_to_context(ctx, init);

  lv_obj_add_event_cb(overlay, on_overlay_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
}

}  // namespace

bool sensor_popup_should_use_state_history(const String& value,
                                           const String& unit) {
  String normalized_unit = unit;
  normalized_unit.trim();
  if (normalized_unit.length()) return false;

  String normalized_value = value;
  normalized_value.trim();
  if (!normalized_value.length()) return false;
  String lower = normalized_value;
  lower.toLowerCase();
  if (lower == "unknown" || lower == "unavailable" || lower == "none" ||
      lower == "null") {
    return false;
  }

  char* end = nullptr;
  const float parsed = strtof(normalized_value.c_str(), &end);
  const bool numeric = end && end != normalized_value.c_str() &&
                       *end == '\0' && isfinite(parsed);
  return !numeric;
}

void show_sensor_popup(const SensorPopupInit& init) {
  hide_pin_popup();
  hide_camera_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;

  // Hide other popups if visible
  hide_light_popup();
  hide_weather_popup();
  hide_media_popup();

  if (g_sensor_popup_ctx && g_sensor_popup_ctx->overlay && g_sensor_popup_ctx->card) {
    apply_init_to_context(g_sensor_popup_ctx, init);
    g_sensor_popup_ctx->history_range = SensorHistoryRange::Day24;
    update_range_buttons(g_sensor_popup_ctx);
    if (g_sensor_popup_ctx->state_history_mode) {
      clear_binary_history(g_sensor_popup_ctx);
      if (g_sensor_popup_ctx->editable) clear_chart(g_sensor_popup_ctx, get_history_range_config(g_sensor_popup_ctx->history_range).points);
    } else {
      clear_chart(g_sensor_popup_ctx,
                  get_history_range_config(
                      g_sensor_popup_ctx->history_range).points);
    }
  } else {
    SensorPopupContext* ctx = new SensorPopupContext();
    g_sensor_popup_ctx = ctx;
    build_popup_ui(ctx, init);
  }
  set_sensor_popup_visible(g_sensor_popup_ctx, true);

  g_pending_history.valid = false;
  g_pending_binary_state.valid = false;
  set_range_buttons_visible(g_sensor_popup_ctx,
                            g_sensor_popup_ctx->state_history_mode);
  request_history_for_context(g_sensor_popup_ctx);
  if (g_sensor_popup_ctx && g_sensor_popup_ctx->card) viewNavigationPopupShown(g_sensor_popup_ctx->card, init.entity_id.c_str());
}

void preload_sensor_popup() {
  if (g_sensor_popup_ctx && g_sensor_popup_ctx->overlay && g_sensor_popup_ctx->card) return;
  SensorPopupInit init;
  init.entity_id = "__preload__";
  init.title = "";
  init.icon_name = "";
  init.value = "";
  init.unit = "";
  init.decimals = 0xFF;
  show_sensor_popup(init);
  if (g_sensor_popup_ctx && g_sensor_popup_ctx->card && g_sensor_popup_ctx->overlay) {
    set_sensor_popup_visible(g_sensor_popup_ctx, false);
  }
}

void hide_sensor_popup() {
  if (g_sensor_popup_ctx) editable_control_close(g_sensor_popup_ctx->control);
  if (!g_sensor_popup_ctx || !g_sensor_popup_ctx->card || !g_sensor_popup_ctx->overlay) return;
  set_sensor_popup_visible(g_sensor_popup_ctx, false);
}

void queue_sensor_popup_value(const char* entity_id, const char* value, const char* unit, uint8_t decimals) {
  if (!entity_id || !*entity_id || !value) return;
  g_pending_value.entity_id = entity_id;
  g_pending_value.value = value;
  g_pending_value.unit = unit ? unit : "";
  g_pending_value.decimals = decimals;
  g_pending_value.valid = true;
}

static String extract_history_entity_id(const String& payload) {
  int key = payload.indexOf("\"entity_id\"");
  if (key < 0) return "";
  int colon = payload.indexOf(':', key);
  if (colon < 0) return "";
  int q1 = payload.indexOf('"', colon + 1);
  if (q1 < 0) return "";
  int q2 = payload.indexOf('"', q1 + 1);
  if (q2 < 0) return "";
  String entity = payload.substring(q1 + 1, q2);
  entity.trim();
  return entity;
}

void queue_sensor_popup_icon_refresh() { g_pending_icon_refresh = true; }

void queue_sensor_popup_history(const char* entity_id, const char* payload, size_t len) {
  if (!payload || len == 0) return;
  if (!g_sensor_popup_ctx || !is_popup_visible(g_sensor_popup_ctx)) return;

  String payload_text = String(payload).substring(0, len);
  String incoming_entity = entity_id ? entity_id : "";
  incoming_entity.trim();
  if (!incoming_entity.length()) {
    incoming_entity = extract_history_entity_id(payload_text);
  }

  // Ignore history updates for other entities while this popup is open.
  if (incoming_entity.length() &&
      !g_sensor_popup_ctx->entity_id.equalsIgnoreCase(incoming_entity)) {
    return;
  }

  g_pending_history.entity_id = incoming_entity;
  g_pending_history.payload = payload_text;
  g_pending_history.valid = true;
}

void queue_sensor_popup_binary_state(const String& entity_id,
                                     const String& state,
                                     bool available,
                                     const String& device_class,
                                     uint64_t last_changed,
                                     const String& icon_name) {
  if (!entity_id.length()) return;
  g_pending_binary_state.entity_id = entity_id;
  g_pending_binary_state.state = state;
  g_pending_binary_state.available = available;
  g_pending_binary_state.device_class = device_class;
  g_pending_binary_state.last_changed = last_changed;
  g_pending_binary_state.icon_name = icon_name;
  g_pending_binary_state.valid = true;
}

void process_sensor_popup_queue() {
  if (!g_sensor_popup_ctx || !g_sensor_popup_ctx->card) {
    g_pending_value.valid = false;
    g_pending_history.valid = false;
    g_pending_binary_state.valid = false;
    return;
  }

  if (g_pending_icon_refresh) {
    g_pending_icon_refresh = false;
    if (is_popup_visible(g_sensor_popup_ctx)) refresh_editable_popup_icon(g_sensor_popup_ctx);
  }
  if (g_sensor_popup_ctx->editable && is_popup_visible(g_sensor_popup_ctx)) {
    auto* ctx = g_sensor_popup_ctx;
    editable_control_refresh(ctx->control);
    if (ctx->editable_generation != editable_value_generation()) {
    ctx->editable_generation = editable_value_generation();
    const EditableValue value = parse_editable_value(haBridgeConfig.findEditableValue(ctx->entity_id));
    if (value.valid && (ctx->editable_state != value.state || ctx->editable_kind != value.kind || ctx->editable_available != value.available)) {
      const bool kind_changed = ctx->editable_kind != value.kind;
      ctx->editable_state = value.state; ctx->editable_kind = value.kind;
      ctx->editable_available = value.available;
      if (value.last_changed) prepend_state_history_activity(ctx, value.last_changed, 2, value.available ? value.state : String("unavailable"));
      ctx->unit = value.unit;
      if (kind_changed) { layout_editable_history(ctx); request_history_for_context(ctx); }
      else schedule_state_history_refresh(ctx);
    }
    }
  }

  if (g_pending_binary_state.valid) {
    if (g_sensor_popup_ctx->binary_mode &&
        g_sensor_popup_ctx->entity_id.equalsIgnoreCase(
            g_pending_binary_state.entity_id) &&
        is_popup_visible(g_sensor_popup_ctx)) {
      const String previous_state = g_sensor_popup_ctx->binary_state;
      const bool previous_available = g_sensor_popup_ctx->binary_available;
      update_binary_state(g_sensor_popup_ctx, g_pending_binary_state.state,
                          g_pending_binary_state.available,
                          g_pending_binary_state.device_class,
                          g_pending_binary_state.last_changed,
                          g_pending_binary_state.icon_name);
      if (previous_state != g_sensor_popup_ctx->binary_state ||
          previous_available != g_sensor_popup_ctx->binary_available) {
        prepend_state_history_activity(
            g_sensor_popup_ctx,
            g_sensor_popup_ctx->binary_last_changed,
            binary_state_code(g_sensor_popup_ctx->binary_state,
                              g_sensor_popup_ctx->binary_available),
            g_sensor_popup_ctx->binary_state);
        schedule_state_history_refresh(g_sensor_popup_ctx);
      }
    }
    g_pending_binary_state.valid = false;
  }

  if (g_pending_value.valid) {
    if (!g_sensor_popup_ctx->editable && !g_sensor_popup_ctx->binary_mode &&
        g_sensor_popup_ctx->entity_id.equalsIgnoreCase(g_pending_value.entity_id) &&
        is_popup_visible(g_sensor_popup_ctx)) {
      const String previous_value = g_sensor_popup_ctx->state_history_value;
      g_sensor_popup_ctx->decimals = g_pending_value.decimals;
      String live_unit = g_sensor_popup_ctx->lock_unit ? g_sensor_popup_ctx->unit : g_pending_value.unit;
      update_value_label(g_sensor_popup_ctx, g_pending_value.value, live_unit);
      if (g_sensor_popup_ctx->state_history_mode) {
        const String next_value =
            normalize_state_live_value(g_pending_value.value);
        g_sensor_popup_ctx->state_history_value = next_value;
        if (next_value != previous_value) {
          prepend_state_history_activity(
              g_sensor_popup_ctx, static_cast<uint64_t>(time(nullptr)), 2,
              next_value);
          schedule_state_history_refresh(g_sensor_popup_ctx);
        }
      }
    }
    g_pending_value.valid = false;
  }

  if (g_pending_history.valid) {
    bool same_entity = true;
    if (g_pending_history.entity_id.length()) {
      same_entity = g_sensor_popup_ctx->entity_id.equalsIgnoreCase(g_pending_history.entity_id);
    }
    if (same_entity && is_popup_visible(g_sensor_popup_ctx)) {
      apply_history_payload(g_sensor_popup_ctx, g_pending_history.payload.c_str());
    }
    g_pending_history.valid = false;
  }

  if (g_sensor_popup_ctx->state_history_mode &&
      g_sensor_popup_ctx->state_history_refresh_pending &&
      is_popup_visible(g_sensor_popup_ctx) &&
      static_cast<uint32_t>(millis() -
                            g_sensor_popup_ctx->state_history_last_request_ms) >=
          state_history_refresh_interval_ms(g_sensor_popup_ctx)) {
    request_history_for_context(g_sensor_popup_ctx);
  }
}





