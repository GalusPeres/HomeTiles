#include "src/ui/shared/ui_surface_style.h"
#include "src/types/camera/renderer.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/camera/camera_popup.h"

namespace {

static const i18n::Strings& camera_text() {
  return i18n::strings(configManager.getConfig().language);
}

struct CameraEventData {
  String entity_id;
  String title;
  String icon_name;
  uint32_t bg_color = 0x2A2A2A;
  // Icon colors record: the popup header icon takes the tile icon's color.
  String icon_colors;
};

static String friendly_camera_name(const String& entity_id) {
  String name = haBridgeConfig.findSensorName(entity_id);
  if (name.length()) return name;
  const int dot = entity_id.indexOf('.');
  name = dot >= 0 ? entity_id.substring(dot + 1) : entity_id;
  name.replace('_', ' ');
  if (name.length() && name[0] >= 'a' && name[0] <= 'z') {
    name.setCharAt(0, static_cast<char>(name[0] - ('a' - 'A')));
  }
  return name;
}

static void camera_tile_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
  CameraEventData* data =
      static_cast<CameraEventData*>(lv_event_get_user_data(event));
  if (!data || !data->entity_id.length()) return;
  finish_press_before_popup(event);
  CameraPopupInit init;
  init.entity_id = data->entity_id;
  init.title = data->title;
  init.icon_name = data->icon_name;
  init.bg_color = data->bg_color;
  // The header icon takes the tile icon's color (fixed or from the source
  // entity's latest state).
  const String source = tileIconSourceEntity(TILE_CAMERA, data->icon_colors);
  String payload;
  if (source.length()) tile_icon_source::cached_payload(source, payload);
  init.icon_color = tile_icon_source::color(data->icon_colors, payload.c_str());
  show_camera_popup(init);
}

static void camera_tile_delete_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
  delete static_cast<CameraEventData*>(lv_event_get_user_data(event));
}

}  // namespace

lv_obj_t* render_camera_tile(lv_obj_t* parent,
                             int col,
                             int row,
                             const Tile& tile,
                             uint8_t,
                             GridType grid_type) {
  if (!parent) return nullptr;

  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;
  const uint32_t card_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(card, lv_color_hex(card_color),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(card,
                            lv_color_hex(brighten_rgb_color(card_color, 0x10)),
                            LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(card, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(card);
  place_tile_card(card, col, row, tile);

  String icon_name = tile.icon_name;
  const bool icon_disabled = isMdiIconDisabled(icon_name);
  icon_name = normalizeMdiIconName(icon_name);
  if (!icon_disabled && !icon_name.length() && tile.sensor_entity.length()) {
    icon_name =
        normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (!icon_disabled && !icon_name.length()) icon_name = "video";

  String title = tile.title;
  title.trim();
  if (!title.length()) title = friendly_camera_name(tile.sensor_entity);
  if (!title.length()) title = camera_text().camera_tile_type;

  // A half-height Camera uses the half-height Sensor header: the icon in the
  // concentric corner disc and the title beside it.
  const bool compact = tile_geometry::compact_icon_title(tile.type, tile.span_w, tile.span_h);
  lv_obj_t* icon = nullptr;
  String icon_char;
  if (icon_name.length() && FONT_MDI_ICONS != nullptr) {
    icon_char = getMdiChar(icon_name);
  }
  if (icon_char.length()) {
    icon = lv_label_create(card);
    set_label_style(icon, lv_color_white(), FONT_MDI_ICONS);
    lv_label_set_text(icon, icon_char.c_str());
    tile_icon_source::apply_initial(icon, tile);
    if (!compact) {
      lv_obj_align(icon, LV_ALIGN_CENTER, 0, tile_layout::scale_i16(-20));
      tile_icon_disc::add_round(card, icon);
    }
  }

  lv_obj_t* title_label = lv_label_create(card);
  set_label_style(title_label, lv_color_white(),
                  tile_layout::header_title_font());
  hometiles_title::tile(title_label, title.c_str(), false);
  if (compact) {
    compact_sensor_layout::apply(card, icon, title_label, nullptr, tile);
  } else if (icon) {
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, tile_layout::scale(35));
  } else {
    lv_obj_center(title_label);
  }

  if (grid_type != GridType::SCREENSAVER && tile.sensor_entity.length()) {
    CameraEventData* event_data = new CameraEventData{
        tile.sensor_entity, title, icon_name, card_color, tile.icon_colors};
    lv_obj_add_event_cb(card, camera_tile_event_cb, LV_EVENT_SHORT_CLICKED,
                        event_data);
    lv_obj_add_event_cb(card, camera_tile_delete_cb, LV_EVENT_DELETE,
                        event_data);
  }
  return card;
}
