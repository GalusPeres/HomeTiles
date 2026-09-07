#include "src/types/sensor/renderer.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include <Arduino.h>
#include "src/types/value/value_control.h"

static const lv_font_t* get_sensor_value_font(const Tile& tile) {
  switch (tile.sensor_value_font) {
    case 1:
      return tile_layout::content_font_20();
    case 2:
      return tile_layout::content_font_24();
    case 3:
      return tile_layout::content_font_32();
    case 4:
      return tile_layout::content_font_40();
    default:
      return FONT_VALUE;
  }
}

struct SensorEventData {
  String entity_id;
  String title;
  String icon_name;
  bool icon_override = false;
  String unit;
  uint8_t decimals = 0xFF;
  uint32_t bg_color = 0;
  bool editable = false;
};

static bool is_disabled_token(const String& value) {
  if (!value.length()) return false;
  String t = value;
  t.trim();
  if (!t.length()) return true;
  t.toLowerCase();
  return t == "-" || t == "none" || t == "null" || t == "no" || t == "off";
}

lv_obj_t* render_sensor_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for sensor tile");
    return nullptr;
  }

  const uint8_t display_mode = tile.sensor_display_mode;  // 0=none, 1=gauge, 2=graph
  const bool gauge_enabled = (display_mode == 1);
  const bool graph_enabled = (display_mode == 2);
  int32_t gauge_min = tile.sensor_gauge_min;
  int32_t gauge_max = tile.sensor_gauge_max;
  if (gauge_max <= gauge_min) {
    gauge_min = 0;
    gauge_max = 100;
  }

  lv_obj_t* card = lv_button_create(parent);
  if (!card) {
    Serial.println("[TileRenderer] ERROR: Could not create sensor card");
    return nullptr;
  }

  // Use the configured color; default to 0x2A2A2A when color is 0.
  uint32_t card_color = tileBgColorOrDefault(tile, 0x2A2A2A);
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(card_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Pressed state: 10% brighter.
  uint32_t pressed_color = brighten_rgb_color(card_color, 0x10);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(card, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);

  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

  // Optional right-aligned icon label when icon_name is set.
  lv_obj_t* icon_lbl = nullptr;
  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
    String iconChar = getMdiChar(icon_name);
    if (iconChar.length() > 0) {
      icon_lbl = lv_label_create(card);
      if (icon_lbl) {
        set_label_style(icon_lbl, lv_color_white(), FONT_MDI_ICONS);
        lv_label_set_text(icon_lbl, iconChar.c_str());
        lv_obj_align(icon_lbl, LV_ALIGN_TOP_LEFT,
                     tile_layout::scale_480(-8),
                     tile_layout::scale_480(-8));
      }
    }
  }

  lv_obj_t* title_label = nullptr;
  // Right-aligned title label, shown only when a title is set.
  if (tile.title.length() > 0) {
    title_label = lv_label_create(card);
    if (title_label) {
      set_label_style(title_label, lv_color_hex(0xFFFFFF),
                      tile_layout::header_title_font());
      lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
      lv_obj_set_width(title_label, LV_PCT(70));
      lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_text(title_label, tile.title.c_str());
      lv_obj_align(title_label, LV_ALIGN_TOP_RIGHT,
                   tile_layout::scale_480(4),
                   tile_layout::scale_480(4));
    }
  }

  lv_obj_t* gauge = nullptr;
  if (gauge_enabled) {
    // Get gauge appearance from tile settings (with defaults)
    uint16_t arc_degrees = tile.sensor_gauge_arc;
    if (arc_degrees < 90) arc_degrees = 90;
    if (arc_degrees > 359) arc_degrees = 359;
    uint16_t gauge_size = tile.sensor_gauge_size;
    if (gauge_size < 100) gauge_size = 100;
    if (gauge_size > 800) gauge_size = 800;
    gauge_size = tile_layout::scale_u16(gauge_size);
    int16_t y_offset = tile.sensor_gauge_y_offset;
    if (y_offset < -100) y_offset = -100;
    if (y_offset > 200) y_offset = 200;
    y_offset = tile_layout::scale_i16(y_offset);
    // Calculate rotation so gap is always at bottom: rotation = 270 - arc/2
    uint16_t rotation = 270 - (arc_degrees / 2);

    gauge = lv_arc_create(card);
    if (gauge) {
      lv_obj_set_size(gauge, gauge_size, gauge_size);
      lv_obj_align(gauge, LV_ALIGN_TOP_MID, 0, y_offset);
      lv_obj_remove_flag(gauge, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(gauge, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_bg_opa(gauge, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(gauge, 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(gauge, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(gauge, 0, LV_PART_MAIN);

      lv_arc_set_range(gauge, 0, GAUGE_ARC_STEPS);
      lv_arc_set_value(gauge, 0);
      lv_arc_set_rotation(gauge, rotation);
      lv_arc_set_bg_angles(gauge, 0, arc_degrees);
      lv_arc_set_angles(gauge, 0, arc_degrees);

      lv_obj_set_style_arc_width(gauge, tile_layout::scale(14),
                                 LV_PART_MAIN);
      lv_obj_set_style_arc_color(gauge, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
      lv_obj_set_style_arc_rounded(gauge, true, LV_PART_MAIN);

      lv_obj_set_style_arc_width(gauge, tile_layout::scale(14),
                                 LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(gauge, lv_color_hex(0x20A4FF), LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(gauge, true, LV_PART_INDICATOR);

      lv_obj_set_style_bg_opa(gauge, LV_OPA_TRANSP, LV_PART_KNOB);
      lv_obj_set_style_border_opa(gauge, LV_OPA_TRANSP, LV_PART_KNOB);
      lv_obj_set_style_shadow_width(gauge, 0, LV_PART_KNOB);
    }
  }
  if (gauge_enabled) {
    if (icon_lbl) lv_obj_move_foreground(icon_lbl);
    if (title_label) lv_obj_move_foreground(title_label);
  }

  // Graph rendering (display_mode == 2) - positioned BELOW value
  // Same settings as popup: 288 points, line width 4, white color
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  if (graph_enabled) {
    // Get graph height from tile settings (with defaults and clamping)
    uint16_t graph_height = tile.sensor_graph_height;
    if (graph_height < 20) graph_height = 20;
    if (graph_height > 200) graph_height = 200;
    graph_height = tile_layout::scale_u16(graph_height);

    chart = lv_chart_create(card);
    if (chart) {
      lv_obj_set_size(chart, LV_PCT(100), graph_height);
      // Position graph at bottom of tile (below value)
      lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);

      lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
      lv_chart_set_point_count(chart, 288);  // Same as popup
      lv_chart_set_div_line_count(chart, 0, 0);
      lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

      // Styling - same as popup
      lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);

      // Series - white color, line width 4 (same as popup)
      series = lv_chart_add_series(chart, lv_color_white(), LV_CHART_AXIS_PRIMARY_Y);
      lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
      lv_obj_set_style_line_color(chart, lv_color_white(), LV_PART_ITEMS);
      lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
      lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

      // Initialize with LV_CHART_POINT_NONE; history will replace it.
      lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
    }
    if (icon_lbl) lv_obj_move_foreground(icon_lbl);
    if (title_label) lv_obj_move_foreground(title_label);
  }

  // Value label combines the value and unit.
  lv_obj_t* v = lv_label_create(card);
  if (!v) {
    Serial.println("[TileRenderer] ERROR: Could not create value label");
    return card;
  }
  set_label_style(v, lv_color_white(), get_sensor_value_font(tile));
  lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(v, LV_PCT(100));
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(v, 8, 0);
  lv_label_set_text(v, "--");

  // Get value y offset from tile settings (with defaults and clamping)
  int16_t value_y_offset = tile.sensor_value_y_offset;
  if (value_y_offset < -100) value_y_offset = -100;
  if (value_y_offset > 200) value_y_offset = 200;
  value_y_offset = tile_layout::scale_i16(value_y_offset);

  if (gauge_enabled) {
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0,
                 tile_layout::scale(12) + value_y_offset);
  } else if (graph_enabled) {
    // Value above graph: center vertically in upper area
    lv_obj_align(v, LV_ALIGN_CENTER, 0,
                 tile_layout::scale(-20) + value_y_offset);
  } else {
    lv_obj_align(v, LV_ALIGN_CENTER, 0,
                 tile_layout::scale(28) + value_y_offset);
  }

  // Store for later updates.
  SensorTileWidgets* target = tile_renderer_get_sensor_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].value_label = v;
    target[index].unit_label = nullptr;
    target[index].gauge = gauge;
    target[index].gauge_min = gauge_min;
    target[index].gauge_max = gauge_max;
    target[index].chart = chart;
    target[index].series = series;
  }

  // PPA presents the screensaver as a complete fullscreen frame. Popups would
  // add a second overlay, so this mode disables them. Normal grids retain
  // their existing behavior.
  if (tile.sensor_entity.length() && grid_type != GridType::SCREENSAVER) {
    bool icon_override = false;
    if (tile.icon_name.length() &&
        (tileTypeIsEditableValue(tile.type) || !isMdiIconDisabled(tile.icon_name))) {
      icon_override = true;
    }
    SensorEventData* data = new SensorEventData{
      tile.sensor_entity,
      tile.title,
      icon_name,
      icon_override,
      tile.sensor_unit,
      tile.sensor_decimals,
      tileBgColorOrDefault(tile, 0x2A2A2A),
      tileTypeIsEditableValue(tile.type)
    };

    const lv_event_code_t popup_event =
        (getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS)
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          lv_event_code_t code = lv_event_get_code(e);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          SensorEventData* data = static_cast<SensorEventData*>(lv_event_get_user_data(e));
          if (!data || !data->entity_id.length()) return;
          SensorPopupInit init;
          init.entity_id = data->entity_id;
          init.title = data->title;
          if (!init.title.length()) {
            init.title = haBridgeConfig.findSensorName(data->entity_id);
          }
          if (!init.title.length()) {
            init.title = data->entity_id;
          }
          String icon_name = data->icon_name;
          if (!data->icon_override) {
            String resolved = normalizeMdiIconName(haBridgeConfig.findEntityIcon(data->entity_id));
            if (resolved.length()) {
              icon_name = resolved;
            }
          }
          init.icon_name = icon_name;
          init.binary_icon_override = data->icon_override;
          String unit = data->unit;
          const bool lock_unit = unit.length() > 0;
          if (is_disabled_token(unit)) {
            unit = "";
          } else if (!unit.length()) {
            unit = haBridgeConfig.findSensorUnit(data->entity_id);
          } else {
            unit.trim();
          }
          init.unit = unit;
          init.lock_unit = lock_unit;
          init.decimals = data->decimals;
          init.bg_color = data->bg_color;
          init.value = haBridgeConfig.findSensorInitialValue(data->entity_id);
          String state_kind =
              haBridgeConfig.findSensorStateKind(data->entity_id);
          state_kind.trim();
          state_kind.toLowerCase();
          if (state_kind == "state") {
            init.state_history_mode = true;
          } else if (state_kind == "number") {
            init.state_history_mode = false;
          } else {
            // Backward compatibility with older Bridge metadata.
            init.state_history_mode =
                sensor_popup_should_use_state_history(init.value, init.unit);
          }
          init.editable = data->editable;
          if (init.editable) {
            const EditableValue value = parse_editable_value(haBridgeConfig.findEditableValue(data->entity_id));
            init.value = value.state; init.unit = value.unit;
            init.state_history_mode = value.kind != "number";
          }
          finish_press_before_popup(e);
          show_sensor_popup(init);
        },
        popup_event,
        data);

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          SensorEventData* data = static_cast<SensorEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        data);
  }

  // Request history for graph tiles
  if (graph_enabled && tile.sensor_entity.length()) {
    request_tile_graph_history(tile.sensor_entity.c_str());
  }

  return card;
}


