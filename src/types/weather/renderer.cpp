#include "src/types/weather/renderer.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/types_registry.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/devices/device_select.h"
#include <Arduino.h>

struct WeatherEventData {
  String entity_id;
  String title;
  lv_obj_t* location_label = nullptr;
  uint32_t bg_color = 0;
  bool long_press_pending = false;
  bool popup_opened_for_press = false;
};

namespace {
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_1280X800)
constexpr lv_coord_t kWeatherTileContentYOffset = -10;
#else
constexpr lv_coord_t kWeatherTileContentYOffset = 0;
#endif

#if defined(DEVICE_LAYOUT_1024X600)
constexpr lv_coord_t kWeatherTileForecastYOffset = -5;
#else
constexpr lv_coord_t kWeatherTileForecastYOffset = 0;
#endif

const lv_font_t* weather_value_font() {
  return FONT_VALUE;
}

const lv_font_t* weather_forecast_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return tile_layout::content_font_20();
#else
  return tile_layout::content_font_24();
#endif
}

const lv_font_t* weather_forecast_day_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return tile_layout::content_font_20();
#else
  return FONT_TITLE;
#endif
}

const lv_font_t* weather_unit_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return LV_FONT_DEFAULT;
#else
  return FONT_SMALL;
#endif
}

void open_current_weather_popup(lv_event_t* event,
                                const WeatherPopupInit& init) {
  lv_obj_t* source = event
                         ? static_cast<lv_obj_t*>(
                               lv_event_get_current_target(event))
                         : nullptr;
  if (!source && event) {
    source = static_cast<lv_obj_t*>(lv_event_get_target(event));
  }
#if !defined(DEVICE_ESP32_S3_RGB_480)
  lv_display_t* display = source ? lv_obj_get_display(source)
                                 : lv_display_get_default();
#endif

  // Release the real button first, then make the already prepared popup part
  // of the same refresh. This preserves the normal pressed feedback without
  // paying for a separate full tile frame before the popup frame.
  const uint32_t refresh_started_ms = millis();
  if (source) {
    lv_obj_clear_state(source, LV_STATE_PRESSED);
  }
  show_weather_popup(init);
#if defined(DEVICE_ESP32_S3_RGB_480)
  // On the S3 RGB panel this callback already runs from LVGL's timer handler.
  // Forcing a nested full refresh here measured 344 ms on hardware. Let the
  // normal refresh timer present the already-invalidated popup instead.
  Serial.printf("[WeatherPopup] Fast open: deferred-refresh=%lu ms\n",
                static_cast<unsigned long>(millis() - refresh_started_ms));
#else
  if (display) {
    lv_refr_now(display);
  }
  Serial.printf("[WeatherPopup] Fast open: combined-refresh=%lu ms\n",
                static_cast<unsigned long>(millis() - refresh_started_ms));
#endif
}
}  // namespace

