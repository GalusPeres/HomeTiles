#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/climate/climate_popup.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/climate/visuals.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"

#include <ctype.h>
#include <cstring>
#include <math.h>

namespace {

constexpr uint8_t kControlCount = 5;
constexpr uint8_t kMaxControlOptions = 10;
constexpr uint32_t kRemoteBlockMs = 2200;
constexpr uint32_t kAdjustmentDebounceMs = 1000;
constexpr uint32_t kModeDebounceMs = 160;
constexpr uint32_t kCardBg = 0x2A2A2A;
constexpr uint32_t kTrackColor = 0x444444;
constexpr uint32_t kPillBg = 0x363636;
constexpr uint32_t kPillPressedBg = 0x5A5A5A;
constexpr uint32_t kSelectedBg = 0x26A69A;
constexpr uint32_t kHumidityAccent = 0x29B6C8;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kGaugeSize = popup_layout::contentScale(380);
constexpr int kGaugeOffsetY = -popup_layout::contentScale(6);
constexpr int kGaugeRotation = 135;
constexpr int kGaugeSweep = 270;
constexpr int kArcWidth = popup_layout::contentScale(28);
constexpr int kControlButtonSize = popup_layout::contentScale(92);
constexpr int kPillGap = popup_layout::scale(8);
constexpr int kPillHeight = popup_layout::kNavHeight;
constexpr int kPillWidth =
    (popup_layout::kContentWidth - ((kControlCount - 1) * kPillGap)) /
    kControlCount;
constexpr int kPillLabelHeight = popup_layout::scale(32);
#if defined(DEVICE_LAYOUT_480X480)
// ui_font_14 needs its full 19 px line height so descenders such as the
// "g" in "Swing" are not clipped.
constexpr int kPillCaptionHeight = 19;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr int kPillCaptionHeight = 21;
#else
constexpr int kPillCaptionHeight = 22;
#endif
constexpr int kPillCaptionYWithValue = -popup_layout::scale(16);
constexpr int kPillValueY = popup_layout::scale(14);
constexpr int kMenuOptionHeight = popup_layout::scale(64);
constexpr int kMenuSeparatorAreaHeight = popup_layout::scale(9);
constexpr int kMenuSeparatorLineHeight = 1;
constexpr int kMenuSeparatorLineBottomInset = popup_layout::scale(4);
constexpr int kTargetToggleWidth = popup_layout::contentScale(188);
constexpr int kTargetToggleHeight = popup_layout::kNavHeight;
constexpr int kTargetToggleButtonSize = popup_layout::contentScale(80);
constexpr int kTargetToggleGap = popup_layout::scale(4);
constexpr int kTargetMarkerSize = popup_layout::contentScale(28);
constexpr int kCurrentMarkerSize = popup_layout::contentScale(14);
constexpr int kRangeLineCenterGap = popup_layout::contentScale(52);
constexpr int kRangeTargetTextHeight = popup_layout::contentScale(114);
constexpr int kRangeBlockVisualOffsetY = -popup_layout::contentScale(24);
#if defined(DEVICE_LAYOUT_480X480)
constexpr int kClimateContentLiftY = popup_layout::scale480(6);
constexpr int kClimateValueLiftY = kClimateContentLiftY + 2;
constexpr int kCurrentCaptionOpticalOffsetY = popup_layout::scale480(3);
#else
constexpr int kClimateContentLiftY = 0;
constexpr int kClimateValueLiftY = 0;
constexpr int kCurrentCaptionOpticalOffsetY = 0;
#endif
// Preserve the original 720 px distance between the state caption ("Heating")
// and the target value, scaled with the popup content.
constexpr int kStateCaptionOffsetY = -popup_layout::contentScale(62);
constexpr int kTargetLabelCenterY = -popup_layout::contentScale(2);
constexpr int kRangeTargetLabelCenterY =
    kGaugeOffsetY + (kRangeLineCenterGap / 2) +
    kRangeBlockVisualOffsetY;
constexpr int kRangeStateCenterY =
    kGaugeOffsetY - kRangeLineCenterGap +
    kRangeBlockVisualOffsetY;

static const lv_font_t* climate_current_value_font() {
#if defined(DEVICE_LAYOUT_480X480)
  return &ui_font_24;
#else
  return popup_layout::font32();
#endif
}

static const lv_font_t* climate_control_caption_font() {
#if defined(DEVICE_LAYOUT_480X480)
  return &ui_font_14;
#else
  return &ui_font_16;
#endif
}

enum class ClimateControlType : uint8_t {
  HVAC = 0,
  PRESET = 1,
  FAN = 2,
  SWING = 3,
  SWING_HORIZONTAL = 4
};

struct ClimatePopupControl {
  lv_obj_t* dropdown = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* caption = nullptr;
  lv_obj_t* value = nullptr;
  String options[kMaxControlOptions];
  uint8_t option_count = 0;
};

struct ClimatePopupContext {
  String entity_id;
  String icon_name;
  String temperature_unit = "\xC2\xB0" "C";
  String hvac_mode;
  String hvac_action;
  String preset_mode;
  String fan_mode;
  String swing_mode;
  String swing_horizontal_mode;
  float current_temperature = 0.0f;
  float current_humidity = 0.0f;
  float target_temperature = 20.0f;
  float target_humidity = 50.0f;
  float target_temp_low = 18.0f;
  float target_temp_high = 24.0f;
  float min_temp = 7.0f;
  float max_temp = 35.0f;
  float min_humidity = 30.0f;
  float max_humidity = 99.0f;
  float step = 0.5f;
  float humidity_step = 1.0f;
  bool has_current = false;
  bool has_humidity = false;
  bool has_target = false;
  bool has_target_humidity = false;
  bool has_range = false;
  bool humidity_control_active = false;
  bool range_drag_high = false;
  bool dragging = false;
  bool suppress_events = false;
  bool icon_visible = true;
  bool dynamic_icon = true;
  bool available = true;
  bool has_supported_features = false;
  uint16_t supported_features = 0;
  uint32_t block_remote_until_ms = 0;

  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* top_caption_label = nullptr;
  lv_obj_t* top_value_label = nullptr;
  lv_obj_t* body = nullptr;
  lv_obj_t* track_arc = nullptr;
  lv_obj_t* color_arc = nullptr;
  lv_obj_t* range_high_arc = nullptr;
  lv_obj_t* active_arc = nullptr;
  lv_obj_t* input_arc = nullptr;
  lv_obj_t* target_marker = nullptr;
  lv_obj_t* range_high_marker = nullptr;
  lv_obj_t* current_marker = nullptr;
  lv_obj_t* target_label = nullptr;
  lv_obj_t* state_caption = nullptr;
  lv_obj_t* minus_button = nullptr;
  lv_obj_t* plus_button = nullptr;
  lv_obj_t* minus_icon = nullptr;
  lv_obj_t* plus_icon = nullptr;
  lv_obj_t* target_toggle = nullptr;
  lv_obj_t* temperature_toggle_button = nullptr;
  lv_obj_t* humidity_toggle_button = nullptr;
  lv_obj_t* temperature_toggle_icon = nullptr;
  lv_obj_t* humidity_toggle_icon = nullptr;
  lv_obj_t* controls_row = nullptr;
  lv_obj_t* control_menu = nullptr;
  int8_t open_control_index = -1;
  uint8_t menu_option_indices[kMaxControlOptions] = {};
  lv_obj_t* menu_option_buttons[kMaxControlOptions] = {};
  uint8_t menu_option_button_count = 0;
  ClimatePopupControl controls[kControlCount];

