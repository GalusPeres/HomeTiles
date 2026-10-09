#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/camera/camera_popup.h"

#include <ArduinoJson.h>

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
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/video/camera_geometry.h"
#include "src/devices/device.h"
#include "src/video/camera_stream.h"

namespace {

constexpr int kVideoFrameWidth = camera_geometry::kWidth;
constexpr int kVideoHeight = camera_geometry::kHeight;
constexpr int kVideoTop = popup_layout::scale480(104);
constexpr int kStatusTop =
    kVideoTop + kVideoHeight + popup_layout::scale480(22);
constexpr uint8_t kRequiredBridgeCameraProtocol = 1;
constexpr uint32_t kBridgeResponseTimeoutMs = 30000;
// A full-screen request the Bridge does not answer goes back to the popup.
constexpr uint32_t kFullScreenResponseTimeoutMs = 10000;
constexpr uint8_t kFullEnter = 1;
constexpr uint8_t kFullLeave = 2;

struct CameraPopupContext {
  String entity_id;
  uint32_t surface_color = 0x2A2A2A;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* close_button = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* image = nullptr;
  lv_obj_t* placeholder = nullptr;
  lv_obj_t* status = nullptr;
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
  // Back from the full screen: the kept popup shows its last frame and
  // status until its own stream shows a frame again (no Buffering flash).
  bool resuming = false;
};

CameraPopupContext* g_camera_popup = nullptr;

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
      error ? lv_color_hex(0xFF6B6B) : lv_color_hex(0xD8DEE9), 0);
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

static void open_popup_stream(CameraPopupContext* ctx) {
  ctx->waiting_for_bridge = true;
  ctx->bridge_response_deadline_ms = millis() + kBridgeResponseTimeoutMs;
  ctx->requested_fps = camera_geometry::kFps;
  mqttPublishCameraCommand(ctx->entity_id.c_str(), "open", ctx->requested_fps);
}

// The popup back at once: the kept UI swapped in and LVGL drawing again
// (what changed meanwhile is drawn again, the rest are the same pixels);
// with reopen also the popup's own stream again.
static void leave_full_screen(bool reopen) {
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->full) return;
  ctx->full = false;
  ctx->full_request = 0;
  ctx->pending_url = String();
  camera_stream_stop();
  Device::displayEndFullFrames();
  if (ctx->full_touch) lv_obj_add_flag(ctx->full_touch, LV_OBJ_FLAG_HIDDEN);
  lv_display_t* display = lv_display_get_default();
  if (display) lv_display_enable_invalidation(display, true);
  // What changed meanwhile outside the popup is drawn again. The popup stays
  // as kept, its last frame and status, until its stream shows again (b315:
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
  ctx->resuming = reopen && ctx->visible;
  if (ctx->resuming) open_popup_stream(ctx);
}

// The popup kept and its frame large at once on black (the PPA enlarges it
// until the first full frame), then the Bridge asked for frames in the
// panel's own size. In the loop: lv_refr_now first, or an area LVGL still
// owes would land on the full screen.
static void enter_full_screen() {
  CameraPopupContext* ctx = g_camera_popup;
  if (!ctx || !ctx->visible || ctx->full) return;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t turn = 0;
  if (!Device::displayFullFrameInfo(width, height, turn)) return;
  lv_display_t* display = lv_display_get_default();
  lv_refr_now(display);
  if (display) lv_display_enable_invalidation(display, false);
  // The popup's frames go with its stream.
  lv_image_set_src(ctx->image, nullptr);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);
  ctx->pending_url = String();
  // The popup's frame is read before its stream stops (and frees it).
  const uint16_t* preview = nullptr;
  int32_t preview_w = 0;
  int32_t preview_h = 0;
  int32_t preview_stride = 0;
  size_t preview_bytes = 0;
  camera_stream_shown_frame(preview, preview_w, preview_h, preview_stride, preview_bytes);
  if (!Device::displayBeginFullFrames(preview, preview_w, preview_h, preview_stride,
                                      preview_bytes, true)) {
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
  hide_popup_shell(g_camera_popup->card);
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
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_RELEASED) {
    close_camera_popup();
  }
}

static void overlay_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  (void)event;
}

static void video_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (g_camera_popup && g_camera_popup->visible && !g_camera_popup->full &&
      camera_stream_is_active()) {
    g_camera_popup->full_request = kFullEnter;
  }
}

static void full_touch_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  if (g_camera_popup && g_camera_popup->full) g_camera_popup->full_request = kFullLeave;
}

