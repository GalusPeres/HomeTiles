#include "src/types/media/renderer.h"
#include "src/types/media/content_layout.h"

#include <Arduino.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/network/bridge/ha_bridge_config.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/devices/device_select.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/media/media_popup.h"

#include <stdlib.h>
#include <string.h>

// LVGL 9.5 no longer exports lv_image_cache_drop() through lvgl.h;
// its declaration is only in the instance header.
#include <misc/cache/instance/lv_image_cache.h>

namespace {

#if defined(DEVICE_WAVESHARE_4B)
static constexpr lv_coord_t kMediaControlButtonSize = 76;
static constexpr lv_coord_t kMediaControlSideOffset = 96;
static constexpr lv_coord_t kMediaControlBottomOffset = -8;
static constexpr lv_coord_t kMediaContentYOffset = 8;
#elif defined(DEVICE_LAYOUT_480X480)
static constexpr lv_coord_t kMediaControlButtonSize = 51;
static constexpr lv_coord_t kMediaControlSideOffset = 64;
static constexpr lv_coord_t kMediaControlBottomOffset = -5;
static constexpr lv_coord_t kMediaContentYOffset = 5;
#else
static constexpr lv_coord_t kMediaControlButtonSize =
    tile_layout::scale(56);
static constexpr lv_coord_t kMediaControlSideOffset =
    tile_layout::scale(76);
static constexpr lv_coord_t kMediaControlBottomOffset = 0;
static constexpr lv_coord_t kMediaContentYOffset = 0;
#endif

struct MediaEventData {
  String entity_id;
  const char* command = "play_pause";
  lv_obj_t* media_title_label = nullptr;
  lv_obj_t* media_subtitle_label = nullptr;
  bool reset_text_scroll = false;
  lv_obj_t* control_label = nullptr;
};

struct MediaPopupEventData {
  String entity_id;
  String title;
  String icon_name;
  uint32_t bg_color = 0x2A2A2A;
  GridType grid_type = GridType::TAB0;
  uint8_t grid_index = 0;
};

static void free_cover_dsc(MediaCoverRef* ref) {
  if (!ref) return;
  if (ref->dsc) {
    // Also drop the cache entry keyed by the descriptor address. Otherwise,
    // a later descriptor at the same malloc address can show the old image.
    lv_image_cache_drop(ref->dsc);
    if (ref->dsc->data) {
      free(const_cast<uint8_t*>(ref->dsc->data));
    }
    free(ref->dsc);
    ref->dsc = nullptr;
  }
  if (ref->popup_dsc) {
    lv_image_cache_drop(ref->popup_dsc);
    if (ref->popup_dsc->data) {
      free(const_cast<uint8_t*>(ref->popup_dsc->data));
    }
    free(ref->popup_dsc);
    ref->popup_dsc = nullptr;
  }
  ref->source_url = "";
  ref->url_hash = 0;
  ref->embedded_url_hash = 0;
  ref->requested_url_hash = 0;
  ref->failed_url_hash = 0;
  ref->failed_at_ms = 0;
}

static void cover_ref_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  MediaCoverRef* ref = static_cast<MediaCoverRef*>(lv_event_get_user_data(e));
  // Clear every widget-array entry referring to this object before freeing
  // it. Otherwise, a cover retry can read freed memory, as seen when deleting
  // a media tile through Web Admin in v0.4.16.
  tile_renderer_forget_media_widgets(ref);
  free_cover_dsc(ref);
  delete ref;
}

