#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/media/media_popup.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/ui_surface_style.h"

// lvgl.h no longer exports lv_image_cache_drop() in 9.5; its declaration
// is available only in the instance header.
#include <misc/cache/instance/lv_image_cache.h>

namespace {

constexpr int kCardMargin = popup_layout::kCardMargin;
constexpr int kCardWidth =
    (SCREEN_WIDTH > SCREEN_HEIGHT)
        ? (SCREEN_HEIGHT - (kCardMargin * 2))
        : (SCREEN_WIDTH - (kCardMargin * 2));
constexpr int kCardHeight = SCREEN_HEIGHT - (kCardMargin * 2);
constexpr int kCardPad = popup_layout::kCardPad;
constexpr int kHeaderIconOffsetX = 0;
constexpr int kHeaderIconOffsetY = 0;
constexpr int kCoverSize = popup_layout::scale(240);
constexpr int kControlButtonSize = popup_layout::scale(78);
constexpr int kControlSideOffset = popup_layout::scale(116);
constexpr int kHeightExtra = (kCardHeight > 712) ? (kCardHeight - 712) : 0;
constexpr int kCoverTop = popup_layout::scale(104);
constexpr int kCoverTextGap = popup_layout::scale(20);
constexpr int kTitleTop = kCoverTop + kCoverSize + kCoverTextGap;
constexpr int kSeekTop = popup_layout::scale(470) + (kHeightExtra / 2);
constexpr int kControlsTop = popup_layout::kNavY - kControlButtonSize - popup_layout::scale(36);
constexpr int kSeekWidth = kCardWidth - (kCardPad * 2) - popup_layout::scale(220);
constexpr int kSeekTimeGap = popup_layout::scale(22);
constexpr int kSeekSliderHeight = popup_layout::scale(10);
constexpr int kSeekSliderKnobSize = popup_layout::scale(26);
constexpr int kSeekSliderClickPad = popup_layout::scale(18);
constexpr int kVolumeWidth =
    (kCardWidth >= 760) ? popup_layout::scale(410) : popup_layout::scale(330);
constexpr int kVolumeSliderHeight = popup_layout::scale(16);
constexpr int kVolumeSliderKnobSize = popup_layout::scale(36);
constexpr int kVolumeSliderClickPad = popup_layout::scale(20);
constexpr int kVolumeSideOffset = (kVolumeWidth / 2) + popup_layout::scale(70);

struct MediaPopupContext;

struct MediaCommandData {
  MediaPopupContext* ctx = nullptr;
  const char* command = "play_pause";
};

struct MediaPopupContext {
  String entity_id;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* cover_clip = nullptr;
  lv_obj_t* cover_image = nullptr;
  lv_obj_t* fallback_icon = nullptr;
  lv_obj_t* media_title_label = nullptr;
  lv_obj_t* media_subtitle_label = nullptr;
  lv_obj_t* seek_slider = nullptr;
  lv_obj_t* seek_current_label = nullptr;
  lv_obj_t* seek_duration_label = nullptr;
  lv_obj_t* volume_row = nullptr;
  lv_obj_t* volume_slider = nullptr;
  lv_obj_t* volume_icon_label = nullptr;
  lv_obj_t* volume_label = nullptr;
  lv_obj_t* previous_label = nullptr;
  lv_obj_t* play_pause_label = nullptr;
  lv_obj_t* next_label = nullptr;
  lv_image_dsc_t* cover_dsc = nullptr;
  uint32_t cover_hash = 0;
  uint32_t bg_color = 0x2A2A2A;
  bool has_media_position = false;
  bool is_playing = false;
  bool seek_dragging = false;
  bool updating_seek = false;
  float media_position = 0.0f;
  float media_duration = 0.0f;
  uint32_t media_position_received_ms = 0;
  lv_timer_t* progress_timer = nullptr;
  bool has_volume = false;
  bool is_muted = false;
  int32_t last_volume_pct = 35;
  int32_t shown_volume_pct = -1;
  uint8_t shown_volume_icon = 255;
};

static MediaPopupContext* g_media_popup_ctx = nullptr;

static void* alloc_popup_memory(size_t bytes, bool prefer_psram = false) {
  if (!bytes) return nullptr;
  void* data = nullptr;
  if (prefer_psram) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (!data) {
    data = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  return data;
}

static void free_cover_dsc(lv_image_dsc_t*& dsc) {
  if (!dsc) return;
  // Also discard the cache entry keyed by the descriptor address; otherwise
  // a later descriptor at the same malloc address can display the old image.
  lv_image_cache_drop(dsc);
  if (dsc->data) {
    free(const_cast<uint8_t*>(dsc->data));
  }
  free(dsc);
  dsc = nullptr;
}

static lv_image_dsc_t* clone_cover_dsc(const lv_image_dsc_t* src) {
  if (!src || !src->data || src->data_size == 0) return nullptr;

  uint8_t* data = static_cast<uint8_t*>(alloc_popup_memory(src->data_size, true));
  if (!data) return nullptr;
  memcpy(data, src->data, src->data_size);

  lv_image_dsc_t* clone = static_cast<lv_image_dsc_t*>(alloc_popup_memory(sizeof(lv_image_dsc_t)));
  if (!clone) {
    free(data);
    return nullptr;
  }
  memcpy(clone, src, sizeof(lv_image_dsc_t));
  clone->data = data;
  return clone;
}

static const lv_anim_t* media_popup_text_scroll_anim() {
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

static void apply_popup_scroll_style(lv_obj_t* label) {
  if (!label) return;
  lv_obj_set_style_anim(label, media_popup_text_scroll_anim(), LV_PART_MAIN);
  lv_obj_set_style_anim_duration(label, lv_anim_speed_clamped(18, 300, 12000), LV_PART_MAIN);
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
                 popup_layout::kHeaderIconX + kHeaderIconOffsetX,
                 icon_y + kHeaderIconOffsetY);
  }
  if (title_label) {
    lv_coord_t title_y = header_center_y - (lv_obj_get_height(title_label) / 2);
    if (title_y < 0) title_y = 0;
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT,
                 popup_layout::kHeaderTitleX, title_y);
  }
}