static CameraPopupContext* create_popup() {
  CameraPopupContext* ctx = new CameraPopupContext();

  const auto parts = create_popup_body(close_event_cb, ctx, 0x2A2A2A);
  ctx->overlay = parts.overlay;
  ctx->card = parts.card;
  ctx->title_label = parts.title;
  ctx->icon_label = parts.icon;
  ctx->close_button = parts.close;
  lv_obj_t* close_button = parts.close;
  lv_obj_set_style_pad_all(ctx->overlay, 0, 0);
  lv_obj_add_event_cb(ctx->overlay, overlay_event_cb, LV_EVENT_CLICKED, ctx);

  lv_obj_t* video = lv_obj_create(ctx->card);
  lv_obj_set_size(video, kVideoFrameWidth, kVideoHeight);
  lv_obj_align(video, LV_ALIGN_TOP_MID, 0, kVideoTop);
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

  ctx->image = lv_image_create(video);
  // The bridge delivers the native frame size requested by this device.
  // No LVGL scaling or letterboxing is needed.
  lv_obj_set_size(ctx->image, kVideoFrameWidth, kVideoHeight);
  lv_image_set_inner_align(ctx->image, LV_IMAGE_ALIGN_CENTER);
  lv_obj_center(ctx->image);
  lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);

  ctx->placeholder = lv_label_create(video);
  lv_obj_set_style_text_font(ctx->placeholder, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(ctx->placeholder, lv_color_hex(0xD8DEE9), 0);
  lv_obj_set_width(ctx->placeholder, LV_PCT(90));
  lv_obj_set_style_text_align(ctx->placeholder, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(ctx->placeholder, camera_text().camera_preparing);
  lv_obj_center(ctx->placeholder);

  ctx->status = lv_label_create(ctx->card);
  lv_obj_set_width(ctx->status, kVideoFrameWidth);
  lv_obj_set_style_text_align(ctx->status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ctx->status, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(ctx->status, lv_color_hex(0xD8DEE9), 0);
  lv_label_set_long_mode(ctx->status, LV_LABEL_LONG_DOT);
  lv_label_set_text(ctx->status, camera_text().camera_ready);
  lv_obj_align(ctx->status, LV_ALIGN_TOP_MID, 0, kStatusTop);

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
  if (!full && !ctx->large_draw_buffer_active) {
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
  if (camera_stream_start(url.c_str(), ctx->surface_color, full)) return;
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
  if (!g_camera_popup) g_camera_popup = create_popup();
}

static void finish_camera_popup_open(const CameraPopupInit& init) {
  if (!g_camera_popup || !g_camera_popup->visible) return;
  g_camera_popup->bridge_response_deadline_ms = millis() + kBridgeResponseTimeoutMs;
  g_camera_popup->requested_fps = camera_geometry::kFps;
  mqttPublishCameraCommand(init.entity_id.c_str(), "open",
                           g_camera_popup->requested_fps);
}

void show_camera_popup(const CameraPopupInit& init) {
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
  hide_climate_popup();

  if (!g_camera_popup) g_camera_popup = create_popup();
  if (!g_camera_popup) return;

  // A different camera may be opened before the deferred restore runs. Reuse
  // the existing large buffer and retain the original small-buffer size.
  g_camera_popup->draw_buffer_restore_pending = false;
  g_camera_popup->resuming = false;
  g_camera_popup->entity_id = init.entity_id;
  g_camera_popup->visible = true;
  g_camera_popup->surface_color =
      init.bg_color != 0 ? init.bg_color : 0x2A2A2A;
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

  lv_image_set_src(g_camera_popup->image, nullptr);
  lv_obj_add_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(g_camera_popup->placeholder, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_camera_popup->placeholder,
                    camera_text().camera_preparing);
  camera_popup_set_status(camera_text().camera_bridge_requesting, false);
  g_camera_popup->waiting_for_bridge = true;
  g_camera_popup->bridge_response_deadline_ms =
      millis() + kBridgeResponseTimeoutMs;
  lv_obj_clear_flag(g_camera_popup->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_camera_popup->overlay, LV_OBJ_FLAG_CLICKABLE);

  displayManager.resetActivityTimer();
  if (!defer_popup_body(g_camera_popup->card, g_camera_popup->title_label,
                        g_camera_popup->icon_label, g_camera_popup->close_button,
                        init, finish_camera_popup_open, true)) finish_camera_popup_open(init);
  if (g_camera_popup && g_camera_popup->card) viewNavigationPopupShown(g_camera_popup->card, init.entity_id.c_str());
  show_popup_shell(g_camera_popup->overlay, g_camera_popup->card, g_camera_popup->title_label, g_camera_popup->icon_label, g_camera_popup->close_button);
}

void hide_camera_popup() {
  close_camera_popup();
}

bool camera_popup_is_visible() {
  return g_camera_popup && g_camera_popup->visible;
}

bool camera_popup_is_busy() {
  return g_camera_popup &&
         (g_camera_popup->visible ||
          g_camera_popup->draw_buffer_restore_pending ||
          g_camera_popup->large_draw_buffer_active);
}

void process_camera_popup() {
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
  } else if (full_request == kFullLeave) {
    leave_full_screen(true);
  }
  if (g_camera_popup->full) {
    // A popup scene change elsewhere may have turned LVGL drawing on again:
    // the full screen must not get UI pixels.
    lv_display_t* display = lv_display_get_default();
    if (display && lv_display_is_invalidation_enabled(display)) {
      lv_display_enable_invalidation(display, false);
    }
  }
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
  camera_stream_process_ui(g_camera_popup->image,
                           g_camera_popup->placeholder,
                           g_camera_popup->resuming ? nullptr : g_camera_popup->status);
  if (g_camera_popup->resuming && !lv_obj_has_flag(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN)) {
    g_camera_popup->resuming = false;
  }
}

void camera_popup_handle_mqtt_status(const char* payload) {
  if (!payload || !*payload || !g_camera_popup ||
      !g_camera_popup->visible) {
    return;
  }

  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, payload)) {
    camera_popup_set_status(camera_text().camera_invalid_response, true);
    return;
  }
  const char* entity = doc["entity_id"] | "";
  if (*entity &&
      !g_camera_popup->entity_id.equalsIgnoreCase(String(entity))) {
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
