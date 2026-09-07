#include "src/types/switch/renderer.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/tiles/config/tile_config.h"
#include <Arduino.h>

struct SwitchEventData {
  String entity_id;
  String title;
  GridType grid_type;
  uint8_t index = 0;
  bool use_switch_widget = false;
};

static SwitchState* get_switch_state_array(GridType grid_type) {
  return tile_renderer_get_switch_states(grid_type);
}

static SwitchState get_switch_state(GridType grid_type, uint8_t index) {
  SwitchState* states = get_switch_state_array(grid_type);
  if (!states || index >= TILES_PER_GRID) return {};
  return states[index];
}

static bool is_switch_widget_tile(const Tile& tile);

static void toggle_switch_tile(const SwitchEventData* data) {
  if (!data || !data->entity_id.length()) return;

  const SwitchState current = get_switch_state(data->grid_type, data->index);
  if (!current.available) return;

  if (data->use_switch_widget) {
    if (current.has_state) {
      const bool next_on = !current.is_on;
      update_switch_tile_state(data->grid_type, data->index, next_on ? "on" : "off");
      mqttPublishSwitchCommand(data->entity_id.c_str(), next_on ? "on" : "off");
      return;
    }
  }

  Serial.printf("[Tile] Switch toggle: %s\n", data->entity_id.c_str());
  mqttPublishSwitchCommand(data->entity_id.c_str(), "toggle");
}

static LightPopupInit build_light_popup_init(const SwitchEventData* data) {
  LightPopupInit init;
  if (!data) return init;
  init.entity_id = data->entity_id;
  init.title = data->title;
  init.is_light = is_light_entity_id(data->entity_id);

  // Get icon from tile config (fallback to HA icon when empty)
  const Tile* tile_ptr = tile_renderer_get_tile_config(data->grid_type, data->index);
  if (tile_ptr) {
    const Tile& tile = *tile_ptr;
    init.keep_icon_white = is_switch_widget_tile(tile);
    init.has_tile_ref = true;
    init.tile_grid = static_cast<uint8_t>(data->grid_type);
    init.tile_index = data->index;
    bool icon_disabled = isMdiIconDisabled(tile.icon_name);
    init.icon_name = normalizeMdiIconName(tile.icon_name);
    if (!icon_disabled && !init.icon_name.length() && data->entity_id.length()) {
      init.icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(data->entity_id));
    }
  }

  const SwitchState state = get_switch_state(data->grid_type, data->index);
  init.available = state.available;
  init.has_state = state.has_state;
  init.has_color = state.has_color;
  init.has_brightness = state.has_brightness;
  init.has_color_temp = state.has_color_temp;
  init.has_hs = state.has_hs;
  init.hs_h = state.hs_h;
  init.hs_s = state.hs_s;
  init.color_temp_kelvin = state.color_temp_kelvin;
  init.min_color_temp_kelvin = state.min_color_temp_kelvin;
  init.max_color_temp_kelvin = state.max_color_temp_kelvin;
  if (state.has_state) {
    init.is_on = state.is_on;
  } else if (state.has_brightness) {
    init.is_on = state.brightness_pct > 0;
  } else {
    init.is_on = true;
  }

  if (init.is_light) {
    init.supports_color = state.supports_color;
    init.supports_brightness = state.supports_brightness || state.supports_color;
    init.supports_temperature = state.supports_temperature;
  } else {
    init.supports_color = false;
    init.supports_brightness = false;
    init.supports_temperature = false;
  }
  if (state.has_color) {
    init.color = state.color;
  }
  if (state.has_brightness) {
    init.brightness_pct = state.brightness_pct;
  } else if (state.has_state && !state.is_on) {
    init.brightness_pct = 0;
  } else {
    init.brightness_pct = 100;
  }
  return init;
}

static bool is_switch_widget_tile(const Tile& tile) {
  return tile.sensor_decimals == 1;
}