  lv_timer_t* temperature_publish_timer = nullptr;
  lv_timer_t* humidity_publish_timer = nullptr;
  lv_timer_t* mode_publish_timer = nullptr;
  lv_timer_t* remote_apply_timer = nullptr;
  ClimatePopupInit pending_remote_init;
  bool has_pending_remote = false;
  String pending_temperature_entity;
  String pending_humidity_entity;
  float pending_target_temperature = 20.0f;
  float pending_target_humidity = 50.0f;
  float pending_target_temp_low = 18.0f;
  float pending_target_temp_high = 24.0f;
  bool pending_target_range = false;
  String pending_mode_entity;
  String pending_hvac_mode;
};

ClimatePopupContext* g_climate_popup = nullptr;

void close_control_menu(ClimatePopupContext* ctx);
void flush_pending_climate_commands(ClimatePopupContext* ctx);
void discard_pending_climate_commands(ClimatePopupContext* ctx);

float clamp_temperature(float value, float min_temp, float max_temp) {
  if (max_temp <= min_temp) {
    min_temp = 7.0f;
    max_temp = 35.0f;
  }
  if (value < min_temp) return min_temp;
  if (value > max_temp) return max_temp;
  return value;
}

float quantize_temperature(float value, float step) {
  if (step <= 0.0f || step > 10.0f) step = 0.5f;
  return roundf(value / step) * step;
}

int arc_value_for(float value) {
  return static_cast<int>(lroundf(value * 10.0f));
}

float temperature_for_arc(int value) {
  return static_cast<float>(value) / 10.0f;
}

float target_center(const ClimatePopupContext* ctx) {
  if (!ctx) return 20.0f;
  return ctx->has_range
             ? (ctx->target_temp_low + ctx->target_temp_high) * 0.5f
             : ctx->target_temperature;
}

float active_minimum(const ClimatePopupContext* ctx) {
  return ctx && ctx->humidity_control_active
             ? ctx->min_humidity
             : (ctx ? ctx->min_temp : 7.0f);
}

float active_maximum(const ClimatePopupContext* ctx) {
  return ctx && ctx->humidity_control_active
             ? ctx->max_humidity
             : (ctx ? ctx->max_temp : 35.0f);
}

float active_current(const ClimatePopupContext* ctx) {
  return ctx && ctx->humidity_control_active
             ? ctx->current_humidity
             : (ctx ? ctx->current_temperature : 0.0f);
}

float active_target(const ClimatePopupContext* ctx) {
  if (!ctx) return 20.0f;
  return ctx->humidity_control_active
             ? ctx->target_humidity
             : target_center(ctx);
}

bool climate_feature_supported(
    const ClimatePopupContext* ctx, uint16_t feature,
    bool legacy_supported) {
  if (!ctx) return false;
  if (!ctx->has_supported_features) return legacy_supported;
  return (ctx->supported_features & feature) != 0;
}

bool temperature_control_available(const ClimatePopupContext* ctx) {
  if (!ctx) return false;
  if (ctx->has_range) {
    return climate_feature_supported(
        ctx, CLIMATE_FEATURE_TARGET_TEMPERATURE_RANGE, true);
  }
  return ctx->has_target && climate_feature_supported(
                                ctx, CLIMATE_FEATURE_TARGET_TEMPERATURE, true);
}

bool humidity_control_available(const ClimatePopupContext* ctx) {
  return ctx && ctx->has_target_humidity && climate_feature_supported(
             ctx, CLIMATE_FEATURE_TARGET_HUMIDITY, true);
}

bool climate_control_available(
    const ClimatePopupContext* ctx, ClimateControlType type) {
  if (!ctx) return false;
  const ClimatePopupControl& control =
      ctx->controls[static_cast<uint8_t>(type)];
  if (control.option_count == 0) return false;
  switch (type) {
    case ClimateControlType::HVAC:
      return true;
    case ClimateControlType::PRESET:
      return climate_feature_supported(
          ctx, CLIMATE_FEATURE_PRESET_MODE, true);
    case ClimateControlType::FAN:
      return climate_feature_supported(ctx, CLIMATE_FEATURE_FAN_MODE, true);
    case ClimateControlType::SWING:
      return climate_feature_supported(ctx, CLIMATE_FEATURE_SWING_MODE, true);
    case ClimateControlType::SWING_HORIZONTAL:
      return climate_feature_supported(
          ctx, CLIMATE_FEATURE_SWING_HORIZONTAL_MODE, true);
  }
  return false;
}

int active_angle(const ClimatePopupContext* ctx, float value) {
  if (!ctx) return 0;
  const int minimum = arc_value_for(active_minimum(ctx));
  const int maximum = arc_value_for(active_maximum(ctx));
  const int mapped_value = arc_value_for(clamp_temperature(
      value, active_minimum(ctx), active_maximum(ctx)));
  return static_cast<int>(
      lv_map(mapped_value, minimum, maximum, 0, 270));
}

bool input_arc_reversed(const ClimatePopupContext* ctx) {
  if (!ctx || ctx->humidity_control_active) return false;
  if (ctx->has_range) return ctx->range_drag_high;
  return ctx->hvac_mode == "cool";
}

int input_arc_value_for(
    const ClimatePopupContext* ctx, float actual_value) {
  if (!ctx) return arc_value_for(actual_value);
  if (input_arc_reversed(ctx)) {
    actual_value =
        active_minimum(ctx) + active_maximum(ctx) - actual_value;
  }
  return arc_value_for(actual_value);
}

float actual_value_from_input_arc(
    const ClimatePopupContext* ctx, int arc_value) {
  float value = temperature_for_arc(arc_value);
  if (ctx && input_arc_reversed(ctx)) {
    value = active_minimum(ctx) + active_maximum(ctx) - value;
  }
  return value;
}

const char* mode_icon(const String& mode) {
  if (mode == "off") return "thermostat";
  if (mode == "heat") return "fire";
  if (mode == "cool") return "snowflake";
  if (mode == "heat_cool") return "sun-snowflake-variant";
  if (mode == "auto") return "thermostat-auto";
  if (mode == "dry") return "water-percent";
  if (mode == "fan_only") return "fan";
  return "thermostat";
}

const char* preset_icon(const String& preset) {
  if (preset == "eco") return "leaf";
  if (preset == "away") return "account-arrow-right";
  if (preset == "boost") return "rocket-launch";
  if (preset == "comfort") return "sofa";
  if (preset == "home") return "home";
  if (preset == "sleep") return "sleep";
  if (preset == "activity") return "run";
  return "circle-small";
}

bool action_is_active(const String& action) {
  return action.length() && action != "idle" && action != "off";
}

String optimistic_action_for_mode(
    const ClimatePopupContext* ctx, const String& mode) {
  if (mode == "off") return "off";
  if (mode == "heat") return "heating";
  if (mode == "cool") return "cooling";
  if (mode == "dry") return "drying";
  if (mode == "fan_only") return "fan";

  if ((mode == "heat_cool" || mode == "auto") && ctx &&
      ctx->has_current) {
    const float tolerance =
        (ctx->step > 0.0f ? ctx->step : 0.5f) * 0.5f;
    if (ctx->has_range) {
      if (ctx->current_temperature <
          ctx->target_temp_low - tolerance) {
        return "heating";
      }
      if (ctx->current_temperature >
          ctx->target_temp_high + tolerance) {
        return "cooling";
      }
    } else if (ctx->has_target) {
      if (ctx->current_temperature <
          ctx->target_temperature - tolerance) {
        return "heating";
      }
      if (ctx->current_temperature >
          ctx->target_temperature + tolerance) {
        return "cooling";
      }
    }
  }
  return "idle";
}

String dynamic_state_icon(
    const String& mode, const String& action, const String& base_icon) {
  if (action == "heating" || action == "preheating") return "fire";
  if (action == "cooling") return "snowflake";
  if (action == "drying") return "water-percent";
  if (action == "fan") return "fan";
  if (action == "defrosting") return "snowflake-melt";
  if (action == "idle") return mode_icon(mode);
  if (mode == "off" || action == "off" || action.length()) {
    return base_icon.length() ? base_icon : String("thermostat");
  }
  return mode_icon(mode);
}

void parse_options(
    ClimatePopupControl& control, String csv, const String& fallback) {
  control.option_count = 0;
  for (String& option : control.options) option = "";
  csv.replace("[", "");
  csv.replace("]", "");
  csv.replace("\"", "");
  csv.replace("'", "");
  int start = 0;
  while (start <= csv.length() &&
         control.option_count < kMaxControlOptions) {
    int comma = csv.indexOf(',', start);
    if (comma < 0) comma = csv.length();
    String option = csv.substring(start, comma);
    option.trim();
    option.toLowerCase();
    if (option.length()) {
      control.options[control.option_count++] = option;
    }
    if (comma >= csv.length()) break;
    start = comma + 1;
  }
  if (control.option_count == 0 && fallback.length()) {
    control.options[control.option_count++] = fallback;
  }
}

String* current_control_value(
    ClimatePopupContext* ctx, ClimateControlType type) {
  if (!ctx) return nullptr;
  switch (type) {
    case ClimateControlType::HVAC: return &ctx->hvac_mode;
    case ClimateControlType::PRESET: return &ctx->preset_mode;
    case ClimateControlType::FAN: return &ctx->fan_mode;
    case ClimateControlType::SWING: return &ctx->swing_mode;
    case ClimateControlType::SWING_HORIZONTAL:
      return &ctx->swing_horizontal_mode;
  }
  return nullptr;
}

const char* control_icon(
    ClimateControlType type, const String& current) {
  switch (type) {
    case ClimateControlType::HVAC: return mode_icon(current);
    case ClimateControlType::PRESET: return preset_icon(current);
    case ClimateControlType::FAN:
      return current == "off" ? "fan-off" : "fan";
    case ClimateControlType::SWING: return "swap-vertical";
    case ClimateControlType::SWING_HORIZONTAL: return "swap-horizontal";
  }
  return "circle-small";
}

void refresh_header_visuals(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->icon_label) return;
  if (!ctx->icon_visible) {
    lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  String icon_name =
      ctx->dynamic_icon
          ? dynamic_state_icon(
                ctx->hvac_mode, ctx->hvac_action, ctx->icon_name)
          : ctx->icon_name;
  String icon = getMdiChar(icon_name);
  if (!icon.length()) icon = getMdiChar("thermostat");
  lv_label_set_text(ctx->icon_label, icon.c_str());
  lv_obj_set_style_text_color(
      ctx->icon_label,
      lv_color_hex(
          ctx->available
              ? climate_visuals::state_foreground_color(
                    ctx->hvac_mode, ctx->hvac_action)
              : 0x9E9E9E),
      0);
  lv_obj_clear_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
}

void set_arc_segment(
    lv_obj_t* arc, int start_angle, int end_angle, bool visible) {
  if (!arc) return;
  if (!visible || end_angle - start_angle < 2) {
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_arc_set_bg_angles(arc, start_angle, end_angle);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
}

void align_marker_to_value(
    ClimatePopupContext* ctx, lv_obj_t* marker, float value) {
  if (!ctx || !ctx->body || !marker) return;
  const float angle =
      (static_cast<float>(kGaugeRotation + active_angle(ctx, value)) *
       kPi) /
      180.0f;
  const float radius =
      (static_cast<float>(kGaugeSize - kArcWidth)) * 0.5f;
  const float center_x =
      static_cast<float>(lv_obj_get_width(ctx->body)) * 0.5f;
  const float center_y =
      static_cast<float>(lv_obj_get_height(ctx->body)) * 0.5f +
      static_cast<float>(kGaugeOffsetY);
  const float marker_half_width =
      static_cast<float>(lv_obj_get_width(marker)) * 0.5f;
  const float marker_half_height =
      static_cast<float>(lv_obj_get_height(marker)) * 0.5f;
  lv_obj_set_pos(
      marker,
      static_cast<lv_coord_t>(lroundf(
          center_x + cosf(angle) * radius - marker_half_width)),
      static_cast<lv_coord_t>(lroundf(
          center_y + sinf(angle) * radius - marker_half_height)));
}

void refresh_step_buttons(ClimatePopupContext* ctx) {
  if (!ctx) return;
  const lv_color_t accent = lv_color_white();
  if (ctx->minus_icon) {
    lv_obj_set_style_text_color(ctx->minus_icon, accent, 0);
  }
  if (ctx->plus_icon) {
    lv_obj_set_style_text_color(ctx->plus_icon, accent, 0);
  }
  auto apply_pressed = [&](lv_obj_t* button) {
    if (!button) return;
    lv_obj_set_style_bg_color(button, accent, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
  };
  apply_pressed(ctx->minus_button);
  apply_pressed(ctx->plus_button);
}

void set_control_disabled(lv_obj_t* object, bool disabled) {
  if (!object) return;
  if (disabled) {
    lv_obj_add_state(object, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(object, LV_STATE_DISABLED);
  }
}

void refresh_interactive_state(ClimatePopupContext* ctx) {
  if (!ctx) return;
  const bool target_supported =
      ctx->humidity_control_active ? humidity_control_available(ctx)
                                   : temperature_control_available(ctx);
  const bool target_disabled = !ctx->available || !target_supported;
  set_control_disabled(ctx->input_arc, target_disabled);
  set_control_disabled(ctx->minus_button, target_disabled);
  set_control_disabled(ctx->plus_button, target_disabled);
  set_control_disabled(ctx->target_toggle, !ctx->available);
  set_control_disabled(ctx->temperature_toggle_button, !ctx->available);
  set_control_disabled(ctx->humidity_toggle_button, !ctx->available);
  for (uint8_t i = 0; i < kControlCount; ++i) {
    set_control_disabled(
        ctx->controls[i].dropdown,
        !ctx->available || !climate_control_available(
                               ctx, static_cast<ClimateControlType>(i)));
  }
}

void refresh_current_marker(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->current_marker) return;
  if (!ctx->available) {
    lv_obj_add_flag(ctx->current_marker, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const bool has_current =
      ctx->humidity_control_active ? ctx->has_humidity : ctx->has_current;
  if (!has_current) {
    lv_obj_add_flag(ctx->current_marker, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const bool range =
      !ctx->humidity_control_active && ctx->has_range;
  const int current_angle =
      active_angle(ctx, active_current(ctx));
  bool current_on_colored_ring = false;
  if (ctx->hvac_mode != "off") {
    if (range) {
      const int low_angle =
          active_angle(ctx, ctx->target_temp_low);
      const int high_angle =
          active_angle(ctx, ctx->target_temp_high);
      current_on_colored_ring =
          current_angle <= low_angle || current_angle >= high_angle;
    } else {
      const int target_angle =
          active_angle(ctx, active_target(ctx));
      if (ctx->humidity_control_active ||
          ctx->hvac_mode == "heat") {
        current_on_colored_ring =
            current_angle <= target_angle;
      } else if (ctx->hvac_mode == "cool") {
        current_on_colored_ring =
            current_angle >= target_angle;
      } else {
        current_on_colored_ring = true;
      }
    }
  }

  lv_obj_set_style_bg_color(
      ctx->current_marker,
      lv_color_hex(
          current_on_colored_ring ? kTrackColor : 0xA8A8A8),
      0);
  lv_obj_clear_flag(ctx->current_marker, LV_OBJ_FLAG_HIDDEN);
  align_marker_to_value(
      ctx, ctx->current_marker, active_current(ctx));
  lv_obj_move_foreground(ctx->current_marker);
}

void refresh_ring(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->body) return;
  const bool range =
      !ctx->humidity_control_active && ctx->has_range;
  const float target =
      range
          ? (ctx->range_drag_high
                 ? ctx->target_temp_high
                 : ctx->target_temp_low)
          : active_target(ctx);
  const int target_angle = active_angle(ctx, target);
  const int low_angle =
      range ? active_angle(ctx, ctx->target_temp_low) : target_angle;
  const int high_angle =
      range ? active_angle(ctx, ctx->target_temp_high) : target_angle;
  const int current_angle = active_angle(ctx, active_current(ctx));
  const uint32_t accent =
      ctx->humidity_control_active
          ? kHumidityAccent
          : climate_visuals::mode_ring_color(ctx->hvac_mode);
  const uint32_t foreground_accent =
      ctx->humidity_control_active
          ? kHumidityAccent
          : climate_visuals::mode_foreground_color(ctx->hvac_mode);
  const uint32_t action_accent =
      climate_visuals::state_foreground_color(
          ctx->hvac_mode, ctx->hvac_action);
  const uint32_t heat_accent =
      climate_visuals::mode_ring_color("heat");
  const uint32_t heat_foreground =
      climate_visuals::mode_foreground_color("heat");
  const uint32_t cool_accent =
      climate_visuals::mode_ring_color("cool");
  const uint32_t cool_foreground =
      climate_visuals::mode_foreground_color("cool");
  const bool off = !ctx->available || ctx->hvac_mode == "off";
  const bool has_current =
      ctx->humidity_control_active ? ctx->has_humidity : ctx->has_current;
  const bool has_target =
      ctx->humidity_control_active
          ? humidity_control_available(ctx)
          : (ctx->has_target || ctx->has_range);
  if (ctx->color_arc) {
    lv_obj_set_style_arc_color(
        ctx->color_arc,
        lv_color_hex(range ? heat_accent : accent),
        LV_PART_MAIN);
    lv_obj_set_style_arc_opa(
        ctx->color_arc,
        (!ctx->humidity_control_active && ctx->hvac_mode == "dry")
            ? LV_OPA_COVER
            : static_cast<lv_opa_t>(145),
        LV_PART_MAIN);
    if (range) {
      set_arc_segment(ctx->color_arc, 0, low_angle, !off);
    } else if (ctx->humidity_control_active || ctx->hvac_mode == "heat") {
      set_arc_segment(ctx->color_arc, 0, target_angle, !off);
    } else if (ctx->hvac_mode == "cool") {
      set_arc_segment(ctx->color_arc, target_angle, 270, !off);
    } else {
      set_arc_segment(ctx->color_arc, 0, 270, !off);
    }
  }
  if (ctx->range_high_arc) {
    lv_obj_set_style_arc_color(
        ctx->range_high_arc, lv_color_hex(cool_accent), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(
        ctx->range_high_arc, static_cast<lv_opa_t>(145), LV_PART_MAIN);
    set_arc_segment(
        ctx->range_high_arc, high_angle, kGaugeSweep, range && !off);
  }

  bool show_active = false;
  int active_start = 0;
  int active_end = 0;
  uint32_t active_color = accent;
  if (!off && has_current) {
    if (range && current_angle <= low_angle) {
      active_start = current_angle;
      active_end = low_angle;
      active_color = heat_foreground;
      show_active = true;
    } else if (range && current_angle >= high_angle) {
      active_start = high_angle;
      active_end = current_angle;
      active_color = cool_foreground;
      show_active = true;
    } else if (!range &&
               (ctx->humidity_control_active || ctx->hvac_mode == "heat") &&
               current_angle <= target_angle) {
      active_start = current_angle;
      active_end = target_angle;
      show_active = true;
    } else if (!range && ctx->hvac_mode == "cool" &&
               current_angle >= target_angle) {
      active_start = target_angle;
      active_end = current_angle;
      show_active = true;
    }
  }
  if (ctx->active_arc) {
    lv_obj_set_style_arc_color(
        ctx->active_arc, lv_color_hex(active_color), LV_PART_MAIN);
    set_arc_segment(
        ctx->active_arc, active_start, active_end, show_active);
  }

  if (ctx->input_arc) {
    const bool reversed = input_arc_reversed(ctx);
    const uint32_t input_color =
        range
            ? (ctx->range_drag_high ? cool_accent : heat_accent)
            : accent;
    const uint32_t input_foreground =
        range
            ? (ctx->range_drag_high ? cool_foreground : heat_foreground)
            : foreground_accent;
    const bool show_native_segment =
        ctx->dragging && !off && has_target &&
        (range || ctx->humidity_control_active ||
         ctx->hvac_mode == "heat" || ctx->hvac_mode == "cool");

    ctx->suppress_events = true;
    lv_arc_set_mode(
        ctx->input_arc,
        reversed ? LV_ARC_MODE_REVERSE : LV_ARC_MODE_NORMAL);
    lv_arc_set_range(
        ctx->input_arc,
        arc_value_for(active_minimum(ctx)),
        arc_value_for(active_maximum(ctx)));
    lv_arc_set_value(
        ctx->input_arc, input_arc_value_for(ctx, target));
    ctx->suppress_events = false;

    lv_obj_set_style_arc_color(
        ctx->input_arc, lv_color_hex(input_color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(
        ctx->input_arc,
        show_native_segment ? static_cast<lv_opa_t>(145) : LV_OPA_TRANSP,
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(
        ctx->input_arc,
        has_target && !off ? LV_OPA_COVER : LV_OPA_TRANSP,
        LV_PART_KNOB);
    lv_obj_set_style_border_opa(
        ctx->input_arc,
        has_target && !off ? LV_OPA_COVER : LV_OPA_TRANSP,
        LV_PART_KNOB);
    lv_obj_set_style_border_color(
        ctx->input_arc, lv_color_hex(input_foreground), LV_PART_KNOB);

    if (show_native_segment) {
      if (range && ctx->range_drag_high) {
        lv_obj_add_flag(ctx->range_high_arc, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(ctx->color_arc, LV_OBJ_FLAG_HIDDEN);
      }
      if (ctx->active_arc) {
        lv_obj_add_flag(ctx->active_arc, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  refresh_current_marker(ctx);
  if (ctx->target_marker) {
    if (!has_target || !range ||
        (!off && !ctx->range_drag_high)) {
      lv_obj_add_flag(ctx->target_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_style_border_color(
          ctx->target_marker,
          lv_color_hex(off ? kTrackColor : heat_foreground),
          0);
      lv_obj_clear_flag(ctx->target_marker, LV_OBJ_FLAG_HIDDEN);
      align_marker_to_value(
          ctx, ctx->target_marker, ctx->target_temp_low);
      lv_obj_move_foreground(ctx->target_marker);
    }
  }
  if (ctx->range_high_marker) {
    if (!has_target || !range ||
        (!off && ctx->range_drag_high)) {
      lv_obj_add_flag(ctx->range_high_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_style_border_color(
          ctx->range_high_marker,
          lv_color_hex(off ? kTrackColor : cool_foreground),
          0);
      lv_obj_clear_flag(ctx->range_high_marker, LV_OBJ_FLAG_HIDDEN);
      align_marker_to_value(
          ctx, ctx->range_high_marker, ctx->target_temp_high);
      lv_obj_move_foreground(ctx->range_high_marker);
    }
  }

  if (ctx->state_caption) {
    const bool idle = ctx->hvac_action == "idle";
    const uint32_t caption_color =
        ctx->humidity_control_active
            ? 0xFFFFFF
            : (action_is_active(ctx->hvac_action)
                   ? action_accent
                   : ((off || idle) ? 0xFFFFFF : accent));
    lv_obj_set_style_text_color(
        ctx->state_caption,
        lv_color_hex(caption_color),
        0);
  }
  refresh_step_buttons(ctx);
}

void refresh_target_label(
    ClimatePopupContext* ctx, bool refresh_layout = true) {
  if (!ctx) return;
  const char* language = configManager.getConfig().language;
  const bool show_range =
      !ctx->humidity_control_active && ctx->has_range;

  String target;
  if (ctx->humidity_control_active) {
    target = humidity_control_available(ctx)
                 ? i18n::format_number(
                       language, ctx->target_humidity, 0)
                 : String("--");
    target += " %";
  } else if (show_range) {
    target = i18n::format_number(language, ctx->target_temp_low, 1);
    target += " ";
    target += ctx->temperature_unit;
    target += "\n";
    target += i18n::format_number(language, ctx->target_temp_high, 1);
    target += " ";
    target += ctx->temperature_unit;
  } else if (ctx->has_target) {
    target = i18n::format_number(language, ctx->target_temperature, 1);
  } else {
    target = "--";
  }
  if (!ctx->humidity_control_active && !show_range) {
    target += " ";
    target += ctx->temperature_unit;
  }
  if (ctx->target_label) {
    lv_label_set_text(ctx->target_label, target.c_str());
    if (refresh_layout) {
      lv_obj_set_style_text_font(
          ctx->target_label,
          show_range ? popup_layout::font40() : popup_layout::font48(), 0);
      lv_obj_set_style_text_line_space(
          ctx->target_label, show_range ? -10 : 0, 0);
      lv_obj_set_height(
          ctx->target_label,
          show_range ? kRangeTargetTextHeight : LV_SIZE_CONTENT);
      lv_obj_align(
          ctx->target_label, LV_ALIGN_CENTER, 0,
          show_range ? kRangeTargetLabelCenterY : kTargetLabelCenterY);
    }
  }
}

void refresh_target_toggle(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->target_toggle) return;
  const bool visible = humidity_control_available(ctx);
  if (!visible) {
    ctx->humidity_control_active = false;
    lv_obj_add_flag(ctx->target_toggle, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(ctx->target_toggle, LV_OBJ_FLAG_HIDDEN);
  auto apply_button = [](lv_obj_t* button, lv_obj_t* icon, bool selected) {
    if (!button) return;
    lv_obj_set_style_bg_color(
        button, selected ? lv_color_white() : lv_color_hex(kPillBg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    if (icon) {
      lv_obj_set_style_text_color(
          icon,
          selected ? lv_color_hex(kPillBg) : lv_color_white(),
          0);
      lv_obj_set_style_text_color(
          icon, lv_color_white(), LV_STATE_PRESSED);
    }
  };
  apply_button(
      ctx->temperature_toggle_button,
      ctx->temperature_toggle_icon,
      !ctx->humidity_control_active);
  apply_button(
      ctx->humidity_toggle_button,
      ctx->humidity_toggle_icon,
      ctx->humidity_control_active);
}

void refresh_state_caption(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->state_caption) return;
  const char* language = configManager.getConfig().language;
  const bool state_only = !ctx->available || ctx->hvac_mode == "off";
  const String caption = !ctx->available
                             ? String(i18n::entity_state_label(
                                   language, "unavailable"))
                         : ctx->humidity_control_active && !state_only
                             ? String(i18n::climate_target_humidity_label(
                                   language))
                             : String(i18n::climate_state_label(
                                   language, ctx->hvac_mode,
                                   ctx->hvac_action));
  lv_label_set_text(ctx->state_caption, caption.c_str());
  lv_obj_set_style_text_font(
      ctx->state_caption,
      state_only ? popup_layout::font40() : popup_layout::font20(), 0);
  const bool show_range =
      !ctx->humidity_control_active && ctx->has_range;
  lv_obj_align(
      ctx->state_caption, LV_ALIGN_CENTER, 0,
      state_only
          ? kTargetLabelCenterY
          : (show_range ? kRangeStateCenterY : kStateCaptionOffsetY));
  if (ctx->target_label) {
    if (state_only) {
      lv_obj_add_flag(ctx->target_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(ctx->target_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void refresh_labels(ClimatePopupContext* ctx) {
  if (!ctx) return;
  const char* language = configManager.getConfig().language;
  refresh_target_label(ctx);

  const bool show_temperature = ctx->available && ctx->has_current;
  const bool show_humidity = ctx->available && ctx->has_humidity;
  if (ctx->top_caption_label) {
    String caption = i18n::climate_value_label(language, 0);
    lv_label_set_text(ctx->top_caption_label, caption.c_str());
  }
  if (ctx->top_value_label) {
    String value;
    if (show_temperature) {
      value += i18n::format_number(
          language, ctx->current_temperature, 1);
      value += " ";
      value += ctx->temperature_unit;
    }
    if (show_temperature && show_humidity) {
      value += "   ";
    }
    if (show_humidity) {
      value += i18n::format_number(
          language, ctx->current_humidity, 0);
      value += "%";
    }
    if (!show_temperature && !show_humidity) {
      value += "--";
    }
    lv_label_set_text(ctx->top_value_label, value.c_str());
  }
  refresh_state_caption(ctx);
  refresh_target_toggle(ctx);
}

void refresh_control(
    ClimatePopupContext* ctx, ClimateControlType type) {
  if (!ctx) return;
  const uint8_t index = static_cast<uint8_t>(type);
  ClimatePopupControl& control = ctx->controls[index];
  if (!control.dropdown) return;
  if (!climate_control_available(ctx, type)) {
    lv_obj_add_flag(control.dropdown, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(control.dropdown, LV_OBJ_FLAG_HIDDEN);

  const char* language = configManager.getConfig().language;
  String* current = current_control_value(ctx, type);

  const bool has_current_value = current && current->length();
  const String current_text =
      has_current_value
          ? i18n::climate_option_label(language, *current)
          : String();
  const bool colored_mode =
      ctx->available && type == ClimateControlType::HVAC && current &&
      current->length() && !current->equalsIgnoreCase("off");
  const lv_color_t control_bg =
      colored_mode
          ? lv_color_hex(
                climate_visuals::mode_foreground_color(ctx->hvac_mode))
          : lv_color_hex(kPillBg);
  const lv_color_t control_text =
      colored_mode ? lv_color_hex(kCardBg) : lv_color_white();
  lv_obj_set_style_bg_color(control.dropdown, control_bg, 0);
  lv_obj_set_style_bg_opa(control.dropdown, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(
      control.dropdown,
      colored_mode ? control_bg : lv_color_hex(kPillPressedBg),
      LV_STATE_PRESSED);
  if (control.caption) {
    lv_label_set_text(
        control.caption,
        i18n::climate_control_label(language, index));
    lv_obj_align(
        control.caption, LV_ALIGN_CENTER, 0,
        has_current_value ? kPillCaptionYWithValue : 0);
    lv_obj_set_style_text_color(
        control.caption,
        colored_mode ? lv_color_hex(kCardBg) : lv_color_hex(0xD0D0D0),
        0);
  }
  if (control.value) {
    lv_label_set_text(control.value, current_text.c_str());
    lv_obj_set_style_text_font(control.value, popup_layout::font24(), 0);
    lv_obj_set_style_text_outline_stroke_color(
        control.value, control_text, 0);
    lv_obj_set_style_text_outline_stroke_width(
        control.value, colored_mode ? 2 : 0, 0);
    lv_obj_set_style_text_outline_stroke_opa(
        control.value, colored_mode ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (has_current_value) {
      lv_obj_clear_flag(control.value, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(
          control.value, LV_ALIGN_CENTER, 0, kPillValueY);
    } else {
      lv_obj_add_flag(control.value, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(control.value, control_text, 0);
  }
  if (control.icon) {
    String icon = getMdiChar(
        control_icon(type, current ? *current : String()));
    if (!icon.length()) icon = getMdiChar("circle-small");
    lv_label_set_text(control.icon, icon.c_str());
    lv_obj_set_style_text_color(
        control.icon,
        colored_mode
            ? lv_color_hex(kCardBg)
            : lv_color_hex(
                  type == ClimateControlType::HVAC
                      ? climate_visuals::mode_foreground_color(ctx->hvac_mode)
                      : 0xFFFFFF),
        0);
  }
}

void refresh_controls(ClimatePopupContext* ctx) {
  if (!ctx) return;
  for (uint8_t i = 0; i < kControlCount; ++i) {
    refresh_control(ctx, static_cast<ClimateControlType>(i));
  }

  if (ctx->controls_row) {
    lv_obj_set_height(ctx->controls_row, kPillHeight);
    lv_obj_align(
        ctx->controls_row, LV_ALIGN_BOTTOM_MID, 0,
        -popup_layout::kNavBottomInset);
    lv_obj_set_flex_flow(ctx->controls_row, LV_FLEX_FLOW_ROW);
  }
  if (ctx->target_toggle) {
    lv_obj_align(
        ctx->target_toggle, LV_ALIGN_BOTTOM_MID, 0, -2);
  }
  for (uint8_t i = 0; i < kControlCount; ++i) {
    ClimatePopupControl& control = ctx->controls[i];
    if (!control.dropdown || !climate_control_available(
                                 ctx, static_cast<ClimateControlType>(i))) {
      continue;
    }
    lv_obj_set_width(control.dropdown, kPillWidth);
    lv_obj_set_flex_grow(control.dropdown, 0);
    if (control.value) lv_obj_set_width(control.value, kPillWidth - 16);
  }
}

void refresh_all(ClimatePopupContext* ctx) {
  refresh_header_visuals(ctx);
  refresh_labels(ctx);
  refresh_ring(ctx);
  refresh_controls(ctx);
  refresh_interactive_state(ctx);
}

void apply_init(ClimatePopupContext* ctx, const ClimatePopupInit& init) {
  if (!ctx) return;
  const bool same_entity =
      ctx->entity_id.equalsIgnoreCase(init.entity_id);
  if (!same_entity) {
    flush_pending_climate_commands(ctx);
  }
  ctx->entity_id = init.entity_id;
  ctx->icon_name = init.icon_name;
  ctx->icon_visible = init.icon_visible;
  ctx->dynamic_icon = init.dynamic_icon;
  ctx->available = init.available;
  ctx->has_supported_features = init.has_supported_features;
  ctx->supported_features = init.supported_features;
  ctx->hvac_mode = init.hvac_mode;
  ctx->hvac_mode.toLowerCase();
  ctx->hvac_action = init.hvac_action;
  ctx->hvac_action.toLowerCase();
  ctx->preset_mode = init.preset_mode;
  ctx->preset_mode.toLowerCase();
  ctx->fan_mode = init.fan_mode;
  ctx->fan_mode.toLowerCase();
  ctx->swing_mode = init.swing_mode;
  ctx->swing_mode.toLowerCase();
  ctx->swing_horizontal_mode = init.swing_horizontal_mode;
  ctx->swing_horizontal_mode.toLowerCase();
  ctx->temperature_unit =
      init.temperature_unit.length()
          ? init.temperature_unit
          : String("\xC2\xB0" "C");
  ctx->has_current = init.has_current_temperature;
  ctx->has_humidity = init.has_current_humidity;
  ctx->has_target = init.has_target_temperature;
  ctx->has_target_humidity = init.has_target_humidity;
  ctx->has_range =
      init.has_target_range && !init.has_target_temperature;
  if (!same_entity || !ctx->has_range) {
    ctx->range_drag_high = false;
  }
  ctx->current_temperature = init.current_temperature;
  ctx->current_humidity = init.current_humidity;
  ctx->target_temperature =
      init.has_target_temperature
          ? init.target_temperature
          : (init.has_target_range
                 ? (init.target_temp_low + init.target_temp_high) * 0.5f
                 : 20.0f);
  ctx->target_humidity =
      init.has_target_humidity
          ? init.target_humidity
          : init.current_humidity;
  ctx->target_temp_low = init.target_temp_low;
  ctx->target_temp_high = init.target_temp_high;
  ctx->min_temp = init.min_temp;
  ctx->max_temp = init.max_temp;
  ctx->min_humidity = init.min_humidity;
  ctx->max_humidity = init.max_humidity;
  ctx->step = init.target_temp_step;
  ctx->humidity_step = init.target_humidity_step;
  if (ctx->max_temp <= ctx->min_temp) {
    ctx->min_temp = 7.0f;
    ctx->max_temp = 35.0f;
  }
  if (ctx->step <= 0.0f || ctx->step > 10.0f) ctx->step = 0.5f;
  if (ctx->humidity_step <= 0.0f || ctx->humidity_step > 100.0f) {
    ctx->humidity_step = 1.0f;
  }
  if (ctx->max_humidity <= ctx->min_humidity) {
    ctx->min_humidity = 30.0f;
    ctx->max_humidity = 99.0f;
  }
  if (!humidity_control_available(ctx)) {
    ctx->humidity_control_active = false;
  }

  if (ctx->title_label) {
    hometiles_title::set(ctx->title_label, init.title.c_str());
  }
  parse_options(
      ctx->controls[static_cast<uint8_t>(ClimateControlType::HVAC)],
      init.hvac_modes, ctx->hvac_mode);
  parse_options(
      ctx->controls[static_cast<uint8_t>(ClimateControlType::PRESET)],
      init.preset_modes, ctx->preset_mode);
  parse_options(
      ctx->controls[static_cast<uint8_t>(ClimateControlType::FAN)],
      init.fan_modes, ctx->fan_mode);
  parse_options(
      ctx->controls[static_cast<uint8_t>(ClimateControlType::SWING)],
      init.swing_modes, ctx->swing_mode);
  parse_options(
      ctx->controls[
          static_cast<uint8_t>(ClimateControlType::SWING_HORIZONTAL)],
      init.swing_horizontal_modes, ctx->swing_horizontal_mode);
  if (!ctx->available) {
    ctx->dragging = false;
    discard_pending_climate_commands(ctx);
  }
  if (!ctx->available ||
      (ctx->open_control_index >= 0 &&
       !climate_control_available(
           ctx, static_cast<ClimateControlType>(
                    ctx->open_control_index)))) {
    close_control_menu(ctx);
  }
  refresh_all(ctx);
}

void cancel_deferred_remote_apply(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->remote_apply_timer) {
    lv_timer_delete(ctx->remote_apply_timer);
    ctx->remote_apply_timer = nullptr;
  }
  ctx->has_pending_remote = false;
}

void remote_apply_timer_cb(lv_timer_t* timer) {
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_timer_get_user_data(timer));
  if (!ctx) {
    lv_timer_delete(timer);
    return;
  }
  if (ctx->dragging || millis() < ctx->block_remote_until_ms) {
    const uint32_t now = millis();
    const uint32_t remaining =
        ctx->block_remote_until_ms > now
            ? (ctx->block_remote_until_ms - now + 20)
            : 50;
    lv_timer_set_period(timer, remaining);
    lv_timer_reset(timer);
    return;
  }

  ctx->remote_apply_timer = nullptr;
  lv_timer_delete(timer);
  if (!ctx->has_pending_remote) return;
  ClimatePopupInit pending = ctx->pending_remote_init;
  ctx->has_pending_remote = false;
  if (!ctx->card || lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN) ||
      !ctx->entity_id.equalsIgnoreCase(pending.entity_id)) {
    return;
  }
  apply_init(ctx, pending);
}

void defer_remote_apply(
    ClimatePopupContext* ctx, const ClimatePopupInit& init) {
  if (!ctx) return;
  ctx->pending_remote_init = init;
  ctx->has_pending_remote = true;
  const uint32_t now = millis();
  const uint32_t delay_ms =
      ctx->block_remote_until_ms > now
          ? (ctx->block_remote_until_ms - now + 20)
          : 50;
  if (ctx->remote_apply_timer) {
    lv_timer_set_period(ctx->remote_apply_timer, delay_ms);
    lv_timer_reset(ctx->remote_apply_timer);
    return;
  }
  ctx->remote_apply_timer =
      lv_timer_create(remote_apply_timer_cb, delay_ms, ctx);
}

void publish_pending_temperature(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->pending_temperature_entity.length()) return;
  ctx->block_remote_until_ms = millis() + kRemoteBlockMs;
  mqttPublishClimateTemperature(
      ctx->pending_temperature_entity.c_str(),
      ctx->pending_target_temperature,
      ctx->pending_target_range,
      ctx->pending_target_temp_low,
      ctx->pending_target_temp_high);
  ctx->pending_temperature_entity = "";
}

void capture_pending_temperature(ClimatePopupContext* ctx) {
  if (!ctx) return;
  ctx->pending_temperature_entity = ctx->entity_id;
  ctx->pending_target_temperature = ctx->target_temperature;
  ctx->pending_target_range = ctx->has_range;
  ctx->pending_target_temp_low = ctx->target_temp_low;
  ctx->pending_target_temp_high = ctx->target_temp_high;
  ctx->block_remote_until_ms = millis() + kRemoteBlockMs;
}

void commit_pending_temperature(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->temperature_publish_timer) {
    lv_timer_delete(ctx->temperature_publish_timer);
    ctx->temperature_publish_timer = nullptr;
  }
  publish_pending_temperature(ctx);
}

void temperature_publish_timer_cb(lv_timer_t* timer) {
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_timer_get_user_data(timer));
  if (!ctx) return;
  ctx->temperature_publish_timer = nullptr;
  publish_pending_temperature(ctx);
}

void schedule_temperature_publish(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->temperature_publish_timer &&
      ctx->pending_temperature_entity.length() &&
      !ctx->pending_temperature_entity.equalsIgnoreCase(ctx->entity_id)) {
    lv_timer_delete(ctx->temperature_publish_timer);
    ctx->temperature_publish_timer = nullptr;
    publish_pending_temperature(ctx);
  }
  capture_pending_temperature(ctx);
  if (ctx->temperature_publish_timer) {
    lv_timer_reset(ctx->temperature_publish_timer);
    return;
  }
  ctx->temperature_publish_timer =
      lv_timer_create(
          temperature_publish_timer_cb, kAdjustmentDebounceMs, ctx);
  if (ctx->temperature_publish_timer) {
    lv_timer_set_repeat_count(ctx->temperature_publish_timer, 1);
  } else {
    publish_pending_temperature(ctx);
  }
}

void publish_pending_humidity(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->pending_humidity_entity.length()) return;
  ctx->block_remote_until_ms = millis() + kRemoteBlockMs;
  mqttPublishClimateHumidity(
      ctx->pending_humidity_entity.c_str(),
      ctx->pending_target_humidity);
  ctx->pending_humidity_entity = "";
}

void capture_pending_humidity(ClimatePopupContext* ctx) {
  if (!ctx) return;
  ctx->pending_humidity_entity = ctx->entity_id;
  ctx->pending_target_humidity = ctx->target_humidity;
  ctx->block_remote_until_ms = millis() + kRemoteBlockMs;
}

void commit_pending_humidity(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->humidity_publish_timer) {
    lv_timer_delete(ctx->humidity_publish_timer);
    ctx->humidity_publish_timer = nullptr;
  }
  publish_pending_humidity(ctx);
}

void humidity_publish_timer_cb(lv_timer_t* timer) {
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_timer_get_user_data(timer));
  if (!ctx) return;
  ctx->humidity_publish_timer = nullptr;
  publish_pending_humidity(ctx);
}

void schedule_humidity_publish(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->humidity_publish_timer &&
      ctx->pending_humidity_entity.length() &&
      !ctx->pending_humidity_entity.equalsIgnoreCase(ctx->entity_id)) {
    lv_timer_delete(ctx->humidity_publish_timer);
    ctx->humidity_publish_timer = nullptr;
    publish_pending_humidity(ctx);
  }
  capture_pending_humidity(ctx);
  if (ctx->humidity_publish_timer) {
    lv_timer_reset(ctx->humidity_publish_timer);
    return;
  }
  ctx->humidity_publish_timer =
      lv_timer_create(
          humidity_publish_timer_cb, kAdjustmentDebounceMs, ctx);
  if (ctx->humidity_publish_timer) {
    lv_timer_set_repeat_count(ctx->humidity_publish_timer, 1);
  } else {
    publish_pending_humidity(ctx);
  }
}

void publish_pending_mode(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->pending_mode_entity.length() ||
      !ctx->pending_hvac_mode.length()) {
    return;
  }
  mqttPublishClimateHvacMode(
      ctx->pending_mode_entity.c_str(), ctx->pending_hvac_mode.c_str());
  ctx->pending_mode_entity = "";
  ctx->pending_hvac_mode = "";
}

void mode_publish_timer_cb(lv_timer_t* timer) {
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_timer_get_user_data(timer));
  if (!ctx) return;
  ctx->mode_publish_timer = nullptr;
  publish_pending_mode(ctx);
}

void commit_pending_mode(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->mode_publish_timer) {
    lv_timer_delete(ctx->mode_publish_timer);
    ctx->mode_publish_timer = nullptr;
  }
  publish_pending_mode(ctx);
}

void flush_pending_climate_commands(ClimatePopupContext* ctx) {
  if (!ctx) return;
  commit_pending_temperature(ctx);
  commit_pending_humidity(ctx);
  commit_pending_mode(ctx);
}

void discard_pending_climate_commands(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->temperature_publish_timer) {
    lv_timer_delete(ctx->temperature_publish_timer);
    ctx->temperature_publish_timer = nullptr;
  }
  if (ctx->humidity_publish_timer) {
    lv_timer_delete(ctx->humidity_publish_timer);
    ctx->humidity_publish_timer = nullptr;
  }
  if (ctx->mode_publish_timer) {
    lv_timer_delete(ctx->mode_publish_timer);
    ctx->mode_publish_timer = nullptr;
  }
  ctx->pending_temperature_entity = "";
  ctx->pending_humidity_entity = "";
  ctx->pending_mode_entity = "";
  ctx->pending_hvac_mode = "";
  ctx->block_remote_until_ms = 0;
}

void schedule_mode_publish(ClimatePopupContext* ctx) {
  if (!ctx) return;
  if (ctx->mode_publish_timer && ctx->pending_mode_entity.length() &&
      !ctx->pending_mode_entity.equalsIgnoreCase(ctx->entity_id)) {
    lv_timer_delete(ctx->mode_publish_timer);
    ctx->mode_publish_timer = nullptr;
    publish_pending_mode(ctx);
  }
  ctx->pending_mode_entity = ctx->entity_id;
  ctx->pending_hvac_mode = ctx->hvac_mode;
  ctx->block_remote_until_ms = millis() + kRemoteBlockMs;
  if (ctx->mode_publish_timer) {
    lv_timer_reset(ctx->mode_publish_timer);
    return;
  }
  ctx->mode_publish_timer =
      lv_timer_create(mode_publish_timer_cb, kModeDebounceMs, ctx);
  if (ctx->mode_publish_timer) {
    lv_timer_set_repeat_count(ctx->mode_publish_timer, 1);
  } else {
    publish_pending_mode(ctx);
  }
}

void shift_target(ClimatePopupContext* ctx, float delta) {
  if (!ctx || !ctx->available) return;
  if (ctx->humidity_control_active) {
    if (!humidity_control_available(ctx)) return;
    ctx->target_humidity = clamp_temperature(
        ctx->target_humidity + delta,
        ctx->min_humidity, ctx->max_humidity);
  } else if (ctx->has_range) {
    if (!temperature_control_available(ctx)) return;
    if (ctx->range_drag_high) {
      ctx->target_temp_high = clamp_temperature(
          quantize_temperature(
              ctx->target_temp_high + delta, ctx->step),
          ctx->target_temp_low, ctx->max_temp);
    } else {
      ctx->target_temp_low = clamp_temperature(
          quantize_temperature(
              ctx->target_temp_low + delta, ctx->step),
          ctx->min_temp, ctx->target_temp_high);
    }
  } else {
    if (!temperature_control_available(ctx)) return;
    ctx->target_temperature = clamp_temperature(
        quantize_temperature(
            ctx->target_temperature + delta, ctx->step),
        ctx->min_temp, ctx->max_temp);
  }
  refresh_target_label(ctx);
  refresh_ring(ctx);
  if (ctx->humidity_control_active) {
    schedule_humidity_publish(ctx);
  } else {
    schedule_temperature_publish(ctx);
  }
}

void on_close(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (!ctx || !ctx->card || !ctx->overlay) return;
  flush_pending_climate_commands(ctx);
  close_control_menu(ctx);
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void on_popup_surface_pressed(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_PRESSED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  close_control_menu(ctx);
}

float snapped_active_target_value(
    const ClimatePopupContext* ctx, float value) {
  if (!ctx) return value;
  const float minimum = active_minimum(ctx);
  const float maximum = active_maximum(ctx);
  const float step =
      ctx->humidity_control_active ? ctx->humidity_step : ctx->step;
  value = clamp_temperature(
      quantize_temperature(value, step), minimum, maximum);
  if (!ctx->humidity_control_active && ctx->has_range) {
    value =
        ctx->range_drag_high
            ? clamp_temperature(
                  value, ctx->target_temp_low, ctx->max_temp)
            : clamp_temperature(
                  value, ctx->min_temp, ctx->target_temp_high);
  }
  return value;
}

bool apply_active_target_value(
    ClimatePopupContext* ctx, float value) {
  if (!ctx) return false;
  value = snapped_active_target_value(ctx, value);

  if (ctx->humidity_control_active) {
    if (fabsf(ctx->target_humidity - value) < 0.01f) return false;
    ctx->target_humidity = value;
  } else if (ctx->has_range) {
    if (ctx->range_drag_high) {
      if (fabsf(ctx->target_temp_high - value) < 0.01f) return false;
      ctx->target_temp_high = value;
    } else {
      if (fabsf(ctx->target_temp_low - value) < 0.01f) return false;
      ctx->target_temp_low = value;
    }
  } else {
    if (fabsf(ctx->target_temperature - value) < 0.01f) return false;
    ctx->target_temperature = value;
  }
  refresh_target_label(ctx, false);
  return true;
}

bool target_value_from_point(
    ClimatePopupContext* ctx, const lv_point_t& point, float* value_out) {
  if (!ctx || !ctx->input_arc || !value_out) return false;
  lv_area_t area;
  lv_obj_get_coords(ctx->input_arc, &area);
  const float width = static_cast<float>(area.x2 - area.x1 + 1);
  const float height = static_cast<float>(area.y2 - area.y1 + 1);
  const float center_x = static_cast<float>(area.x1) + width * 0.5f;
  const float center_y = static_cast<float>(area.y1) + height * 0.5f;
  const float dx = static_cast<float>(point.x) - center_x;
  const float dy = static_cast<float>(point.y) - center_y;
  const float distance = sqrtf((dx * dx) + (dy * dy));
  const float radius =
      ((width < height ? width : height) - kArcWidth) * 0.5f;
  // Only a press on the visible ring may move the target value. Touches in
  // the center still belong to the labels and target selector.
  if (fabsf(distance - radius) > (kArcWidth + 20.0f)) return false;

  float angle = atan2f(dy, dx) * 180.0f / kPi;
  if (angle < 0.0f) angle += 360.0f;
  float relative = angle - static_cast<float>(kGaugeRotation);
  while (relative < 0.0f) relative += 360.0f;
  while (relative >= 360.0f) relative -= 360.0f;
  // Clamp the open 90-degree segment to its nearest endpoint instead of
  // jumping across the full value range.
  if (relative > static_cast<float>(kGaugeSweep)) {
    relative =
        relative >= 315.0f ? 0.0f : static_cast<float>(kGaugeSweep);
  }
  const float ratio =
      relative / static_cast<float>(kGaugeSweep);
  *value_out =
      active_minimum(ctx) +
      ratio * (active_maximum(ctx) - active_minimum(ctx));
  return true;
}

void on_arc_event(lv_event_t* event) {
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (!ctx || ctx->suppress_events || !ctx->available) return;
  const bool target_supported =
      ctx->humidity_control_active ? humidity_control_available(ctx)
                                   : temperature_control_available(ctx);
  if (!target_supported) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    close_control_menu(ctx);
    lv_indev_t* indev = lv_indev_get_act();
    if (indev) {
      lv_point_t point;
      lv_indev_get_point(indev, &point);
      float tapped_value = 0.0f;
      if (target_value_from_point(ctx, point, &tapped_value)) {
        if (ctx->has_range && !ctx->humidity_control_active) {
          ctx->range_drag_high =
              fabsf(tapped_value - ctx->target_temp_high) <
              fabsf(tapped_value - ctx->target_temp_low);
        }
        ctx->dragging = true;
        refresh_ring(ctx);

        if (ctx->has_range && !ctx->humidity_control_active) {
          tapped_value =
              ctx->range_drag_high
                  ? clamp_temperature(
                        tapped_value, ctx->target_temp_low, ctx->max_temp)
                  : clamp_temperature(
                        tapped_value, ctx->min_temp, ctx->target_temp_high);
        }
        tapped_value =
            snapped_active_target_value(ctx, tapped_value);
        ctx->suppress_events = true;
        lv_arc_set_value(
            ctx->input_arc, input_arc_value_for(ctx, tapped_value));
        ctx->suppress_events = false;
        if (apply_active_target_value(ctx, tapped_value)) {
          refresh_current_marker(ctx);
        }
      } else {
        ctx->dragging = false;
      }
    }
    return;
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    if (!ctx->dragging) return;
    const float value = snapped_active_target_value(
        ctx,
        actual_value_from_input_arc(
            ctx, lv_arc_get_value(ctx->input_arc)));
    ctx->suppress_events = true;
    lv_arc_set_value(
        ctx->input_arc, input_arc_value_for(ctx, value));
    ctx->suppress_events = false;
    if (apply_active_target_value(ctx, value)) {
      refresh_current_marker(ctx);
    }
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    const bool was_dragging = ctx->dragging;
    ctx->dragging = false;
    if (was_dragging) {
      refresh_ring(ctx);
      if (ctx->humidity_control_active) {
        capture_pending_humidity(ctx);
        commit_pending_humidity(ctx);
      } else {
        capture_pending_temperature(ctx);
        commit_pending_temperature(ctx);
      }
    }
  }
}

void on_minus(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (ctx && ctx->available &&
      (ctx->humidity_control_active ? humidity_control_available(ctx)
                                    : temperature_control_available(ctx))) {
    close_control_menu(ctx);
    shift_target(
        ctx,
        ctx->humidity_control_active ? -ctx->humidity_step : -ctx->step);
  }
}

void on_plus(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (ctx && ctx->available &&
      (ctx->humidity_control_active ? humidity_control_available(ctx)
                                    : temperature_control_available(ctx))) {
    close_control_menu(ctx);
    shift_target(
        ctx,
        ctx->humidity_control_active ? ctx->humidity_step : ctx->step);
  }
}

void on_target_toggle(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (!ctx || !ctx->available || !humidity_control_available(ctx) ||
      ctx->dragging) {
    return;
  }
  close_control_menu(ctx);
  lv_obj_t* target =
      static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  const bool activate_humidity =
      target == ctx->humidity_toggle_button;
  if (ctx->humidity_control_active == activate_humidity) return;
  ctx->humidity_control_active = activate_humidity;
  refresh_target_toggle(ctx);
  refresh_target_label(ctx);
  refresh_state_caption(ctx);
  refresh_ring(ctx);
}

void close_control_menu(ClimatePopupContext* ctx) {
  if (!ctx || !ctx->control_menu) return;
  if (ctx->open_control_index >= 0 &&
      ctx->open_control_index < static_cast<int8_t>(kControlCount)) {
    lv_obj_t* button =
        ctx->controls[ctx->open_control_index].dropdown;
    if (button) lv_obj_set_style_opa(button, LV_OPA_COVER, 0);
  }
  lv_obj_delete(ctx->control_menu);
  ctx->control_menu = nullptr;
  ctx->open_control_index = -1;
  ctx->menu_option_button_count = 0;
  for (lv_obj_t*& button : ctx->menu_option_buttons) button = nullptr;
}

void apply_control_selection(
    ClimatePopupContext* ctx, uint8_t control_index, uint8_t selected) {
  if (!ctx || !ctx->available || control_index >= kControlCount) return;
  ClimatePopupControl& control = ctx->controls[control_index];
  const ClimateControlType type =
      static_cast<ClimateControlType>(control_index);
  if (!climate_control_available(ctx, type)) return;
  if (selected >= control.option_count) return;
  String* current = current_control_value(ctx, type);
  if (!current) return;
  *current = control.options[selected];
  ctx->block_remote_until_ms = millis() + kRemoteBlockMs;

  switch (type) {
    case ClimateControlType::HVAC:
      ctx->hvac_action =
          optimistic_action_for_mode(ctx, ctx->hvac_mode);
      refresh_header_visuals(ctx);
      refresh_labels(ctx);
      refresh_ring(ctx);
      schedule_mode_publish(ctx);
      break;
    case ClimateControlType::PRESET:
      mqttPublishClimatePresetMode(
          ctx->entity_id.c_str(), current->c_str());
      break;
    case ClimateControlType::FAN:
      mqttPublishClimateFanMode(
          ctx->entity_id.c_str(), current->c_str());
      break;
    case ClimateControlType::SWING:
      mqttPublishClimateSwingMode(
          ctx->entity_id.c_str(), current->c_str());
      break;
    case ClimateControlType::SWING_HORIZONTAL:
      mqttPublishClimateHorizontalSwingMode(
          ctx->entity_id.c_str(), current->c_str());
      break;
  }
  close_control_menu(ctx);
  refresh_controls(ctx);
}

void on_control_option_click(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (!ctx || !ctx->control_menu || ctx->open_control_index < 0) return;
  lv_obj_t* target =
      static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  for (uint8_t i = 0; i < ctx->menu_option_button_count; ++i) {
    if (ctx->menu_option_buttons[i] == target) {
      apply_control_selection(
          ctx, static_cast<uint8_t>(ctx->open_control_index),
          ctx->menu_option_indices[i]);
      return;
    }
  }
}

void on_control_menu_current_click(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  close_control_menu(ctx);
}

void open_control_menu(ClimatePopupContext* ctx, uint8_t control_index) {
  if (!ctx || !ctx->available || control_index >= kControlCount) return;
  ClimatePopupControl& control = ctx->controls[control_index];
  if (!control.dropdown || !climate_control_available(
                               ctx, static_cast<ClimateControlType>(
                                        control_index))) {
    return;
  }
  if (ctx->control_menu &&
      ctx->open_control_index == static_cast<int8_t>(control_index)) {
    close_control_menu(ctx);
    return;
  }
  close_control_menu(ctx);

  String* current = current_control_value(
      ctx, static_cast<ClimateControlType>(control_index));
  int selected_index = -1;
  if (current) {
    for (uint8_t i = 0; i < control.option_count; ++i) {
      if (control.options[i].equalsIgnoreCase(*current)) {
        selected_index = static_cast<int>(i);
        break;
      }
    }
  }
  const uint8_t choice_count = control.option_count;
  const int choices_height =
      static_cast<int>(choice_count) * kMenuOptionHeight;
  lv_obj_update_layout(ctx->card);
  lv_area_t button_area;
  lv_area_t card_content_area;
  lv_obj_get_coords(control.dropdown, &button_area);
  lv_obj_get_content_coords(ctx->card, &card_content_area);
  const int available_menu_height =
      (button_area.y2 + 1) - card_content_area.y1;
  const int fixed_menu_height =
      (choice_count > 0 ? kMenuSeparatorAreaHeight : 0) +
      kPillHeight;
  const int max_choices_height =
      available_menu_height > fixed_menu_height
          ? available_menu_height - fixed_menu_height
          : kMenuOptionHeight;
  const int visible_choices_height =
      choices_height < max_choices_height
          ? choices_height
          : max_choices_height;
  const int menu_height =
      visible_choices_height + fixed_menu_height;
  const int current_pill_y =
      visible_choices_height +
      (choice_count > 0 ? kMenuSeparatorAreaHeight : 0);
  const bool choices_scrollable =
      choices_height > visible_choices_height;
  const ClimateControlType current_type =
      static_cast<ClimateControlType>(control_index);
  const bool colored_current =
      current_type == ClimateControlType::HVAC && current &&
      current->length() && !current->equalsIgnoreCase("off");
  const lv_color_t current_bg =
      colored_current
          ? lv_color_hex(climate_visuals::mode_foreground_color(*current))
          : lv_color_hex(kPillBg);
  const lv_color_t current_text_color =
      colored_current ? lv_color_hex(kCardBg) : lv_color_white();
  ctx->control_menu = lv_obj_create(ctx->card);
  ctx->open_control_index = static_cast<int8_t>(control_index);
  ctx->menu_option_button_count = 0;
  for (lv_obj_t*& button : ctx->menu_option_buttons) button = nullptr;
  lv_obj_set_size(ctx->control_menu, kPillWidth, menu_height);
  // The visible shells draw their own subtle outline. This lets the upper
  // list radius and lower pill radius differ without exposing the parent.
  lv_obj_set_style_bg_color(
      ctx->control_menu, lv_color_hex(kPillBg), 0);
  lv_obj_set_style_bg_opa(ctx->control_menu, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->control_menu, 0, 0);
  lv_obj_set_style_border_opa(ctx->control_menu, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(ctx->control_menu, 0, 0);
  lv_obj_set_style_shadow_width(ctx->control_menu, 0, 0);
  lv_obj_set_style_shadow_opa(ctx->control_menu, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_spread(ctx->control_menu, 0, 0);
  lv_obj_set_style_clip_corner(ctx->control_menu, false, 0);
  lv_obj_set_style_pad_all(ctx->control_menu, 0, 0);
  lv_obj_add_flag(ctx->control_menu, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_clear_flag(
      ctx->control_menu,
      static_cast<lv_obj_flag_t>(
          LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
          LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));

  // Build one continuous silhouette with distinct top and bottom radii.
  // The full-width middle rectangle hides the inner rounded edges, so no
  // seam or waist can appear where the two caps meet.
  lv_obj_t* top_shell = lv_obj_create(ctx->control_menu);
  lv_obj_set_size(top_shell, kPillWidth, kMenuOptionHeight);
  lv_obj_set_pos(top_shell, 0, 0);
  lv_obj_set_style_bg_color(top_shell, lv_color_hex(kPillBg), 0);
  lv_obj_set_style_bg_opa(top_shell, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(top_shell, 1, 0);
  lv_obj_set_style_border_color(top_shell, lv_color_black(), 0);
  lv_obj_set_style_border_opa(top_shell, LV_OPA_40, 0);
  lv_obj_set_style_border_side(
      top_shell,
      static_cast<lv_border_side_t>(
          LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT),
      0);
  lv_obj_set_style_radius(top_shell, kMenuOptionHeight / 2, 0);
  lv_obj_set_style_pad_all(top_shell, 0, 0);
  lv_obj_clear_flag(
      top_shell,
      static_cast<lv_obj_flag_t>(
          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  lv_obj_t* middle_shell = lv_obj_create(ctx->control_menu);
  lv_obj_set_size(
      middle_shell, kPillWidth,
      menu_height - (kMenuOptionHeight / 2) - (kPillHeight / 2));
  lv_obj_set_pos(middle_shell, 0, kMenuOptionHeight / 2);
  lv_obj_set_style_bg_color(middle_shell, lv_color_hex(kPillBg), 0);
  lv_obj_set_style_bg_opa(middle_shell, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(middle_shell, 1, 0);
  lv_obj_set_style_border_color(middle_shell, lv_color_black(), 0);
  lv_obj_set_style_border_opa(middle_shell, LV_OPA_40, 0);
  lv_obj_set_style_border_side(
      middle_shell,
      static_cast<lv_border_side_t>(
          LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT),
      0);
  lv_obj_set_style_radius(middle_shell, 0, 0);
  lv_obj_set_style_pad_all(middle_shell, 0, 0);
  lv_obj_clear_flag(
      middle_shell,
      static_cast<lv_obj_flag_t>(
          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  lv_obj_t* bottom_shell = lv_obj_create(ctx->control_menu);
  lv_obj_set_size(bottom_shell, kPillWidth, kPillHeight);
  lv_obj_set_pos(bottom_shell, 0, current_pill_y);
  lv_obj_set_style_bg_color(bottom_shell, current_bg, 0);
  lv_obj_set_style_bg_opa(bottom_shell, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bottom_shell, 1, 0);
  lv_obj_set_style_border_color(bottom_shell, lv_color_black(), 0);
  lv_obj_set_style_border_opa(bottom_shell, LV_OPA_40, 0);
  lv_obj_set_style_border_side(
      bottom_shell,
      static_cast<lv_border_side_t>(
          LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT |
          LV_BORDER_SIDE_RIGHT),
      0);
  lv_obj_set_style_radius(bottom_shell, kPillHeight / 2, 0);
  lv_obj_set_style_pad_all(bottom_shell, 0, 0);
  lv_obj_clear_flag(
      bottom_shell,
      static_cast<lv_obj_flag_t>(
          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  lv_obj_t* choices_viewport = lv_obj_create(ctx->control_menu);
  lv_obj_set_size(
      choices_viewport, kPillWidth, visible_choices_height);
  lv_obj_set_pos(choices_viewport, 0, 0);
  lv_obj_set_style_bg_opa(choices_viewport, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(choices_viewport, 0, 0);
  lv_obj_set_style_radius(
      choices_viewport, kMenuOptionHeight / 2, 0);
  lv_obj_set_style_clip_corner(choices_viewport, true, 0);
  lv_obj_set_style_pad_all(choices_viewport, 0, 0);
  lv_obj_set_scroll_dir(choices_viewport, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(
      choices_viewport, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(
      choices_viewport,
      static_cast<lv_obj_flag_t>(
          LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN));
  if (choices_scrollable) {
    lv_obj_add_flag(
        choices_viewport,
        static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
            LV_OBJ_FLAG_SCROLL_MOMENTUM));
  } else {
    lv_obj_clear_flag(
        choices_viewport,
        static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
            LV_OBJ_FLAG_SCROLL_MOMENTUM));
  }

  const char* language = configManager.getConfig().language;
  lv_obj_t* selected_option_button = nullptr;
  for (uint8_t i = 0; i < control.option_count; ++i) {
    const bool selected = static_cast<int>(i) == selected_index;
    const uint8_t menu_index = ctx->menu_option_button_count++;
    ctx->menu_option_indices[menu_index] = i;
    lv_obj_t* option = lv_button_create(choices_viewport);
    ctx->menu_option_buttons[menu_index] = option;
    if (selected) selected_option_button = option;
    lv_obj_set_size(option, kPillWidth, kMenuOptionHeight);
    lv_obj_set_pos(option, 0, static_cast<int>(i) * kMenuOptionHeight);
    lv_obj_set_style_bg_color(
        option,
        selected ? lv_color_white() : lv_color_hex(kPillBg), 0);
    lv_obj_set_style_bg_opa(
        option, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        option, lv_color_hex(kPillPressedBg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(option, kMenuOptionHeight / 2, 0);
    lv_obj_set_style_radius(
        option, kMenuOptionHeight / 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(option, 0, 0);
    lv_obj_set_style_border_width(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(option, 0, 0);
    lv_obj_set_style_outline_width(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(option, 0, 0);
    lv_obj_set_style_shadow_width(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(option, 0, 0);
    lv_obj_set_style_pad_all(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_anim_time(option, 0, 0);
    lv_obj_set_style_anim_time(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(option, 0, 0);
    lv_obj_set_style_transform_width(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(option, 0, 0);
    lv_obj_set_style_transform_height(option, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(option, 0, 0);
    lv_obj_set_style_translate_y(option, 0, LV_STATE_PRESSED);
    lv_obj_add_flag(option, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(
        option,
        static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
            LV_OBJ_FLAG_SCROLL_MOMENTUM |
            LV_OBJ_FLAG_SCROLL_CHAIN_HOR));
    // Keep vertical scroll chaining enabled. A swipe usually starts on an
    // option button (or its label), and LVGL must be allowed to continue
    // searching up to the scrollable choices viewport.
    lv_obj_add_flag(option, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_event_cb(
        option, on_control_option_click, LV_EVENT_CLICKED, ctx);

    lv_obj_t* label = lv_label_create(option);
    lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
    lv_obj_set_style_text_color(
        label, selected ? lv_color_hex(kCardBg) : lv_color_white(), 0);
    const String text =
        i18n::climate_option_label(language, control.options[i]);
    lv_label_set_text(label, text.c_str());
    lv_obj_set_width(label, kPillWidth - 8);
    lv_obj_set_height(label, kPillLabelHeight);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
  }

  if (choice_count > 0) {
    lv_obj_t* separator_line = lv_obj_create(ctx->control_menu);
    lv_obj_set_size(
        separator_line, kPillWidth - 24, kMenuSeparatorLineHeight);
    lv_obj_set_pos(
        separator_line, 12,
        visible_choices_height +
            kMenuSeparatorAreaHeight -
            kMenuSeparatorLineHeight -
            kMenuSeparatorLineBottomInset);
    lv_obj_set_style_bg_color(
        separator_line, lv_color_hex(0x777777), 0);
    lv_obj_set_style_bg_opa(separator_line, LV_OPA_60, 0);
    lv_obj_set_style_border_width(separator_line, 0, 0);
    lv_obj_set_style_radius(separator_line, 0, 0);
    lv_obj_set_style_pad_all(separator_line, 0, 0);
    lv_obj_clear_flag(
        separator_line,
        static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }

  lv_obj_t* current_pill = lv_button_create(ctx->control_menu);
  lv_obj_set_size(current_pill, kPillWidth, kPillHeight);
  lv_obj_set_pos(current_pill, 0, current_pill_y);
  lv_obj_set_style_bg_color(
      current_pill, lv_color_hex(kPillBg), 0);
  lv_obj_set_style_bg_opa(current_pill, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(
      current_pill, lv_color_hex(kPillPressedBg), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(
      current_pill, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(current_pill, kPillHeight / 2, 0);
  lv_obj_set_style_radius(
      current_pill, kPillHeight / 2, LV_STATE_PRESSED);
  lv_obj_set_style_border_width(current_pill, 0, 0);
  lv_obj_set_style_border_width(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_outline_width(current_pill, 0, 0);
  lv_obj_set_style_outline_width(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(current_pill, 0, 0);
  lv_obj_set_style_shadow_width(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(current_pill, 0, 0);
  lv_obj_set_style_pad_all(current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_anim_time(current_pill, 0, 0);
  lv_obj_set_style_anim_time(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(current_pill, 0, 0);
  lv_obj_set_style_transform_width(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(current_pill, 0, 0);
  lv_obj_set_style_transform_height(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_set_style_translate_y(current_pill, 0, 0);
  lv_obj_set_style_translate_y(
      current_pill, 0, LV_STATE_PRESSED);
  lv_obj_add_flag(current_pill, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_clear_flag(
      current_pill,
      static_cast<lv_obj_flag_t>(
          LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
          LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_add_event_cb(
      current_pill, on_control_menu_current_click, LV_EVENT_CLICKED, ctx);

  const bool has_current_value = current && current->length();
  lv_obj_t* current_caption = lv_label_create(current_pill);
  lv_obj_set_style_text_font(
      current_caption, climate_control_caption_font(), 0);
  lv_obj_set_style_text_color(
      current_caption,
      colored_current ? lv_color_hex(kCardBg) : lv_color_hex(0xD0D0D0), 0);
  lv_label_set_text(
      current_caption,
      i18n::climate_control_label(language, control_index));
  lv_obj_set_width(current_caption, kPillWidth - 16);
  lv_obj_set_height(current_caption, kPillCaptionHeight);
  lv_label_set_long_mode(current_caption, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(
      current_caption, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(
      current_caption, LV_ALIGN_CENTER, 0,
      has_current_value ? kPillCaptionYWithValue : 0);
  lv_obj_clear_flag(current_caption, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(current_caption, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* current_label = lv_label_create(current_pill);
  lv_obj_set_style_text_font(current_label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(current_label, current_text_color, 0);
  lv_obj_set_style_text_outline_stroke_color(
      current_label, current_text_color, 0);
  lv_obj_set_style_text_outline_stroke_width(
      current_label, colored_current ? 2 : 0, 0);
  lv_obj_set_style_text_outline_stroke_opa(
      current_label,
      colored_current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  const String current_text =
      has_current_value
          ? i18n::climate_option_label(language, *current)
          : String();
  lv_label_set_text(current_label, current_text.c_str());
  lv_obj_set_width(current_label, kPillWidth - 16);
  lv_obj_set_height(current_label, kPillLabelHeight);
  lv_label_set_long_mode(current_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(
      current_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(
      current_label, LV_ALIGN_CENTER, 0, kPillValueY);
  if (!has_current_value) {
    lv_obj_add_flag(current_label, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(current_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(current_label, LV_OBJ_FLAG_SCROLLABLE);

  const int menu_x = button_area.x1 - card_content_area.x1;
  const int menu_y =
      (button_area.y2 + 1 - menu_height) - card_content_area.y1;
  lv_obj_set_align(ctx->control_menu, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(ctx->control_menu, menu_x, menu_y);
  lv_obj_set_style_opa(control.dropdown, LV_OPA_TRANSP, 0);
  lv_obj_move_foreground(ctx->control_menu);
  const int selected_label_bottom =
      selected_index >= 0
          ? selected_index * kMenuOptionHeight +
                (kMenuOptionHeight + kPillLabelHeight) / 2
          : 0;
  const bool selected_label_clipped =
      selected_label_bottom > visible_choices_height;
  if (choices_scrollable && selected_option_button &&
      selected_label_clipped) {
    lv_obj_update_layout(choices_viewport);
    lv_obj_scroll_to_view(selected_option_button, LV_ANIM_OFF);
  }
}

void on_control_event(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (!ctx) return;
  lv_obj_t* target =
      static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  for (uint8_t i = 0; i < kControlCount; ++i) {
    if (ctx->controls[i].dropdown == target) {
      open_control_menu(ctx, i);
      return;
    }
  }
}

void style_round_icon_button(
    lv_obj_t* button, lv_obj_t* icon) {
  if (!button) return;
  auto apply = [&](lv_style_selector_t selector, lv_opa_t opa) {
    lv_obj_set_style_bg_color(
        button, lv_color_white(), selector);
    lv_obj_set_style_bg_opa(button, opa, selector);
    lv_obj_set_style_border_width(button, 0, selector);
    lv_obj_set_style_outline_width(button, 0, selector);
    lv_obj_set_style_shadow_width(button, 0, selector);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, selector);
    lv_obj_set_style_anim_time(button, 0, selector);
    lv_obj_set_style_transform_width(button, 0, selector);
    lv_obj_set_style_transform_height(button, 0, selector);
  };
  apply(0, LV_OPA_TRANSP);
  apply(LV_STATE_PRESSED, LV_OPA_20);
  if (icon) lv_obj_set_style_text_color(icon, lv_color_white(), 0);
}

lv_obj_t* create_step_button(
    lv_obj_t* parent, const char* icon_name, lv_align_t align, int x,
    ClimatePopupContext* ctx, lv_event_cb_t callback,
    lv_obj_t** icon_out) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_size(button, kControlButtonSize, kControlButtonSize);
  lv_obj_align(button, align, x, 0);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_ext_click_area(button, 10);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, ctx);
  lv_obj_t* label = lv_label_create(button);
  lv_obj_set_style_text_font(label, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(label);
  const String icon = getMdiChar(icon_name);
  lv_label_set_text(label, icon.c_str());
  lv_obj_center(label);
  style_round_icon_button(button, label);
  if (icon_out) *icon_out = label;
  return button;
}

lv_obj_t* create_visual_arc(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* arc = lv_arc_create(parent);
  lv_obj_set_size(arc, kGaugeSize, kGaugeSize);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, kGaugeOffsetY);
  lv_arc_set_rotation(arc, kGaugeRotation);
  lv_arc_set_bg_angles(arc, 0, kGaugeSweep);
  lv_obj_set_style_arc_width(arc, kArcWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_color(
      arc, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(arc, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  return arc;
}

void on_overlay_delete(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
  ClimatePopupContext* ctx =
      static_cast<ClimatePopupContext*>(lv_event_get_user_data(event));
  if (g_climate_popup == ctx) g_climate_popup = nullptr;
  flush_pending_climate_commands(ctx);
  if (ctx && ctx->remote_apply_timer) {
    lv_timer_delete(ctx->remote_apply_timer);
    ctx->remote_apply_timer = nullptr;
  }
  delete ctx;
}

}  // namespace

static void finish_climate_popup_open(const ClimatePopupInit& init) {
  if (!g_climate_popup) return;
  apply_init(g_climate_popup, init);
}

static void prepare_climate_popup_open(const ClimatePopupInit& init) {
  auto* ctx = g_climate_popup;
  hometiles_title::set(ctx->title_label, init.title.c_str());
  const String name = init.dynamic_icon
      ? dynamic_state_icon(init.hvac_mode, init.hvac_action, init.icon_name)
      : init.icon_name;
  String icon = getMdiChar(name);
  if (!icon.length()) icon = getMdiChar("thermostat");
  lv_label_set_text(ctx->icon_label, icon.c_str());
  if (init.icon_visible) lv_obj_remove_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ctx->icon_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(init.available
      ? climate_visuals::state_foreground_color(init.hvac_mode, init.hvac_action)
      : 0x9E9E9E), 0);
  if (!defer_popup_body(ctx->card, ctx->title_label, ctx->icon_label,
                        ctx->close_button, init, finish_climate_popup_open,
                        ctx->entity_id == init.entity_id))
    apply_init(ctx, init);
}

void show_climate_popup(const ClimatePopupInit& init) {
  hide_pin_popup();
  hide_camera_popup();
  if (!init.entity_id.length()) return;
  hide_cover_popup();
  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_media_popup();

  if (g_climate_popup && g_climate_popup->overlay &&
      g_climate_popup->card) {
    cancel_deferred_remote_apply(g_climate_popup);
    prepare_climate_popup_open(init);
    lv_obj_clear_flag(
        g_climate_popup->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(
        g_climate_popup->overlay, LV_OBJ_FLAG_CLICKABLE);

    if (g_climate_popup && g_climate_popup->card) viewNavigationPopupShown(g_climate_popup->card, init.entity_id.c_str());
    show_popup_shell(g_climate_popup->overlay, g_climate_popup->card, g_climate_popup->title_label, g_climate_popup->icon_label, g_climate_popup->close_button);
    return;
  }

  ClimatePopupContext* ctx = new ClimatePopupContext();
  g_climate_popup = ctx;
  const auto parts = create_popup_body(on_close, ctx, kCardBg);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* close = parts.close;
  lv_obj_add_event_cb(ctx->overlay, on_popup_surface_pressed, LV_EVENT_PRESSED, ctx);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ctx->card, on_popup_surface_pressed, LV_EVENT_PRESSED, ctx);

  lv_obj_t* value_box = lv_obj_create(ctx->card);
  lv_obj_remove_style_all(value_box);
  lv_obj_set_size(value_box, LV_PCT(100), popup_layout::kValueHeight);
  lv_obj_align(
      value_box, LV_ALIGN_TOP_MID, 0,
      popup_layout::kValueY - kClimateValueLiftY);
  lv_obj_set_style_bg_opa(value_box, LV_OPA_TRANSP, 0);
  lv_obj_set_layout(value_box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(value_box, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      value_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(value_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(value_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_column(
      value_box, popup_layout::scale480(28), 0);

  ctx->top_caption_label = lv_label_create(value_box);
  lv_obj_set_width(ctx->top_caption_label, LV_SIZE_CONTENT);
  lv_obj_set_height(ctx->top_caption_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(ctx->top_caption_label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(
      ctx->top_caption_label, lv_color_white(), 0);
  lv_obj_set_style_translate_y(
      ctx->top_caption_label,
      popup_layout::kLargeValueTextOffsetY +
          kCurrentCaptionOpticalOffsetY,
      0);
  lv_label_set_long_mode(
      ctx->top_caption_label, LV_LABEL_LONG_CLIP);
  lv_obj_clear_flag(
      ctx->top_caption_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(
      ctx->top_caption_label, LV_OBJ_FLAG_SCROLLABLE);

  ctx->top_value_label = lv_label_create(value_box);
  lv_obj_set_width(ctx->top_value_label, LV_SIZE_CONTENT);
  lv_obj_set_height(ctx->top_value_label, LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(
      ctx->top_value_label, climate_current_value_font(), 0);
  lv_obj_set_style_text_color(
      ctx->top_value_label, lv_color_white(), 0);
  lv_obj_set_style_text_align(
      ctx->top_value_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_translate_y(
      ctx->top_value_label, popup_layout::kLargeValueTextOffsetY, 0);
  lv_label_set_long_mode(
      ctx->top_value_label, LV_LABEL_LONG_DOT);
  lv_obj_clear_flag(
      ctx->top_value_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(
      ctx->top_value_label, LV_OBJ_FLAG_SCROLLABLE);

  ctx->body = lv_obj_create(ctx->card);
  lv_obj_set_size(ctx->body, LV_PCT(100), popup_layout::kBodyHeight);
  lv_obj_align(
      ctx->body, LV_ALIGN_TOP_MID, 0,
      popup_layout::kBodyY - kClimateContentLiftY);
  lv_obj_set_style_bg_opa(ctx->body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->body, 0, 0);
  lv_obj_set_style_pad_all(ctx->body, 0, 0);
  lv_obj_remove_flag(ctx->body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->body, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(ctx->body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      ctx->body, on_popup_surface_pressed, LV_EVENT_PRESSED, ctx);

  ctx->track_arc = create_visual_arc(ctx->body, kTrackColor);
  ctx->color_arc = create_visual_arc(ctx->body, 0xFF6F22);
  ctx->range_high_arc = create_visual_arc(ctx->body, 0x2196F3);
  ctx->active_arc = create_visual_arc(ctx->body, 0xFF6F22);

  ctx->input_arc = lv_arc_create(ctx->body);
  lv_obj_set_size(ctx->input_arc, kGaugeSize, kGaugeSize);
  lv_obj_align(
      ctx->input_arc, LV_ALIGN_CENTER, 0, kGaugeOffsetY);
  lv_arc_set_rotation(ctx->input_arc, kGaugeRotation);
  lv_arc_set_bg_angles(ctx->input_arc, 0, kGaugeSweep);
  // LVGL's default limits movement to 720 degrees/s.
  // For a direct touch control, let the arc follow the finger without
  // this visible slew-rate limit.
  lv_arc_set_change_rate(ctx->input_arc, 10000);
  lv_obj_set_style_arc_width(
      ctx->input_arc, kArcWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(
      ctx->input_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(
      ctx->input_arc, kArcWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(
      ctx->input_arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(
      ctx->input_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(
      ctx->input_arc, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(
      ctx->input_arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_border_width(
      ctx->input_arc, 4, LV_PART_KNOB);
  lv_obj_set_style_border_opa(
      ctx->input_arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_radius(
      ctx->input_arc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_outline_width(
      ctx->input_arc, 0, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(
      ctx->input_arc, 0, LV_PART_KNOB);
  lv_obj_set_style_pad_all(ctx->input_arc, 0, LV_PART_KNOB);
  lv_obj_add_flag(ctx->input_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ctx->input_arc, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_clear_flag(ctx->input_arc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(
      ctx->input_arc, on_arc_event, LV_EVENT_PRESSED, ctx);
  lv_obj_add_event_cb(
      ctx->input_arc, on_arc_event, LV_EVENT_VALUE_CHANGED, ctx);
  lv_obj_add_event_cb(
      ctx->input_arc, on_arc_event, LV_EVENT_RELEASED, ctx);
  lv_obj_add_event_cb(
      ctx->input_arc, on_arc_event, LV_EVENT_PRESS_LOST, ctx);

  ctx->target_marker = lv_obj_create(ctx->body);
  lv_obj_set_size(
      ctx->target_marker, kTargetMarkerSize, kTargetMarkerSize);
  lv_obj_set_style_radius(ctx->target_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(
      ctx->target_marker, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(ctx->target_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ctx->target_marker, 4, 0);
  lv_obj_set_style_border_color(
      ctx->target_marker, lv_color_hex(0xFF6F22), 0);
  lv_obj_set_style_border_opa(ctx->target_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(ctx->target_marker, 0, 0);
  lv_obj_set_style_shadow_width(ctx->target_marker, 0, 0);
  lv_obj_clear_flag(ctx->target_marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ctx->target_marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->target_marker, LV_OBJ_FLAG_HIDDEN);

  ctx->range_high_marker = lv_obj_create(ctx->body);
  lv_obj_set_size(
      ctx->range_high_marker, kTargetMarkerSize, kTargetMarkerSize);
  lv_obj_set_style_radius(
      ctx->range_high_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(
      ctx->range_high_marker, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(
      ctx->range_high_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ctx->range_high_marker, 4, 0);
  lv_obj_set_style_border_color(
      ctx->range_high_marker, lv_color_hex(0x2196F3), 0);
  lv_obj_set_style_border_opa(
      ctx->range_high_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(ctx->range_high_marker, 0, 0);
  lv_obj_set_style_shadow_width(ctx->range_high_marker, 0, 0);
  lv_obj_clear_flag(
      ctx->range_high_marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(
      ctx->range_high_marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->range_high_marker, LV_OBJ_FLAG_HIDDEN);

  ctx->current_marker = lv_obj_create(ctx->body);
  lv_obj_set_size(
      ctx->current_marker, kCurrentMarkerSize, kCurrentMarkerSize);
  lv_obj_set_style_radius(ctx->current_marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(
      ctx->current_marker, lv_color_hex(0xA8A8A8), 0);
  lv_obj_set_style_bg_opa(ctx->current_marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ctx->current_marker, 0, 0);
  lv_obj_set_style_pad_all(ctx->current_marker, 0, 0);
  lv_obj_set_style_shadow_width(ctx->current_marker, 0, 0);
  lv_obj_clear_flag(ctx->current_marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ctx->current_marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->current_marker, LV_OBJ_FLAG_HIDDEN);

  ctx->state_caption = lv_label_create(ctx->body);
  lv_obj_set_style_text_font(ctx->state_caption, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(
      ctx->state_caption, lv_color_white(), 0);
  lv_obj_set_width(
      ctx->state_caption, kGaugeSize - popup_layout::contentScale(110));
  lv_obj_set_style_text_align(
      ctx->state_caption, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(
      ctx->state_caption, LV_ALIGN_CENTER, 0, kStateCaptionOffsetY);

  ctx->target_label = lv_label_create(ctx->body);
  lv_obj_set_style_text_font(ctx->target_label, popup_layout::font48(), 0);
  lv_obj_set_style_text_color(
      ctx->target_label, lv_color_white(), 0);
  lv_obj_set_width(
      ctx->target_label, kGaugeSize - popup_layout::contentScale(80));
  lv_obj_set_style_text_align(
      ctx->target_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(
      ctx->target_label, LV_ALIGN_CENTER, 0, kTargetLabelCenterY);

  ctx->minus_button =
      create_step_button(
          ctx->body, "minus", LV_ALIGN_LEFT_MID,
          popup_layout::contentScale(18), ctx, on_minus,
          &ctx->minus_icon);
  ctx->plus_button =
      create_step_button(
          ctx->body, "plus", LV_ALIGN_RIGHT_MID,
          -popup_layout::contentScale(18), ctx, on_plus,
          &ctx->plus_icon);

  ctx->target_toggle = lv_obj_create(ctx->body);
  lv_obj_set_size(
      ctx->target_toggle, kTargetToggleWidth, kTargetToggleHeight);
  lv_obj_align(ctx->target_toggle, LV_ALIGN_BOTTOM_MID, 0,
               -popup_layout::contentScale(2));
  lv_obj_set_style_bg_color(
      ctx->target_toggle, lv_color_hex(kPillBg), 0);
  lv_obj_set_style_bg_opa(ctx->target_toggle, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(
      ctx->target_toggle, kTargetToggleHeight / 2, 0);
  lv_obj_set_style_border_width(ctx->target_toggle, 0, 0);
  lv_obj_set_style_pad_all(
      ctx->target_toggle, popup_layout::contentScale(6), 0);
  lv_obj_set_style_pad_column(
      ctx->target_toggle, kTargetToggleGap, 0);
  lv_obj_set_layout(ctx->target_toggle, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(ctx->target_toggle, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      ctx->target_toggle,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(ctx->target_toggle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->target_toggle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      ctx->target_toggle,
      on_popup_surface_pressed, LV_EVENT_PRESSED, ctx);

  auto create_target_toggle_button =
      [&](const char* icon_name,
          lv_obj_t** button_out,
          lv_obj_t** icon_out) {
        lv_obj_t* button = lv_button_create(ctx->target_toggle);
        lv_obj_set_size(
            button, kTargetToggleButtonSize, kTargetToggleButtonSize);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(
            button, LV_RADIUS_CIRCLE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(
            button, lv_color_hex(kPillPressedBg), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(
            button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_border_width(
            button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(button, 0, 0);
        lv_obj_set_style_outline_width(
            button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_shadow_width(
            button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_pad_all(
            button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_anim_time(button, 0, 0);
        lv_obj_set_style_anim_time(
            button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_width(button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_transform_height(button, 0, LV_STATE_PRESSED);
        lv_obj_set_style_translate_y(button, 0, LV_STATE_PRESSED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            button, on_target_toggle, LV_EVENT_CLICKED, ctx);
        lv_obj_t* icon = lv_label_create(button);
        lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
        popup_layout::applyIconScale(icon);
        const String icon_char = getMdiChar(icon_name);
        lv_label_set_text(icon, icon_char.c_str());
        lv_obj_center(icon);
        if (button_out) *button_out = button;
        if (icon_out) *icon_out = icon;
      };
  create_target_toggle_button(
      "thermometer",
      &ctx->temperature_toggle_button,
      &ctx->temperature_toggle_icon);
  create_target_toggle_button(
      "water-percent",
      &ctx->humidity_toggle_button,
      &ctx->humidity_toggle_icon);
  lv_obj_add_flag(ctx->target_toggle, LV_OBJ_FLAG_HIDDEN);

  ctx->controls_row = lv_obj_create(ctx->card);
  lv_obj_set_size(
      ctx->controls_row, LV_PCT(100), popup_layout::kNavHeight);
  lv_obj_align(
      ctx->controls_row, LV_ALIGN_BOTTOM_MID, 0,
      -popup_layout::kNavBottomInset);
  lv_obj_set_style_bg_opa(ctx->controls_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->controls_row, 0, 0);
  lv_obj_set_style_pad_hor(ctx->controls_row, 0, 0);
  lv_obj_set_style_pad_ver(ctx->controls_row, 0, 0);
  lv_obj_set_style_pad_column(ctx->controls_row, kPillGap, 0);
  lv_obj_set_style_pad_row(ctx->controls_row, kPillGap, 0);
  lv_obj_set_layout(ctx->controls_row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(ctx->controls_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      ctx->controls_row, LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(ctx->controls_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->controls_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(
      ctx->controls_row,
      on_popup_surface_pressed, LV_EVENT_PRESSED, ctx);

  for (uint8_t i = 0; i < kControlCount; ++i) {
    ClimatePopupControl& control = ctx->controls[i];
    control.dropdown = lv_button_create(ctx->controls_row);
    lv_obj_set_width(control.dropdown, kPillWidth);
    lv_obj_set_height(control.dropdown, kPillHeight);
    lv_obj_set_flex_grow(control.dropdown, 0);
    lv_obj_set_style_bg_color(
        control.dropdown, lv_color_hex(kPillBg), 0);
    lv_obj_set_style_bg_opa(
        control.dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        control.dropdown,
        lv_color_hex(kPillPressedBg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(
        control.dropdown, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(control.dropdown, kPillHeight / 2, 0);
    lv_obj_set_style_radius(
        control.dropdown, kPillHeight / 2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(control.dropdown, 0, 0);
    lv_obj_set_style_border_width(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(control.dropdown, 0, 0);
    lv_obj_set_style_outline_width(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(control.dropdown, 0, 0);
    lv_obj_set_style_shadow_width(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_pad_left(
        control.dropdown, popup_layout::scale480(8), 0);
    lv_obj_set_style_pad_left(
        control.dropdown, popup_layout::scale480(8), LV_STATE_PRESSED);
    lv_obj_set_style_pad_right(
        control.dropdown, popup_layout::scale480(8), 0);
    lv_obj_set_style_pad_right(
        control.dropdown, popup_layout::scale480(8), LV_STATE_PRESSED);
    lv_obj_set_style_pad_top(control.dropdown, 0, 0);
    lv_obj_set_style_pad_top(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_pad_bottom(control.dropdown, 0, 0);
    lv_obj_set_style_pad_bottom(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_text_font(
        control.dropdown, popup_layout::font24(), LV_PART_MAIN);
    lv_obj_set_style_text_color(
        control.dropdown, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_anim_time(control.dropdown, 0, 0);
    lv_obj_set_style_anim_time(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_width(control.dropdown, 0, 0);
    lv_obj_set_style_transform_width(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(control.dropdown, 0, 0);
    lv_obj_set_style_transform_height(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(control.dropdown, 0, 0);
    lv_obj_set_style_translate_y(
        control.dropdown, 0, LV_STATE_PRESSED);
    lv_obj_add_flag(control.dropdown, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(
        control.dropdown,
        static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
            LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN));
    lv_obj_add_event_cb(
        control.dropdown, on_control_event, LV_EVENT_CLICKED, ctx);

    control.caption = lv_label_create(control.dropdown);
    lv_obj_set_style_text_font(
        control.caption, climate_control_caption_font(), 0);
    lv_obj_set_style_text_color(
        control.caption, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_width(control.caption, kPillWidth - 16);
    lv_obj_set_height(control.caption, kPillCaptionHeight);
    lv_label_set_long_mode(control.caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(
        control.caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(control.caption);
    lv_obj_clear_flag(control.caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(control.caption, LV_OBJ_FLAG_SCROLLABLE);

    control.value = lv_label_create(control.dropdown);
    lv_obj_set_style_text_font(control.value, popup_layout::font24(), 0);
    lv_obj_set_style_text_color(
        control.value, lv_color_white(), 0);
    lv_obj_set_width(control.value, 1);
    lv_obj_set_height(control.value, kPillLabelHeight);
    lv_label_set_long_mode(control.value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(
        control.value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(
        control.value, LV_ALIGN_CENTER, 0, kPillValueY);
    lv_obj_clear_flag(control.value, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(control.value, LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_obj_add_event_cb(
      ctx->overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);
  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(close);
  apply_init(ctx, init);

  if (g_climate_popup && g_climate_popup->card) viewNavigationPopupShown(g_climate_popup->card, init.entity_id.c_str());
  show_popup_shell(g_climate_popup->overlay, g_climate_popup->card, g_climate_popup->title_label, g_climate_popup->icon_label, g_climate_popup->close_button);
}

void update_climate_popup(const ClimatePopupInit& init) {
  ClimatePopupContext* ctx = g_climate_popup;
  if (!ctx || !ctx->card || !ctx->overlay) return;
  if (!ctx->entity_id.equalsIgnoreCase(init.entity_id)) return;
  if (lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN)) return;
  const bool capability_removed =
      ctx->has_supported_features && init.has_supported_features &&
      (ctx->supported_features & ~init.supported_features) != 0;
  const bool safety_contract_changed =
      init.available != ctx->available || capability_removed;
  if (safety_contract_changed) {
    cancel_deferred_remote_apply(ctx);
    if (!init.available || capability_removed) {
      discard_pending_climate_commands(ctx);
    }
    apply_init(ctx, init);
    return;
  }
  if (ctx->dragging || millis() < ctx->block_remote_until_ms) {
    defer_remote_apply(ctx, init);
    return;
  }
  cancel_deferred_remote_apply(ctx);
  apply_init(ctx, init);
}

void preload_climate_popup() {
  if (g_climate_popup && g_climate_popup->card) return;
  ClimatePopupInit init;
  init.entity_id = "__preload__";
  init.title = "";
  init.icon_name = "thermostat";
  init.hvac_mode = "off";
  init.hvac_modes = "off,heat,cool,heat_cool,auto,dry,fan_only";
  init.preset_mode = "comfort";
  init.preset_modes = "none,eco,away,boost,comfort,home,sleep,activity";
  init.fan_mode = "auto";
  init.fan_modes = "auto,low,medium,high";
  init.swing_mode = "off";
  init.swing_modes = "off,vertical,horizontal,both";
  init.swing_horizontal_mode = "center";
  init.swing_horizontal_modes = "off,left,center,right,swing";
  init.temperature_unit = "\xC2\xB0" "C";
  init.has_current_temperature = true;
  init.current_temperature = 20.0f;
  init.has_current_humidity = true;
  init.current_humidity = 45.0f;
  init.has_target_temperature = true;
  init.target_temperature = 21.0f;
  init.has_target_humidity = true;
  init.target_humidity = 50.0f;
  init.min_humidity = 30.0f;
  init.max_humidity = 99.0f;
  show_climate_popup(init);
  hide_climate_popup();
}

void hide_climate_popup() {
  if (!g_climate_popup || !g_climate_popup->card ||
      !g_climate_popup->overlay) {
    return;
  }
  flush_pending_climate_commands(g_climate_popup);
  close_control_menu(g_climate_popup);
  hide_popup_shell(g_climate_popup->card);
  cancel_popup_open(g_climate_popup->card);
  lv_obj_add_flag(g_climate_popup->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(
      g_climate_popup->overlay, LV_OBJ_FLAG_CLICKABLE);
}
