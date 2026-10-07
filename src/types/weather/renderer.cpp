#include "src/ui/shared/ui_surface_style.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/types/weather/renderer.h"
#include "src/types/weather/weather_icons.h"
#include "src/types/weather/tile_layout.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/types_registry.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/popups/popup_shell.h"
#include "src/devices/device_select.h"
#include <Arduino.h>
#include <string.h>
#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"

struct WeatherEventData {
  String entity_id;
  String title;
  lv_obj_t* location_label = nullptr;
  uint32_t bg_color = 0;
  bool colored_icons = true;
};

// The widest text of a forecast day: its label in the language (the short
// "today" like the popup's, the weekdays) or a two-digit negative
// temperature with its unit. The Web Admin preview gets the same number.
lv_coord_t weather_forecast_text_width(const char* language) {
  auto width = [](const char* text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return static_cast<lv_coord_t>(size.x);
  };
  const lv_font_t* day_font = weather_tile::forecast_day_font();
  lv_coord_t widest = width(i18n::weather_today_button_label(language), day_font);
  for (int day = 1; day <= 7; ++day) {
    char iso[11];
    snprintf(iso, sizeof(iso), "2024-01-%02d", day);
    const lv_coord_t weekday = width(i18n::weather_weekday_short(language, iso).c_str(), day_font);
    if (weekday > widest) widest = weekday;
  }
  String unit = String(weather_tile::unit_gap()) + "\xC2\xB0" "C";
  const lv_coord_t temperature = width("-12", weather_tile::forecast_font()) +
                                 width(unit.c_str(), weather_tile::unit_font());
  return temperature > widest ? temperature : widest;
}

// The panel's language, measured once per language.
lv_coord_t weather_forecast_text_width() {
  static char measured_language[8] = "";
  static lv_coord_t widest = 0;
  const char* language = configManager.getConfig().language;
  if (widest > 0 && strncmp(measured_language, language, sizeof(measured_language) - 1) == 0) return widest;
  widest = weather_forecast_text_width(language);
  strncpy(measured_language, language, sizeof(measured_language) - 1);
  measured_language[sizeof(measured_language) - 1] = '\0';
  return widest;
}