lv_obj_t* render_switch_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type) {
  const bool use_switch_widget = is_switch_widget_tile(tile);
  lv_obj_t* container = use_switch_widget ? lv_obj_create(parent) : lv_button_create(parent);
  lv_obj_set_style_radius(container, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(container, 0, 0);

  // Use the configured color; default to 0x353535 when color is 0.
  uint32_t tile_color = tileBgColorOrDefault(tile, 0x2A2A2A);
  lv_obj_set_style_bg_color(container, lv_color_hex(tile_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(container, lv_color_hex(tile_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(container, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

  if (!use_switch_widget) {
    // Pressed state: 10% brighter.
    uint32_t pressed_color = brighten_rgb_color(tile_color, 0x10);
    lv_obj_set_style_bg_color(container, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(container, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(container, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  }

  lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(container, 0, 0);
  if (use_switch_widget) {
    lv_obj_set_style_pad_hor(container, tile_layout::scale_480(20), 0);
    lv_obj_set_style_pad_ver(container, tile_layout::scale_480(24), 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  if (!use_switch_widget) disable_pressed_button_animation(container);

  set_tile_grid_cell(container, col, row, tile.span_w, tile.span_h);

  // Optional icon label when icon_name is set.
  lv_obj_t* icon_lbl = nullptr;
  lv_obj_t* title_lbl = nullptr;
  String icon_name = tile.icon_name;
  bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  String iconChar;
  if (icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
    iconChar = getMdiChar(icon_name);
  }
  bool has_icon = iconChar.length() > 0;
  bool has_title = tile.title.length() > 0;

  if (has_icon) {
    icon_lbl = lv_label_create(container);
    if (icon_lbl) {
      set_label_style(icon_lbl, lv_color_white(), FONT_MDI_ICONS);
      lv_label_set_text(icon_lbl, iconChar.c_str());

      if (use_switch_widget) {
        lv_obj_align(icon_lbl, LV_ALIGN_TOP_RIGHT,
                     tile_layout::scale_480(4),
                     tile_layout::scale_480(-8));
      } else {
        // Center icon and title on two lines, or the icon alone on one line.
        if (has_title) {
          lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0,
                       tile_layout::scale_480(-20));
        } else {
          lv_obj_center(icon_lbl);
        }
      }
    }
  }

  // Show the title label only when a title is set.
  if (has_title) {
    title_lbl = lv_label_create(container);
    if (title_lbl) {
      set_label_style(title_lbl, lv_color_white(),
                      tile_layout::header_title_font());
      if (use_switch_widget && has_icon) lv_obj_set_width(title_lbl, LV_PCT(70));
      hometiles_title::tile(title_lbl, tile.title.c_str(), use_switch_widget);

      if (use_switch_widget) {
        lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0,
                     tile_layout::scale_480(4));
      } else {
        // Position below the icon, or center when there is no icon.
        if (icon_lbl) {
          lv_obj_align(title_lbl, LV_ALIGN_CENTER, 0,
                       tile_layout::scale_480(35));
        } else {
          lv_obj_center(title_lbl);
        }
      }
    }
  }

  lv_obj_t* switch_obj = nullptr;
  if (use_switch_widget) {
    switch_obj = lv_switch_create(container);
    if (switch_obj) {
      lv_obj_set_size(switch_obj, tile_layout::scale(90),
                      tile_layout::scale(44));
      lv_obj_align(switch_obj, LV_ALIGN_CENTER, 0,
                   tile_layout::scale(28));
      lv_obj_set_ext_click_area(switch_obj, tile_layout::scale(18));
      lv_obj_clear_flag(switch_obj, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(switch_obj, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(switch_obj, LV_SCROLLBAR_MODE_OFF);
      lv_obj_set_style_bg_color(switch_obj, lv_color_hex(tile_color), LV_PART_KNOB);
      lv_obj_set_style_bg_color(switch_obj, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_color(switch_obj, lv_color_hex(0x3B82F6), LV_PART_INDICATOR | LV_STATE_CHECKED);
    }
  }

  SwitchTileWidgets* target = tile_renderer_get_switch_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].icon_label = icon_lbl;
    target[index].title_label = title_lbl;
    target[index].switch_obj = switch_obj;
  }

  if (tile.sensor_entity.length()) {
    String initial = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    if (initial.length()) {
      update_switch_tile_state(grid_type, index, initial.c_str());
    }
  }

  if (tile.sensor_entity.length()) {
    SwitchEventData* event_data = new SwitchEventData{
      tile.sensor_entity,
      tile.title,
      grid_type,
      index,
      use_switch_widget
    };
    const bool allow_popup = grid_type != GridType::SCREENSAVER;
    const bool popup_on_short =
        allow_popup &&
        getTilePopupOpenMode(tile) == TILE_POPUP_OPEN_SHORT_PRESS;
    const lv_event_code_t popup_event =
        popup_on_short ? LV_EVENT_SHORT_CLICKED : LV_EVENT_LONG_PRESSED;
    const lv_event_code_t toggle_event =
        popup_on_short ? LV_EVENT_LONG_PRESSED : LV_EVENT_SHORT_CLICKED;

    lv_obj_add_event_cb(
        container,
        [](lv_event_t* e) {
          lv_event_code_t code = lv_event_get_code(e);
          if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
          SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
          toggle_switch_tile(data);
        },
        toggle_event,
        event_data);

    if (allow_popup) {
      lv_obj_add_event_cb(
          container,
          [](lv_event_t* e) {
            lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) return;
            SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
            if (!data) return;
            LightPopupInit init = build_light_popup_init(data);
            finish_press_before_popup(e);
            show_light_popup(init);
          },
          popup_event,
          event_data);
    }

    lv_obj_add_event_cb(
        container,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          SwitchEventData* data = static_cast<SwitchEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        event_data);
  }

  return container;
}