void enable_event_bubble(lv_obj_t* obj) {
  if (!obj) return;
  lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static void reset_media_label_scroll(lv_obj_t* label) {
  if (!label || lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) return;
  const char* current = lv_label_get_text(label);
  if (!current || !current[0]) return;
  if (lv_label_get_long_mode(label) != LV_LABEL_LONG_SCROLL) return;
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
}

static void media_command_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  MediaEventData* data = static_cast<MediaEventData*>(lv_event_get_user_data(e));
  if (!data || !data->entity_id.length()) return;
  if (data->reset_text_scroll) {
    reset_media_label_scroll(data->media_title_label);
    reset_media_label_scroll(data->media_subtitle_label);
  }
  const char* command = data->command ? data->command : "play_pause";
  if (strcmp(command, "play_pause") == 0 && data->control_label) {
    const char* current = lv_label_get_text(data->control_label);
    const String pause_icon = getMdiChar("pause");
    const String next_icon = getMdiChar(
        current && pause_icon.length() && strcmp(current, pause_icon.c_str()) == 0
            ? "play"
            : "pause");
    lv_label_set_text(data->control_label, next_icon.c_str());
  }
  mqttPublishMediaCommand(data->entity_id.c_str(), command);
}

static void media_event_data_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  MediaEventData* data = static_cast<MediaEventData*>(lv_event_get_user_data(e));
  delete data;
}

static String media_label_text(lv_obj_t* label) {
  if (!label || lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) return "";
  const char* text = lv_label_get_text(label);
  return text ? String(text) : String();
}

static bool media_widget_is_playing(const MediaTileWidgets& widgets) {
  if (!widgets.play_pause_label) return false;
  const char* current = lv_label_get_text(widgets.play_pause_label);
  String pause_icon = getMdiChar("pause");
  return current && pause_icon.length() && strcmp(current, pause_icon.c_str()) == 0;
}

static void show_media_popup_event_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
  MediaPopupEventData* data = static_cast<MediaPopupEventData*>(lv_event_get_user_data(e));
  if (!data || !data->entity_id.length()) return;

  MediaTileWidgets* target = tile_renderer_get_media_widgets(data->grid_type);
  if (!target || data->grid_index >= TILES_PER_GRID) return;
  MediaTileWidgets& widgets = target[data->grid_index];

  MediaPopupInit init;
  init.entity_id = data->entity_id;
  init.title = data->title;
  init.icon_name = data->icon_name;
  init.icon_char = media_label_text(widgets.icon_label);
  init.bg_color = data->bg_color;
  init.media_title = media_label_text(widgets.media_title_label);
  init.media_subtitle = media_label_text(widgets.media_subtitle_label);
  init.is_playing = media_widget_is_playing(widgets);
  init.has_media_position = widgets.has_media_position;
  init.media_position = widgets.media_position;
  init.media_duration = widgets.media_duration;
  init.media_position_received_ms = widgets.media_position_received_ms;
  init.has_volume = widgets.has_media_volume;
  init.volume_level = widgets.media_volume_level;
  init.is_muted = widgets.media_is_muted;
  const bool cover_visible = widgets.cover_ref &&
                             widgets.cover_ref->dsc &&
                             widgets.cover_clip &&
                             !lv_obj_has_flag(widgets.cover_clip, LV_OBJ_FLAG_HIDDEN);
  if (cover_visible) {
    init.cover_dsc = widgets.cover_ref->popup_dsc
                         ? widgets.cover_ref->popup_dsc
                         : widgets.cover_ref->dsc;
    init.cover_hash = widgets.cover_ref->url_hash;
  }
  finish_press_before_popup(e);
  show_media_popup(init);
}

static void media_popup_event_data_delete_cb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  MediaPopupEventData* data = static_cast<MediaPopupEventData*>(lv_event_get_user_data(e));
  delete data;
}

static const lv_anim_t* media_text_scroll_anim() {
  static lv_anim_t anim;
  static bool initialized = false;
  if (!initialized) {
    lv_anim_init(&anim);
    lv_anim_set_delay(&anim, 2000);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&anim, 2000);
    lv_anim_set_reverse_delay(&anim, 2000);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    initialized = true;
  }
  return &anim;
}

static void apply_media_text_scroll_style(lv_obj_t* label) {
  if (!label) return;
  lv_obj_set_style_anim(label, media_text_scroll_anim(), LV_PART_MAIN);
  lv_obj_set_style_anim_duration(label, lv_anim_speed_clamped(18, 300, 10000), LV_PART_MAIN);
}