static void update_cover(MediaPopupContext* ctx, const lv_image_dsc_t* cover_dsc, uint32_t cover_hash) {
  if (!ctx || !ctx->cover_image || !ctx->fallback_icon) return;
  if (cover_dsc && ctx->cover_hash == cover_hash && ctx->cover_dsc) {
    lv_obj_clear_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_image_dsc_t* old = ctx->cover_dsc;
  ctx->cover_dsc = nullptr;
  ctx->cover_hash = 0;

  if (cover_dsc) {
    ctx->cover_dsc = clone_cover_dsc(cover_dsc);
    if (ctx->cover_dsc) {
      ctx->cover_hash = cover_hash;
      lv_image_set_src(ctx->cover_image, ctx->cover_dsc);
      lv_obj_clear_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
      free_cover_dsc(old);
      return;
    }
  }

  lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->fallback_icon, LV_OBJ_FLAG_HIDDEN);
  free_cover_dsc(old);
}

static void set_popup_label(lv_obj_t* label, const String& text) {
  if (!label) return;
  lv_label_set_text(label, text.c_str());
}

static bool popup_text_same(String a, String b) {
  a.trim();
  b.trim();
  return a.length() && b.length() && a.equalsIgnoreCase(b);
}

static uint8_t volume_icon_bucket_for_percent(int32_t pct, bool muted) {
  if (muted || pct <= 0) return 0;
  if (pct < 35) return 1;
  if (pct < 70) return 2;
  return 3;
}

static const char* volume_icon_for_bucket(uint8_t bucket) {
  switch (bucket) {
    case 1: return "volume-low";
    case 2: return "volume-medium";
    case 3: return "volume-high";
    default: return "volume-off";
  }
}

static void set_volume_widgets(MediaPopupContext* ctx, int32_t pct, bool muted, bool update_slider = true) {
  if (!ctx) return;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  ctx->is_muted = muted || pct == 0;
  if (pct > 0) ctx->last_volume_pct = pct;
  if (update_slider && ctx->volume_slider) lv_slider_set_value(ctx->volume_slider, pct, LV_ANIM_OFF);
  if (ctx->volume_icon_label) {
    const uint8_t bucket = volume_icon_bucket_for_percent(pct, ctx->is_muted);
    if (ctx->shown_volume_icon != bucket) {
      ctx->shown_volume_icon = bucket;
      lv_label_set_text(ctx->volume_icon_label, getMdiChar(volume_icon_for_bucket(bucket)).c_str());
    }
  }
  if (ctx->volume_label && ctx->shown_volume_pct != pct) {
    ctx->shown_volume_pct = pct;
    char text[8];
    snprintf(text, sizeof(text), "%ld%%", static_cast<long>(pct));
    lv_label_set_text(ctx->volume_label, text);
  }
}

