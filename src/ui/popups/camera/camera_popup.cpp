#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/camera/camera_popup.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <time.h>

// lvgl.h no longer exports lv_image_cache_drop() (tile_renderer.cpp).
#include <misc/cache/instance/lv_image_cache.h>

#include <algorithm>
#include <vector>

#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/power/power_manager.h"
#include "src/fonts/ui_fonts.h"
#include "src/network/mqtt/mqtt_handlers.h"
#include "src/network/network_manager.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/device/device_popup.h"
#include "src/ui/popups/vacuum/vacuum_popup.h"
#include "src/ui/popups/calendar/calendar_popup.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/media/media_popup_layout.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/video/camera_geometry.h"
#include "src/devices/device.h"
#include "src/video/camera_stream.h"
#include "src/video/camera_transport.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/camera/camera_entries.h"
#include "src/types/camera/picture_layout.h"
#include "src/types/camera/renderer.h"
#include "src/types/clock/clock_format.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/screensaver/wallpaper_cover.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/tabs/settings/settings_style.h"

namespace {

namespace ml = media_popup_layout;

constexpr int kVideoFrameWidth = camera_geometry::kWidth;
constexpr int kVideoHeight = camera_geometry::kHeight;
// In card coordinates, as in the Media popup (mockup webcam-radar/radar-tall.html):
// the picture whole, never cropped, centred in its area, which ends one edge above
// the bottom row: Play, the time bar with its times under it, the "..." menu. With
// neither a history nor a second entry there is no bottom row and the area also
// takes its height. The full screen stays a tap on the picture: no button for it
// (user 2026-11-05).
constexpr int kButtonTop = ml::kVolumeTop;
constexpr int kAreaTop = camera_geometry::kAreaTop;
constexpr int kAreaHeightWithRow = camera_geometry::kBoxHeight;
constexpr int kAreaHeightPlain = kButtonTop + ml::kButton - kAreaTop;
constexpr int kBarGap = popup_layout::scale(22);
constexpr int kBarTimeGap = popup_layout::scale(8);
constexpr int kBarLeft = ml::kEdge + ml::kButton + kBarGap;
// The menu, as the Media popup's (build_menu, list_row): only as high as its
// entries, five rows at most, then it scrolls.
constexpr int kMenuRow = popup_layout::scale(72);
constexpr int kMenuRowGap = 2;  // settings_parts.cpp kOptionGap
constexpr size_t kMenuRowsShown = 5;
// The time bar: the frames of a camera with a history stay in the Bridge, the
// panel knows only their times and shows the one it asks for over the stream.
constexpr uint8_t kMaxFrames = 150;
constexpr uint32_t kFrameSendMs = 120;
// Play runs in a loop: the newest picture stays a little longer. A webcam's day
// (up to 150 pictures, one per 10 minutes) goes in larger steps.
constexpr uint32_t kPlayStepMs = 500;
constexpr uint32_t kPlayHoldMs = 1500;
constexpr uint8_t kPlayLongHistory = 40;
constexpr uint8_t kPlayLongStep = 3;
constexpr uint32_t kPlayLongStepMs = 400;
// The last picture of a still camera per entity, shown at once at the next
// opening instead of black (JPEG, 15-40 KB each).
constexpr size_t kKeptPictures = 4;
constexpr uint8_t kRequiredBridgeCameraProtocol = 1;
// One of the Bridge's own cameras may just be coming into being (its first picture,
// its entity right after the Web Admin saved it): asked again a few times.
constexpr uint32_t kSourceRetryMs = 3000;
constexpr uint8_t kSourceRetries = 6;
constexpr uint32_t kBridgeResponseTimeoutMs = 30000;
// A full-screen request the Bridge does not answer goes back to the popup.
constexpr uint32_t kFullScreenResponseTimeoutMs = 10000;
// Without a connection to the Bridge (Home Assistant restarting, Wi-Fi gone) the
// picture stays without words; only a connection gone this long is told.
constexpr uint32_t kBridgeLostTellMs = 30000;
constexpr uint8_t kFullEnter = 1;
constexpr uint8_t kFullLeave = 2;
// A tap in a full screen a tile opened straight (CameraPopupInit::full_screen):
// back to the tiles without the popup in between.
constexpr uint8_t kFullClose = 3;

struct CameraPopupContext {
  String entity_id;
  uint32_t surface_color = ui_theme::popup_card();
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* image = nullptr;
  lv_obj_t* status = nullptr;
  lv_obj_t* video = nullptr;
  // The head's second line ("Live", the picture's time): the shell's hidden
  // source (show_popup_shell).
  lv_obj_t* value_label = nullptr;
  // A neutral message that stays over the picture ("stopped").
  bool status_sticky = false;
  // The tile's entries (one source, types/camera/camera_entries.h); empty for a
  // single entity. The shown one and whether the tile follows a choice.
  std::vector<camera_entries::Entry> entries;
  size_t entry_index = 0;
  uint32_t remember_key = 0;
  // This stream's picture, and how it sits in its area.
  uint16_t frame_w = kVideoFrameWidth;
  uint16_t frame_h = kVideoHeight;
  // The last picture of a still camera until the stream's first frame.
  lv_obj_t* kept_image = nullptr;
  lv_image_dsc_t* kept_dsc = nullptr;
  // The stream runs and its JPEGs may be kept (a still camera).
  bool keeping = false;
  // The menu ("..."), its rows built once per opening.
  lv_obj_t* menu_button = nullptr;
  lv_obj_t* menu_label = nullptr;
  lv_obj_t* menu_shield = nullptr;
  lv_obj_t* menu_box = nullptr;
  lv_obj_t* menu_title = nullptr;
  lv_obj_t* menu_rule = nullptr;
  lv_obj_t* menu_list = nullptr;
  lv_obj_t* menu_shadow = nullptr;
  lv_draw_buf_t* menu_shadow_parts[8] = {};
  int menu_shadow_rows = 0;
  bool menu_open = false;
  bool menu_rows_stale = true;
  uint32_t icon_color = 0xFFFFFF;
  // The controls in card coordinates.
  lv_obj_t* root = nullptr;
  lv_obj_t* bar_track = nullptr;
  lv_obj_t* bar_cap = nullptr;
  lv_obj_t* bar = nullptr;
  lv_obj_t* time_shown = nullptr;
  lv_obj_t* time_newest = nullptr;
  lv_obj_t* play_button = nullptr;
  lv_obj_t* play_label = nullptr;
  // The history the Bridge told: the times, oldest first, and the shown one.
  uint32_t frame_times[kMaxFrames] = {};
  uint8_t frame_count = 0;
  uint8_t shown_frame = 0;
  bool frame_dirty = false;
  uint32_t frame_sent_ms = 0;
  bool playing = false;
  uint32_t play_next_ms = 0;
  bool live_kind = false;
  // Asked again after the Bridge had no picture of its own camera yet.
  uint32_t source_retry_at_ms = 0;
  uint8_t source_retries = 0;
  // The opening, for the delayed status and the log.
  uint32_t opened_ms = 0;
  bool open_logged = false;
  size_t previous_draw_buffer_requested_lines = 0;
  bool previous_draw_buffer_fast = false;
  bool large_draw_buffer_active = false;
  bool draw_buffer_restore_pending = false;
  uint32_t draw_buffer_restore_retry_at_ms = 0;
  uint32_t bridge_response_deadline_ms = 0;
  // Frame rate of the pending "open"; lowered once when an older Bridge
  // rejects camera_geometry::kFps.
  uint8_t requested_fps = camera_geometry::kFps;
  bool waiting_for_bridge = false;
  // Since when the connection to the Bridge is gone while the popup is open
  // (0 = it is there), and whether that was told.
  uint32_t bridge_lost_ms = 0;
  bool bridge_lost_told = false;
  bool visible = false;
  // Camera full screen (#65): the stream in the panel's own size straight in
  // the framebuffer (Device full frames). A tap on the video asks for it, a
  // tap anywhere ends it; both run in process_camera_popup(), outside the
  // LVGL event. full_touch takes that tap and is never drawn (LVGL
  // invalidation stays off while the full screen shows).
  lv_obj_t* full_touch = nullptr;
  uint8_t full_request = 0;
  bool full = false;
  uint16_t full_width = 0;
  uint16_t full_height = 0;
  uint16_t full_turn = 0;
  // A stream URL waits until the previous stream's task has ended.
  String pending_url;
  bool pending_full = false;
  // Back from the full screen: the kept popup shows its black video area
  // and status until its own stream shows a frame again (no Buffering flash,
  // no frozen frame).
  bool resuming = false;
  // Switch timing for the log: the tap and whether the first frame of the
  // new stream was logged.
  uint32_t switch_started_ms = 0;
  bool switch_frame_logged = true;
  // The running stream used the fast transport (camera_transport).
  bool stream_fast = false;
  // Opened by the View select's full-screen option (#65): no popup stream,
  // the full screen right after the popup opened.
  bool open_full = false;
  // Opened in full screen by its tile: a tap there closes the popup.
  bool close_on_leave = false;
  // ...and the popup card was not shown on the way (user 2026-10-11: it
  // showed briefly first); it shows only where the full screen cannot start
  // (reveal_skipped_popup).
  bool shell_skipped = false;
#if defined(DEVICE_ESP32_S3_RGB_480)
  // The S3's full screen (software decode, camera_stream.cpp): LVGL shows the
  // whole picture centred on black over everything; a tap leaves it.
  lv_obj_t* soft_full = nullptr;
  lv_obj_t* soft_full_image = nullptr;
#endif
};

CameraPopupContext* g_camera_popup = nullptr;
// Set by the View select just before it opens a camera tile's popup.
bool g_open_next_full = false;

struct KeptPicture {
  String entity;
  uint8_t* jpeg = nullptr;
  size_t bytes = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  // The history's times: bar and Play are there at once, the Bridge's answer
  // only corrects them.
  std::vector<uint32_t> times;
};
std::vector<KeptPicture> g_kept_pictures;

static const i18n::Strings& camera_text() {
  return i18n::strings(configManager.getConfig().language);
}

static const char* localize_camera_error(const char* error_code) {
  const auto& text = camera_text();
  if (!error_code || !*error_code) return text.camera_unavailable;
  if (strcmp(error_code, "unknown_camera") == 0) return text.camera_unknown;
  if (strcmp(error_code, "camera_has_no_stream_source") == 0 ||
      strcmp(error_code, "camera_image_unavailable") == 0) {
    return text.camera_no_source;
  }
  if (strcmp(error_code, "home_assistant_url_unavailable") == 0) {
    return text.camera_ha_url_unavailable;
  }
  if (strcmp(error_code, "camera_setup_failed") == 0) {
    return text.camera_setup_failed;
  }
  return text.camera_unavailable;
}

static void set_status_label(const char* text, bool error) {
  if (!g_camera_popup || !g_camera_popup->status) return;
  lv_label_set_text(g_camera_popup->status, text ? text : "");
  lv_obj_set_style_text_color(
      g_camera_popup->status,
      error ? lv_color_hex(0xFF6B6B) : lv_color_hex(ui_theme::text_secondary()), 0);
}

static bool restore_previous_draw_buffer(CameraPopupContext* ctx) {
  if (!ctx || !ctx->large_draw_buffer_active) return true;
  const size_t requested_lines =
      ctx->previous_draw_buffer_requested_lines;
  if (requested_lines == 0) return false;

  // The normal UI buffer is retained while the temporary camera buffer is
  // active. Restoring it directly avoids reallocating and progressively
  // fragmenting the internal DMA heap.
  bool restored = displayManager.restoreDrawBufferAfterSinglePsram();
  if (!restored && ctx->previous_draw_buffer_fast) {
    restored = displayManager.restoreBufferLinesAfterOta(requested_lines);
    if (!restored) {
      Serial.println(
          "[Camera] SRAM buffer currently fragmented; "
          "restoring normal PSRAM buffer");
    }
  }
  if (!restored) {
    // If the UI was already using PSRAM before opening the camera, or the
    // previous SRAM band cannot be reallocated without touching the network
    // reserve, restore the ordinary smaller PSRAM buffer immediately. Staying
    // in the 424-line camera buffer makes every later UI rebuild slower.
    restored = displayManager.setBufferLines(requested_lines);
  }
  if (!restored) return false;

  ctx->draw_buffer_restore_pending = false;
  ctx->large_draw_buffer_active = false;
  ctx->previous_draw_buffer_requested_lines = 0;
  ctx->previous_draw_buffer_fast = false;
  ctx->draw_buffer_restore_retry_at_ms = 0;
  Serial.printf("[Camera] Display buffer restored (%u lines requested)\n",
                static_cast<unsigned>(requested_lines));
  return true;
}

static void note_switch(CameraPopupContext* ctx, const char* step) {
  if (!ctx->switch_started_ms) return;
  Serial.printf("[Camera] Switch: %s after %u ms\n", step,
                static_cast<unsigned>(millis() - ctx->switch_started_ms));
}

static void reveal_skipped_popup(CameraPopupContext* ctx);

// The Bridge's rain radar, by its entity (camera_sources.py): it always gets its time
// bar, so its row is kept from the start and the picture never moves when it comes.
static bool is_radar(const CameraPopupContext* ctx) {
  return ctx->entity_id.startsWith("camera.ht_radar_");
}

// ---- The time bar and the head's second line --------------------------------

static void set_shown(lv_obj_t* obj, bool shown) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != shown) return;
  if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void format_frame_time(uint32_t when, char* out, size_t size) {
  const time_t stamp = static_cast<time_t>(when);
  struct tm local {};
  localtime_r(&stamp, &local);
  const DeviceConfig& cfg = configManager.getConfig();
  const uint8_t format =
      clock_tile::resolve_time_format(clock_tile::TIME_FORMAT_AUTO, cfg.global_time_format, cfg.language);
  strftime(out, size, format == clock_tile::TIME_FORMAT_24H ? "%H:%M" : "%I:%M %p", &local);
}

