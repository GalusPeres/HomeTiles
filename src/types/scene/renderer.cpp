#include "src/ui/shared/ui_surface_style.h"
#include "src/types/scene/renderer.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/shared/tone_color.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_icon_disc.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/devices/device.h"
#include "src/network/bridge/ha_bridge_config.h"
#include <Arduino.h>
#include "src/types/scene/picture.h"
#include "src/ui/shared/ui_theme.h"

// A picture with a title takes the MDI icon's box: the icon font's height on
// every layout (32/40/48 px), so the icon-and-title group keeps its place.
static lv_coord_t picture_icon_box() {
  return FONT_MDI_ICONS ? lv_font_get_line_height(FONT_MDI_ICONS) : tile_layout::scale_480(48);
}

// A picture covers the card's own pressed color: it dims while the card is
// pressed instead (the card's state, also when the input sets it later).
static void picture_press_cb(lv_event_t* e) {
  lv_obj_t* card = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_obj_t* img = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
  if (!card || !img) return;
  lv_obj_set_style_image_opa(img, lv_obj_has_state(card, LV_STATE_PRESSED) ? LV_OPA_60 : LV_OPA_COVER, 0);
}

// --- Scene tile renderer ---

struct SceneEventData {
  String scene_alias;
  scene_publish_cb_t callback;
};