static void update_volume(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx || !ctx->volume_slider) return;
  ctx->has_volume = init.has_volume;

  float volume = init.volume_level;
  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;
  int32_t pct = static_cast<int32_t>(volume * 100.0f + 0.5f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  if (init.has_volume) {
    lv_obj_clear_state(ctx->volume_slider, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(ctx->volume_slider, LV_STATE_DISABLED);
  }

  set_volume_widgets(ctx, pct, init.is_muted);
}

static void format_media_time(float seconds, char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  if (seconds < 0.0f) seconds = 0.0f;
  uint32_t total = static_cast<uint32_t>(seconds + 0.5f);
  const uint32_t hours = total / 3600U;
  const uint32_t minutes = (total % 3600U) / 60U;
  const uint32_t secs = total % 60U;
  if (hours > 0) {
    snprintf(out, out_len, "%lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(secs));
  } else {
    snprintf(out, out_len, "%lu:%02lu",
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(secs));
  }
}

static float current_media_position(const MediaPopupContext* ctx) {
  if (!ctx || !ctx->has_media_position) return 0.0f;
  float position = ctx->media_position;
  if (ctx->is_playing && !ctx->seek_dragging && ctx->media_position_received_ms != 0) {
    position += static_cast<float>(static_cast<uint32_t>(millis() - ctx->media_position_received_ms)) / 1000.0f;
  }
  if (position < 0.0f) position = 0.0f;
  if (position > ctx->media_duration) position = ctx->media_duration;
  return position;
}

static void set_seek_widgets(MediaPopupContext* ctx, float position, bool update_slider) {
  if (!ctx || !ctx->seek_slider) return;
  const bool available = ctx->has_media_position && ctx->media_duration > 0.0f;
  if (!available) {
    lv_obj_add_state(ctx->seek_slider, LV_STATE_DISABLED);
    lv_obj_add_flag(ctx->seek_slider, LV_OBJ_FLAG_HIDDEN);
    if (ctx->seek_current_label) lv_obj_add_flag(ctx->seek_current_label, LV_OBJ_FLAG_HIDDEN);
    if (ctx->seek_duration_label) lv_obj_add_flag(ctx->seek_duration_label, LV_OBJ_FLAG_HIDDEN);
    if (update_slider) lv_slider_set_value(ctx->seek_slider, 0, LV_ANIM_OFF);
    return;
  }

  lv_obj_clear_state(ctx->seek_slider, LV_STATE_DISABLED);
  lv_obj_clear_flag(ctx->seek_slider, LV_OBJ_FLAG_HIDDEN);
  if (ctx->seek_current_label) lv_obj_clear_flag(ctx->seek_current_label, LV_OBJ_FLAG_HIDDEN);
  if (ctx->seek_duration_label) lv_obj_clear_flag(ctx->seek_duration_label, LV_OBJ_FLAG_HIDDEN);
  if (position < 0.0f) position = 0.0f;
  if (position > ctx->media_duration) position = ctx->media_duration;
  if (update_slider) {
    ctx->updating_seek = true;
    const int32_t value = static_cast<int32_t>((position / ctx->media_duration) * 1000.0f + 0.5f);
    lv_slider_set_value(ctx->seek_slider, value, LV_ANIM_OFF);
    ctx->updating_seek = false;
  }

  char current_text[16];
  char duration_text[16];
  format_media_time(position, current_text, sizeof(current_text));
  format_media_time(ctx->media_duration, duration_text, sizeof(duration_text));
  if (ctx->seek_current_label) lv_label_set_text(ctx->seek_current_label, current_text);
  if (ctx->seek_duration_label) lv_label_set_text(ctx->seek_duration_label, duration_text);
}

static void update_seek(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx || !ctx->seek_slider) return;
  ctx->is_playing = init.is_playing;
  ctx->has_media_position = init.has_media_position && init.media_duration > 0.0f;
  ctx->media_duration = ctx->has_media_position ? init.media_duration : 0.0f;
  if (!ctx->seek_dragging) {
    ctx->media_position = ctx->has_media_position ? init.media_position : 0.0f;
    ctx->media_position_received_ms = ctx->has_media_position
                                          ? (init.media_position_received_ms != 0
                                                 ? init.media_position_received_ms
                                                 : millis())
                                          : 0;
    set_seek_widgets(ctx, current_media_position(ctx), true);
  }
}

static void media_progress_timer_cb(lv_timer_t* timer) {
  MediaPopupContext* ctx = timer ? static_cast<MediaPopupContext*>(lv_timer_get_user_data(timer)) : nullptr;
  if (!ctx || !ctx->card || lv_obj_has_flag(ctx->card, LV_OBJ_FLAG_HIDDEN) || ctx->seek_dragging) return;
  set_seek_widgets(ctx, current_media_position(ctx), true);
}

static void apply_init_to_context(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx) return;
  ctx->entity_id = init.entity_id;
  ctx->bg_color = init.bg_color != 0 ? init.bg_color : 0x2A2A2A;

  if (ctx->card) {
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(ctx->bg_color), 0);
  }

  if (ctx->title_label) {
    String title = init.title;
    title.trim();
    if (!title.length()) title = init.entity_id;
    hometiles_title::set(ctx->title_label, title.c_str());
  }

  String icon_char = init.icon_char;
  icon_char.trim();
  if (!icon_char.length()) {
    String icon_name = init.icon_name;
    icon_name.trim();
    if (!icon_name.length()) icon_name = "television";
    icon_char = getMdiChar(icon_name);
    if (!icon_char.length()) icon_char = getMdiChar("television");
  }
  if (ctx->icon_label) lv_label_set_text(ctx->icon_label, icon_char.c_str());
  if (ctx->fallback_icon) lv_label_set_text(ctx->fallback_icon, icon_char.c_str());
  align_header_row(ctx->card, ctx->title_label, ctx->icon_label);

  String media_title = init.media_title;
  media_title.trim();
  if (!media_title.length()) {
    media_title = i18n::strings(configManager.getConfig().language).media_no_playback;
  }
  set_popup_label(ctx->media_title_label, media_title);

  String subtitle = init.media_subtitle;
  subtitle.trim();
  if (popup_text_same(subtitle, media_title)) {
    subtitle = "";
  }
  if (ctx->media_subtitle_label) {
    if (subtitle.length()) {
      lv_label_set_text(ctx->media_subtitle_label, subtitle.c_str());
      lv_obj_clear_flag(ctx->media_subtitle_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(ctx->media_subtitle_label, "");
      lv_obj_add_flag(ctx->media_subtitle_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (ctx->play_pause_label) {
    String play_icon = getMdiChar(init.is_playing ? "pause" : "play");
    lv_label_set_text(ctx->play_pause_label, play_icon.c_str());
  }
  if (ctx->play_pause_label) {
    lv_obj_set_style_text_color(ctx->play_pause_label, lv_color_hex(ctx->bg_color), 0);
  }
  update_seek(ctx, init);
  update_volume(ctx, init);
  update_cover(ctx, init.cover_dsc, init.cover_hash);
}

static void on_close_click(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->overlay || !ctx->card) return;
  ctx->seek_dragging = false;
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

static void on_overlay_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)e;
}

static void on_overlay_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  if (ctx->progress_timer) {
    lv_timer_delete(ctx->progress_timer);
    ctx->progress_timer = nullptr;
  }
  free_cover_dsc(ctx->cover_dsc);
  if (g_media_popup_ctx == ctx) g_media_popup_ctx = nullptr;
  delete ctx;
}