// The head's second line (user 2026-11-06, like RainViewer's own line): "Synced"
// and the newest picture's clock time, also with a single picture, and it stays
// while dragging or playing; "Live" for a running camera. The shown picture's
// time stands under the bar.
static void show_head_text(CameraPopupContext* ctx, uint32_t newest) {
  if (!ctx->value_label) return;
  char text[64] = "";
  if (newest) {
    char when[16] = "";
    format_frame_time(newest, when, sizeof(when));
    snprintf(text, sizeof(text), camera_text().camera_synced_fmt, when);
  } else if (ctx->live_kind) {
    snprintf(text, sizeof(text), "%s", camera_text().camera_live);
  }
  if (strcmp(lv_label_get_text(ctx->value_label), text) == 0) return;
  lv_label_set_text(ctx->value_label, text);
  if (ctx->visible && !ctx->shell_skipped && !ctx->full) sync_popup_shell();
}

static void show_head_value(CameraPopupContext* ctx) {
  show_head_text(ctx, ctx->frame_count ? ctx->frame_times[ctx->frame_count - 1] : 0);
}

static void set_playing(CameraPopupContext* ctx, bool playing) {
  if (ctx->playing == playing) return;
  ctx->playing = playing;
  ctx->play_next_ms = millis() + kPlayStepMs;
  if (ctx->play_label) lv_label_set_text(ctx->play_label, getMdiChar(playing ? "pause" : "play").c_str());
}

// Bar, times and Play follow the history; a value from outside never moves the
// knob under the finger (it follows on release, bar_event_cb).
static void show_bar(CameraPopupContext* ctx) {
  if (!ctx->bar || ctx->full) return;
  const bool shown = ctx->frame_count >= 2;
  const bool many = ctx->entries.size() >= 2;
  for (lv_obj_t* obj : {ctx->bar_track, ctx->bar_cap, ctx->bar, ctx->time_shown, ctx->time_newest, ctx->play_button}) {
    set_shown(obj, shown);
  }
  set_shown(ctx->menu_button, many);
  // The bar makes room for the menu button.
  const int bar_right = popup_layout::kCardWidth - ml::kEdge - (many ? ml::kButton + kBarGap : 0);
  if (lv_obj_get_width(ctx->bar_track) != bar_right - kBarLeft) {
    lv_obj_set_width(ctx->bar_track, bar_right - kBarLeft);
    lv_obj_set_width(ctx->bar, bar_right - kBarLeft);
    lv_obj_align(ctx->time_newest, LV_ALIGN_TOP_RIGHT, bar_right - popup_layout::kCardWidth,
                 lv_obj_get_y_aligned(ctx->time_shown));
  }
  if (!shown) {
    set_playing(ctx, false);
    return;
  }
  if (!lv_slider_is_dragged(ctx->bar)) {
    const int32_t last = ctx->frame_count - 1;
    if (lv_slider_get_max_value(ctx->bar) != last) lv_slider_set_range(ctx->bar, 0, last);
    if (lv_slider_get_value(ctx->bar) != ctx->shown_frame) lv_slider_set_value(ctx->bar, ctx->shown_frame, LV_ANIM_OFF);
  }
  char when[16] = "";
  format_frame_time(ctx->frame_times[ctx->shown_frame], when, sizeof(when));
  if (strcmp(lv_label_get_text(ctx->time_shown), when) != 0) lv_label_set_text(ctx->time_shown, when);
  format_frame_time(ctx->frame_times[ctx->frame_count - 1], when, sizeof(when));
  if (strcmp(lv_label_get_text(ctx->time_newest), when) != 0) lv_label_set_text(ctx->time_newest, when);
}

// The picture in this stream's size, its middle on the card's middle (user
// 2026-11-19: centred in its area below the head, a picture without the bottom
// row sat 32 px low at 480 x 480, 50 px on the V2), kept within its area: never
// over the head or the bottom row. The radar fills its area as before.
static void apply_layout(CameraPopupContext* ctx) {
  const bool row = ctx->frame_count >= 2 || ctx->entries.size() >= 2 || is_radar(ctx);
  const int area = row ? kAreaHeightWithRow : kAreaHeightPlain;
  lv_obj_set_size(ctx->video, ctx->frame_w, ctx->frame_h);
  lv_obj_set_size(ctx->image, ctx->frame_w, ctx->frame_h);
  const int centred = (popup_layout::kCardHeight - ctx->frame_h) / 2;
  const int top = std::max(kAreaTop, std::min(centred, kAreaTop + area - ctx->frame_h));
  lv_obj_align(ctx->video, LV_ALIGN_TOP_MID, 0, top - popup_layout::kCardPad);
  // The stream writes straight to the image's place on the panel (P4).
  lv_obj_update_layout(ctx->card);
}

// The Bridge's times, the shown frame kept by its time: the newest stays the
// newest, a frame that is gone becomes the next older one. Up to 32 as they are,
// more as minutes after the first (camera_sources.py frames_payload).
static bool frames_in(JsonVariantConst doc) {
  return !doc["frames"].isNull() || !doc["frames_min"].isNull();
}

// A radar place that is still loading has fewer frames than the bar already shows:
// the bar stays until the place's own come ("frames").
static bool keep_shown_frames(const CameraPopupContext* ctx, JsonVariantConst doc) {
  return is_radar(ctx) && ctx->frame_count >= 2 && doc["frames"].size() + doc["frames_min"].size() < 2;
}

static void take_frames(CameraPopupContext* ctx, JsonVariantConst doc) {
  const bool at_newest = ctx->frame_count == 0 || ctx->shown_frame + 1 >= ctx->frame_count;
  const uint32_t shown_time = ctx->frame_count ? ctx->frame_times[ctx->shown_frame] : 0;
  uint8_t count = 0;
  const auto add = [&](uint32_t when) {
    if (!when) return;
    if (count == kMaxFrames) {  // The newest are kept.
      memmove(ctx->frame_times, ctx->frame_times + 1, sizeof(ctx->frame_times[0]) * (kMaxFrames - 1));
      --count;
    }
    ctx->frame_times[count++] = when;
  };
  JsonArrayConst minutes = doc["frames_min"];
  if (!minutes.isNull()) {
    const uint32_t base = doc["frames_base"] | 0u;
    for (JsonVariantConst item : minutes) add(base ? base + item.as<uint32_t>() * 60 : 0);
  } else {
    JsonArrayConst frames = doc["frames"];
    for (JsonVariantConst item : frames) add(item.as<uint32_t>());
  }
  ctx->frame_count = count;
  ctx->shown_frame = count ? count - 1 : 0;
  if (at_newest || !count) return;
  ctx->shown_frame = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (ctx->frame_times[i] <= shown_time) ctx->shown_frame = i;
  }
  if (ctx->frame_times[ctx->shown_frame] != shown_time) ctx->frame_dirty = true;
}

// One step of Play, in a loop: after the newest picture the oldest again.
static void play_step(CameraPopupContext* ctx) {
  const bool long_history = ctx->frame_count > kPlayLongHistory;
  const uint8_t last = ctx->frame_count - 1;
  const uint8_t step = long_history ? kPlayLongStep : 1;
  if (ctx->shown_frame >= last) ctx->shown_frame = 0;
  else ctx->shown_frame = last - ctx->shown_frame < step ? last : ctx->shown_frame + step;
  ctx->frame_dirty = true;
  ctx->play_next_ms = millis() + (ctx->shown_frame >= last ? kPlayHoldMs : long_history ? kPlayLongStepMs : kPlayStepMs);
  if (ctx->full) return;
  show_bar(ctx);
  show_head_value(ctx);
}

// The steps of Play and the picked frame to the Bridge, at most every
// kFrameSendMs: while dragging only the newest position goes out.
static void process_time_bar(CameraPopupContext* ctx) {
  const uint32_t now = millis();
  if (ctx->playing && static_cast<int32_t>(now - ctx->play_next_ms) >= 0) play_step(ctx);
  if (!ctx->frame_dirty || now - ctx->frame_sent_ms < kFrameSendMs) return;
  ctx->frame_dirty = false;
  ctx->frame_sent_ms = now;
  const bool newest = ctx->shown_frame + 1 >= ctx->frame_count;
  mqttPublishCameraFrame(ctx->entity_id.c_str(), newest ? 0 : ctx->frame_times[ctx->shown_frame]);
}

// ---- The last picture, shown at once at the next opening ----------------------

