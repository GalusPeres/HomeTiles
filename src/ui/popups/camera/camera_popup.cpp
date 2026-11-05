#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/camera/camera_popup.h"

#include <ArduinoJson.h>
#include <time.h>

#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/power/power_manager.h"
#include "src/fonts/ui_fonts.h"
#include "src/network/mqtt/mqtt_handlers.h"
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
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/video/camera_geometry.h"
#include "src/devices/device.h"
#include "src/video/camera_stream.h"
#include "src/video/camera_transport.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/clock/clock_format.h"
#include "src/ui/shared/ui_theme.h"

namespace {

namespace ml = media_popup_layout;

constexpr int kVideoFrameWidth = camera_geometry::kWidth;
constexpr int kVideoHeight = camera_geometry::kHeight;
// In card coordinates, as in the Media popup (mockup webcam-radar/radar.html):
// with a time bar the picture one edge below the head like the Media cover,
// without one in the middle of the card below the head. The full screen stays
// a tap on the picture: no button for it (user 2026-11-05).
constexpr int kHeadBottom = ml::kHeadTop + ml::kDisc;
constexpr int kButtonTop = ml::kVolumeTop;
constexpr int kVideoTopWithBar = kHeadBottom + ml::kEdge;
constexpr int kVideoTopPlain =
    kHeadBottom + (popup_layout::kCardHeight - ml::kEdge - kHeadBottom - kVideoHeight) / 2;
// The time bar: the frames of a camera with a history stay in the Bridge, the
// panel knows only their times and shows the one it asks for over the stream.
constexpr uint8_t kMaxFrames = 32;
constexpr uint32_t kFrameSendMs = 120;
// Play runs in a loop: the newest picture stays a little longer.
constexpr uint32_t kPlayStepMs = 500;
constexpr uint32_t kPlayHoldMs = 1500;
// The newest picture counts as "now" while it is this fresh (the sources update
// every 10 minutes); older ones, a source that stopped, show their time.
constexpr uint32_t kFreshFrameS = 30 * 60;
constexpr time_t kClockSetEpoch = 1700000000;
// No words for a picture that comes at once: they show only after this long.
constexpr uint32_t kStatusDelayMs = 1500;
constexpr uint8_t kRequiredBridgeCameraProtocol = 1;
constexpr uint32_t kBridgeResponseTimeoutMs = 30000;
// A full-screen request the Bridge does not answer goes back to the popup.
constexpr uint32_t kFullScreenResponseTimeoutMs = 10000;
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
  lv_obj_t* placeholder = nullptr;
  lv_obj_t* status = nullptr;
  lv_obj_t* video = nullptr;
  // The head's second line ("Live", the picture's time): the shell's hidden
  // source (show_popup_shell).
  lv_obj_t* value_label = nullptr;
  // A neutral message that stays over the picture ("stopped").
  bool status_sticky = false;
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
  bool with_bar = false;
  // The opening, for the delayed status and the log.
  uint32_t opened_ms = 0;
  bool open_logged = false;
  // The camera last known to have a history: it opens in its layout at once.
  String bar_entity;
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

// The head's second line: "Live", or where the shown picture lies: "Now" on
// the newest, else how long before it. Its exact time stands under the bar.
static void show_head_value(CameraPopupContext* ctx) {
  if (!ctx->value_label) return;
  char text[48] = "";
  if (ctx->frame_count >= 2) {
    const uint32_t newest = ctx->frame_times[ctx->frame_count - 1];
    if (ctx->shown_frame + 1 < ctx->frame_count) {
      const uint32_t minutes = (newest - ctx->frame_times[ctx->shown_frame] + 30) / 60;
      snprintf(text, sizeof(text), camera_text().camera_minutes_ago_fmt, static_cast<unsigned>(minutes));
    } else {
      const time_t now = time(nullptr);
      if (now >= kClockSetEpoch && static_cast<uint32_t>(now) > newest + kFreshFrameS) {
        format_frame_time(newest, text, sizeof(text));
      } else {
        snprintf(text, sizeof(text), "%s", camera_text().camera_now);
      }
    }
  } else if (ctx->frame_count == 0 && ctx->live_kind) {
    snprintf(text, sizeof(text), "%s", camera_text().camera_live);
  }
  if (strcmp(lv_label_get_text(ctx->value_label), text) == 0) return;
  lv_label_set_text(ctx->value_label, text);
  if (ctx->visible && !ctx->shell_skipped && !ctx->full) sync_popup_shell();
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
  for (lv_obj_t* obj : {ctx->bar_track, ctx->bar_cap, ctx->bar, ctx->time_shown, ctx->time_newest, ctx->play_button}) {
    set_shown(obj, shown);
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
  lv_label_set_text(ctx->time_shown, when);
  format_frame_time(ctx->frame_times[ctx->frame_count - 1], when, sizeof(when));
  lv_label_set_text(ctx->time_newest, when);
}

static void apply_layout(CameraPopupContext* ctx, bool with_bar) {
  ctx->with_bar = with_bar;
  lv_obj_align(ctx->video, LV_ALIGN_TOP_MID, 0, (with_bar ? kVideoTopWithBar : kVideoTopPlain) - popup_layout::kCardPad);
}

// The Bridge's times, the shown frame kept by its time: the newest stays the
// newest, a frame that is gone becomes the next older one.
static void take_frames(CameraPopupContext* ctx, JsonArrayConst frames) {
  const bool at_newest = ctx->frame_count == 0 || ctx->shown_frame + 1 >= ctx->frame_count;
  const uint32_t shown_time = ctx->frame_count ? ctx->frame_times[ctx->shown_frame] : 0;
  uint8_t count = 0;
  for (JsonVariantConst item : frames) {
    const uint32_t when = item.as<uint32_t>();
    if (!when) continue;
    if (count == kMaxFrames) {  // The newest are kept.
      memmove(ctx->frame_times, ctx->frame_times + 1, sizeof(ctx->frame_times[0]) * (kMaxFrames - 1));
      --count;
    }
    ctx->frame_times[count++] = when;
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
  ctx->shown_frame = ctx->shown_frame + 1 >= ctx->frame_count ? 0 : ctx->shown_frame + 1;
  ctx->frame_dirty = true;
  ctx->play_next_ms = millis() + (ctx->shown_frame + 1 >= ctx->frame_count ? kPlayHoldMs : kPlayStepMs);
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

static void open_popup_stream(CameraPopupContext* ctx) {
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
  lv_obj_add_flag(ctx->placeholder, LV_OBJ_FLAG_HIDDEN);
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
  lv_obj_add_flag(ctx->placeholder, LV_OBJ_FLAG_HIDDEN);
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

static void close_camera_popup() {
  if (!g_camera_popup || !g_camera_popup->visible) return;
  leave_full_screen(false);
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

static void video_event_cb(lv_event_t* event) {
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (g_camera_popup && g_camera_popup->visible && !g_camera_popup->full &&
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
  if (ctx->full_touch) lv_obj_delete(ctx->full_touch);
#if defined(DEVICE_ESP32_S3_RGB_480)
  if (ctx->soft_full) lv_obj_delete(ctx->soft_full);
#endif
  if (g_camera_popup == ctx) g_camera_popup = nullptr;
  delete ctx;
}

static lv_obj_t* plain_part(lv_obj_t* parent) {
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

// The time bar as in the Media popup (make_slider, media_popup.cpp): from edge
// to edge, the knob travelling inside it, the times under it on the edges,
// with the same space from the picture to the knob as from the times' digits
// to the buttons.
static void build_controls(CameraPopupContext* ctx) {
  ctx->root = plain_part(ctx->card);
  lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(ctx->root, popup_layout::kCardWidth, popup_layout::kCardHeight);
  lv_obj_set_pos(ctx->root, -popup_layout::kCardPad, -popup_layout::kCardPad);

  const int line = lv_font_get_line_height(popup_layout::font20());
  const int mid = (kButtonTop + kVideoTopWithBar + kVideoHeight + ml::kSeekKnob / 2 - ml::kSeekHeight / 2 -
                   ml::kTimeGap - line * 4 / 5) / 2;
  const int width = popup_layout::kCardWidth - 2 * ml::kEdge;
  const int bar_y = mid - ml::kSeekHeight / 2;
  ctx->bar_track = plain_part(ctx->root);
  lv_obj_set_pos(ctx->bar_track, ml::kEdge, bar_y);
  lv_obj_set_size(ctx->bar_track, width, ml::kSeekHeight);
  lv_obj_set_style_radius(ctx->bar_track, LV_RADIUS_CIRCLE, 0);
  ctx->bar_cap = plain_part(ctx->root);
  lv_obj_set_pos(ctx->bar_cap, ml::kEdge, bar_y);
  lv_obj_set_size(ctx->bar_cap, ml::kSeekKnob, ml::kSeekHeight);
  lv_obj_set_style_radius(ctx->bar_cap, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ctx->bar_cap, LV_OPA_COVER, 0);
  ctx->bar = lv_slider_create(ctx->root);
  lv_obj_remove_style_all(ctx->bar);
  lv_obj_set_pos(ctx->bar, ml::kEdge, bar_y);
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

  const int time_top = mid + ml::kSeekHeight / 2 + ml::kTimeGap;
  for (lv_obj_t** label : {&ctx->time_shown, &ctx->time_newest}) {
    *label = lv_label_create(ctx->root);
    lv_obj_set_style_text_font(*label, popup_layout::font20(), 0);
    lv_label_set_text(*label, "");
    lv_obj_remove_flag(*label, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_pos(ctx->time_shown, ml::kEdge, time_top);
  lv_obj_align(ctx->time_newest, LV_ALIGN_TOP_RIGHT, -ml::kEdge, time_top);

  ctx->play_button = control_button(ctx->root, "play", &ctx->play_label);
  lv_obj_set_pos(ctx->play_button, popup_layout::kCardWidth / 2 - ml::kButton / 2, kButtonTop);
  lv_obj_add_event_cb(ctx->play_button, play_event_cb, LV_EVENT_CLICKED, ctx);
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
  // Play stays the round button in the icon role with the card's color.
  lv_obj_set_style_bg_color(ctx->play_button, lv_color_hex(ui_theme::icon()), 0);
  lv_obj_set_style_bg_opa(ctx->play_button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(ctx->play_button, lv_color_hex(ui_theme::text_soft()), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(ctx->play_button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(ctx->play_label, lv_color_hex(ctx->surface_color), 0);
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

  ctx->image = lv_image_create(video);
  // The bridge delivers the native frame size requested by this device.
  // No LVGL scaling or letterboxing is needed.
  lv_obj_set_size(ctx->image, kVideoFrameWidth, kVideoHeight);
  lv_image_set_inner_align(ctx->image, LV_IMAGE_ALIGN_CENTER);
  lv_obj_center(ctx->image);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);

  ctx->placeholder = lv_label_create(video);
  lv_obj_set_style_text_font(ctx->placeholder, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(ctx->placeholder, lv_color_hex(ui_theme::text_secondary()), 0);
  lv_obj_set_width(ctx->placeholder, LV_PCT(90));
  lv_obj_set_style_text_align(ctx->placeholder, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(ctx->placeholder, camera_text().camera_preparing);
  lv_obj_center(ctx->placeholder);

  // In the video area until the first frame shows, afterwards only an error
  // (process_camera_popup): no status line under a running picture.
  ctx->status = lv_label_create(video);
  lv_obj_set_width(ctx->status, LV_PCT(90));
  lv_obj_set_style_text_align(ctx->status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ctx->status, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(ctx->status, lv_color_hex(ui_theme::text_secondary()), 0);
  lv_label_set_long_mode(ctx->status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(ctx->status, camera_text().camera_ready);
  lv_obj_align(ctx->status, LV_ALIGN_CENTER, 0, popup_layout::scale480(48));

  build_controls(ctx);
  apply_layout(ctx, false);
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
    if (displayManager.setSinglePsramBufferLines(kVideoHeight)) {
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
  if (camera_stream_start(url.c_str(), ctx->surface_color, full)) {
    ctx->stream_fast = fast;
    camera_transport::stream_started(fast);
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

  // A new opening: the newest frame, no history until the Bridge tells it; a
  // camera known to have one opens in its layout at once.
  set_playing(g_camera_popup, false);
  g_camera_popup->frame_count = 0;
  g_camera_popup->shown_frame = 0;
  g_camera_popup->frame_dirty = false;
  g_camera_popup->live_kind = false;
  g_camera_popup->status_sticky = false;
  lv_label_set_text(g_camera_popup->value_label, "");
  style_controls(g_camera_popup, init.icon_color);
  apply_layout(g_camera_popup, g_camera_popup->bar_entity.equalsIgnoreCase(init.entity_id));
  show_bar(g_camera_popup);

  lv_image_set_src(g_camera_popup->image, nullptr);
  lv_obj_add_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_camera_popup->placeholder, LV_OBJ_FLAG_HIDDEN);
  // Black without words: they follow only if the picture takes long.
  lv_label_set_text(g_camera_popup->placeholder, "");
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
#if defined(DEVICE_ESP32_S3_RGB_480)
  // The S3's full screen shows the frames in its own image (LVGL).
  if (g_camera_popup->full) {
    camera_stream_process_ui(g_camera_popup->soft_full_image, nullptr, nullptr);
  } else
#endif
  camera_stream_process_ui(g_camera_popup->image,
                           g_camera_popup->placeholder,
                           g_camera_popup->resuming ? nullptr : g_camera_popup->status);
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
    if (g_camera_popup->full) {
      g_camera_popup->waiting_for_bridge = true;
      g_camera_popup->bridge_response_deadline_ms = millis() + kFullScreenResponseTimeoutMs;
      mqttPublishCameraFullScreenOpen(g_camera_popup->entity_id.c_str(), camera_geometry::kFps,
                                      g_camera_popup->full_width, g_camera_popup->full_height,
                                      g_camera_popup->full_turn);
    } else {
      open_popup_stream(g_camera_popup);
    }
  }
  if (g_camera_popup->resuming && !lv_obj_has_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN)) {
    g_camera_popup->resuming = false;
  }
  process_time_bar(g_camera_popup);
  // The words show only if the first frame takes long; over a running picture
  // only an error or a message that must stay.
  const bool no_frame = lv_obj_has_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN);
  const bool waited = millis() - g_camera_popup->opened_ms >= kStatusDelayMs;
  if (no_frame && waited && !lv_label_get_text(g_camera_popup->placeholder)[0]) {
    lv_label_set_text(g_camera_popup->placeholder, camera_text().camera_preparing);
  }
  set_shown(g_camera_popup->status, (no_frame && waited) || g_camera_popup->status_sticky ||
                                        camera_stream_status_is_error());
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
  DynamicJsonDocument doc(1536);
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
    JsonArrayConst frames = doc["frames"];
    if (!frames.isNull()) {
      take_frames(g_camera_popup, frames);
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
             : width == camera_geometry::kWidth &&
                   height == camera_geometry::kHeight;
    if (!size_ok ||
        fps < 1 || fps > camera_geometry::kFps ||
        strcmp(transport, "tcp-ack-v1") != 0 ||
        strcmp(framing, "ack-jpeg-v1") != 0) {
      Serial.printf(
          "[Camera] Invalid stream format: %ux%u@%u transport=%s "
          "framing=%s (expected %ux%u@<=%u tcp-ack-v1/ack-jpeg-v1)\n",
          width, height, fps, transport, framing,
          camera_geometry::kWidth, camera_geometry::kHeight,
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
    JsonArrayConst frames = doc["frames"];
    const bool has_history = !frames.isNull();
    if (has_history) {
      take_frames(g_camera_popup, frames);
      g_camera_popup->bar_entity = g_camera_popup->entity_id;
      if (g_camera_popup->shown_frame + 1 < g_camera_popup->frame_count) g_camera_popup->frame_dirty = true;
    } else {
      g_camera_popup->frame_count = 0;
      g_camera_popup->shown_frame = 0;
      if (g_camera_popup->bar_entity.equalsIgnoreCase(g_camera_popup->entity_id)) g_camera_popup->bar_entity = String();
    }
    if (has_history != g_camera_popup->with_bar) apply_layout(g_camera_popup, has_history);
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