lv_obj_t* render_weather_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for weather tile");
    return nullptr;
  }

  const uint8_t span_w = tile.span_w < 1 ? 1 : tile.span_w;
  const uint8_t span_h = tile.span_h < 1 ? 1 : tile.span_h;
  const bool show_forecast = (span_h >= 2);
  uint8_t forecast_cols = show_forecast ? weather_forecast_count(span_w) : 0;

  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_PRESS_LOCK);

  uint32_t default_color = get_tile_type_default_bg(tile.type);
  if (default_color == 0) default_color = 0x2A2A2A;
  uint32_t card_color = tileBgColorOrDefault(tile, default_color);
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  uint32_t pressed_color = brighten_rgb_color(card_color, 0x10);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  const int16_t pad_hor = tile_layout::scale_480(20);
  const int16_t pad_ver = tile_layout::scale_480(24);
  lv_obj_set_style_pad_hor(card, pad_hor, 0);
  lv_obj_set_style_pad_ver(card, pad_ver, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

  // Ensure press animation behaves like other tiles, even when tapping child widgets.
  lv_obj_add_event_cb(
      card,
      [](lv_event_t* e) {
        lv_event_code_t code = lv_event_get_code(e);
        lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        if (!obj) return;
        if (code == LV_EVENT_PRESSED) {
          lv_obj_add_state(obj, LV_STATE_PRESSED);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
          lv_obj_clear_state(obj, LV_STATE_PRESSED);
        }
      },
      LV_EVENT_ALL,
      nullptr);

  auto enable_bubble = [](lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  };

  // Title (location)
  String location = tile.title;
  if (!location.length() && tile.sensor_entity.length()) {
    location = haBridgeConfig.findSensorName(tile.sensor_entity);
  }
  if (!location.length()) location = "--";

  lv_obj_t* icon_label = lv_label_create(card);
  set_label_style(icon_label, lv_color_white(), FONT_MDI_ICONS);
  lv_obj_align(icon_label, LV_ALIGN_TOP_LEFT,
               tile_layout::scale_480(-8),
               tile_layout::scale_480(-8));
  enable_bubble(icon_label);

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
  enable_bubble(location_label);

  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  String iconChar;
  if (!icon_disabled && icon_name.length() && FONT_MDI_ICONS != nullptr) {
    iconChar = getMdiChar(icon_name);
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

  lv_obj_t* value_row = lv_obj_create(card);
  lv_obj_remove_style_all(value_row);
  lv_obj_set_size(value_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(value_row, tile_layout::scale(14), 0);
  lv_obj_set_style_bg_opa(value_row, LV_OPA_TRANSP, 0);
  enable_bubble(value_row);
  lv_obj_remove_flag(value_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* condition_label = nullptr;
  lv_obj_t* sep_label = nullptr;
  lv_obj_t* temp_label = nullptr;

  if (span_w > 1) {
    const lv_font_t* condition_font = weather_value_font();

    condition_label = lv_label_create(value_row);
    set_label_style(condition_label, lv_color_white(), condition_font);
    lv_label_set_long_mode(condition_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(condition_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(
        condition_label,
        tile_layout::scale((span_w >= 3) ? 220 : 140), 0);
    lv_label_set_text(condition_label, "--");
    lv_obj_add_flag(condition_label, LV_OBJ_FLAG_HIDDEN);
    enable_bubble(condition_label);

    sep_label = lv_label_create(value_row);
    set_label_style(sep_label, lv_color_hex(0xB0B0B0),
                    weather_value_font());
    lv_label_set_text(sep_label, "|");
    lv_obj_add_flag(sep_label, LV_OBJ_FLAG_HIDDEN);
    enable_bubble(sep_label);
  }

  temp_label = lv_label_create(value_row);
  set_label_style(temp_label, lv_color_white(), weather_value_font());
  lv_label_set_text(temp_label, "--");
  enable_bubble(temp_label);

  // Position like a 1x1 sensor tile: keep the same top offset regardless of span.
  lv_obj_update_layout(value_row);
  lv_coord_t row_h = lv_obj_get_height(value_row);
  lv_coord_t base_center =
      ((GRID_CELL_H - tile_layout::scale_480(48)) / 2) +
      tile_layout::scale_480(28);
  lv_coord_t value_row_y = base_center - (row_h / 2) + kWeatherTileContentYOffset;
  lv_obj_align(value_row, LV_ALIGN_TOP_MID, 0, value_row_y);

  lv_obj_t* forecast_row = nullptr;
  if (show_forecast) {
    constexpr lv_coord_t kTileForecastTopHeadroom =
        tile_layout::scale(52);
    forecast_row = lv_obj_create(card);
    lv_obj_remove_style_all(forecast_row);
    lv_obj_set_size(forecast_row,
                    (span_w * GRID_CELL_W) + ((span_w - 1) * GRID_GAP),
                    GRID_CELL_H + kTileForecastTopHeadroom);
    lv_obj_set_style_bg_opa(forecast_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(forecast_row, 0, 0);
    enable_bubble(forecast_row);
    lv_obj_remove_flag(forecast_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(forecast_row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_coord_t forecast_x = -pad_hor;
    lv_coord_t forecast_y =
        ((span_h - 1) * (GRID_CELL_H + GRID_GAP)) - pad_ver - kTileForecastTopHeadroom +
        kWeatherTileContentYOffset + kWeatherTileForecastYOffset;
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
      constexpr lv_coord_t kTileForecastTopHeadroom =
          tile_layout::scale(52);
      const lv_coord_t total_w = (span_w * GRID_CELL_W) + ((span_w - 1) * GRID_GAP);
      const lv_coord_t cols_total = forecast_cols * WEATHER_FORECAST_COL_W;
      const lv_coord_t remaining = total_w - cols_total;
      // Distribute evenly: left margin + gaps + right margin = forecast_cols + 1 spaces.
      const lv_coord_t spacing = remaining / (forecast_cols + 1);
      for (uint8_t i = 0; i < forecast_cols; ++i) {
        lv_obj_t* col = lv_obj_create(forecast_row);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, WEATHER_FORECAST_COL_W, GRID_CELL_H + kTileForecastTopHeadroom);
        lv_obj_set_style_pad_hor(col, pad_hor, 0);
        lv_obj_set_style_pad_ver(col, pad_ver, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        enable_bubble(col);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_pos(col,
                       spacing + i * (WEATHER_FORECAST_COL_W + spacing),
                       0);

        constexpr lv_coord_t kTileForecastDayTop =
            kTileForecastTopHeadroom - tile_layout::scale(33);
        constexpr lv_coord_t kTileForecastIconTop =
            kTileForecastTopHeadroom - tile_layout::scale(8);
        constexpr lv_coord_t kTileForecastTempTop =
            kTileForecastTopHeadroom + tile_layout::scale(54);

        lv_obj_t* day = lv_label_create(col);
        set_label_style(day, lv_color_white(), weather_forecast_day_font());
        lv_label_set_long_mode(day, LV_LABEL_LONG_DOT);
        lv_obj_set_width(day, LV_PCT(100));
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(day, "--");
        lv_obj_set_pos(day, 0, kTileForecastDayTop);
        enable_bubble(day);

        lv_obj_t* icon = lv_label_create(col);
        set_label_style(icon, lv_color_white(), FONT_MDI_ICONS);
        lv_obj_set_width(icon, LV_PCT(100));
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(icon, "");
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(icon, 0, kTileForecastIconTop);
        enable_bubble(icon);

        constexpr lv_coord_t kTileForecastLowTop =
            kTileForecastTempTop + tile_layout::scale(30);

        lv_obj_t* hi_val = lv_label_create(col);
        set_label_style(hi_val, lv_color_white(), weather_forecast_font());
        lv_obj_set_style_text_align(hi_val, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(hi_val, "");
        lv_obj_add_flag(hi_val, LV_OBJ_FLAG_HIDDEN);
        enable_bubble(hi_val);

        lv_obj_t* hi_unit = lv_label_create(col);
        set_label_style(hi_unit, lv_color_white(), weather_unit_font());
        lv_obj_set_style_text_align(hi_unit, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(hi_unit, "");
        lv_obj_add_flag(hi_unit, LV_OBJ_FLAG_HIDDEN);
        enable_bubble(hi_unit);

        lv_obj_t* lo_val = lv_label_create(col);
        set_label_style(lo_val, lv_color_white(), weather_forecast_font());
        lv_obj_set_style_text_align(lo_val, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(lo_val, "");
        lv_obj_add_flag(lo_val, LV_OBJ_FLAG_HIDDEN);
        enable_bubble(lo_val);

        lv_obj_t* lo_unit = lv_label_create(col);
        set_label_style(lo_unit, lv_color_white(), weather_unit_font());
        lv_obj_set_style_text_align(lo_unit, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_text(lo_unit, "");
        lv_obj_add_flag(lo_unit, LV_OBJ_FLAG_HIDDEN);
        enable_bubble(lo_unit);

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
      card_color
    };

    auto show_popup = [](lv_event_t* e) {
      lv_event_code_t code = lv_event_get_code(e);
      WeatherEventData* data = static_cast<WeatherEventData*>(lv_event_get_user_data(e));
      if (!data || !data->entity_id.length()) return;

      if (code == LV_EVENT_PRESSED) {
        data->long_press_pending = false;
        data->popup_opened_for_press = false;
        return;
      }
      if (code == LV_EVENT_PRESS_LOST) {
        data->long_press_pending = false;
        return;
      }
      if (code == LV_EVENT_LONG_PRESSED) {
        // A long press is confirmed here, but the popup must wait until the
        // finger is released so the new overlay cannot receive this press.
        data->long_press_pending = true;
        return;
      }

      bool should_open = false;
      if (code == LV_EVENT_RELEASED) {
        should_open = data->long_press_pending;
        data->long_press_pending = false;
        if (should_open) {
          lv_indev_t* indev = lv_indev_active();
          if (indev && lv_indev_get_scroll_obj(indev)) {
            return;
          }
        }
      } else if (code == LV_EVENT_SHORT_CLICKED) {
        // LVGL sends SHORT_CLICKED only after a non-scrolling short release.
        should_open = true;
      } else {
        return;
      }

      if (!should_open || data->popup_opened_for_press) return;
      data->popup_opened_for_press = true;
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
      init.bg_color = data->bg_color;
#if defined(DEVICE_ESP32_S3_RGB_480)
      // The S3 RGB path must not spend a separate full refresh on the released
      // tile. Show the prepared card/header now; cached forecast parsing and
      // the heavier child rebuild remain in the existing two-phase queue.
      open_current_weather_popup(e, init);
#else
      if (weather_popup_has_current_cached_payload(init.entity_id.c_str())) {
        open_current_weather_popup(e, init);
      } else {
        defer_popup_until_source_refreshed(e, init, show_weather_popup);
      }
#endif
    };

    lv_obj_add_event_cb(card, show_popup, LV_EVENT_ALL, data);

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

  return card;
}