static void remember_kept_picture(CameraPopupContext* ctx, uint8_t* jpeg, size_t bytes) {
  for (KeptPicture& kept : g_kept_pictures) {
    if (!kept.entity.equalsIgnoreCase(ctx->entity_id)) continue;
    free(kept.jpeg);
    kept.jpeg = jpeg;
    kept.bytes = bytes;
    kept.width = ctx->frame_w;
    kept.height = ctx->frame_h;
    kept.times.assign(ctx->frame_times, ctx->frame_times + ctx->frame_count);
    return;
  }
  if (g_kept_pictures.size() >= kKeptPictures) {
    free(g_kept_pictures.front().jpeg);
    g_kept_pictures.erase(g_kept_pictures.begin());
  }
  KeptPicture kept;
  kept.entity = ctx->entity_id;
  kept.jpeg = jpeg;
  kept.bytes = bytes;
  kept.width = ctx->frame_w;
  kept.height = ctx->frame_h;
  kept.times.assign(ctx->frame_times, ctx->frame_times + ctx->frame_count);
  g_kept_pictures.push_back(kept);
}

static void take_kept_jpeg(CameraPopupContext* ctx) {
  uint8_t* jpeg = nullptr;
  size_t bytes = 0;
  if (camera_stream_take_kept_jpeg(jpeg, bytes)) remember_kept_picture(ctx, jpeg, bytes);
}

// `redraw` false: the stream's frame already lies over it in the same place, and a
// whole picture drawn again by LVGL stalls the loop for nothing.
static void hide_kept_picture(CameraPopupContext* ctx, bool redraw = true) {
  if (!ctx->kept_dsc) return;
  lv_display_t* display = lv_obj_get_display(ctx->kept_image);
  const bool invalidating = display && lv_display_is_invalidation_enabled(display);
  if (!redraw && invalidating) lv_display_enable_invalidation(display, false);
  lv_image_set_src(ctx->kept_image, nullptr);
  lv_obj_add_flag(ctx->kept_image, LV_OBJ_FLAG_HIDDEN);
  if (!redraw && invalidating) lv_display_enable_invalidation(display, true);
  lv_image_cache_drop(ctx->kept_dsc);
  free(const_cast<uint8_t*>(ctx->kept_dsc->data));
  free(ctx->kept_dsc);
  ctx->kept_dsc = nullptr;
}

// The picture's size until the Bridge tells it: the radar fills the box in every
// place, a camera or webcam is 16:9 unless it says otherwise. So the radar does not
// open small and grow (user 2026-11-06).
static void default_frame_size(CameraPopupContext* ctx) {
  ctx->frame_w = kVideoFrameWidth;
  ctx->frame_h = is_radar(ctx) ? camera_geometry::kBoxHeight : kVideoHeight;
}

// An entity's last picture over the black until the stream's first frame; its size
// comes with it, and for the shown entity also the head's time. While switching the
// previous entity's stays until the new one has a picture.
static bool show_kept_picture(CameraPopupContext* ctx, const String& entity, bool own) {
  hide_kept_picture(ctx);
  for (const KeptPicture& kept : g_kept_pictures) {
    if (!kept.entity.equalsIgnoreCase(entity)) continue;
    uint16_t w = 0, h = 0;
    uint16_t* pixels = image_screensaver_decode_picture(kept.jpeg, kept.bytes, kept.width, kept.height, w, h);
    if (!pixels || w != kept.width || h != kept.height) {
      free(pixels);
      return false;
    }
    lv_image_dsc_t* dsc = static_cast<lv_image_dsc_t*>(malloc(sizeof(lv_image_dsc_t)));
    if (!dsc) {
      free(pixels);
      return false;
    }
    const uint16_t radius = ui_surface_style::radius(camera_geometry::kCornerRadius);
    camera_picture_layout::round_corners(pixels, w, h, radius, camera_picture_layout::rgb565(ctx->surface_color),
                                         wallpaper_cover::rounded_pixel_coverage);
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.stride = static_cast<uint32_t>(w) * 2;
    dsc->data_size = static_cast<uint32_t>(w) * h * 2;
    dsc->data = reinterpret_cast<const uint8_t*>(pixels);
    ctx->kept_dsc = dsc;
    ctx->frame_w = w;
    ctx->frame_h = h;
    lv_image_set_src(ctx->kept_image, dsc);
    lv_obj_remove_flag(ctx->kept_image, LV_OBJ_FLAG_HIDDEN);
    if (own && !kept.times.empty()) {
      ctx->frame_count = static_cast<uint8_t>(kept.times.size() < kMaxFrames ? kept.times.size() : kMaxFrames);
      memcpy(ctx->frame_times, kept.times.data() + kept.times.size() - ctx->frame_count,
             sizeof(ctx->frame_times[0]) * ctx->frame_count);
      ctx->shown_frame = ctx->frame_count - 1;
      show_head_value(ctx);
    }
    return true;
  }
  return false;
}

static void open_popup_stream(CameraPopupContext* ctx) {
  ctx->source_retry_at_ms = 0;
  ctx->waiting_for_bridge = true;
  ctx->bridge_response_deadline_ms = millis() + kBridgeResponseTimeoutMs;
  ctx->requested_fps = camera_geometry::kFps;
  mqttPublishCameraCommand(ctx->entity_id.c_str(), "open", ctx->requested_fps);
}

#if defined(DEVICE_ESP32_S3_RGB_480)
// The S3's full screen (user 2026-10-09: the whole picture, nothing cut,
// fewer frames if need be): its stream in the screen's size, black bars from
// the Bridge, decoded in software and drawn by LVGL on black over everything.
static void enter_soft_full_screen() {
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->visible || ctx->full || !ctx->soft_full) return;
  // The popup's frames go with its stream; its video area drawn black.
  lv_image_set_src(ctx->image, nullptr);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(lv_obj_get_parent(ctx->image));
  ctx->pending_url = String();
  camera_stream_stop();
  lv_image_set_src(ctx->soft_full_image, nullptr);
  lv_obj_add_flag(ctx->soft_full_image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->soft_full, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ctx->soft_full);
  ctx->full = true;
  // The Bridge's answer is checked against these (and a transport fallback
  // asks again with them).
  ctx->full_width = camera_geometry::kSoftFullWidth;
  ctx->full_height = camera_geometry::kSoftFullHeight;
  ctx->full_turn = 0;
  ctx->waiting_for_bridge = true;
  ctx->bridge_response_deadline_ms = millis() + kFullScreenResponseTimeoutMs;
  note_switch(ctx, "full screen black on screen");
  ctx->switch_frame_logged = false;
  Serial.printf("[Camera] Full screen (software): frames within %ux%u, fit %s\n",
                static_cast<unsigned>(camera_geometry::kSoftFullWidth),
                static_cast<unsigned>(camera_geometry::kSoftFullHeight),
                camera_geometry::kFullFit);
  mqttPublishCameraFullScreenOpen(ctx->entity_id.c_str(), camera_geometry::kFps,
                                  camera_geometry::kSoftFullWidth,
                                  camera_geometry::kSoftFullHeight, 0);
}

static void leave_soft_full_screen(bool reopen) {
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->full) return;
  ctx->full = false;
  set_playing(ctx, false);
  ctx->full_request = 0;
  ctx->pending_url = String();
  // LVGL lets go of the frame before the stream frees it.
  lv_image_set_src(ctx->soft_full_image, nullptr);
  lv_obj_add_flag(ctx->soft_full_image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ctx->soft_full, LV_OBJ_FLAG_HIDDEN);
  camera_stream_stop();
  Serial.println("[Camera] Full screen ended");
  note_switch(ctx, "popup with black video on screen");
  ctx->switch_frame_logged = false;
  ctx->resuming = reopen && ctx->visible;
  if (ctx->resuming) {
    reveal_skipped_popup(ctx);
    open_popup_stream(ctx);
  }
}
#endif

// A tile's full screen that cannot start or ends without a tap: the popup it
// skipped shows after all, with its own stream.
static void reveal_skipped_popup(CameraPopupContext* ctx) {
  if (!ctx || !ctx->shell_skipped || !ctx->visible) return;
  ctx->shell_skipped = false;
  ctx->close_on_leave = false;
  lv_obj_clear_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  show_popup_shell(ctx->overlay, ctx->card, ctx->title_label, ctx->icon_label, ctx->close_button, nullptr,
                   ctx->value_label);
}

// The popup back at once: the kept UI swapped in and LVGL drawing again
// (what changed meanwhile is drawn again, the rest are the same pixels);
// with reopen also the popup's own stream again.
static void leave_full_screen(bool reopen) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  leave_soft_full_screen(reopen);
  return;
#endif
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->full) return;
  ctx->full = false;
  set_playing(ctx, false);
  ctx->full_request = 0;
  ctx->pending_url = String();
  camera_stream_stop();
  Device::displayEndFullFrames();
  if (ctx->full_touch) lv_obj_add_flag(ctx->full_touch, LV_OBJ_FLAG_HIDDEN);
  lv_display_t* display = lv_display_get_default();
  if (display) lv_display_enable_invalidation(display, true);
  // What changed meanwhile outside the popup is drawn again. The popup stays
  // as kept, black video and status, until its stream shows again (b315:
  // the whole screen drawn again took 350 ms and showed Buffering).
  lv_area_t card{};
  lv_obj_get_coords(ctx->card, &card);
  const int32_t width = display ? lv_display_get_horizontal_resolution(display) : 0;
  const int32_t height = display ? lv_display_get_vertical_resolution(display) : 0;
  const lv_area_t outside[] = {
      {0, 0, card.x1 - 1, height - 1},
      {card.x2 + 1, 0, width - 1, height - 1},
      {card.x1, 0, card.x2, card.y1 - 1},
      {card.x1, card.y2 + 1, card.x2, height - 1},
  };
  for (const lv_area_t& area : outside) {
    if (area.x2 >= area.x1 && area.y2 >= area.y1) lv_obj_invalidate_area(lv_screen_active(), &area);
  }
  Serial.println("[Camera] Full screen ended");
  note_switch(ctx, "popup with black video on screen");
  ctx->switch_frame_logged = false;
  ctx->resuming = reopen && ctx->visible;
  if (ctx->resuming) {
    // The popup card was never drawn: the whole screen again, then the popup.
    if (ctx->shell_skipped) lv_obj_invalidate(lv_screen_active());
    reveal_skipped_popup(ctx);
    open_popup_stream(ctx);
  }
}