static lv_obj_t* create_media_control_button(lv_obj_t* parent,
                                             const String& entity_id,
                                             const char* command,
                                             const char* icon_name,
                                             lv_coord_t x_ofs,
                                             uint32_t tile_bg_color,
                                             bool primary = false,
                                             lv_obj_t* media_title_label = nullptr,
                                             lv_obj_t* media_subtitle_label = nullptr,
                                             bool reset_text_scroll = false) {
  if (!parent || !entity_id.length() || !command || !icon_name) return nullptr;

  lv_obj_t* btn = lv_button_create(parent);
  if (!btn) return nullptr;
  lv_obj_set_size(btn, kMediaControlButtonSize, kMediaControlButtonSize);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x_ofs, kMediaControlBottomOffset);
  lv_obj_set_style_bg_color(btn, primary ? lv_color_white() : lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, primary ? lv_color_hex(0xD8D8D8) : lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_IGNORE_LAYOUT);
  disable_pressed_button_animation(btn);

  lv_obj_t* label = lv_label_create(btn);
  if (label) {
    set_label_style(label, primary ? lv_color_hex(tile_bg_color) : lv_color_white(), FONT_MDI_ICONS);
    String icon_char = getMdiChar(icon_name);
    lv_label_set_text(label, icon_char.length() ? icon_char.c_str() : "");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, primary ? 0 : 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
  }

  MediaEventData* data = new MediaEventData{entity_id,
                                             command,
                                             media_title_label,
                                             media_subtitle_label,
                                             reset_text_scroll,
                                             label};
  lv_obj_add_event_cb(btn, media_command_event_cb, LV_EVENT_SHORT_CLICKED, data);
  lv_obj_add_event_cb(btn, media_event_data_delete_cb, LV_EVENT_DELETE, data);
  return label;
}

static String media_friendly_name_from_entity(const String& entity_id) {
  int dot = entity_id.indexOf('.');
  String name = dot >= 0 ? entity_id.substring(dot + 1) : entity_id;
  name.replace('_', ' ');
  name.trim();
  bool capitalize_next = true;
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name.charAt(i);
    if (capitalize_next && c >= 'a' && c <= 'z') {
      name.setCharAt(i, static_cast<char>(c - 32));
    }
    capitalize_next = (c == ' ');
  }
  return name;
}

}  // namespace