lv_obj_t* render_scene_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, scene_publish_cb_t scene_cb) {
  lv_obj_t* btn = lv_button_create(parent);
  ui_surface_style::apply_radius(btn, tile_layout::scale_480(22), 0);
  lv_obj_set_style_border_width(btn, 0, 0);

  uint32_t btn_color = tileBgColorOrDefault(tile, tileDefaultBgColor());
  lv_obj_set_style_bg_color(btn, lv_color_hex(btn_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_color(btn, lv_color_hex(btn_color), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
  uint32_t pressed_color = tone_color::pressed_card(btn_color);
  lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_color(btn, lv_color_hex(pressed_color), LV_PART_MAIN | LV_STATE_PRESSED);
lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_transform_width(btn, 0, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(btn, 0, LV_STATE_PRESSED);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

  place_tile_card(btn, col, row, tile);

  String title_trim = tile.title;
  title_trim.trim();
  const bool has_title = title_trim.length() > 0;

  // --- Picture (scene_picture.h): fills the card without a title, else
  // takes the icon's place. A missing or undecodable file keeps the MDI icon.
  lv_obj_t* icon_img = nullptr;
  bool icon_full_bleed = false;
  String image_path = tile.image_path;
  image_path.trim();
  if (image_path.length() > 0 && scene_picture::valid_path(image_path)) {
    if (!has_title) {
      lv_obj_update_layout(btn);
      const lv_coord_t bleed = tile_layout::scale_480(2);
      const lv_coord_t pic_w = lv_obj_get_width(btn) + bleed * 2;
      const lv_coord_t pic_h = lv_obj_get_height(btn) + bleed * 2;
      const lv_image_dsc_t* dsc = (pic_w > 0 && pic_h > 0)
          ? scene_picture::acquire(image_path, static_cast<uint16_t>(pic_w), static_cast<uint16_t>(pic_h), 0)
          : nullptr;
      if (dsc) {
        icon_full_bleed = true;
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_clip_corner(btn, false, 0);
        lv_obj_set_style_clip_corner(btn, false, LV_STATE_PRESSED);

        // Full-size center crop with the card's rounded corners (cover).
        icon_img = lv_obj_create(btn);
        lv_obj_set_size(icon_img, LV_PCT(100), LV_PCT(100));
        lv_obj_align(icon_img, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_opa(icon_img, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(icon_img, 0, 0);
        lv_obj_set_style_pad_all(icon_img, 0, 0);
        ui_surface_style::apply_radius(icon_img, tile_layout::scale_480(22), 0);
        lv_obj_set_style_clip_corner(icon_img, true, 0);
        lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(icon_img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(icon_img, LV_OBJ_FLAG_IGNORE_LAYOUT);

        lv_obj_t* img = lv_image_create(icon_img);
        lv_image_set_src(img, dsc);
        lv_obj_set_size(img, pic_w, pic_h);
        lv_obj_set_pos(img, -bleed, -bleed);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_COVER);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_event_cb(img, scene_picture::release_on_delete, LV_EVENT_DELETE, const_cast<lv_image_dsc_t*>(dsc));
        lv_obj_add_event_cb(btn, picture_press_cb, LV_EVENT_STATE_CHANGED, img);
      }
    } else {
      // Icon mode: the picture in the icon's box, contained.
      const lv_coord_t icon_box = picture_icon_box();
      const lv_image_dsc_t* dsc = scene_picture::acquire(image_path, 0, 0, static_cast<uint16_t>(icon_box));
      if (dsc) {
        icon_img = lv_obj_create(btn);
        lv_obj_set_size(icon_img, icon_box, icon_box);
        lv_obj_set_style_bg_opa(icon_img, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(icon_img, 0, 0);
        lv_obj_set_style_pad_all(icon_img, 0, 0);
        ui_surface_style::apply_radius(icon_img, icon_box / 6, 0);
        lv_obj_set_style_clip_corner(icon_img, true, 0);
        lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(icon_img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(icon_img, LV_OBJ_FLAG_IGNORE_LAYOUT);

        lv_obj_t* img = lv_image_create(icon_img);
        lv_image_set_src(img, dsc);
        lv_obj_set_size(img, icon_box, icon_box);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_event_cb(img, scene_picture::release_on_delete, LV_EVENT_DELETE, const_cast<lv_image_dsc_t*>(dsc));
      }
    }
  }

  // --- Fallback: MDI icon ---
  lv_obj_t* icon_lbl = nullptr;
  if (!icon_img) {
    String icon_name = tile.icon_name;
    bool icon_disabled = isMdiIconDisabled(icon_name);
    icon_name = normalizeMdiIconName(icon_name);
    if (!icon_disabled && !icon_name.length() && tile.scene_alias.length()) {
      String scene_entity = haBridgeConfig.findSceneEntity(tile.scene_alias);
      if (scene_entity.length()) {
        icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(scene_entity));
      }
    }
    String iconChar;
    if (icon_name.length() > 0 && FONT_MDI_ICONS != nullptr) {
      iconChar = getMdiChar(icon_name);
    }
    if (iconChar.length() > 0) {
      icon_lbl = lv_label_create(btn);
      if (icon_lbl) {
        set_label_style(icon_lbl, lv_color_hex(ui_theme::icon()), FONT_MDI_ICONS);
        lv_label_set_text(icon_lbl, iconChar.c_str());
      }
    }
  }

  bool has_icon = (icon_img != nullptr) || (icon_lbl != nullptr);
  // A half-height Scene uses the half-height Sensor header: the icon in the
  // concentric corner disc and the title beside it.
  const bool compact = tile_geometry::compact_icon_title(tile.type, tile.span_w, tile.span_h);
  const tile_icon_disc::IconTitleOffsets offsets =
      tile_icon_disc::icon_title_offsets(FONT_MDI_ICONS, tile_layout::header_title_font());

  // Position icon
  if (icon_img) {
    if (!icon_full_bleed) {
      if (compact) {
        // An image icon takes the disc's size and corner position.
        const int size = tile_icon_disc::diameter();
        lv_obj_set_size(icon_img, size, size);
        ui_surface_style::apply_radius(icon_img, tile_icon_disc::radius_baseline(), 0);
        if (lv_obj_t* img = lv_obj_get_child(icon_img, 0)) lv_obj_set_size(img, size, size);
        lv_obj_align(icon_img, LV_ALIGN_TOP_LEFT, tile_icon_disc::inset(), tile_icon_disc::inset());
      } else if (has_title) {
        lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, offsets.icon);
      } else {
        lv_obj_center(icon_img);
      }
    }
  } else if (icon_lbl) {
    tile_icon_source::apply_initial(icon_lbl, tile);
    if (!compact) {
      if (has_title) {
        lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, offsets.icon);
      } else {
        lv_obj_center(icon_lbl);
      }
      // Only MDI icons get the disc; user images keep their own frame.
      tile_icon_disc::add_round(btn, icon_lbl);
    }
  }

  // Title
  lv_obj_t* title_lbl = nullptr;
  if (has_title) {
    lv_obj_t* l = lv_label_create(btn);
    title_lbl = l;
    if (l) {
      set_label_style(l, lv_color_hex(ui_theme::text()), tile_layout::header_title_font());
      hometiles_title::tile(l, tile.title.c_str(), false);
      if (compact) {
        // compact_sensor_layout places it beside the disc below.
      } else if (has_icon) {
        lv_obj_align(l, LV_ALIGN_CENTER, 0, offsets.title);
      } else {
        lv_obj_center(l);
      }
    }
  }
  if (compact) compact_sensor_layout::apply(btn, icon_lbl, title_lbl, nullptr, tile);

  // Event handler for scene activation.
  if (scene_cb && tile.scene_alias.length()) {
    SceneEventData* event_data = new SceneEventData{tile.scene_alias, scene_cb};

    lv_obj_add_event_cb(
        btn,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
          SceneEventData* data = static_cast<SceneEventData*>(lv_event_get_user_data(e));
          if (data && data->callback) {
            Serial.printf("[Tile] Scene activated: %s\n", data->scene_alias.c_str());
            data->callback(data->scene_alias.c_str());
          }
        },
        LV_EVENT_CLICKED,
        event_data);
    lv_obj_add_event_cb(
        btn,
        [](lv_event_t* e) {
          if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
          SceneEventData* data = static_cast<SceneEventData*>(lv_event_get_user_data(e));
          delete data;
        },
        LV_EVENT_DELETE,
        event_data);
  }

  return btn;
}