static void on_media_command(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  MediaCommandData* data = static_cast<MediaCommandData*>(lv_event_get_user_data(e));
  if (!data || !data->ctx || !data->ctx->entity_id.length()) return;
  const char* command = data->command ? data->command : "play_pause";

  // Play/pause is safe to reflect immediately. HA remains authoritative and
  // the next state packet corrects the icon if the service call fails, but the
  // button no longer appears frozen while MQTT/HA round-trips.
  if (strcmp(command, "play_pause") == 0) {
    MediaPopupContext* ctx = data->ctx;
    const float position = current_media_position(ctx);
    ctx->is_playing = !ctx->is_playing;
    ctx->media_position = position;
    ctx->media_position_received_ms = millis();
    if (ctx->play_pause_label) {
      const String icon = getMdiChar(ctx->is_playing ? "pause" : "play");
      lv_label_set_text(ctx->play_pause_label, icon.c_str());
    }
  }

  mqttPublishMediaCommand(data->ctx->entity_id.c_str(), command);
}

static void on_media_command_delete(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
  MediaCommandData* data = static_cast<MediaCommandData*>(lv_event_get_user_data(e));
  delete data;
}

static void on_volume_slider_event(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  const bool released = code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
  if (code != LV_EVENT_VALUE_CHANGED && !released) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->volume_slider || !ctx->has_volume) return;

  int32_t raw = lv_slider_get_value(ctx->volume_slider);
  if (raw < 0) raw = 0;
  if (raw > 100) raw = 100;
  set_volume_widgets(ctx, raw, raw == 0, false);
  if (released) {
    mqttPublishMediaVolume(ctx->entity_id.c_str(), static_cast<float>(raw) / 100.0f);
  }
}

