#include "src/ui/shared/ui_surface_style.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/media/media_browse.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

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
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"

// lvgl.h no longer exports lv_image_cache_drop() in 9.5; its declaration
// is available only in the instance header.
#include <misc/cache/instance/lv_image_cache.h>

namespace {

constexpr int kCardWidth = popup_layout::kCardWidth;
constexpr int kCardHeight = popup_layout::kCardHeight;
constexpr int kCardPad = popup_layout::kCardPad;
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
// Media browse: the toggle sits right of "next" in the controls row; the
// browse panel covers the popup body below the header.
constexpr int kContentWidth = popup_layout::kContentWidth;
constexpr int kBrowseToggleOffset = kControlSideOffset * 2;
constexpr int kBrowseTop = kCoverTop - popup_layout::scale(8);
constexpr int kBrowsePanelHeight =
    kCardHeight - (kCardPad * 2) - kBrowseTop;
constexpr int kBrowseBarHeight = popup_layout::scale(72);
constexpr int kBrowseBarButtonSize = popup_layout::scale(64);
constexpr int kBrowseRowHeight = popup_layout::scale(76);
constexpr int kBrowseRowIconWidth = popup_layout::scale(64);
constexpr int kBrowseRowPad = popup_layout::scale(8);
// Thumbnails: square JPEGs from the Bridge, side a multiple of 16 for the
// P4 hardware decoder. One request in flight; decoded images are cached.
constexpr int kBrowseThumbSize =
    (kBrowseRowIconWidth / 16) * 16 < 32 ? 32 : (kBrowseRowIconWidth / 16) * 16;
constexpr uint32_t kBrowseThumbTimeoutMs = 6000;
constexpr uint32_t kBrowseThumbPollMs = 120;
constexpr size_t kBrowseThumbCacheMax = 48;  // 64x64 RGB565 = 8 KB each, PSRAM.

struct BrowseRowThumb {
  String url;
  lv_obj_t* image = nullptr;
  lv_obj_t* icon = nullptr;
};

struct MediaPopupContext;

struct MediaCommandData {
  MediaPopupContext* ctx = nullptr;
  const char* command = "play_pause";
};

struct MediaPopupContext {
  String entity_id;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
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
  bool available = true;
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
  // Media browse view (catalog navigation, state in media_browse.cpp).
  lv_obj_t* browse_toggle_label = nullptr;
  lv_obj_t* browse_panel = nullptr;
  lv_obj_t* browse_back_button = nullptr;
  lv_obj_t* browse_title_label = nullptr;
  lv_obj_t* browse_status_label = nullptr;
  lv_obj_t* browse_list = nullptr;
  lv_obj_t* browse_message_label = nullptr;
  uint32_t browse_items_version = 0xFFFFFFFFu;
  // Browse thumbnails (folder rows only).
  std::vector<BrowseRowThumb> browse_rows;
  std::vector<String> browse_thumb_queue;
  lv_timer_t* browse_thumb_timer = nullptr;
  uint32_t browse_thumb_inflight_id = 0;
  String browse_thumb_inflight_url;
  uint32_t browse_thumb_inflight_ms = 0;
};

static MediaPopupContext* g_media_popup_ctx = nullptr;

// Decoded browse thumbnails, oldest first. Global so they survive popup
// reuse. Keyed by URL only: the card color can follow the cover and change
// with every track, re-fetching for each color would multiply the traffic.
struct BrowseThumbCacheEntry {
  String url;
  lv_image_dsc_t* dsc = nullptr;
};
static std::vector<BrowseThumbCacheEntry> g_browse_thumb_cache;
// URLs the Bridge could not deliver; not requested again.
static std::vector<String> g_browse_thumb_failed;
static uint32_t g_browse_thumb_seq = 0;

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

// Previous, next and the volume button (pressed) and both sliders follow the
// popup control rule (popup_nav_style.h), with the card and header icon
// color; play stays white.
static void apply_control_colors(MediaPopupContext* ctx) {
  if (!ctx || !ctx->card) return;
  const lv_color_t popup = lv_obj_get_style_bg_color(ctx->card, LV_PART_MAIN);
  const lv_color_t icon =
      ctx->icon_label ? lv_obj_get_style_text_color(ctx->icon_label, LV_PART_MAIN) : lv_color_white();
  lv_obj_t* const labels[] = {ctx->previous_label, ctx->next_label, ctx->volume_icon_label,
                              ctx->browse_toggle_label};
  for (lv_obj_t* label : labels) {
    if (label) popup_nav_style::style_press(lv_obj_get_parent(label), popup, icon);
  }
  popup_nav_style::style_slider(ctx->seek_slider, popup, icon);
  popup_nav_style::style_slider(ctx->volume_slider, popup, icon);
}

// Home Assistant disables the controls of an unavailable player; the buttons
// dim like the Cover popup's disabled sliders (LV_OPA_30, set at creation).
// The sliders already follow the reported volume and position (an
// unavailable player reports neither), and their handlers check `available`.
static void set_control_enabled(lv_obj_t* obj, bool enabled) {
  if (!obj || lv_obj_has_state(obj, LV_STATE_DISABLED) == !enabled) return;
  if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static void apply_availability(MediaPopupContext* ctx) {
  for (lv_obj_t* label : {ctx->previous_label, ctx->play_pause_label, ctx->next_label, ctx->volume_icon_label}) {
    set_control_enabled(label ? lv_obj_get_parent(label) : nullptr, ctx->available);
  }
}

// Media browse view, defined further below.
static void browse_hide(MediaPopupContext* ctx);
static bool browse_supported(const MediaPopupContext* ctx);

static void apply_init_to_context(MediaPopupContext* ctx, const MediaPopupInit& init) {
  if (!ctx) return;
  // The popup was reused for another player: close its browse view so it
  // never shows (or controls) the catalog of the previous player.
  if (ctx->entity_id.length() && !ctx->entity_id.equalsIgnoreCase(init.entity_id)) {
    browse_hide(ctx);
  }
  ctx->entity_id = init.entity_id;
  ctx->available = init.available;
  if (!ctx->available) ctx->seek_dragging = false;
  ctx->bg_color = init.bg_color != 0 ? init.bg_color : 0x2A2A2A;

  if (ctx->card) {
    lv_obj_set_style_bg_color(ctx->card, lv_color_hex(ctx->bg_color), 0);
  }
  if (ctx->browse_panel) {
    lv_obj_set_style_bg_color(ctx->browse_panel, lv_color_hex(ctx->bg_color), 0);
  }
  if (ctx->browse_toggle_label) {
    // Browsing needs a real media_player entity (not the preload placeholder).
    set_control_enabled(lv_obj_get_parent(ctx->browse_toggle_label),
                        browse_supported(ctx));
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
  if (ctx->icon_label) {
    lv_label_set_text(ctx->icon_label, icon_char.c_str());
    // The header icon takes the tile icon's color; the shell tints its disc.
    lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(init.icon_color), 0);
  }
  if (ctx->fallback_icon) lv_label_set_text(ctx->fallback_icon, icon_char.c_str());
  popup_layout::alignHeader(ctx->card, ctx->title_label, ctx->icon_label);

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
  apply_control_colors(ctx);
  apply_availability(ctx);
}

static void on_close_click(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_CLICKED && code != LV_EVENT_RELEASED) return;
  MediaPopupContext* ctx = static_cast<MediaPopupContext*>(lv_event_get_user_data(e));
  if (!ctx || !ctx->overlay || !ctx->card) return;
  ctx->seek_dragging = false;
  browse_hide(ctx);
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
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
  if (ctx->browse_thumb_timer) {
    lv_timer_delete(ctx->browse_thumb_timer);
    ctx->browse_thumb_timer = nullptr;
  }
  free_cover_dsc(ctx->cover_dsc);
  if (g_media_popup_ctx == ctx) {
    media_browse_set_listener(nullptr);
    g_media_popup_ctx = nullptr;
  }
  delete ctx;
}

static void on_media_command(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  MediaCommandData* data = static_cast<MediaCommandData*>(lv_event_get_user_data(e));
  if (!data || !data->ctx || !data->ctx->entity_id.length() || !data->ctx->available) return;
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
  if (!ctx || !ctx->volume_slider || !ctx->has_volume || !ctx->available) return;

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
  ctx->seek_dragging = !released && ctx->has_media_position && ctx->available;
  if (!ctx->has_media_position || !ctx->available) return;

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
  if (!ctx || !ctx->entity_id.length() || !ctx->volume_slider || !ctx->available) return;
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
  lv_obj_set_style_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
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

// ---------------------------------------------------------------------------
// Media browse view
//
// A panel over the popup body (below the header) with a bar (back, title,
// status, close) and a scrollable list of catalog entries. The state lives in
// media_browse.cpp; this view only renders it and forwards taps. The browse
// session is bound to the popup's entity, so every media tile browses its own
// player.
// ---------------------------------------------------------------------------

enum class BrowseText : uint8_t { Root, Loading, Empty, ErrorPrefix, Truncated };

static const char* browse_text(BrowseText id) {
  const char* lang =
      i18n::normalize_language_code(configManager.getConfig().language);
  const bool de = lang && strcmp(lang, "de") == 0;
  const bool fr = lang && strcmp(lang, "fr") == 0;
  switch (id) {
    case BrowseText::Root:
      return de ? "Mediathek" : (fr ? "Médiathèque" : "Media library");
    case BrowseText::Loading:
      return de ? "Lädt..." : (fr ? "Chargement..." : "Loading...");
    case BrowseText::Empty:
      return de ? "Keine Einträge" : (fr ? "Aucun élément" : "No items");
    case BrowseText::ErrorPrefix:
      return de ? "Fehler: " : (fr ? "Erreur : " : "Error: ");
    case BrowseText::Truncated:
      return de ? "Nur die ersten Einträge werden angezeigt"
                : (fr ? "Seuls les premiers éléments sont affichés"
                      : "Only the first entries are shown");
  }
  return "";
}

static const char* browse_icon_name(const MediaBrowseItem& item) {
  const String& c = item.media_class;
  if (c == "album") return "album";
  if (c == "artist") return "account-music";
  if (c == "playlist") return "playlist-music";
  if (c == "podcast" || c == "episode") return "podcast";
  if (c == "track" || c == "music") return "music-note";
  // Sonos favorites: the "Radio" folder and its stations are both "genre".
  if (c == "channel" || c == "genre") return item.can_expand ? "folder-music" : "radio";
  if (c == "movie") return "movie";
  if (c == "tv_show" || c == "season") return "television";
  if (c == "video") return "video";
  if (c == "image") return "image";
  if (c == "app") return "apps";
  return item.can_expand ? "folder" : "music-note";
}

static bool browse_visible(const MediaPopupContext* ctx) {
  return ctx && ctx->browse_panel &&
         !lv_obj_has_flag(ctx->browse_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_browse_row_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const size_t index =
      static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  const auto& items = media_browse_items();
  if (index >= items.size()) return;
  // Row tap: folders open, pure leaves (tracks, stations) play.
  if (items[index].can_expand) {
    media_browse_open(index);
  } else if (items[index].can_play) {
    media_browse_play(index);
  }
}

// Separate play button for entries that can both open and play (albums,
// playlists, artists): the row opens them, this button plays them.
static void on_browse_play_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_event_stop_bubbling(e);
  const size_t index =
      static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  media_browse_play(index);
}

// --- Browse thumbnails ----------------------------------------------------

static lv_image_dsc_t* browse_thumb_cached(const String& url) {
  for (const auto& entry : g_browse_thumb_cache) {
    if (entry.url == url) return entry.dsc;
  }
  return nullptr;
}

static bool browse_thumb_failed(const String& url) {
  for (const auto& failed : g_browse_thumb_failed) {
    if (failed == url) return true;
  }
  return false;
}

static void browse_thumb_mark_failed(const String& url) {
  if (!url.length() || browse_thumb_failed(url)) return;
  if (g_browse_thumb_failed.size() >= 64) {
    g_browse_thumb_failed.erase(g_browse_thumb_failed.begin());
  }
  g_browse_thumb_failed.push_back(url);
}

static bool browse_thumb_in_use(const MediaPopupContext* ctx, const String& url) {
  if (!ctx) return false;
  for (const auto& row : ctx->browse_rows) {
    if (row.url == url) return true;
  }
  return false;
}

// Takes ownership of dsc. Returns false (and frees dsc) when the cache is
// full of thumbnails the current rows still show.
static bool browse_thumb_store(MediaPopupContext* ctx, const String& url,
                               lv_image_dsc_t*& dsc) {
  if (g_browse_thumb_cache.size() >= kBrowseThumbCacheMax) {
    for (size_t i = 0; i < g_browse_thumb_cache.size(); ++i) {
      if (browse_thumb_in_use(ctx, g_browse_thumb_cache[i].url)) continue;
      tile_renderer_free_image_dsc(g_browse_thumb_cache[i].dsc);
      g_browse_thumb_cache.erase(g_browse_thumb_cache.begin() + i);
      break;
    }
  }
  if (g_browse_thumb_cache.size() >= kBrowseThumbCacheMax) {
    tile_renderer_free_image_dsc(dsc);
    return false;
  }
  BrowseThumbCacheEntry entry;
  entry.url = url;
  entry.dsc = dsc;
  g_browse_thumb_cache.push_back(entry);
  dsc = nullptr;
  return true;
}

static void browse_thumb_apply(MediaPopupContext* ctx, const String& url,
                               const lv_image_dsc_t* dsc) {
  if (!ctx || !dsc) return;
  for (auto& row : ctx->browse_rows) {
    if (row.url != url || !row.image) continue;
    lv_image_set_src(row.image, dsc);
    lv_obj_clear_flag(lv_obj_get_parent(row.image), LV_OBJ_FLAG_HIDDEN);
    if (row.icon) lv_obj_add_flag(row.icon, LV_OBJ_FLAG_HIDDEN);
  }
}

static void browse_thumb_enqueue(MediaPopupContext* ctx, const String& url) {
  if (!ctx || !url.length() || browse_thumb_failed(url)) return;
  if (url == ctx->browse_thumb_inflight_url) return;
  for (const auto& queued : ctx->browse_thumb_queue) {
    if (queued == url) return;
  }
  ctx->browse_thumb_queue.push_back(url);
}

// Sends the next queued request; one at a time keeps MQTT traffic and the
// Bridge's fetches small.
static void browse_thumb_timer_cb(lv_timer_t* timer) {
  MediaPopupContext* ctx =
      timer ? static_cast<MediaPopupContext*>(lv_timer_get_user_data(timer)) : nullptr;
  if (!ctx) return;

  if (ctx->browse_thumb_inflight_id != 0) {
    if (static_cast<uint32_t>(millis() - ctx->browse_thumb_inflight_ms) <
        kBrowseThumbTimeoutMs) {
      return;
    }
    Serial.printf("[MediaBrowse] Thumbnail %lu timed out\n",
                  static_cast<unsigned long>(ctx->browse_thumb_inflight_id));
    ctx->browse_thumb_inflight_id = 0;
    ctx->browse_thumb_inflight_url = "";
  }

  if (!ctx->browse_panel ||
      lv_obj_has_flag(ctx->browse_panel, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }

  while (!ctx->browse_thumb_queue.empty()) {
    const String url = ctx->browse_thumb_queue.front();
    ctx->browse_thumb_queue.erase(ctx->browse_thumb_queue.begin());
    if (const lv_image_dsc_t* hit = browse_thumb_cached(url)) {
      browse_thumb_apply(ctx, url, hit);
      continue;
    }
    if (!browse_thumb_in_use(ctx, url)) continue;  // Level changed meanwhile.

    const uint32_t id = ++g_browse_thumb_seq ? g_browse_thumb_seq : ++g_browse_thumb_seq;
    if (!mqttPublishMediaThumbnail(media_browse_entity().c_str(),
                                   media_browse_session().c_str(), id,
                                   url.c_str(), kBrowseThumbSize,
                                   ctx->bg_color)) {
      // MQTT busy or offline: retry this URL on the next tick.
      ctx->browse_thumb_queue.insert(ctx->browse_thumb_queue.begin(), url);
      return;
    }
    ctx->browse_thumb_inflight_id = id;
    ctx->browse_thumb_inflight_url = url;
    ctx->browse_thumb_inflight_ms = millis();
    return;
  }
}

static void browse_rebuild_rows(MediaPopupContext* ctx) {
  lv_obj_t* list = ctx->browse_list;
  if (!list) return;
  lv_obj_clean(list);
  // The rows (and their image objects) are gone; pending requests of the
  // old level are dropped. An answer already in flight still fills the cache.
  ctx->browse_rows.clear();
  ctx->browse_thumb_queue.clear();

  const auto& items = media_browse_items();
  const lv_coord_t title_width = kContentWidth - (kBrowseRowPad * 2) -
                                 (kBrowseRowIconWidth * 2) -
                                 popup_layout::scale(24);
  for (size_t i = 0; i < items.size(); ++i) {
    const MediaBrowseItem& item = items[i];

    lv_obj_t* row = lv_button_create(list);
    lv_obj_set_size(row, LV_PCT(100), kBrowseRowHeight);
    lv_obj_set_style_bg_color(row, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, popup_layout::scale480(12), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, kBrowseRowPad, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    disable_pressed_button_animation(row);

    lv_obj_t* icon = lv_label_create(row);
    set_label_style(icon, lv_color_white(), FONT_MDI_ICONS);
    lv_label_set_text(icon, getMdiChar(browse_icon_name(item)).c_str());
    lv_obj_set_width(icon, kBrowseRowIconWidth);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    // Folder rows with artwork: a rounded image slot over the icon. The icon
    // stays until the thumbnail arrives (or for good if it cannot load).
    if (item.can_expand && item.thumbnail.length()) {
      lv_obj_t* clip = lv_obj_create(row);
      lv_obj_remove_style_all(clip);
      lv_obj_set_size(clip, kBrowseThumbSize, kBrowseThumbSize);
      lv_obj_align(clip, LV_ALIGN_LEFT_MID,
                   (kBrowseRowIconWidth - kBrowseThumbSize) / 2, 0);
      lv_obj_set_style_radius(clip, popup_layout::scale480(8), 0);
      lv_obj_set_style_clip_corner(clip, true, 0);
      lv_obj_remove_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(clip, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(clip, LV_OBJ_FLAG_HIDDEN);

      lv_obj_t* image = lv_image_create(clip);
      lv_obj_set_size(image, kBrowseThumbSize, kBrowseThumbSize);
      lv_obj_center(image);
      lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
      lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);

      BrowseRowThumb thumb;
      thumb.url = item.thumbnail;
      thumb.image = image;
      thumb.icon = icon;
      ctx->browse_rows.push_back(thumb);

      if (const lv_image_dsc_t* hit = browse_thumb_cached(item.thumbnail)) {
        lv_image_set_src(image, hit);
        lv_obj_clear_flag(clip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
      } else {
        browse_thumb_enqueue(ctx, item.thumbnail);
      }
    }

    lv_obj_t* title = lv_label_create(row);
    set_label_style(title, lv_color_white(), popup_layout::font24());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, title_width);
    lv_label_set_text(title, item.title.c_str());
    lv_obj_align(title, LV_ALIGN_LEFT_MID,
                 kBrowseRowIconWidth + popup_layout::scale(12), 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    if (item.can_play) {
      // Own tap target so "play" works for folders too (album, playlist).
      lv_obj_t* play = lv_button_create(row);
      lv_obj_set_size(play, kBrowseRowIconWidth, kBrowseRowIconWidth);
      lv_obj_align(play, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_set_style_bg_color(play, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_opa(play, LV_OPA_10, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_opa(play, LV_OPA_40, LV_PART_MAIN | LV_STATE_PRESSED);
      lv_obj_set_style_radius(play, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(play, 0, 0);
      lv_obj_set_style_shadow_width(play, 0, 0);
      lv_obj_set_style_pad_all(play, 0, 0);
      lv_obj_remove_flag(play, LV_OBJ_FLAG_SCROLLABLE);
      disable_pressed_button_animation(play);
      lv_obj_t* play_icon = lv_label_create(play);
      set_label_style(play_icon, lv_color_white(), FONT_MDI_ICONS);
      lv_label_set_text(play_icon, getMdiChar("play").c_str());
      lv_obj_center(play_icon);
      lv_obj_clear_flag(play_icon, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(play, on_browse_play_click, LV_EVENT_CLICKED,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    } else if (item.can_expand) {
      lv_obj_t* trail = lv_label_create(row);
      set_label_style(trail, lv_color_hex(0xD8DEE9), FONT_MDI_ICONS);
      lv_label_set_text(trail, getMdiChar("chevron-right").c_str());
      lv_obj_set_width(trail, kBrowseRowIconWidth);
      lv_obj_set_style_text_align(trail, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(trail, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_clear_flag(trail, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_add_event_cb(row, on_browse_row_click, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }
  lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
}

// Renders the current browse state. Cheap when nothing but the status changed:
// rows are only rebuilt when the item list was replaced.
static void browse_refresh(MediaPopupContext* ctx) {
  if (!browse_visible(ctx)) return;
  const MediaBrowseStatus status = media_browse_status();

  const MediaBrowseLevel* level = media_browse_current_level();
  const char* title = (level && level->title.length())
                          ? level->title.c_str()
                          : browse_text(BrowseText::Root);
  if (ctx->browse_title_label) lv_label_set_text(ctx->browse_title_label, title);
  set_control_enabled(ctx->browse_back_button, media_browse_can_go_back());

  String status_text;
  uint32_t status_color = 0xD8DEE9;
  if (status == MediaBrowseStatus::Loading) {
    status_text = browse_text(BrowseText::Loading);
  } else if (status == MediaBrowseStatus::Error) {
    status_text = browse_text(BrowseText::ErrorPrefix);
    status_text += media_browse_error();
    status_color = 0xFF8A80;
  } else if (media_browse_truncated()) {
    status_text = browse_text(BrowseText::Truncated);
  }
  if (ctx->browse_status_label) {
    lv_label_set_text(ctx->browse_status_label, status_text.c_str());
    lv_obj_set_style_text_color(ctx->browse_status_label,
                                lv_color_hex(status_color), 0);
    if (status_text.length()) {
      lv_obj_clear_flag(ctx->browse_status_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->browse_status_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  const uint32_t version = media_browse_items_version();
  if (ctx->browse_items_version != version) {
    ctx->browse_items_version = version;
    browse_rebuild_rows(ctx);
  }

  if (ctx->browse_message_label) {
    const bool empty =
        status == MediaBrowseStatus::Ready && media_browse_items().empty();
    if (empty) {
      lv_label_set_text(ctx->browse_message_label, browse_text(BrowseText::Empty));
      lv_obj_clear_flag(ctx->browse_message_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ctx->browse_message_label, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void on_media_browse_changed() {
  browse_refresh(g_media_popup_ctx);
}

static bool browse_supported(const MediaPopupContext* ctx) {
  return ctx && ctx->entity_id.startsWith("media_player.");
}

static void browse_show(MediaPopupContext* ctx) {
  if (!ctx || !ctx->browse_panel || !browse_supported(ctx)) return;
  lv_obj_clear_flag(ctx->browse_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ctx->browse_panel);
  if (ctx->browse_thumb_timer) lv_timer_resume(ctx->browse_thumb_timer);
  // Force a row rebuild: the list may still show another player's items.
  ctx->browse_items_version = 0xFFFFFFFFu;

  const bool same_entity = media_browse_entity().equalsIgnoreCase(ctx->entity_id);
  if (!same_entity || media_browse_status() == MediaBrowseStatus::Idle) {
    // New player (or first open): start at its root catalog.
    media_browse_start(ctx->entity_id.c_str());
  } else if (media_browse_status() == MediaBrowseStatus::Error &&
             media_browse_depth() == 0) {
    // The root never loaded (for example MQTT was offline): try again.
    media_browse_reload();
  }
  browse_refresh(ctx);
}

static void browse_hide(MediaPopupContext* ctx) {
  if (!ctx || !ctx->browse_panel) return;
  // The session is kept: reopening the browser for the same player resumes
  // at the last level. Thumbnail requests pause; an answer in flight still
  // fills the cache.
  lv_obj_add_flag(ctx->browse_panel, LV_OBJ_FLAG_HIDDEN);
  if (ctx->browse_thumb_timer) lv_timer_pause(ctx->browse_thumb_timer);
}

static void on_browse_toggle_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  browse_show(static_cast<MediaPopupContext*>(lv_event_get_user_data(e)));
}

static void on_browse_back_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  media_browse_back();
}

static void on_browse_close_click(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  browse_hide(static_cast<MediaPopupContext*>(lv_event_get_user_data(e)));
}

static lv_obj_t* create_round_icon_button(lv_obj_t* parent, const char* icon_name,
                                          lv_coord_t size, lv_event_cb_t cb,
                                          MediaPopupContext* ctx,
                                          lv_obj_t** label_out = nullptr) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
  lv_obj_set_style_bg_color(btn, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(btn);

  lv_obj_t* label = lv_label_create(btn);
  set_label_style(label, lv_color_white(), FONT_MDI_ICONS);
  lv_label_set_text(label, getMdiChar(icon_name).c_str());
  lv_obj_center(label);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
  if (label_out) *label_out = label;

  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ctx);
  return btn;
}

static void create_browse_view(lv_obj_t* card, MediaPopupContext* ctx,
                               uint32_t bg_color) {
  // Toggle in the controls row, right of "next".
  lv_obj_t* toggle = create_round_icon_button(card, "folder-music", kControlButtonSize,
                                              on_browse_toggle_click, ctx,
                                              &ctx->browse_toggle_label);
  lv_obj_align(toggle, LV_ALIGN_TOP_MID, kBrowseToggleOffset, kControlsTop);

  // Panel over the popup body. Opaque, same color as the card.
  lv_obj_t* panel = lv_obj_create(card);
  ctx->browse_panel = panel;
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, kContentWidth, kBrowsePanelHeight);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, kBrowseTop);
  lv_obj_set_style_bg_color(panel, lv_color_hex(bg_color), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);  // Swallow taps on gaps.
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

  // Bar: back | title + status | close.
  lv_obj_t* bar = lv_obj_create(panel);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, kContentWidth, kBrowseBarHeight);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  ctx->browse_back_button = create_round_icon_button(
      bar, "chevron-left", kBrowseBarButtonSize, on_browse_back_click, ctx);
  lv_obj_align(ctx->browse_back_button, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* close = create_round_icon_button(
      bar, "close", kBrowseBarButtonSize, on_browse_close_click, ctx);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, 0, 0);

  const lv_coord_t text_width =
      kContentWidth - (kBrowseBarButtonSize * 2) - popup_layout::scale(24);
  ctx->browse_title_label = lv_label_create(bar);
  set_label_style(ctx->browse_title_label, lv_color_white(), popup_layout::font24());
  lv_label_set_long_mode(ctx->browse_title_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ctx->browse_title_label, text_width);
  lv_obj_set_style_text_align(ctx->browse_title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(ctx->browse_title_label, LV_ALIGN_TOP_MID, 0, popup_layout::scale(4));
  lv_label_set_text(ctx->browse_title_label, "");

  ctx->browse_status_label = lv_label_create(bar);
  set_label_style(ctx->browse_status_label, lv_color_hex(0xD8DEE9), popup_layout::font20());
  lv_label_set_long_mode(ctx->browse_status_label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ctx->browse_status_label, text_width);
  lv_obj_set_style_text_align(ctx->browse_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(ctx->browse_status_label, LV_ALIGN_BOTTOM_MID, 0, -popup_layout::scale(2));
  lv_label_set_text(ctx->browse_status_label, "");
  lv_obj_add_flag(ctx->browse_status_label, LV_OBJ_FLAG_HIDDEN);

  // Scrollable list of entries.
  const lv_coord_t list_top = kBrowseBarHeight + popup_layout::scale(8);
  lv_obj_t* list = lv_obj_create(panel);
  ctx->browse_list = list;
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, kContentWidth, kBrowsePanelHeight - list_top);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, list_top);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, popup_layout::scale(4), 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  ctx->browse_thumb_timer = lv_timer_create(browse_thumb_timer_cb, kBrowseThumbPollMs, ctx);
  if (ctx->browse_thumb_timer) lv_timer_pause(ctx->browse_thumb_timer);

  ctx->browse_message_label = lv_label_create(panel);
  set_label_style(ctx->browse_message_label, lv_color_hex(0xD8DEE9), popup_layout::font24());
  lv_obj_align(ctx->browse_message_label, LV_ALIGN_TOP_MID, 0,
               list_top + popup_layout::scale(40));
  lv_label_set_text(ctx->browse_message_label, "");
  lv_obj_add_flag(ctx->browse_message_label, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

static void finish_media_popup_open(const MediaPopupInit& init) {
  if (!g_media_popup_ctx) return;
  MediaPopupInit current = init;
  // Resolve pixels on the UI thread when content is actually applied. A tile
  // may have been removed since the press; never retain its borrowed pointer.
  current.cover_dsc = tile_renderer_find_media_cover(init.entity_id, current.cover_hash);
  apply_init_to_context(g_media_popup_ctx, current);
}

static void prepare_media_popup_open(const MediaPopupInit& init) {
  auto* ctx = g_media_popup_ctx;
  String title = init.title;
  title.trim();
  if (!title.length()) title = init.entity_id;
  hometiles_title::set(ctx->title_label, title.c_str());
  String icon = init.icon_char;
  icon.trim();
  if (!icon.length()) icon = getMdiChar(init.icon_name);
  if (!icon.length()) icon = getMdiChar("television");
  lv_label_set_text(ctx->icon_label, icon.c_str());
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_hex(init.icon_color), 0);
  lv_obj_set_style_bg_color(ctx->card,
      lv_color_hex(init.bg_color ? init.bg_color : 0x2A2A2A), 0);
  MediaPopupInit pending = init;
  pending.cover_dsc = nullptr;
  if (!defer_popup_body(ctx->card, ctx->title_label, ctx->icon_label,
                        ctx->close_button, pending, finish_media_popup_open,
                        ctx->entity_id == init.entity_id))
    apply_init_to_context(ctx, init);
}

void show_media_popup(const MediaPopupInit& init) {
  hide_pin_popup();
  hide_camera_popup();
  hide_device_popup();
  hide_climate_popup();
  hide_cover_popup();
  if (!init.entity_id.length()) return;
  hide_light_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();

  if (g_media_popup_ctx && g_media_popup_ctx->overlay && g_media_popup_ctx->card) {
    prepare_media_popup_open(init);
    lv_obj_clear_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
    if (g_media_popup_ctx && g_media_popup_ctx->card) viewNavigationPopupShown(g_media_popup_ctx->card, init.entity_id.c_str());
    show_popup_shell(g_media_popup_ctx->overlay, g_media_popup_ctx->card, g_media_popup_ctx->title_label, g_media_popup_ctx->icon_label, g_media_popup_ctx->close_button);
    return;
  }

  MediaPopupContext* ctx = new MediaPopupContext();
  g_media_popup_ctx = ctx;

  const auto parts = create_popup_body(on_close_click, ctx, init.bg_color ? init.bg_color : 0x2A2A2A);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* overlay = parts.overlay;
  lv_obj_t* card = parts.card;
  lv_obj_t* close_btn = parts.close;

  ctx->cover_clip = lv_obj_create(card);
  lv_obj_set_size(ctx->cover_clip, kCoverSize, kCoverSize);
  lv_obj_align(ctx->cover_clip, LV_ALIGN_TOP_MID, 0, kCoverTop);
  lv_obj_set_style_bg_color(ctx->cover_clip, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_opa(ctx->cover_clip, LV_OPA_40, 0);
  lv_obj_set_style_border_width(ctx->cover_clip, 0, 0);
  lv_obj_set_style_shadow_width(ctx->cover_clip, 0, 0);
  lv_obj_set_style_pad_all(ctx->cover_clip, 0, 0);
  ui_surface_style::apply_radius(ctx->cover_clip, popup_layout::scale480(18), 0);
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
  lv_obj_set_style_opa(volume_btn, LV_OPA_30, LV_PART_MAIN | LV_STATE_DISABLED);
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
  create_browse_view(card, ctx, init.bg_color != 0 ? init.bg_color : 0x2A2A2A);
  media_browse_set_listener(on_media_browse_changed);

  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(close_btn);

  apply_init_to_context(ctx, init);
  ctx->progress_timer = lv_timer_create(media_progress_timer_cb, 1000, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_click, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(overlay, on_overlay_delete, LV_EVENT_DELETE, ctx);
  if (g_media_popup_ctx && g_media_popup_ctx->card) viewNavigationPopupShown(g_media_popup_ctx->card, init.entity_id.c_str());
  show_popup_shell(g_media_popup_ctx->overlay, g_media_popup_ctx->card, g_media_popup_ctx->title_label, g_media_popup_ctx->icon_label, g_media_popup_ctx->close_button);
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
    hide_popup_shell(g_media_popup_ctx->card);
    cancel_popup_open(g_media_popup_ctx->card);
    lv_obj_add_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  }
}

bool media_popup_showing(const String& entity_id) {
  if (!g_media_popup_ctx || !g_media_popup_ctx->overlay || !g_media_popup_ctx->card) return false;
  if (!g_media_popup_ctx->entity_id.length()) return false;
  if (!g_media_popup_ctx->entity_id.equalsIgnoreCase(entity_id)) return false;
  return !lv_obj_has_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
}

void update_media_popup(const MediaPopupInit& init) {
  if (!media_popup_showing(init.entity_id)) return;
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
  browse_hide(g_media_popup_ctx);
  hide_popup_shell(g_media_popup_ctx->card);
  cancel_popup_open(g_media_popup_ctx->card);
  lv_obj_add_flag(g_media_popup_ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_media_popup_ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void media_popup_handle_browse_thumbnail(const char* payload, size_t length) {
  MediaPopupContext* ctx = g_media_popup_ctx;
  if (!payload || !length || !ctx || ctx->browse_thumb_inflight_id == 0) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.printf("[MediaBrowse] Thumbnail parse failed (%u bytes)\n",
                  static_cast<unsigned>(length));
    return;
  }
  const uint32_t thumb_id = doc["thumb_id"] | 0u;
  const char* session = doc["session"] | "";
  if (thumb_id != ctx->browse_thumb_inflight_id ||
      media_browse_session() != session) {
    return;  // Late answer after a timeout, or another session.
  }

  const String url = ctx->browse_thumb_inflight_url;
  ctx->browse_thumb_inflight_id = 0;
  ctx->browse_thumb_inflight_url = "";

  const char* status = doc["status"] | "";
  const char* data = doc["data"] | "";
  if (strcmp(status, "ok") != 0 || !*data) {
    Serial.printf("[MediaBrowse] Thumbnail %lu failed: %s %s\n",
                  static_cast<unsigned long>(thumb_id),
                  doc["error"] | "unknown", doc["detail"] | "");
    browse_thumb_mark_failed(url);
  } else {
    lv_image_dsc_t* dsc = tile_renderer_decode_image_base64(String(data));
    if (!dsc) {
      Serial.printf("[MediaBrowse] Thumbnail %lu could not be decoded\n",
                    static_cast<unsigned long>(thumb_id));
      browse_thumb_mark_failed(url);
    } else if (browse_thumb_store(ctx, url, dsc)) {
      browse_thumb_apply(ctx, url, browse_thumb_cached(url));
    }
  }

  // Next request right away instead of waiting for the next poll.
  if (ctx->browse_thumb_timer && browse_visible(ctx)) {
    lv_timer_ready(ctx->browse_thumb_timer);
  }
}