// The video area turns black and the popup is kept, the screen black at once
// until the first full frame, then the Bridge asked for frames in the panel's
// own size. The user wants black rather than a still frame either way (b318's
// enlarged popup frame was smaller and stood still; back, the kept last frame
// stood still): the kept popup comes back with the black video area. In the
// loop: lv_refr_now first, or an area LVGL still owes would land on the full
// screen.
static void enter_full_screen() {
#if defined(DEVICE_ESP32_S3_RGB_480)
  enter_soft_full_screen();
  return;
#endif
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->visible || ctx->full) return;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t turn = 0;
  if (!Device::displayFullFrameInfo(width, height, turn)) return;
  lv_display_t* display = lv_display_get_default();
  // The popup's frames go with its stream; its video area drawn black.
  lv_image_set_src(ctx->image, nullptr);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(lv_obj_get_parent(ctx->image));
  lv_refr_now(display);
  if (display) lv_display_enable_invalidation(display, false);
  ctx->pending_url = String();
  if (!Device::displayBeginFullFrames()) {
    Serial.println("[Camera] Full screen unavailable; the popup stays");
    // The running popup stream shows its next frame again.
    if (display) lv_display_enable_invalidation(display, true);
    lv_obj_invalidate(ctx->card);
    return;
  }
  camera_stream_stop();
  ctx->full = true;
  ctx->full_width = width;
  ctx->full_height = height;
  ctx->full_turn = turn;
  if (ctx->full_touch) {
    lv_obj_clear_flag(ctx->full_touch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->full_touch);
  }
  ctx->waiting_for_bridge = true;
  ctx->bridge_response_deadline_ms = millis() + kFullScreenResponseTimeoutMs;
  note_switch(ctx, "full screen black on screen");
  ctx->switch_frame_logged = false;
  Serial.printf("[Camera] Full screen: %ux%u frames turned %u clockwise\n",
                static_cast<unsigned>(width), static_cast<unsigned>(height),
                static_cast<unsigned>(turn));
  mqttPublishCameraFullScreenOpen(ctx->entity_id.c_str(), camera_geometry::kFps,
                                  width, height, turn);
}

lv_obj_t* plain_part(lv_obj_t* parent);
static void close_menu(CameraPopupContext* ctx);
static void free_menu_shadow(CameraPopupContext* ctx, bool delete_objects);

static void close_camera_popup() {
  if (!g_camera_popup || !g_camera_popup->visible) return;
  leave_full_screen(false);
  close_menu(g_camera_popup);
  free_menu_shadow(g_camera_popup, true);
  camera_stream_keep_jpeg(false);
  g_camera_popup->keeping = false;
  take_kept_jpeg(g_camera_popup);
  hide_kept_picture(g_camera_popup);
  g_camera_popup->pending_url = String();
  g_camera_popup->resuming = false;
  g_camera_popup->switch_started_ms = 0;
  // A full screen asked for but not reached yet must not open with the next
  // popup.
  g_camera_popup->full_request = 0;
  g_camera_popup->open_full = false;
  g_camera_popup->close_on_leave = false;
  const bool shell_shown = !g_camera_popup->shell_skipped;
  g_camera_popup->shell_skipped = false;
  camera_transport::popup_closed();
  const String entity_id = g_camera_popup->entity_id;
  g_camera_popup->visible = false;

  // LVGL must no longer reference a PSRAM frame before the decoder task is
  // asked to release its frame buffers.
  if (g_camera_popup->image) {
    lv_image_set_src(g_camera_popup->image, nullptr);
    lv_obj_add_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN);
  }
  camera_stream_stop();

  if (entity_id.length()) {
    mqttPublishCameraCommand(entity_id.c_str(), "close");
  }
  if (shell_shown) {
    hide_popup_shell(g_camera_popup->card);
  } else {
    // Straight from a full screen without the popup: the tiles drawn again
    // where its frames were (leave_full_screen redraws only around the card).
    lv_obj_invalidate(lv_screen_active());
  }
  cancel_popup_open(g_camera_popup->card);
  lv_obj_add_flag(g_camera_popup->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_camera_popup->overlay, LV_OBJ_FLAG_CLICKABLE);

  // Closing usually runs from an LVGL button callback. Reallocating LVGL draw
  // buffers there would call lv_refr_now() recursively from lv_timer_handler().
  // Defer the swap to process_camera_popup() in the next normal loop pass.
  if (g_camera_popup->large_draw_buffer_active) {
    g_camera_popup->draw_buffer_restore_pending = true;
    g_camera_popup->draw_buffer_restore_retry_at_ms = 0;
  }
}

static void close_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_RELEASED) {
    close_camera_popup();
  }
}

static void overlay_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  (void)event;
}

// ---- The menu ("..."): the tile's other entries ------------------------------

// The box's shadow as pictures, freed with the popup (media_popup.cpp
// build_menu_shadow, which measured why): drawn by LVGL at the box's own radius it
// pushes the card's shadow corner out of LVGL's one-entry shadow cache, and every
// band of a frame computes both corners again. LVGL draws it once around a small
// probe; its four corners and a short piece of each side stay as alpha pictures.
constexpr int32_t kMenuShadowSidePiece = 16;  // px along a side, tiled

static void free_menu_shadow(CameraPopupContext* ctx, bool delete_objects) {
  if (ctx->menu_shadow && delete_objects) lv_obj_delete(ctx->menu_shadow);
  ctx->menu_shadow = nullptr;
  ctx->menu_shadow_rows = 0;
  for (lv_draw_buf_t*& part : ctx->menu_shadow_parts) {
    if (!part) continue;
    lv_image_cache_drop(part);
    lv_draw_buf_destroy(part);
    part = nullptr;
  }
}

static void build_menu_shadow(CameraPopupContext* ctx, int box_x, int box_y, int box_w, int box_h) {
  free_menu_shadow(ctx, true);
  const int32_t card_radius = lv_obj_get_style_radius(ctx->card, LV_PART_MAIN);
  const int32_t radius = lv_obj_get_style_radius(ctx->menu_box, LV_PART_MAIN);
  const int32_t blur = popup_layout::shadow_width_for(card_radius);
  const int32_t corner = blur + radius;
  lv_obj_t* probe = plain_part(ctx->root);
  lv_obj_set_size(probe, 2 * corner + kMenuShadowSidePiece, 2 * corner + kMenuShadowSidePiece);
  lv_obj_set_style_radius(probe, radius, 0);
  popup_layout::apply_card_shadow(probe);
  lv_obj_set_style_shadow_width(probe, blur, 0);
  lv_obj_update_layout(probe);
  const int32_t ext = lv_obj_get_ext_draw_size(probe);
  lv_draw_buf_t* snap = lv_snapshot_take(probe, LV_COLOR_FORMAT_A8);
  lv_obj_delete(probe);
  if (!snap) return;
  const int32_t sw = snap->header.w;
  const int32_t sh = snap->header.h;
  const int32_t piece = kMenuShadowSidePiece;
  const lv_area_t from[8] = {{0, 0, corner - 1, corner - 1},
                             {sw - corner, 0, sw - 1, corner - 1},
                             {0, sh - corner, corner - 1, sh - 1},
                             {sw - corner, sh - corner, sw - 1, sh - 1},
                             {corner, 0, corner + piece - 1, ext - 1},
                             {corner, sh - ext, corner + piece - 1, sh - 1},
                             {0, corner, ext - 1, corner + piece - 1},
                             {sw - ext, corner, sw - 1, corner + piece - 1}};
  const int32_t w = box_w + 2 * ext;
  const int32_t h = box_h + 2 * ext;
  const lv_area_t to[8] = {{0, 0, corner - 1, corner - 1},
                           {w - corner, 0, w - 1, corner - 1},
                           {0, h - corner, corner - 1, h - 1},
                           {w - corner, h - corner, w - 1, h - 1},
                           {corner, 0, w - corner - 1, ext - 1},
                           {corner, h - ext, w - corner - 1, h - 1},
                           {0, corner, ext - 1, h - corner - 1},
                           {w - ext, corner, w - 1, h - corner - 1}};
  ctx->menu_shadow = plain_part(ctx->root);
  lv_obj_set_pos(ctx->menu_shadow, box_x - ext, box_y - ext);
  lv_obj_set_size(ctx->menu_shadow, w, h);
  lv_obj_add_flag(ctx->menu_shadow, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < 8; ++i) {
    const lv_area_t& a = from[i];
    const int32_t pw = lv_area_get_width(&a);
    const int32_t ph = lv_area_get_height(&a);
    lv_draw_buf_t* part = lv_draw_buf_create(pw, ph, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    if (!part) continue;
    for (int32_t y = 0; y < ph; ++y) {
      memcpy(part->data + y * part->header.stride, snap->data + (a.y1 + y) * snap->header.stride + a.x1, pw);
    }
    ctx->menu_shadow_parts[i] = part;
    lv_obj_t* image = lv_image_create(ctx->menu_shadow);
    lv_image_set_src(image, part);
    lv_obj_set_pos(image, to[i].x1, to[i].y1);
    lv_obj_set_size(image, lv_area_get_width(&to[i]), lv_area_get_height(&to[i]));
    if (i >= 4) lv_image_set_inner_align(image, LV_IMAGE_ALIGN_TILE);
    lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_draw_buf_destroy(snap);
}

static void set_shown_menu_parts(CameraPopupContext* ctx, bool shown) {
  set_shown(ctx->menu_shadow, shown);
  set_shown(ctx->menu_box, shown);
  if (shown) lv_obj_add_flag(ctx->menu_shield, LV_OBJ_FLAG_CLICKABLE);
  else lv_obj_remove_flag(ctx->menu_shield, LV_OBJ_FLAG_CLICKABLE);
}

static void close_menu(CameraPopupContext* ctx) {
  if (!ctx || !ctx->menu_open) return;
  ctx->menu_open = false;
  set_shown_menu_parts(ctx, false);
}

// Switching keeps the popup: the other entity's stream, its last picture at once.
static void open_entry(CameraPopupContext* ctx, size_t index) {
  const camera_entries::Entry& entry = ctx->entries[index];
  camera_stream_keep_jpeg(false);
  ctx->keeping = false;
  take_kept_jpeg(ctx);
  if (ctx->entity_id.length()) mqttPublishCameraCommand(ctx->entity_id.c_str(), "close");
  lv_image_set_src(ctx->image, nullptr);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);
  ctx->pending_url = String();
  camera_stream_stop();
  const String previous = ctx->entity_id;
  const bool radar_before = is_radar(ctx);
  ctx->entry_index = index;
  ctx->entity_id = entry.entity.c_str();
  hometiles_title::set(ctx->title_label, camera_entry_title(entry).c_str());
  String icon_name = camera_entry_icon(entry);
  if (!icon_name.length()) icon_name = "video";
  lv_label_set_text(ctx->icon_label, getMdiChar(icon_name).c_str());
  set_playing(ctx, false);
  ctx->source_retries = 0;
  // The bottom row and the head stay while switching (user 2026-11-07: they came
  // back later each time): every radar place has the same times; the Bridge's
  // answer corrects them.
  if (radar_before && is_radar(ctx) && ctx->frame_count) {
    ctx->shown_frame = ctx->frame_count - 1;
  } else {
    ctx->frame_count = 0;
    ctx->shown_frame = 0;
    lv_label_set_text(ctx->value_label, "");
  }
  ctx->frame_dirty = false;
  ctx->live_kind = false;
  ctx->status_sticky = false;
  set_shown(ctx->status, false);
  default_frame_size(ctx);
  if (!show_kept_picture(ctx, ctx->entity_id, true)) show_kept_picture(ctx, previous, false);
  apply_layout(ctx);
  show_bar(ctx);
  ctx->opened_ms = millis();
  ctx->open_logged = false;
  camera_popup_set_status(camera_text().camera_bridge_requesting, false);
  open_popup_stream(ctx);
  if (ctx->visible && !ctx->shell_skipped) sync_popup_shell();
  if (ctx->remember_key) camera_tile_entry_chosen(ctx->remember_key, index);
}

static void menu_row_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  close_menu(ctx);
  if (index != ctx->entry_index && index < ctx->entries.size()) open_entry(ctx, index);
}