static void on_seek_slider_event(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  const bool released = code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
  if (code != LV_EVENT_VALUE_CHANGED && !released) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->seek_slider || ctx->updating_seek) return;
  // End the gesture even if playback became unavailable while dragging.
  ctx->seek_dragging = !released && ctx->has_media_position;
  if (!ctx->has_media_position) return;

  int32_t raw = lv_slider_get_value(ctx->seek_slider);
  if (raw < 0) raw = 0;
  if (raw > 1000) raw = 1000;
  const float position = ctx->media_duration * (static_cast<float>(raw) / 1000.0f);
  set_seek_widgets(ctx, position, false);

  if (released) {
    ctx->media_position = position;
    ctx->media_position_received_ms = millis();
    mqttPublishMediaSeek(ctx->entity_id.c_str(), position);
  }
}

static void on_volume_mute_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->entity_id.length() || !ctx->volume_slider) return;
  int32_t current = lv_slider_get_value(ctx->volume_slider);
  if (current < 0) current = 0;
  if (current > 100) current = 100;
  int32_t next = 0;
  if (current <= 0 || ctx->is_muted) {
    next = ctx->last_volume_pct > 0 ? ctx->last_volume_pct : 35;
  }
  set_volume_widgets(ctx, next, next == 0);
  mqttPublishMediaVolume(ctx->entity_id.c_str(), static_cast<float>(next) / 100.0f);
}
static lv_obj_t* create_control_button(lv_obj_t* parent,
                                       MediaPopupContext* ctx,
                                       const char* command,
                                       const char* icon_name,
                                       lv_coord_t x_ofs,
                                       uint32_t bg_color,
                                       bool primary) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, kControlButtonSize, kControlButtonSize);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, x_ofs, kControlsTop);
  lv_obj_set_style_bg_color(btn, primary ? lv_color_white() : lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, primary ? lv_color_hex(0xD8D8D8) : lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(btn);

  lv_obj_t* label = lv_label_create(btn);
  set_label_style(label, primary ? lv_color_hex(bg_color) : lv_color_white(), FONT_MDI_ICONS);
  lv_label_set_text(label, getMdiChar(icon_name).c_str());
  lv_obj_center(label);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

  MediaCommandData* data = new MediaCommandData{ctx, command};
  lv_obj_add_event_cb(btn, on_media_command, LV_EVENT_CLICKED, data);
  lv_obj_add_event_cb(btn, on_media_command_delete, LV_EVENT_DELETE, data);
  return label;
}

}  // namespace