lv_obj_t* render_weather_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for weather tile");
    return nullptr;
  }

  // The real card size decides what shows (widgets.h).
  // Pixel geometry uses the real card size, including half steps.
  const lv_coord_t tile_w = tile_geometry::extent(
      tile.col, tile.span_w < 1 ? 1.0f : tile.span_w, GRID_CELL_W, GRID_GAP);
  const lv_coord_t tile_h = tile_geometry::extent(
      tile.row, tile.span_h < 1 ? 1.0f : tile.span_h, GRID_CELL_H, GRID_GAP);
  const bool show_forecast = weather_shows_forecast(tile_h);
  uint8_t forecast_cols = show_forecast ? weather_forecast_count(tile_w) : 0;

  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;

  uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  uint32_t pressed_color = brighten_rgb_color(card_color, 0x10);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  const int16_t pad_hor = tile_layout::scale_480(20);
  const int16_t pad_ver = tile_layout::scale_480(24);
  lv_obj_set_style_pad_hor(card, pad_hor, 0);
  lv_obj_set_style_pad_ver(card, pad_ver, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  place_tile_card(card, col, row, tile);

  // Title (location)
  String location = tile.title;
  if (!location.length() && tile.sensor_entity.length()) {
    location = haBridgeConfig.findSensorName(tile.sensor_entity);
  }
  if (!location.length()) location = "--";

  lv_obj_t* icon_label = lv_label_create(card);
  set_label_style(icon_label, lv_color_white(), FONT_MDI_ICONS);
  weather_icons::style_label(icon_label);
  lv_obj_align(icon_label, LV_ALIGN_TOP_LEFT,
               tile_layout::scale_480(-8),
               tile_layout::scale_480(-8));

  lv_obj_t* location_label = lv_label_create(card);
  set_label_style(location_label, lv_color_white(),
                  tile_layout::header_title_font());
  lv_label_set_long_mode(location_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(location_label, LV_PCT(70));
  lv_obj_set_style_text_align(location_label, LV_TEXT_ALIGN_RIGHT, 0);
  hometiles_title::tile(location_label, location.c_str(), true);
  lv_obj_align(location_label, LV_ALIGN_TOP_RIGHT,
               tile_layout::scale_480(4),
               tile_layout::scale_480(4));

  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  String iconChar;
  if (!icon_disabled && icon_name.length() && FONT_MDI_ICONS != nullptr) {
    iconChar = weather_icons::text(icon_name, weatherColoredIcons(tile)
                                                  ? weather_icons::Style::Colored
                                                  : weather_icons::Style::Outline);
    // A colored icon carries its weather color (Tile color "From icon").
    if (weatherColoredIcons(tile) && weather_icons::tint(icon_name)) {
      lv_obj_set_style_text_color(icon_label, lv_color_hex(weather_icons::tint(icon_name)), 0);
    }
  }
  if (icon_label) {
    if (iconChar.length()) {
      lv_label_set_text(icon_label, iconChar.c_str());
      lv_obj_clear_flag(icon_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(icon_label, "");
      lv_obj_add_flag(icon_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // The disc takes over the icon's hidden state; state updates toggle both.
  tile_icon_disc::add_round(card, icon_label);

  lv_obj_t* value_row = lv_obj_create(card);
  lv_obj_remove_style_all(value_row);
  lv_obj_set_size(value_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(value_row, weather_tile::kValueGap, 0);
  lv_obj_set_style_bg_opa(value_row, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(value_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(value_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* condition_label = nullptr;
  lv_obj_t* sep_label = nullptr;
  lv_obj_t* temp_label = nullptr;

  if (weather_shows_condition(tile.span_w)) {
    const lv_font_t* condition_font = weather_tile::value_font();

    condition_label = lv_label_create(value_row);
    set_label_style(condition_label, lv_color_white(), condition_font);
    lv_label_set_long_mode(condition_label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(condition_label, lv_font_get_line_height(condition_font));
    lv_obj_set_width(condition_label, LV_SIZE_CONTENT);
    // The state update narrows this to the room left beside the temperature.
    lv_obj_set_style_max_width(condition_label, tile_w - 2 * pad_hor, 0);
    lv_label_set_text(condition_label, "--");
    lv_obj_add_flag(condition_label, LV_OBJ_FLAG_HIDDEN);

    sep_label = lv_label_create(value_row);
    set_label_style(sep_label, lv_color_hex(0xB0B0B0),
                    weather_tile::value_font());
    lv_label_set_text(sep_label, "|");
    lv_obj_add_flag(sep_label, LV_OBJ_FLAG_HIDDEN);
  }

  temp_label = lv_label_create(value_row);
  set_label_style(temp_label, lv_color_white(), weather_tile::value_font());
  lv_label_set_text(temp_label, "--");

  if (!show_forecast) {
    // Like the Sensor tile: centred in the real card plus the same offset, so
    // a half-step taller card moves the value down with it (identical at 1 cell).
    lv_obj_align(value_row, LV_ALIGN_CENTER, 0, weather_tile::kValueDy);
  } else {
    // With a forecast the value stays in the top cell above the forecast row.
    lv_obj_update_layout(value_row);
    lv_coord_t row_h = lv_obj_get_height(value_row);
    lv_coord_t value_row_y =
        (GRID_CELL_H - 2 * pad_ver) / 2 - row_h / 2 + weather_tile::kValueDy;
    lv_obj_align(value_row, LV_ALIGN_TOP_MID, 0, value_row_y);
  }

  lv_obj_t* forecast_row = nullptr;
  if (show_forecast) {
    constexpr lv_coord_t kTileForecastTopHeadroom = weather_tile::kForecastHeadroom;
    forecast_row = lv_obj_create(card);
    lv_obj_remove_style_all(forecast_row);
    lv_obj_set_size(forecast_row, tile_w,
                    GRID_CELL_H + kTileForecastTopHeadroom);
    lv_obj_set_style_bg_opa(forecast_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(forecast_row, 0, 0);
    lv_obj_remove_flag(forecast_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(forecast_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(forecast_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_coord_t forecast_x = -pad_hor;
    lv_coord_t forecast_y =
        (tile_h - GRID_CELL_H) - pad_ver - kTileForecastTopHeadroom +
        weather_tile::kForecastYOffset;
    lv_obj_set_pos(forecast_row, forecast_x, forecast_y);
  }

  WeatherTileWidgets* target = tile_renderer_get_weather_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    WeatherTileWidgets widgets{};
    widgets.icon_label = icon_label;
    widgets.temp_label = temp_label;
    widgets.condition_label = condition_label;
    widgets.condition_sep_label = sep_label;
    widgets.location_label = location_label;

    if (forecast_row && forecast_cols > 0) {
      constexpr lv_coord_t kTileForecastTopHeadroom = weather_tile::kForecastHeadroom;
      // Even room around the days' texts: the same between two days and at
      // the card's edges; each column centres on its day.
      const lv_coord_t text_w = weather_forecast_text_width();
      const lv_coord_t spacing = (tile_w - forecast_cols * text_w) / (forecast_cols + 1);
      for (uint8_t i = 0; i < forecast_cols; ++i) {
        lv_obj_t* col = lv_obj_create(forecast_row);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, WEATHER_FORECAST_COL_W, GRID_CELL_H + kTileForecastTopHeadroom);
        lv_obj_set_style_pad_hor(col, pad_hor, 0);
        lv_obj_set_style_pad_ver(col, pad_ver, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_pos(col,
                       spacing + text_w / 2 + i * (text_w + spacing) - WEATHER_FORECAST_COL_W / 2,
                       0);

        lv_obj_t* day = lv_label_create(col);
        set_label_style(day, lv_color_white(), weather_tile::forecast_day_font());
        lv_label_set_long_mode(day, LV_LABEL_LONG_DOT);
        lv_obj_set_width(day, LV_PCT(100));
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(day, "--");
        lv_obj_set_pos(day, 0, weather_tile::kForecastDayTop);

        lv_obj_t* icon = lv_label_create(col);
        set_label_style(icon, lv_color_white(), FONT_MDI_ICONS);
        weather_icons::style_label(icon);
        lv_obj_set_width(icon, LV_PCT(100));
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(icon, "");
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(icon, 0, weather_tile::kForecastIconTop);

        lv_obj_t* hi_val = lv_label_create(col);
        set_label_style(hi_val, lv_color_white(), weather_tile::forecast_font());
        lv_obj_set_style_text_align(hi_val, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(hi_val, "");
        lv_obj_add_flag(hi_val, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* hi_unit = lv_label_create(col);
        set_label_style(hi_unit, lv_color_white(), weather_tile::unit_font());
        lv_obj_set_style_text_align(hi_unit, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(hi_unit, "");
        lv_obj_add_flag(hi_unit, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* lo_val = lv_label_create(col);
        set_label_style(lo_val, lv_color_white(), weather_tile::forecast_font());
        lv_obj_set_style_text_align(lo_val, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(lo_val, "");
        lv_obj_add_flag(lo_val, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* lo_unit = lv_label_create(col);
        set_label_style(lo_unit, lv_color_white(), weather_tile::unit_font());
        lv_obj_set_style_text_align(lo_unit, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(lo_unit, "");
        lv_obj_add_flag(lo_unit, LV_OBJ_FLAG_HIDDEN);

        widgets.forecast[i].day_label = day;
        widgets.forecast[i].icon_label = icon;
        widgets.forecast[i].temp_label = nullptr;
        widgets.forecast[i].temp_high_label = hi_val;
        widgets.forecast[i].temp_high_unit_label = hi_unit;
        widgets.forecast[i].temp_low_label = lo_val;
        widgets.forecast[i].temp_low_unit_label = lo_unit;
      }
    }

    target[index] = widgets;
  }

  if (tile.sensor_entity.length() && grid_type != GridType::SCREENSAVER) {
    WeatherEventData* data = new WeatherEventData{
      tile.sensor_entity,
      location,
      location_label,
      card_color,
      weatherColoredIcons(tile)
    };

    const lv_event_code_t popup_event =
        (getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS)
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;

    auto show_popup = [](lv_event_t* e) {
      WeatherEventData* data = static_cast<WeatherEventData*>(lv_event_get_user_data(e));
      if (!data || !data->entity_id.length()) return;

      String title = data->title;
      if (data->location_label) {
        const char* label_text = hometiles_title::text(data->location_label);
        if (label_text && *label_text) {
          title = label_text;
        }
      }
      title.trim();
      if (title == "--") title = "";
      WeatherPopupInit init;
      init.entity_id = data->entity_id;
      init.title = title;
      init.bg_color = tile_icon_source::popup_background(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), data->bg_color);
      // The popup header icon shows the tile icon's current color.
      if (lv_obj_t* icon = tile_icon_source::card_icon(static_cast<lv_obj_t*>(lv_event_get_current_target(e)))) {
        init.icon_color = lv_color_to_u32(lv_obj_get_style_text_color(icon, LV_PART_MAIN)) & 0xFFFFFF;
        lv_color_t forced;
        init.icon_forced = tile_icon_disc::forced_color(icon, forced);
      }
      init.colored_icons = data->colored_icons;
      finish_press_before_popup(e);
      show_weather_popup(init);
    };

    lv_obj_add_event_cb(card, show_popup, popup_event, data);

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          WeatherEventData* data = static_cast<WeatherEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        data);
  }

  if (grid_type != GridType::SCREENSAVER) register_popup_background(card);
  return card;
}