static void menu_shield_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) close_menu(g_camera_popup);
}

// The rows are built once per opening of the popup; deleted only here, never from
// their own click.
static void build_menu_rows(CameraPopupContext* ctx) {
  lv_color_t control = {};
  lv_opa_t control_opa = 0;
  popup_shell_control_fill(ctx->surface_color, ctx->icon_color, control, control_opa);
  lv_obj_clean(ctx->menu_list);
  const int pad = settings_style::kRowPadLeft - settings_style::kOptionInset;
  for (size_t i = 0; i < ctx->entries.size(); ++i) {
    lv_obj_t* row = plain_part(ctx->menu_list);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_PCT(100), kMenuRow - kMenuRowGap);
    settings_style::apply_radius(row, settings_style::kGroupRadius - settings_style::kOptionInset);
    lv_obj_set_style_bg_color(row, control, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row, control_opa, LV_PART_MAIN | LV_STATE_PRESSED);
    popup_nav_style::no_press_filter(row, LV_PART_MAIN | LV_STATE_PRESSED);
    disable_pressed_button_animation(row);
    lv_obj_set_style_pad_left(row, pad, 0);
    lv_obj_set_style_pad_right(row, pad, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, Device::kGridGap, 0);
    lv_obj_t* icon = lv_label_create(row);
    lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(ui_theme::text_secondary()), 0);
    lv_label_set_text(icon, getMdiChar(camera_entry_list_icon(ctx->entries[i])).c_str());
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* label = lv_label_create(row);
    lv_obj_set_style_text_font(label, settings_style::row_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ui_theme::text()), 0);
    lv_label_set_text(label, camera_entry_title(ctx->entries[i]).c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, lv_font_get_line_height(settings_style::row_font()));
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_add_event_cb(row, menu_row_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }
  ctx->menu_rows_stale = false;
}

// A box inside the card as high as its entries (the Media popup's geometry): its
// edge halfway in the card's edge, a head row as high as a button, a hairline, the
// rows, and the menu button stays where it is.
static void open_menu(CameraPopupContext* ctx) {
  const int offset = ml::kEdge / 2;
  const int pad = ml::kEdge - offset;
  const int gap = Device::kGridGap;
  const size_t shown = ctx->entries.size() < kMenuRowsShown ? ctx->entries.size() : kMenuRowsShown;
  const int list_height = static_cast<int>(shown) * kMenuRow;
  const int top = kButtonTop - gap - list_height - gap - ml::kButton - pad;
  const int content = popup_layout::kCardWidth - 2 * ml::kEdge;
  const int32_t card_radius = lv_obj_get_style_radius(ctx->card, LV_PART_MAIN);
  lv_obj_set_pos(ctx->menu_box, offset, top);
  lv_obj_set_size(ctx->menu_box, popup_layout::kCardWidth - 2 * offset, popup_layout::kCardHeight - offset - top);
  lv_obj_set_style_radius(ctx->menu_box, card_radius > offset ? card_radius - offset : 0, 0);
  lv_obj_set_style_bg_color(ctx->menu_box, lv_color_hex(ctx->surface_color), 0);
  const auto& text = camera_text();
  const char kind = ctx->entries.empty() ? camera_entries::kHomeAssistant : ctx->entries[0].kind;
  lv_label_set_text(ctx->menu_title, kind == camera_entries::kWebcam           ? text.camera_source_webcam
                                     : kind == camera_entries::kHomeAssistant ? text.screensaver_picture_entity
                                                                              : text.camera_place);
  lv_obj_set_style_text_color(ctx->menu_title, lv_color_hex(ui_theme::text()), 0);
  lv_obj_set_pos(ctx->menu_title, pad + settings_style::kRowPadLeft - settings_style::kOptionInset,
                 pad + ml::kButton / 2 - lv_font_get_line_height(settings_style::row_font()) / 2);
  lv_obj_set_pos(ctx->menu_rule, pad, pad + ml::kButton + gap / 2);
  lv_obj_set_size(ctx->menu_rule, content, 1);
  lv_obj_set_style_bg_color(ctx->menu_rule, lv_color_hex(ui_theme::text()), 0);
  lv_obj_set_pos(ctx->menu_list, pad, pad + ml::kButton + gap);
  lv_obj_set_size(ctx->menu_list, content, list_height);
  if (ctx->menu_rows_stale) build_menu_rows(ctx);
  lv_color_t control = {};
  lv_opa_t control_opa = 0;
  popup_shell_control_fill(ctx->surface_color, ctx->icon_color, control, control_opa);
  const uint32_t rows = lv_obj_get_child_count(ctx->menu_list);
  for (uint32_t i = 0; i < rows; ++i) {
    lv_obj_set_style_bg_opa(lv_obj_get_child(ctx->menu_list, static_cast<int32_t>(i)),
                            i == ctx->entry_index ? control_opa : LV_OPA_TRANSP, 0);
  }
  lv_obj_scroll_to_y(ctx->menu_list, 0, LV_ANIM_OFF);
  if (!ctx->menu_shadow || ctx->menu_shadow_rows != static_cast<int>(shown)) {
    build_menu_shadow(ctx, offset, top, popup_layout::kCardWidth - 2 * offset, popup_layout::kCardHeight - offset - top);
    ctx->menu_shadow_rows = static_cast<int>(shown);
  }
  lv_obj_move_foreground(ctx->menu_box);
  lv_obj_move_foreground(ctx->menu_button);
  if (ctx->menu_shadow) lv_obj_move_to_index(ctx->menu_shadow, lv_obj_get_index(ctx->menu_box));
  set_shown_menu_parts(ctx, true);
  ctx->menu_open = true;
}

static void menu_button_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || lv_event_get_code(event) != LV_EVENT_CLICKED || ctx->entries.size() < 2) return;
  if (ctx->menu_open) close_menu(ctx);
  else open_menu(ctx);
}

static void video_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (g_camera_popup && g_camera_popup->visible && !g_camera_popup->full && !g_camera_popup->menu_open &&
      camera_stream_is_active()) {
    g_camera_popup->full_request = kFullEnter;
    g_camera_popup->switch_started_ms = millis();
  }
}

static void bar_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->bar || !ctx->frame_count) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_VALUE_CHANGED) {
    int32_t index = lv_slider_get_value(ctx->bar);
    if (index >= ctx->frame_count) index = ctx->frame_count - 1;
    set_playing(ctx, false);
    if (index == ctx->shown_frame) return;
    ctx->shown_frame = static_cast<uint8_t>(index);
    ctx->frame_dirty = true;
  } else if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
    return;
  }
  show_bar(ctx);
  show_head_value(ctx);
}

static void play_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || lv_event_get_code(event) != LV_EVENT_CLICKED || ctx->frame_count < 2) return;
  if (ctx->playing) {
    set_playing(ctx, false);
    return;
  }
  // The next picture at once, from the newest the oldest.
  set_playing(ctx, true);
  play_step(ctx);
}

static void full_touch_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (g_camera_popup && g_camera_popup->full) {
    g_camera_popup->full_request = g_camera_popup->close_on_leave ? kFullClose : kFullLeave;
    g_camera_popup->switch_started_ms = millis();
  }
}

// A theme change deletes the popup (popup_shell_delete_popups) while it is
// idle (camera_popup_is_busy holds the change): its full-screen layers on the
// top layer go with it; preload_camera_popup builds it again.
static void overlay_deleted_cb(lv_event_t* e) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  auto* ctx = static_cast<CameraPopupContext*>(lv_event_get_user_data(e));
  if (!ctx) return;
  hide_kept_picture(ctx);
  free_menu_shadow(ctx, false);
  if (ctx->full_touch) lv_obj_delete(ctx->full_touch);
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (ctx->soft_full) lv_obj_delete(ctx->soft_full);
#endif
  if (g_camera_popup == ctx) g_camera_popup = nullptr;
  delete ctx;
}

lv_obj_t* plain_part(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  return obj;
}

static lv_obj_t* control_button(lv_obj_t* parent, const char* icon, lv_obj_t** label_out) {
  lv_obj_t* button = plain_part(parent);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(button, ml::kButton, ml::kButton);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  disable_pressed_button_animation(button);
  lv_obj_t* label = lv_label_create(button);
  lv_obj_set_style_text_font(label, FONT_MDI_ICONS, 0);
  lv_label_set_text(label, getMdiChar(icon).c_str());
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(label);
  if (label_out) *label_out = label;
  return button;
}