lv_obj_t* render_media_tile(lv_obj_t* parent,
                            int col,
                            int row,
                            const Tile& tile,
                            uint8_t index,
                            GridType grid_type) {
  if (!parent) {
    Serial.println("[TileRenderer] ERROR: parent NULL for media tile");
    return nullptr;
  }

  lv_obj_t* card = lv_button_create(parent);
  if (!card) return nullptr;

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

  MediaCoverRef* cover_ref = new MediaCoverRef();
  lv_obj_add_event_cb(card, cover_ref_delete_cb, LV_EVENT_DELETE, cover_ref);

  lv_obj_t* cover_clip = nullptr;
  lv_obj_t* cover_img = nullptr;
  // The final content geometry below determines the artwork size before the
  // first state payload is decoded, including empty and cached tiles.
  const lv_coord_t cover_size = 1;
  cover_clip = lv_obj_create(card);
  if (cover_clip) {
    lv_obj_set_size(cover_clip, cover_size, cover_size);
    lv_obj_align(cover_clip,
                 LV_ALIGN_TOP_LEFT,
                 tile_layout::scale(-2),
                 tile_layout::scale(
                     (tile.span_w > 1 || tile.span_h > 1) ? 58 : 42) +
                     kMediaContentYOffset);
    lv_obj_set_style_bg_opa(cover_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cover_clip, 0, 0);
    lv_obj_set_style_shadow_width(cover_clip, 0, 0);
    lv_obj_set_style_pad_all(cover_clip, 0, 0);
    lv_obj_set_style_radius(cover_clip, tile_layout::scale(12), 0);
    lv_obj_set_style_clip_corner(cover_clip, true, 0);
    lv_obj_remove_flag(cover_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cover_clip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cover_clip, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(cover_clip, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(cover_clip, LV_OBJ_FLAG_HIDDEN);

    cover_img = lv_img_create(cover_clip);
  }

  if (cover_img) {
    lv_obj_set_size(cover_img, cover_size, cover_size);
    lv_obj_align(cover_img, LV_ALIGN_CENTER, 0, 0);
    lv_image_set_inner_align(cover_img, LV_IMAGE_ALIGN_COVER);
    lv_obj_set_style_opa(cover_img, LV_OPA_COVER, 0);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cover_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(cover_img, LV_OBJ_FLAG_IGNORE_LAYOUT);
  }

  lv_obj_t* previous_label = nullptr;
  lv_obj_t* play_pause_label = nullptr;
  lv_obj_t* next_label = nullptr;

  String icon_name = normalizeMdiIconName(tile.icon_name);
  if (!icon_name.length() && tile.sensor_entity.length()) {
    icon_name = normalizeMdiIconName(haBridgeConfig.findEntityIcon(tile.sensor_entity));
  }
  if (!icon_name.length()) icon_name = "television";

  lv_obj_t* icon_label = lv_label_create(card);
  if (icon_label) {
    set_label_style(icon_label, lv_color_white(), FONT_MDI_ICONS);
    String icon_char = icon_name.length() ? getMdiChar(icon_name) : "";
    if (icon_char.length()) {
      lv_label_set_text(icon_label, icon_char.c_str());
      lv_obj_clear_flag(icon_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(icon_label, "");
      lv_obj_add_flag(icon_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(icon_label, LV_ALIGN_TOP_LEFT,
                 tile_layout::scale_480(-8),
                 tile_layout::scale_480(-8));
    enable_event_bubble(icon_label);
  }

  String title_text = tile.title;
  title_text.trim();
  if (!title_text.length() && tile.sensor_entity.length()) {
    title_text = haBridgeConfig.findSensorName(tile.sensor_entity);
  }
  if (!title_text.length() && tile.sensor_entity.length()) {
    title_text = media_friendly_name_from_entity(tile.sensor_entity);
  }
  if (!title_text.length()) title_text = "Media";

  lv_obj_t* title_label = lv_label_create(card);
  if (title_label) {
    set_label_style(title_label, lv_color_white(),
                    tile_layout::header_title_font());
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label, LV_PCT(70));
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_RIGHT, 0);
    hometiles_title::tile(title_label, title_text.c_str(), true);
    lv_obj_align(title_label, LV_ALIGN_TOP_RIGHT,
                 tile_layout::scale_480(4),
                 tile_layout::scale_480(4));
    enable_event_bubble(title_label);
  }

#if defined(DEVICE_WAVESHARE_4B)
  const lv_font_t* media_font = (tile.span_w > 1 || tile.span_h > 1) ? &ui_font_28 : &ui_font_24;
  const lv_font_t* media_subtitle_font = &ui_font_20;
#elif defined(DEVICE_LAYOUT_480X480)
  const lv_font_t* media_font =
      (tile.span_w > 1 || tile.span_h > 1) ? &ui_font_20 : &ui_font_16;
  const lv_font_t* media_subtitle_font = &ui_font_14;
#else
  const lv_font_t* media_font =
      (tile.span_w > 1 || tile.span_h > 1)
          ? tile_layout::content_font_24()
          : tile_layout::content_font_20();
  const lv_font_t* media_subtitle_font = FONT_SMALL;
#endif

  lv_obj_t* media_title = lv_label_create(card);
  if (media_title) {
    set_label_style(media_title, lv_color_white(), media_font);
    lv_label_set_long_mode(media_title, LV_LABEL_LONG_SCROLL);
    apply_media_text_scroll_style(media_title);
    lv_obj_set_width(media_title, LV_PCT(82));
    lv_obj_set_style_text_align(media_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(
        media_title,
        i18n::strings(configManager.getConfig().language).media_no_playback);
    lv_obj_align(media_title, LV_ALIGN_TOP_LEFT, tile_layout::scale(20),
                 tile_layout::scale(108) + kMediaContentYOffset);
    enable_event_bubble(media_title);
  }

  lv_obj_t* subtitle = lv_label_create(card);
  if (subtitle) {
    set_label_style(subtitle, lv_color_hex(0xD8DEE9), media_subtitle_font);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_SCROLL);
    apply_media_text_scroll_style(subtitle);
    lv_obj_set_width(subtitle, LV_PCT(82));
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(subtitle, "");
    lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, tile_layout::scale(20),
                 tile_layout::scale(34) + kMediaContentYOffset);
    lv_obj_add_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
    enable_event_bubble(subtitle);
  }

  lv_obj_t* state_label = lv_label_create(card);
  if (state_label) {
    set_label_style(state_label, lv_color_hex(0xB7C1D6), FONT_SMALL);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(state_label, LV_PCT(72));
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(state_label, "");
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_RIGHT,
                 tile_layout::scale_480(4),
                 tile_layout::scale_480(4));
    lv_obj_add_flag(state_label, LV_OBJ_FLAG_HIDDEN);
    enable_event_bubble(state_label);
  }

  if (tile.sensor_entity.length()) {
    previous_label = create_media_control_button(card,
                                                 tile.sensor_entity,
                                                 "previous",
                                                 "skip-previous",
                                                 -kMediaControlSideOffset,
                                                 card_color,
                                                 false,
                                                 media_title,
                                                 subtitle,
                                                 false);
    play_pause_label = create_media_control_button(card, tile.sensor_entity, "play_pause", "play", 0, card_color, true);
    next_label = create_media_control_button(card,
                                             tile.sensor_entity,
                                             "next",
                                             "skip-next",
                                             kMediaControlSideOffset,
                                             card_color,
                                             false,
                                             media_title,
                                             subtitle,
                                             false);
  }

  MediaTileWidgets* target = tile_renderer_get_media_widgets(grid_type);
  if (target && index < TILES_PER_GRID) {
    target[index].cover_clip = cover_clip;
    target[index].cover_image = cover_img;
    target[index].cover_ref = cover_ref;
    target[index].icon_label = icon_label;
    target[index].previous_label = previous_label;
    target[index].play_pause_label = play_pause_label;
    target[index].next_label = next_label;
    target[index].title_label = title_label;
    target[index].media_title_label = media_title;
    target[index].media_subtitle_label = subtitle;
    target[index].state_label = state_label;
    target[index].last_payload_hash = 0;
    target[index].dynamic_icon = false;
    target[index].has_media_volume = false;
    target[index].media_volume_level = 0.0f;
    target[index].media_is_muted = false;
    target[index].has_media_position = false;
    target[index].media_position = 0.0f;
    target[index].media_duration = 0.0f;
    target[index].media_position_received_ms = 0;
    set_media_cover_text_layout(target[index], false);
  }

  if (tile.sensor_entity.length()) {
    if (grid_type != GridType::SCREENSAVER) {
      MediaPopupEventData* popup_data = new MediaPopupEventData{
        tile.sensor_entity,
        title_text,
        icon_name,
        card_color,
        grid_type,
        index
      };
      lv_obj_add_event_cb(card, show_media_popup_event_cb, LV_EVENT_SHORT_CLICKED, popup_data);
      lv_obj_add_event_cb(card, media_popup_event_data_delete_cb, LV_EVENT_DELETE, popup_data);
    }

    String initial = haBridgeConfig.findSensorInitialValue(tile.sensor_entity);
    if (initial.length()) {
      update_media_tile_state(grid_type, index, initial.c_str());
    }

  }

  return card;
}