void show_media_popup(const MediaPopupInit& init) {
  hide_pin_popup();
  hide_camera_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;
  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();

  if (g_media_popup_ctx && g_media_popup_ctx->overlay && g_media_popup_ctx->card) {
    apply_init_to_context(g_media_popup_ctx, init);
    lv_obj_clear_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
    if (g_media_popup_ctx && g_media_popup_ctx->card) viewNavigationPopupShown(g_media_popup_ctx->card, init.entity_id.c_str());
    return;
  }

  MediaPopupContext* ctx = new MediaPopupContext();
  g_media_popup_ctx = ctx;

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
  lv_obj_set_style_bg_color(card, lv_color_hex(init.bg_color != 0 ? init.bg_color : 0x2A2A2A), 0);
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

  ctx->title_label = lv_label_create(card);
  set_label_style(ctx->title_label, lv_color_white(),
                  popup_layout::headerTitleFont());
  lv_obj_set_width(ctx->title_label, LV_PCT(62));
  lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

  ctx->icon_label = lv_label_create(card);
  lv_obj_set_style_text_font(ctx->icon_label, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(ctx->icon_label);
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_white(), 0);

  lv_obj_t* close_btn =
      popup_layout::createCloseButton(card, on_close_click, ctx);

  ctx->cover_clip = lv_obj_create(card);
  lv_obj_set_size(ctx->cover_clip, kCoverSize, kCoverSize);
  lv_obj_align(ctx->cover_clip, LV_ALIGN_TOP_MID, 0, kCoverTop);
  lv_obj_set_style_bg_color(ctx->cover_clip, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(ctx->cover_clip, LV_OPA_40, 0);
  lv_obj_set_style_border_width(ctx->cover_clip, 0, 0);
  lv_obj_set_style_shadow_width(ctx->cover_clip, 0, 0);
  lv_obj_set_style_pad_all(ctx->cover_clip, 0, 0);
  lv_obj_set_style_radius(ctx->cover_clip, popup_layout::scale480(18), 0);
  lv_obj_set_style_clip_corner(ctx->cover_clip, true, 0);
  lv_obj_remove_flag(ctx->cover_clip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ctx->cover_clip, LV_OBJ_FLAG_CLICKABLE);

  ctx->cover_image = lv_img_create(ctx->cover_clip);
  lv_obj_set_size(ctx->cover_image, kCoverSize, kCoverSize);
  lv_obj_align(ctx->cover_image, LV_ALIGN_CENTER, 0, 0);
  lv_image_set_inner_align(ctx->cover_image, LV_IMAGE_ALIGN_COVER);
  lv_obj_add_flag(ctx->cover_image, LV_OBJ_FLAG_HIDDEN);

  ctx->fallback_icon = lv_label_create(ctx->cover_clip);
  set_label_style(ctx->fallback_icon, lv_color_white(), FONT_MDI_ICONS);
  lv_obj_set_style_text_font(ctx->fallback_icon, FONT_MDI_ICONS, 0);
  lv_obj_center(ctx->fallback_icon);

  const lv_coord_t text_width = kCardWidth - (kCardPad * 2) - 72;
  ctx->media_title_label = lv_label_create(card);
  set_label_style(ctx->media_title_label, lv_color_white(), popup_layout::font32());
  lv_obj_set_width(ctx->media_title_label, text_width);
  lv_obj_set_style_text_align(ctx->media_title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(ctx->media_title_label, LV_LABEL_LONG_SCROLL);
  apply_popup_scroll_style(ctx->media_title_label);
  lv_obj_align(ctx->media_title_label, LV_ALIGN_TOP_MID, 0, kTitleTop);

  ctx->media_subtitle_label = lv_label_create(card);
  set_label_style(ctx->media_subtitle_label, lv_color_hex(0xD8DEE9), popup_layout::font24());
  lv_obj_set_width(ctx->media_subtitle_label, text_width);
  lv_obj_set_style_text_align(ctx->media_subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(ctx->media_subtitle_label, LV_LABEL_LONG_SCROLL);
  apply_popup_scroll_style(ctx->media_subtitle_label);
  lv_obj_align_to(ctx->media_subtitle_label, ctx->media_title_label,
                  LV_ALIGN_OUT_BOTTOM_MID, 0, popup_layout::scale480(8));

  ctx->seek_slider = lv_slider_create(card);
  lv_obj_set_size(ctx->seek_slider, kSeekWidth, kSeekSliderHeight);
  lv_obj_align(ctx->seek_slider, LV_ALIGN_TOP_MID, 0, kSeekTop);
  lv_slider_set_range(ctx->seek_slider, 0, 1000);
  lv_slider_set_value(ctx->seek_slider, 0, LV_ANIM_OFF);
  lv_obj_set_style_width(ctx->seek_slider, kSeekSliderKnobSize, LV_PART_KNOB);
  lv_obj_set_style_height(ctx->seek_slider, kSeekSliderKnobSize, LV_PART_KNOB);
  lv_obj_set_ext_click_area(ctx->seek_slider, kSeekSliderClickPad);
  lv_obj_set_style_bg_color(ctx->seek_slider, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ctx->seek_slider, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_style_radius(ctx->seek_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ctx->seek_slider, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(ctx->seek_slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(ctx->seek_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ctx->seek_slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(ctx->seek_slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(ctx->seek_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_clear_flag(ctx->seek_slider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ctx->seek_slider, on_seek_slider_event, LV_EVENT_VALUE_CHANGED, ctx);
  lv_obj_add_event_cb(ctx->seek_slider, on_seek_slider_event, LV_EVENT_RELEASED, ctx);
  lv_obj_add_event_cb(ctx->seek_slider, on_seek_slider_event, LV_EVENT_PRESS_LOST, ctx);

  ctx->seek_current_label = lv_label_create(card);
  set_label_style(ctx->seek_current_label, lv_color_hex(0xD8DEE9), popup_layout::font20());
  lv_obj_set_width(ctx->seek_current_label, 80);
  lv_obj_set_style_text_align(ctx->seek_current_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(ctx->seek_current_label, "--:--");
  lv_obj_align_to(ctx->seek_current_label,
                  ctx->seek_slider,
                  LV_ALIGN_OUT_LEFT_MID,
                  -kSeekTimeGap,
                  0);

  ctx->seek_duration_label = lv_label_create(card);
  set_label_style(ctx->seek_duration_label, lv_color_hex(0xD8DEE9), popup_layout::font20());
  lv_obj_set_width(ctx->seek_duration_label, 80);
  lv_obj_set_style_text_align(ctx->seek_duration_label, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(ctx->seek_duration_label, "--:--");
  lv_obj_align_to(ctx->seek_duration_label,
                  ctx->seek_slider,
                  LV_ALIGN_OUT_RIGHT_MID,
                  kSeekTimeGap,
                  0);

  ctx->volume_row = lv_obj_create(card);
  lv_obj_remove_style_all(ctx->volume_row);
  lv_obj_set_size(ctx->volume_row, popup_layout::kContentWidth, popup_layout::kNavHeight);
  lv_obj_align(ctx->volume_row, LV_ALIGN_BOTTOM_MID, 0, -popup_layout::kNavBottomInset);
  lv_obj_set_style_bg_opa(ctx->volume_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->volume_row, 0, 0);
  lv_obj_set_style_pad_all(ctx->volume_row, 0, 0);
  lv_obj_clear_flag(ctx->volume_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* volume_btn = lv_button_create(ctx->volume_row);
  lv_obj_set_size(volume_btn, kControlButtonSize, kControlButtonSize);
  lv_obj_align(volume_btn, LV_ALIGN_CENTER, -kVolumeSideOffset, 0);
  lv_obj_set_style_bg_color(volume_btn, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(volume_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(volume_btn, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(volume_btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(volume_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(volume_btn, 0, 0);
  lv_obj_set_style_shadow_width(volume_btn, 0, 0);
  lv_obj_set_style_pad_all(volume_btn, 0, 0);
  lv_obj_remove_flag(volume_btn, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(volume_btn);
  lv_obj_add_event_cb(volume_btn, on_volume_mute_click, LV_EVENT_CLICKED, ctx);

  ctx->volume_icon_label = lv_label_create(volume_btn);
  set_label_style(ctx->volume_icon_label, lv_color_white(), FONT_MDI_ICONS);
  lv_label_set_text(ctx->volume_icon_label, getMdiChar("volume-high").c_str());
  lv_obj_center(ctx->volume_icon_label);
  lv_obj_clear_flag(ctx->volume_icon_label, LV_OBJ_FLAG_CLICKABLE);

  ctx->volume_slider = lv_slider_create(ctx->volume_row);
  lv_obj_set_size(ctx->volume_slider, kVolumeWidth, kVolumeSliderHeight);
  lv_obj_align(ctx->volume_slider, LV_ALIGN_CENTER, 0, 0);
  lv_slider_set_range(ctx->volume_slider, 0, 100);
  lv_slider_set_value(ctx->volume_slider, 0, LV_ANIM_OFF);
  lv_obj_set_style_width(ctx->volume_slider, kVolumeSliderKnobSize, LV_PART_KNOB);
  lv_obj_set_style_height(ctx->volume_slider, kVolumeSliderKnobSize, LV_PART_KNOB);
  lv_obj_set_ext_click_area(ctx->volume_slider, kVolumeSliderClickPad);
  lv_obj_set_style_bg_color(ctx->volume_slider, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_radius(ctx->volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ctx->volume_slider, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(ctx->volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ctx->volume_slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(ctx->volume_slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(ctx->volume_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_clear_flag(ctx->volume_slider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ctx->volume_slider, on_volume_slider_event, LV_EVENT_VALUE_CHANGED, ctx);
  lv_obj_add_event_cb(ctx->volume_slider, on_volume_slider_event, LV_EVENT_RELEASED, ctx);
  lv_obj_add_event_cb(ctx->volume_slider, on_volume_slider_event, LV_EVENT_PRESS_LOST, ctx);

  ctx->volume_label = lv_label_create(ctx->volume_row);
  set_label_style(ctx->volume_label, lv_color_hex(0xD8DEE9), popup_layout::font20());
  lv_label_set_text(ctx->volume_label, "0%");
  lv_obj_set_width(ctx->volume_label, kControlButtonSize);
  lv_obj_set_style_text_align(ctx->volume_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(ctx->volume_label, LV_ALIGN_CENTER, kVolumeSideOffset, 0);

  ctx->previous_label = create_control_button(card,
                                              ctx,
                                              "previous",
                                              "skip-previous",
                                              -kControlSideOffset,
                                              init.bg_color != 0 ? init.bg_color : 0x2A2A2A,
                                              false);
  ctx->play_pause_label = create_control_button(card,
                                                ctx,
                                                "play_pause",
                                                "play",
                                                0,
                                                init.bg_color != 0 ? init.bg_color : 0x2A2A2A,
                                                true);
  ctx->next_label = create_control_button(card,
                                          ctx,
                                          "next",
                                          "skip-next",
                                          kControlSideOffset,
                                          init.bg_color != 0 ? init.bg_color : 0x2A2A2A,
                                          false);

  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(close_btn);

  apply_init_to_context(ctx, init);
  ctx->progress_timer = lv_timer_create(media_progress_timer_cb, 1000, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
  if (g_media_popup_ctx && g_media_popup_ctx->card) viewNavigationPopupShown(g_media_popup_ctx->card, init.entity_id.c_str());
}

void preload_media_popup() {
  if (g_media_popup_ctx && g_media_popup_ctx->overlay && g_media_popup_ctx->card) return;
  MediaPopupInit init;
  init.entity_id = "__preload__";
  init.title = "";
  init.icon_name = "television";
  init.media_title = "";
  init.media_subtitle = "";
  init.bg_color = 0x2A2A2A;
  show_media_popup(init);
  if (g_media_popup_ctx && g_media_popup_ctx->card && g_media_popup_ctx->overlay) {
    lv_obj_add_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  }
}

void update_media_popup(const MediaPopupInit& init) {
  if (!g_media_popup_ctx || !g_media_popup_ctx->overlay || !g_media_popup_ctx->card) return;
  if (!g_media_popup_ctx->entity_id.length()) return;
  if (!g_media_popup_ctx->entity_id.equalsIgnoreCase(init.entity_id)) return;
  if (lv_obj_has_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN)) return;
  apply_init_to_context(g_media_popup_ctx, init);
}

void update_media_popup_cover(const char* entity_id, const lv_image_dsc_t* cover_dsc, uint32_t cover_hash) {
  if (!entity_id || !g_media_popup_ctx || !g_media_popup_ctx->card) return;
  if (!g_media_popup_ctx->entity_id.equalsIgnoreCase(entity_id)) return;
  if (lv_obj_has_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN)) return;
  update_cover(g_media_popup_ctx, cover_dsc, cover_hash);
}

void hide_media_popup() {
  if (!g_media_popup_ctx || !g_media_popup_ctx->card || !g_media_popup_ctx->overlay) return;
  g_media_popup_ctx->seek_dragging = false;
  lv_obj_add_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}