// The bottom row (mockup webcam-radar/radar-tall.html): Play on the left, the time
// bar as in the Media popup (make_slider, media_popup.cpp) with the knob travelling
// inside it and the times under it, the menu button in the Media popup's corner.
// Knob and times centred on the row.
static void build_controls(CameraPopupContext* ctx) {
  ctx->root = plain_part(ctx->card);
  lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(ctx->root, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_set_pos(ctx->root, -popup_layout::kCardPad, -popup_layout::kCardPad);

  const int line = lv_font_get_line_height(popup_layout::font20());
  const int knob_top = kButtonTop + (ml::kButton - (ml::kSeekKnob + kBarTimeGap + line)) / 2;
  const int width = popup_layout::kCardWidth - ml::kEdge - kBarLeft;
  const int bar_y = knob_top + ml::kSeekKnob / 2 - ml::kSeekHeight / 2;
  ctx->bar_track = plain_part(ctx->root);
  lv_obj_set_pos(ctx->bar_track, kBarLeft, bar_y);
  lv_obj_set_size(ctx->bar_track, width, ml::kSeekHeight);
  lv_obj_set_style_radius(ctx->bar_track, LV_RADIUS_CIRCLE, 0);
  ctx->bar_cap = plain_part(ctx->root);
  lv_obj_set_pos(ctx->bar_cap, kBarLeft, bar_y);
  lv_obj_set_size(ctx->bar_cap, ml::kSeekKnob, ml::kSeekHeight);
  lv_obj_set_style_radius(ctx->bar_cap, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ctx->bar_cap, LV_OPA_COVER, 0);
  ctx->bar = lv_slider_create(ctx->root);
  lv_obj_remove_style_all(ctx->bar);
  lv_obj_set_pos(ctx->bar, kBarLeft, bar_y);
  lv_obj_set_size(ctx->bar, width, ml::kSeekHeight);
  lv_obj_set_style_pad_left(ctx->bar, ml::kSeekKnob / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_right(ctx->bar, ml::kSeekKnob / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ctx->bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(ctx->bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(ctx->bar, (ml::kSeekKnob - ml::kSeekHeight) / 2, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(ctx->bar, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(ctx->bar, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_ext_click_area(ctx->bar, popup_layout::scale(18));
  lv_obj_remove_flag(ctx->bar, LV_OBJ_FLAG_SCROLLABLE);
  for (lv_event_code_t code : {LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
    lv_obj_add_event_cb(ctx->bar, bar_event_cb, code, ctx);
  }

  const int time_top = knob_top + ml::kSeekKnob + kBarTimeGap;
  for (lv_obj_t** label : {&ctx->time_shown, &ctx->time_newest}) {
    *label = lv_label_create(ctx->root);
    lv_obj_set_style_text_font(*label, popup_layout::font20(), 0);
    lv_label_set_text(*label, "");
    lv_obj_remove_flag(*label, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_pos(ctx->time_shown, kBarLeft, time_top);
  lv_obj_align(ctx->time_newest, LV_ALIGN_TOP_RIGHT, -ml::kEdge, time_top);

  ctx->play_button = control_button(ctx->root, "play", &ctx->play_label);
  lv_obj_set_pos(ctx->play_button, ml::kEdge, kButtonTop);
  lv_obj_add_event_cb(ctx->play_button, play_event_cb, LV_EVENT_CLICKED, ctx);

  // The menu: a tap beside its box closes it, the box itself keeps a tap.
  ctx->menu_shield = plain_part(ctx->root);
  lv_obj_set_size(ctx->menu_shield, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_add_event_cb(ctx->menu_shield, menu_shield_event_cb, LV_EVENT_CLICKED, ctx);
  ctx->menu_box = plain_part(ctx->root);
  lv_obj_add_flag(ctx->menu_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(ctx->menu_box, LV_OPA_COVER, 0);
  lv_obj_add_flag(ctx->menu_box, LV_OBJ_FLAG_HIDDEN);
  ctx->menu_title = lv_label_create(ctx->menu_box);
  lv_obj_set_style_text_font(ctx->menu_title, settings_style::row_font(), 0);
  lv_label_set_text(ctx->menu_title, "");
  lv_obj_remove_flag(ctx->menu_title, LV_OBJ_FLAG_CLICKABLE);
  ctx->menu_rule = plain_part(ctx->menu_box);
  lv_obj_set_style_bg_opa(ctx->menu_rule, settings_style::kSeparatorOpa, 0);
  ctx->menu_list = plain_part(ctx->menu_box);
  lv_obj_set_flex_flow(ctx->menu_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(ctx->menu_list, kMenuRowGap, 0);
  lv_obj_clear_flag(ctx->menu_list, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_flag(ctx->menu_list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                                                            LV_OBJ_FLAG_SCROLL_MOMENTUM));
  lv_obj_set_scroll_dir(ctx->menu_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ctx->menu_list, LV_SCROLLBAR_MODE_OFF);
  ctx->menu_button = control_button(ctx->root, "dots-horizontal", &ctx->menu_label);
  lv_obj_set_pos(ctx->menu_button, popup_layout::kCardWidth - ml::kEdge - ml::kButton, kButtonTop);
  lv_obj_add_event_cb(ctx->menu_button, menu_button_event_cb, LV_EVENT_CLICKED, ctx);
  lv_obj_add_flag(ctx->menu_button, LV_OBJ_FLAG_HIDDEN);
}

// The colors follow the opener's card and icon, as in the Media popup.
static void style_controls(CameraPopupContext* ctx, uint32_t icon_color) {
  if (!ctx->bar) return;
  lv_color_t control = {};
  lv_opa_t control_opa = 0;
  bool tinted = false;
  popup_shell_control_fill(ctx->surface_color, icon_color, control, control_opa, &tinted);
  const lv_color_t fill = lv_color_hex(tinted ? icon_color : ui_theme::icon());
  lv_obj_set_style_bg_color(ctx->bar_track, control, 0);
  lv_obj_set_style_bg_opa(ctx->bar_track, control_opa, 0);
  lv_obj_set_style_bg_color(ctx->bar_cap, fill, 0);
  lv_obj_set_style_bg_color(ctx->bar, fill, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ctx->bar, lv_color_hex(ui_theme::icon()), LV_PART_KNOB);
  for (lv_obj_t* label : {ctx->time_shown, ctx->time_newest}) {
    lv_obj_set_style_text_color(label, lv_color_hex(ui_theme::text_secondary()), 0);
  }
  // Play in the circle's color with the icon role (user 2026-11-06): dark the
  // tone with a white icon, light the pale tone with the dark icon.
  lv_color_t raised = {};
  lv_opa_t raised_opa = 0;
  popup_shell_control_raised_fill(ctx->surface_color, icon_color, raised, raised_opa);
  lv_obj_set_style_bg_color(ctx->play_button, control, 0);
  lv_obj_set_style_bg_opa(ctx->play_button, control_opa, 0);
  lv_obj_set_style_bg_color(ctx->play_button, raised, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ctx->play_button, raised_opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(ctx->play_button, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(ctx->play_label, lv_color_hex(ui_theme::icon()), 0);
  // The menu button: a circle only while pressed.
  lv_obj_set_style_bg_color(ctx->menu_button, control, 0);
  lv_obj_set_style_bg_opa(ctx->menu_button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(ctx->menu_button, control_opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(ctx->menu_button, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(ctx->menu_label, lv_color_hex(ui_theme::icon()), 0);
}

static CameraPopupContext* create_popup() {
  CameraPopupContext* ctx = new CameraPopupContext();

  const auto parts = create_popup_body(close_event_cb, ctx, ui_theme::popup_card());
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* close_button = parts.close;
  ctx->value_label = lv_label_create(ctx->card);
  lv_obj_add_flag(ctx->value_label, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(ctx->value_label, "");
  lv_obj_set_style_pad_all(ctx->overlay, 0, 0);
  lv_obj_add_event_cb(ctx->overlay, overlay_event_cb, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(ctx->overlay, overlay_deleted_cb, LV_EVENT_DELETE, ctx);

  lv_obj_t* video = lv_obj_create(ctx->card);
  ctx->video = video;
  lv_obj_set_size(video, kVideoFrameWidth, kVideoHeight);
  lv_obj_set_style_bg_color(video, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(video, LV_OPA_COVER, 0);
  ui_surface_style::apply_radius(video, camera_geometry::kCornerRadius, 0);
  // Camera frames carry their own small rounded-corner mask. Generic child
  // clipping makes every full video redraw pixel-bound and stalls the P4 UI.
  lv_obj_set_style_clip_corner(video, false, 0);
  lv_obj_set_style_border_width(video, 0, 0);
  lv_obj_set_style_shadow_width(video, 0, 0);
  lv_obj_set_style_pad_all(video, 0, 0);
  lv_obj_remove_flag(video, LV_OBJ_FLAG_SCROLLABLE);
  // A tap on the video shows it full screen where the panel can (#65).
  lv_obj_add_flag(video, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(video, video_event_cb, LV_EVENT_CLICKED, ctx);
  ctx->full_touch = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ctx->full_touch);
  lv_obj_set_size(ctx->full_touch, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(ctx->full_touch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ctx->full_touch, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(ctx->full_touch, full_touch_event_cb, LV_EVENT_CLICKED, ctx);
#if defined(DEVICE_ESP32_S3_RGB_480)
  ctx->soft_full = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ctx->soft_full);
  lv_obj_set_size(ctx->soft_full, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(ctx->soft_full, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ctx->soft_full, LV_OPA_COVER, 0);
  lv_obj_remove_flag(ctx->soft_full, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->soft_full, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ctx->soft_full, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(ctx->soft_full, full_touch_event_cb, LV_EVENT_CLICKED, ctx);
  // The frame's own size (the whole picture without bars, Bridge fit
  // "inside"), centred on the black: only the picture is drawn each frame.
  ctx->soft_full_image = lv_image_create(ctx->soft_full);
  lv_obj_set_size(ctx->soft_full_image, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_center(ctx->soft_full_image);
  lv_obj_add_flag(ctx->soft_full_image, LV_OBJ_FLAG_HIDDEN);
#endif

  // The last picture lies under the stream's image until its first frame.
  ctx->kept_image = lv_image_create(video);
  lv_obj_set_size(ctx->kept_image, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_center(ctx->kept_image);
  lv_obj_remove_flag(ctx->kept_image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ctx->kept_image, LV_OBJ_FLAG_HIDDEN);

  ctx->image = lv_image_create(video);
  // The bridge delivers the native frame size requested by this device.
  // No LVGL scaling or letterboxing is needed.
  lv_obj_set_size(ctx->image, kVideoFrameWidth, kVideoHeight);
  lv_image_set_inner_align(ctx->image, LV_IMAGE_ALIGN_CENTER);
  lv_obj_center(ctx->image);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);

  // In the video area, only for an error or a message that must stay
  // (process_camera_popup): no words while a picture comes.
  ctx->status = lv_label_create(video);
  lv_obj_set_width(ctx->status, LV_PCT(90));
  lv_obj_set_style_text_align(ctx->status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ctx->status, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(ctx->status, lv_color_hex(ui_theme::text_secondary()), 0);
  lv_label_set_long_mode(ctx->status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(ctx->status, "");
  lv_obj_align(ctx->status, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(ctx->status, LV_OBJ_FLAG_HIDDEN);

  build_controls(ctx);
  apply_layout(ctx);
  show_bar(ctx);

  lv_obj_move_foreground(ctx->icon_label);
  lv_obj_move_foreground(ctx->title_label);
  lv_obj_move_foreground(close_button);
  hide_popup_shell(ctx->card);
  cancel_popup_open(ctx->card);
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  return ctx;
}

}  // namespace

// Starts the stream the Bridge announced. While the previous stream's task
// still ends (stop is asynchronous), the URL waits for process_camera_popup.
static void start_camera_stream(const String& url, bool full) {
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->visible || full != ctx->full) return;
  if (camera_stream_is_active()) {
    ctx->pending_url = url;
    ctx->pending_full = full;
    return;
  }
  ctx->pending_url = String();
  // The S3 keeps its internal draw band: a PSRAM band as tall as the video
  // would add PSRAM traffic next to its RGB scan-out (and the frames are
  // drawn by LVGL there anyway).
#if defined(DEVICE_ESP32_S3_RGB_480)
  constexpr bool kLargeDrawBuffer = false;
#else
  constexpr bool kLargeDrawBuffer = true;
#endif
  if (kLargeDrawBuffer && !full && !ctx->large_draw_buffer_active) {
    const size_t previous_requested_lines =
        displayManager.getRequestedBufferLines() != 0
            ? displayManager.getRequestedBufferLines()
            : displayManager.getBufferLines();
    const bool previous_fast =
        displayManager.isUsingFastInternalBuffer();
    if (displayManager.setSinglePsramBufferLines(camera_geometry::kBoxHeight)) {
      ctx->previous_draw_buffer_requested_lines =
          previous_requested_lines;
      ctx->previous_draw_buffer_fast = previous_fast;
      ctx->large_draw_buffer_active = true;
    } else {
      Serial.println(
          "[Camera] Large LVGL draw buffer unavailable; "
          "normal safe rendering path remains active");
    }
  }
  const bool fast = camera_transport::request().fast;
  bool started = camera_stream_start(url.c_str(), ctx->surface_color, full, ctx->frame_w, ctx->frame_h);
  // The stream comes before the kept picture when PSRAM is short.
  if (!started && ctx->kept_dsc) {
    Serial.println("[Camera] Stream did not start; the kept picture makes room");
    hide_kept_picture(ctx);
    started = camera_stream_start(url.c_str(), ctx->surface_color, full, ctx->frame_w, ctx->frame_h);
    if (started) camera_popup_set_status(camera_text().camera_connecting, false);
  }
  if (started) {
    ctx->stream_fast = fast;
    camera_transport::stream_started(fast);
    ctx->keeping = !full && !ctx->live_kind;
    return;
  }
  if (full) {
    leave_full_screen(true);
    return;
  }
  if (ctx->large_draw_buffer_active && !restore_previous_draw_buffer(ctx)) {
    ctx->draw_buffer_restore_pending = true;
    ctx->draw_buffer_restore_retry_at_ms = millis() + 2000;
  }
}

void preload_camera_popup() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::theme_scope();
  if (!g_camera_popup) g_camera_popup = create_popup();
}

static void finish_camera_popup_open(const CameraPopupInit& init) {
  if (!g_camera_popup || !g_camera_popup->visible) return;
  if (g_camera_popup->open_full) {
    // Straight to the full screen (process_camera_popup, once the popup is
    // open); a popup stream first would only be stopped again.
    g_camera_popup->full_request = kFullEnter;
    g_camera_popup->switch_started_ms = millis();
    return;
  }
  g_camera_popup->bridge_response_deadline_ms = millis() + kBridgeResponseTimeoutMs;
  g_camera_popup->requested_fps = camera_geometry::kFps;
  mqttPublishCameraCommand(init.entity_id.c_str(), "open",
                           g_camera_popup->requested_fps);
}

void show_camera_popup(const CameraPopupInit& init) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  hide_pin_popup();
  hide_camera_popup();
  if (!init.entity_id.length()) return;
  hide_cover_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_light_popup();
  hide_media_popup();
  hide_device_popup();
  hide_vacuum_popup();
  hide_calendar_popup();
  hide_climate_popup();

  if (!g_camera_popup) g_camera_popup = create_popup();
  if (!g_camera_popup) return;

  // A different camera may be opened before the deferred restore runs. Reuse
  // the existing large buffer and retain the original small-buffer size.
  g_camera_popup->draw_buffer_restore_pending = false;
  g_camera_popup->resuming = false;
  g_camera_popup->open_full = g_open_next_full || init.full_screen;
  g_camera_popup->close_on_leave = init.full_screen;
  g_camera_popup->shell_skipped = init.full_screen;
  g_open_next_full = false;
  g_camera_popup->entity_id = init.entity_id;
  g_camera_popup->visible = true;
  g_camera_popup->surface_color =
      init.bg_color ? init.bg_color : ui_theme::popup_card();
  lv_obj_set_style_bg_color(
      g_camera_popup->card,
      lv_color_hex(g_camera_popup->surface_color), 0);

  g_camera_popup->entries = camera_entries::parse(init.entries.c_str());
  g_camera_popup->entry_index = camera_entries::index_of(g_camera_popup->entries, init.entity_id.c_str());
  g_camera_popup->remember_key = init.remember_key;
  g_camera_popup->menu_rows_stale = true;
  g_camera_popup->menu_open = false;
  set_shown_menu_parts(g_camera_popup, false);
  g_camera_popup->icon_color = init.icon_color;

  String title = init.title;
  title.trim();
  if (!title.length()) title = init.entity_id;
  hometiles_title::set(g_camera_popup->title_label, title.c_str());

  String icon_name = normalizeMdiIconName(init.icon_name);
  if (!icon_name.length()) icon_name = "video";
  lv_label_set_text(g_camera_popup->icon_label,
                    getMdiChar(icon_name).c_str());
  lv_obj_set_style_text_color(g_camera_popup->icon_label, lv_color_hex(init.icon_color), 0);
  popup_layout::alignHeader(g_camera_popup->card, g_camera_popup->title_label, g_camera_popup->icon_label);

  // A new opening: the newest frame, no history until the Bridge tells it; the
  // last picture of a still camera shows at once in its size.
  set_playing(g_camera_popup, false);
  g_camera_popup->source_retries = 0;
  g_camera_popup->source_retry_at_ms = 0;
  g_camera_popup->frame_count = 0;
  g_camera_popup->shown_frame = 0;
  g_camera_popup->frame_dirty = false;
  g_camera_popup->live_kind = false;
  g_camera_popup->status_sticky = false;
  set_shown(g_camera_popup->status, false);
  g_camera_popup->bridge_lost_ms = 0;
  g_camera_popup->bridge_lost_told = false;
  lv_label_set_text(g_camera_popup->value_label, "");
  style_controls(g_camera_popup, init.icon_color);
  default_frame_size(g_camera_popup);
  // Not on the way to the full screen.
  if (!g_camera_popup->open_full) show_kept_picture(g_camera_popup, g_camera_popup->entity_id, true);
  apply_layout(g_camera_popup);
  show_bar(g_camera_popup);

  lv_image_set_src(g_camera_popup->image, nullptr);
  lv_obj_add_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN);
  g_camera_popup->opened_ms = millis();
  g_camera_popup->open_logged = false;
  camera_popup_set_status(camera_text().camera_bridge_requesting, false);
  g_camera_popup->waiting_for_bridge = true;
  g_camera_popup->bridge_response_deadline_ms =
      millis() + kBridgeResponseTimeoutMs;
  displayManager.resetActivityTimer();
  if (g_camera_popup->shell_skipped) {
    // Straight to the full screen (process_camera_popup); the card stays
    // hidden unless the full screen cannot start.
    lv_obj_add_flag(g_camera_popup->card, LV_OBJ_FLAG_HIDDEN);
    finish_camera_popup_open(init);
    viewNavigationPopupShown(g_camera_popup->card, init.entity_id.c_str());
    return;
  }
  lv_obj_clear_flag(g_camera_popup->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_camera_popup->overlay, LV_OBJ_FLAG_CLICKABLE);

  if (!defer_popup_body(g_camera_popup->card, g_camera_popup->title_label,
                        g_camera_popup->icon_label, g_camera_popup->close_button,
                        init, finish_camera_popup_open, true)) finish_camera_popup_open(init);
  if (g_camera_popup && g_camera_popup->card) viewNavigationPopupShown(g_camera_popup->card, init.entity_id.c_str());
  show_popup_shell(g_camera_popup->overlay, g_camera_popup->card, g_camera_popup->title_label, g_camera_popup->icon_label, g_camera_popup->close_button, nullptr, g_camera_popup->value_label);
}

void hide_camera_popup() {
  close_camera_popup();
}

bool camera_popup_is_visible() {
  return g_camera_popup && g_camera_popup->visible;
}

void camera_popup_open_next_in_full_screen(bool full_screen) {
  g_open_next_full = full_screen;
}

bool camera_popup_is_full_screen() {
  return g_camera_popup && g_camera_popup->visible && g_camera_popup->full;
}

bool camera_popup_is_busy() {
  return g_camera_popup &&
         (g_camera_popup->visible ||
          g_camera_popup->draw_buffer_restore_pending ||
          g_camera_popup->large_draw_buffer_active);
}

// The same view again on a new stream: the full screen in its size, else the popup's.
static void reopen_view(CameraPopupContext* ctx) {
  if (ctx->full) {
    ctx->waiting_for_bridge = true;
    ctx->bridge_response_deadline_ms = millis() + kFullScreenResponseTimeoutMs;
    mqttPublishCameraFullScreenOpen(ctx->entity_id.c_str(), camera_geometry::kFps,
                                    ctx->full_width, ctx->full_height, ctx->full_turn);
  } else {
    open_popup_stream(ctx);
  }
}

// The connection to the Bridge (the direct link or MQTT). Without it nothing can be
// asked: the last picture stays without words, and once the connection is back the
// same view is asked for again. Only a connection gone for kBridgeLostTellMs is
// told (user 2026-11-07: "MQTT disconnected" while Home Assistant restarted).
static void process_bridge_link(CameraPopupContext* ctx) {
  const uint32_t now = millis();
  if (!networkManager.isMqttConnected()) {
    if (!ctx->bridge_lost_ms) {
      ctx->bridge_lost_ms = now ? now : 1;
      ctx->bridge_lost_told = false;
      Serial.println("[Camera] No connection to the Bridge; the popup waits for it");
    }
    // No answer can come before the connection: no "no response" meanwhile.
    if (ctx->waiting_for_bridge) ctx->bridge_response_deadline_ms = now + kBridgeResponseTimeoutMs;
    ctx->source_retry_at_ms = 0;
    if (!ctx->bridge_lost_told && now - ctx->bridge_lost_ms >= kBridgeLostTellMs) {
      ctx->bridge_lost_told = true;
      if (ctx->full) leave_full_screen(true);
      camera_popup_set_status(camera_text().camera_bridge_disconnected, true);
    }
    return;
  }
  if (!ctx->bridge_lost_ms) return;
  Serial.printf("[Camera] Bridge connection back after %u ms; asking again\n",
                static_cast<unsigned>(now - ctx->bridge_lost_ms));
  ctx->bridge_lost_ms = 0;
  ctx->bridge_lost_told = false;
  ctx->status_sticky = false;
  camera_popup_set_status(camera_text().camera_bridge_requesting, false);
  reopen_view(ctx);
}

void process_camera_popup() {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (!g_camera_popup) return;
  if (g_camera_popup->draw_buffer_restore_pending &&
      !g_camera_popup->visible) {
    const uint32_t now = millis();
    if (g_camera_popup->draw_buffer_restore_retry_at_ms == 0 ||
        static_cast<int32_t>(
            now - g_camera_popup->draw_buffer_restore_retry_at_ms) >= 0) {
      if (!restore_previous_draw_buffer(g_camera_popup)) {
        g_camera_popup->draw_buffer_restore_retry_at_ms = now + 2000;
        const size_t restore_lines =
            g_camera_popup->previous_draw_buffer_requested_lines;
        Serial.printf(
            "[Camera] Display buffer restore will be retried (%u lines)\n",
            static_cast<unsigned>(restore_lines));
      }
    }
  }
  if (!g_camera_popup->visible || popup_open_pending(g_camera_popup->card)) return;
  const uint8_t full_request = g_camera_popup->full_request;
  g_camera_popup->full_request = 0;
  if (full_request == kFullEnter) {
    enter_full_screen();
    // Opened for the full screen without a popup stream: where the full
    // screen cannot start, the popup's own stream runs instead (and a popup a
    // tile skipped shows after all).
    if (g_camera_popup->open_full && !g_camera_popup->full) {
      reveal_skipped_popup(g_camera_popup);
      open_popup_stream(g_camera_popup);
    }
    g_camera_popup->open_full = false;
  } else if (full_request == kFullLeave) {
    leave_full_screen(true);
  } else if (full_request == kFullClose) {
    close_camera_popup();
    return;
  }
#if !defined(DEVICE_ESP32_S3_RGB_480)
  if (g_camera_popup->full) {
    // A popup scene change elsewhere may have turned LVGL drawing on again:
    // the full screen must not get UI pixels.
    lv_display_t* display = lv_display_get_default();
    if (display && lv_display_is_invalidation_enabled(display)) {
      lv_display_enable_invalidation(display, false);
    }
  }
#endif
  if (g_camera_popup->pending_url.length() && !camera_stream_is_active()) {
    const String url = g_camera_popup->pending_url;
    start_camera_stream(url, g_camera_popup->pending_full);
  }
  process_bridge_link(g_camera_popup);
  if (g_camera_popup->waiting_for_bridge &&
      static_cast<int32_t>(
          millis() - g_camera_popup->bridge_response_deadline_ms) >= 0) {
    g_camera_popup->waiting_for_bridge = false;
    g_camera_popup->bridge_response_deadline_ms = 0;
    Serial.println(
        "[Camera] No camera response before the bridge deadline");
    if (g_camera_popup->full) {
      leave_full_screen(true);
    } else {
      camera_popup_set_status(camera_text().camera_bridge_no_response, true);
    }
  }
  if (powerManager.isInSleep()) {
    close_camera_popup();
    return;
  }
  // Under the open menu the picture stands: a new frame goes straight to the
  // panel and would cover the menu (P4).
  if (g_camera_popup->menu_open) {
  } else
#if defined(DEVICE_ESP32_S3_RGB_480)
  // The S3's full screen shows the frames in its own image (LVGL).
  if (g_camera_popup->full) {
    camera_stream_process_ui(g_camera_popup->soft_full_image, nullptr);
  } else
#endif
  camera_stream_process_ui(g_camera_popup->image,
                           g_camera_popup->resuming ? nullptr : g_camera_popup->status);
  // A still camera's newest picture, for the next opening.
  camera_stream_keep_jpeg(g_camera_popup->keeping && !g_camera_popup->full && !g_camera_popup->playing &&
                          !g_camera_popup->frame_dirty &&
                          g_camera_popup->shown_frame + 1 >= g_camera_popup->frame_count);
  take_kept_jpeg(g_camera_popup);
  if (!g_camera_popup->switch_frame_logged && camera_stream_first_frame_ms()) {
    g_camera_popup->switch_frame_logged = true;
    note_switch(g_camera_popup, g_camera_popup->full ? "first full-screen frame" : "first popup frame");
    g_camera_popup->switch_started_ms = 0;
  }
  // A fast stream that ended in a transport error: the 8 KB transport until
  // the next restart, and the same view again on it.
  if (camera_stream_take_transport_failure() && g_camera_popup->stream_fast) {
    g_camera_popup->stream_fast = false;
    camera_transport::fall_back("the stream ended in a transport error");
    reopen_view(g_camera_popup);
  }
  if (g_camera_popup->resuming && !lv_obj_has_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN)) {
    g_camera_popup->resuming = false;
  }
  process_time_bar(g_camera_popup);
  if (g_camera_popup->source_retry_at_ms &&
      static_cast<int32_t>(millis() - g_camera_popup->source_retry_at_ms) >= 0) {
    open_popup_stream(g_camera_popup);
  }
  // No words while a picture comes (Ready, Buffering): only an error or a message
  // that must stay.
  const bool no_frame = lv_obj_has_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN);
  if (!no_frame) hide_kept_picture(g_camera_popup, false);
  // While the connection is gone only its own message, once it lasted (not the
  // stream's "connection ended" from the same cause).
  const bool bridge_quiet = g_camera_popup->bridge_lost_ms && !g_camera_popup->bridge_lost_told;
  set_shown(g_camera_popup->status,
            !bridge_quiet && (g_camera_popup->status_sticky || camera_stream_status_is_error()));
  if (!no_frame && !g_camera_popup->open_logged) {
    g_camera_popup->open_logged = true;
    Serial.printf("[Camera] First frame shown %u ms after the opening\n",
                  static_cast<unsigned>(millis() - g_camera_popup->opened_ms));
  }
}

void camera_popup_handle_mqtt_status(const char* payload) {
  if (!payload || !*payload || !g_camera_popup ||
      !g_camera_popup->visible) {
    return;
  }

  // Room for a history's times (kMaxFrames) next to the stream's fields.
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, payload)) {
    camera_popup_set_status(camera_text().camera_invalid_response, true);
    return;
  }
  const char* entity = doc["entity_id"] | "";
  if (*entity &&
      !g_camera_popup->entity_id.equalsIgnoreCase(String(entity))) {
    return;
  }
  // New frames while the popup shows them: no answer to a pending request.
  if (strcmp(doc["status"] | "", "frames") == 0) {
    if (frames_in(doc.as<JsonVariantConst>()) && !keep_shown_frames(g_camera_popup, doc.as<JsonVariantConst>())) {
      const bool had_row = g_camera_popup->frame_count >= 2;
      take_frames(g_camera_popup, doc.as<JsonVariantConst>());
      if (had_row != (g_camera_popup->frame_count >= 2)) apply_layout(g_camera_popup);
      show_bar(g_camera_popup);
      show_head_value(g_camera_popup);
      // No controls in the full screen: a history plays by itself there.
      if (g_camera_popup->full && g_camera_popup->frame_count >= 2) set_playing(g_camera_popup, true);
    }
    return;
  }
  g_camera_popup->waiting_for_bridge = false;
  g_camera_popup->bridge_response_deadline_ms = 0;
  const JsonVariantConst protocol_field = doc["protocol_version"];
  if (protocol_field.isNull() || !protocol_field.is<uint8_t>()) {
    Serial.println("[Camera] Camera response has no valid protocol version");
    camera_popup_set_status(camera_text().camera_invalid_response, true);
    return;
  }
  const uint8_t protocol_version = protocol_field.as<uint8_t>();
  if (protocol_version != kRequiredBridgeCameraProtocol) {
    Serial.printf(
        "[Camera] Bridge camera protocol %u is incompatible "
        "(expected %u; Bridge v0.6.28+)\n",
        protocol_version, kRequiredBridgeCameraProtocol);
    camera_popup_set_status(
        camera_text().camera_bridge_update_required, true);
    return;
  }
  const char* status = doc["status"] | "";
  Serial.printf("[Camera] Bridge status: entity=%s status=%s\n",
                entity,
                status);
  if (strcmp(status, "ready") == 0) {
    const char* url = doc["url"] | "";
    if (!*url) {
      camera_popup_set_status(camera_text().camera_no_stream_url, true);
      return;
    }
    const uint16_t width = doc["width"] | 0;
    const uint16_t height = doc["height"] | 0;
    const uint8_t fps = doc["fps"] | 0;
    const char* transport = doc["transport"] | "";
    const char* framing = doc["framing"] | "";
    const bool full = g_camera_popup->full;
    const char* view = doc["view"] | "popup";
    const bool size_ok =
        full ? width == g_camera_popup->full_width &&
                   height == g_camera_popup->full_height &&
                   strcmp(view, "full") == 0
             : width >= 64 && width <= camera_geometry::kWidth && width % 8 == 0 &&
                   height >= 16 && height <= camera_geometry::kBoxHeight && height % 2 == 0;
    if (!size_ok ||
        fps < 1 || fps > camera_geometry::kFps ||
        strcmp(transport, "tcp-ack-v1") != 0 ||
        strcmp(framing, "ack-jpeg-v1") != 0) {
      Serial.printf(
          "[Camera] Invalid stream format: %ux%u@%u transport=%s "
          "framing=%s (expected at most %ux%u@<=%u tcp-ack-v1/ack-jpeg-v1)\n",
          width, height, fps, transport, framing,
          camera_geometry::kWidth, camera_geometry::kBoxHeight,
          camera_geometry::kFps);
      camera_popup_set_status(camera_text().camera_invalid_response, true);
      return;
    }
    Serial.printf("[Camera] Stream URL received (%u characters)\n",
                  static_cast<unsigned>(strlen(url)));
    note_switch(g_camera_popup, "Bridge ready");
    // What this camera is: a history brings its times, the bar and Play. A
    // frame picked before a new stream (back from the full screen) goes out again.
    g_camera_popup->live_kind = strcmp(doc["kind"] | "", "live") == 0;
    if (keep_shown_frames(g_camera_popup, doc.as<JsonVariantConst>())) {
      // The bar stays as it is.
    } else if (frames_in(doc.as<JsonVariantConst>())) {
      take_frames(g_camera_popup, doc.as<JsonVariantConst>());
      if (g_camera_popup->shown_frame + 1 < g_camera_popup->frame_count) g_camera_popup->frame_dirty = true;
    } else {
      g_camera_popup->frame_count = 0;
      g_camera_popup->shown_frame = 0;
    }
    if (!full) {
      // The frames' own shape; the picture centred in its area.
      g_camera_popup->frame_w = width;
      g_camera_popup->frame_h = height;
      apply_layout(g_camera_popup);
    }
    show_bar(g_camera_popup);
    show_head_value(g_camera_popup);
    // No controls in the full screen: a history plays by itself there.
    if (full && g_camera_popup->frame_count >= 2) set_playing(g_camera_popup, true);
    Serial.printf("[Camera] Bridge ready %u ms after the opening\n",
                  static_cast<unsigned>(millis() - g_camera_popup->opened_ms));
    camera_popup_set_status(camera_text().camera_connecting, false);
    start_camera_stream(String(url), full);
    return;
  }
  if (strcmp(status, "error") == 0) {
    const char* error = doc["error"] | "";
    if (g_camera_popup->full) {
      // A Bridge before full-screen frames, or a camera it cannot turn.
      Serial.printf("[Camera] Full screen refused by the Bridge: %s\n", error);
      leave_full_screen(true);
      return;
    }
    if (strcmp(error, "camera_invalid_stream_request") == 0 &&
        g_camera_popup->requested_fps > camera_geometry::kFallbackFps) {
      // Bridges before v0.7.1b9 accept at most 24 FPS: ask once more at 24.
      Serial.printf("[Camera] Bridge rejected %u FPS; asking again at %u\n",
                    static_cast<unsigned>(g_camera_popup->requested_fps),
                    static_cast<unsigned>(camera_geometry::kFallbackFps));
      g_camera_popup->requested_fps = camera_geometry::kFallbackFps;
      g_camera_popup->waiting_for_bridge = true;
      g_camera_popup->bridge_response_deadline_ms =
          millis() + kBridgeResponseTimeoutMs;
      mqttPublishCameraCommand(g_camera_popup->entity_id.c_str(), "open",
                               g_camera_popup->requested_fps);
      return;
    }
    Serial.printf("[Camera] Bridge error: %s\n", error);
    const bool own_source = g_camera_popup->entity_id.startsWith("camera.ht_");
    if (own_source && g_camera_popup->source_retries < kSourceRetries &&
        (strcmp(error, "camera_image_unavailable") == 0 || strcmp(error, "unknown_camera") == 0)) {
      g_camera_popup->source_retry_at_ms = millis() + kSourceRetryMs;
      // The first answer stays quiet: it may simply not be there yet.
      if (g_camera_popup->source_retries++ == 0) return;
    }
    camera_popup_set_status(localize_camera_error(error), true);
    return;
  }
  if (strcmp(status, "stopped") == 0) {
    leave_full_screen(false);
    g_camera_popup->status_sticky = true;
    camera_popup_set_status(camera_text().camera_stream_stopped, false);
  }
}

void camera_popup_set_status(const char* text, bool error) {
  if (error && g_camera_popup) {
    g_camera_popup->waiting_for_bridge = false;
    g_camera_popup->bridge_response_deadline_ms = 0;
    g_camera_popup->resuming = false;
  }
  if (!error && g_camera_popup && g_camera_popup->resuming) {
    camera_stream_set_external_status(text, error);
    return;
  }
  set_status_label(text, error);
  camera_stream_set_external_status(text, error);
}
