#include "src/types/energy/renderer.h"

#include <Arduino.h>

#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/energy/energy_data.h"
#include "src/ui/popups/energy/energy_popup.h"

namespace {

struct EnergyEventData {
  String entity_id;
  String title;
  String icon_name;
  bool icon_override = false;
  String unit;
  uint8_t decimals = 1;
  uint32_t bg_color = 0;
};

const lv_font_t* get_energy_value_font(const Tile& tile) {
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

bool is_disabled_token(const String& value) {
  if (!value.length()) return false;
  String t = value;
  t.trim();
  if (!t.length()) return true;
  t.toLowerCase();
  return t == "-" || t == "none" || t == "null" || t == "no" || t == "off";
}

}  // namespace

lv_obj_t* render_energy_tile(lv_obj_t* parent,
                             int col,
                             int row,
                             const Tile& tile,
                             uint8_t index,
                             GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for energy tile");
    return nullptr;
  }

  lv_obj_t* card = lv_button_create(parent);
  if (!card) {
    Serial.println("[TileRenderer] ERROR: Could not create energy card");
    return nullptr;
  }

  uint32_t card_color = tileBgColorOrDefault(tile, 0x2A2A2A);
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
  lv_obj_set_style_pad_hor(card, tile_layout::scale_480(20), 0);
  lv_obj_set_style_pad_ver(card, tile_layout::scale_480(24), 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);

  set_tile_grid_cell(card, col, row, tile.span_w, tile.span_h);

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

  String title_text = tile.title;
  if (!title_text.length() && tile.sensor_entity.length()) {
    title_text = haBridgeConfig.findSensorName(tile.sensor_entity);
  }

  if (title_text.length() > 0) {
    lv_obj_t* title_label = lv_label_create(card);
    if (title_label) {
      set_label_style(title_label, lv_color_hex(0xFFFFFF),
                      tile_layout::header_title_font());
      lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
      lv_obj_set_width(title_label, LV_PCT(70));
      lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_RIGHT, 0);
      hometiles_title::tile(title_label, title_text.c_str(), true);
      lv_obj_align(title_label, LV_ALIGN_TOP_RIGHT,
                   tile_layout::scale_480(4),
                   tile_layout::scale_480(4));
    }
  }

  lv_obj_t* value_label = lv_label_create(card);
  if (!value_label) {
    return card;
  }
  set_label_style(value_label, lv_color_white(), get_energy_value_font(tile));
  lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(value_label, LV_PCT(100));
  lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(value_label, tile_layout::scale_480(8), 0);
  lv_label_set_text(value_label, "--");

  int16_t value_y_offset = tile.sensor_value_y_offset;
  if (value_y_offset < -100) value_y_offset = -100;
  if (value_y_offset > 200) value_y_offset = 200;
  value_y_offset = tile_layout::scale_i16(value_y_offset);
  lv_obj_align(value_label, LV_ALIGN_CENTER, 0,
               tile_layout::scale(28) + value_y_offset);

  SensorTileWidgets* target = tile_renderer_get_sensor_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].value_label = value_label;
    target[index].unit_label = nullptr;
    target[index].gauge = nullptr;
    target[index].gauge_min = 0;
    target[index].gauge_max = 100;
    target[index].chart = nullptr;
    target[index].series = nullptr;
  }

  if (tile.sensor_entity.length() && grid_type != GridType::SCREENSAVER) {
    bool icon_override = false;
    if (tile.icon_name.length() && !isMdiIconDisabled(tile.icon_name)) {
      icon_override = true;
    }
    EnergyEventData* data = new EnergyEventData();
    data->entity_id = tile.sensor_entity;
    data->title = title_text;
    data->icon_name = icon_name;
    data->icon_override = icon_override;
    data->unit = tile.sensor_unit;
    data->decimals = tile.sensor_decimals == 0xFF ? static_cast<uint8_t>(1) : tile.sensor_decimals;
    data->bg_color = card_color;

    const lv_event_code_t popup_event =
        (getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS)
            ? LV_EVENT_SHORT_CLICKED
            : LV_EVENT_LONG_PRESSED;

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          lv_event_code_t code = lv_event_get_code(e);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          EnergyEventData* data = static_cast<EnergyEventData*>(lv_event_get_user_data(e));
          if (!data || !data->entity_id.length()) return;

          EnergyPopupInit init;
          init.entity_id = data->entity_id;
          init.title = data->title.length() ? data->title : haBridgeConfig.findSensorName(data->entity_id);
          if (!init.title.length()) init.title = data->entity_id;
          init.icon_name = data->icon_name;

          String unit = data->unit;
          if (is_disabled_token(unit)) {
            unit = "";
          } else if (!unit.length()) {
            unit = haBridgeConfig.findSensorUnit(data->entity_id);
          } else {
            unit.trim();
          }
          init.unit = unit;
          init.decimals = data->decimals;
          init.bg_color = data->bg_color;

          finish_press_before_popup(e);
          show_energy_popup(init);
        },
        popup_event,
        data);

    lv_obj_add_event_cb(
        card,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          EnergyEventData* data = static_cast<EnergyEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        data);

    energy_request_period("day", false);
  }

  return card;
}
