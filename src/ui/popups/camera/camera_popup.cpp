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
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/video/camera_geometry.h"
#include "src/video/camera_stream.h"

namespace {

constexpr int kVideoFrameWidth = camera_geometry::kWidth;
constexpr int kVideoHeight = camera_geometry::kHeight;
constexpr int kVideoTop = popup_layout::scale480(104);
constexpr int kStatusTop =
    kVideoTop + kVideoHeight + popup_layout::scale480(22);
constexpr uint8_t kRequiredBridgeCameraProtocol = 1;
constexpr uint32_t kBridgeResponseTimeoutMs = 30000;

struct CameraPopupContext {
  String entity_id;
  uint32_t surface_color = 0x2A2A2A;
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
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
  bool waiting_for_bridge = false;
  bool visible = false;
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

static void align_header_row(CameraPopupContext* ctx) {
  if (!ctx || !ctx->card) return;
  lv_obj_update_layout(ctx->card);
  lv_coord_t center_y =
      popup_layout::kHeaderCenterY -
      lv_obj_get_style_pad_top(ctx->card, LV_PART_MAIN);
  if (center_y < 0) center_y = 0;

  if (ctx->icon_label) {
    lv_coord_t y = center_y - (lv_obj_get_height(ctx->icon_label) / 2);
    if (y < 0) y = 0;
    lv_obj_align(ctx->icon_label, LV_ALIGN_TOP_LEFT,
                 popup_layout::kHeaderIconX, y);
  }
  if (ctx->title_label) {
    lv_coord_t y = center_y - (lv_obj_get_height(ctx->title_label) / 2);
    if (y < 0) y = 0;
    lv_obj_align(ctx->title_label, LV_ALIGN_TOP_LEFT,
                 popup_layout::kHeaderTitleX, y);
  }
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

static void close_camera_popup() {
  if (!g_camera_popup || !g_camera_popup->visible) return;
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

static CameraPopupContext* create_popup() {
  CameraPopupContext* ctx = new CameraPopupContext();

  ctx->overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ctx->overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(ctx->overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctx->overlay, 0, 0);
  lv_obj_set_style_pad_all(ctx->overlay, 0, 0);
  lv_obj_remove_flag(ctx->overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ctx->overlay, overlay_event_cb, LV_EVENT_CLICKED, ctx);

  ctx->card = lv_obj_create(ctx->overlay);
  lv_obj_set_size(ctx->card, popup_layout::kCardWidth,
                  popup_layout::kCardHeight);
  lv_obj_center(ctx->card);
  lv_obj_set_style_bg_color(ctx->card, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ctx->card, popup_layout::kCardRadius, 0);
  lv_obj_set_style_border_width(ctx->card, 0, 0);
  ui_surface_style::apply_global_tile_border(ctx->card);
  lv_obj_set_style_pad_all(ctx->card, popup_layout::kCardPad, 0);
  lv_obj_set_style_shadow_width(ctx->card, popup_layout::scale480(28), 0);
  lv_obj_set_style_shadow_color(ctx->card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(ctx->card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(ctx->card, popup_layout::scale480(2), 0);
  lv_obj_remove_flag(ctx->card, LV_OBJ_FLAG_SCROLLABLE);

  ctx->title_label = lv_label_create(ctx->card);
  lv_obj_set_style_text_font(ctx->title_label,
                             popup_layout::headerTitleFont(), 0);
  lv_obj_set_style_text_color(ctx->title_label, lv_color_white(), 0);
  lv_obj_set_width(ctx->title_label, LV_PCT(62));
  lv_label_set_long_mode(ctx->title_label, LV_LABEL_LONG_DOT);

  ctx->icon_label = lv_label_create(ctx->card);
  lv_obj_set_style_text_font(ctx->icon_label, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(ctx->icon_label);
  lv_obj_set_style_text_color(ctx->icon_label, lv_color_white(), 0);

  lv_obj_t* close_button = lv_button_create(ctx->card);
  lv_obj_set_size(close_button, popup_layout::kCloseButtonSize,
                  popup_layout::kCloseButtonSize);
  lv_obj_set_style_bg_opa(close_button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(close_button, lv_color_hex(0xFFFFFF),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(close_button, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(close_button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(close_button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(close_button, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(close_button,
                          popup_layout::kCloseButtonRadius, 0);
  lv_obj_set_style_pad_all(close_button, 0, 0);
  lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT,
               popup_layout::kCloseButtonOffsetX,
               popup_layout::kCloseButtonOffsetY);
  lv_obj_set_ext_click_area(close_button,
                            popup_layout::kCloseButtonClickArea);
  lv_obj_add_flag(close_button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_clear_flag(close_button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(close_button, close_event_cb, LV_EVENT_CLICKED, ctx);
  lv_obj_add_event_cb(close_button, close_event_cb, LV_EVENT_RELEASED, ctx);

  lv_obj_t* close_icon = lv_label_create(close_button);
  lv_obj_set_style_text_font(close_icon, FONT_MDI_ICONS, 0);
  popup_layout::applyIconScale(close_icon);
  lv_obj_set_style_text_color(close_icon, lv_color_white(), 0);
  lv_label_set_text(close_icon, getMdiChar("window-close").c_str());
  lv_obj_center(close_icon);

  lv_obj_t* video = lv_obj_create(ctx->card);
  lv_obj_set_size(video, kVideoFrameWidth, kVideoHeight);
  lv_obj_align(video, LV_ALIGN_TOP_MID, 0, kVideoTop);
  lv_obj_set_style_bg_color(video, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(video, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(video, camera_geometry::kCornerRadius, 0);
  // Camera frames carry their own small rounded-corner mask. Generic child
  // clipping makes every full video redraw pixel-bound and stalls the P4 UI.
  lv_obj_set_style_clip_corner(video, false, 0);
  lv_obj_set_style_border_width(video, 0, 0);
  lv_obj_set_style_shadow_width(video, 0, 0);
  lv_obj_set_style_pad_all(video, 0, 0);
  lv_obj_remove_flag(video, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(video, LV_OBJ_FLAG_CLICKABLE);

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
  lv_obj_add_flag(ctx->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_CLICKABLE);
  return ctx;
}

}  // namespace

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
  hide_climate_popup();

  if (!g_camera_popup) g_camera_popup = create_popup();
  if (!g_camera_popup) return;

  // A different camera may be opened before the deferred restore runs. Reuse
  // the existing large buffer and retain the original small-buffer size.
  g_camera_popup->draw_buffer_restore_pending = false;
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
  align_header_row(g_camera_popup);

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
  lv_obj_move_foreground(g_camera_popup->overlay);
  displayManager.resetActivityTimer();
  mqttPublishCameraCommand(init.entity_id.c_str(), "open");
  if (g_camera_popup && g_camera_popup->card) viewNavigationPopupShown(g_camera_popup->card, init.entity_id.c_str());
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
  if (!g_camera_popup->visible) return;
  if (g_camera_popup->waiting_for_bridge &&
      static_cast<int32_t>(
          millis() - g_camera_popup->bridge_response_deadline_ms) >= 0) {
    g_camera_popup->waiting_for_bridge = false;
    g_camera_popup->bridge_response_deadline_ms = 0;
    Serial.println(
        "[Camera] No camera response before the bridge deadline");
    camera_popup_set_status(camera_text().camera_bridge_no_response, true);
  }
  if (powerManager.isInSleep()) {
    close_camera_popup();
    return;
  }
  camera_stream_process_ui(g_camera_popup->image,
                           g_camera_popup->placeholder,
                           g_camera_popup->status);
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
    if (width != camera_geometry::kWidth ||
        height != camera_geometry::kHeight ||
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
    if (!g_camera_popup->large_draw_buffer_active) {
      const size_t previous_requested_lines =
          displayManager.getRequestedBufferLines() != 0
              ? displayManager.getRequestedBufferLines()
              : displayManager.getBufferLines();
      const bool previous_fast =
          displayManager.isUsingFastInternalBuffer();
      if (displayManager.setSinglePsramBufferLines(kVideoHeight)) {
        g_camera_popup->previous_draw_buffer_requested_lines =
            previous_requested_lines;
        g_camera_popup->previous_draw_buffer_fast = previous_fast;
        g_camera_popup->large_draw_buffer_active = true;
      } else {
        Serial.println(
            "[Camera] Large LVGL draw buffer unavailable; "
            "normal safe rendering path remains active");
      }
    }
    if (!camera_stream_start(url, g_camera_popup->surface_color) &&
        g_camera_popup->large_draw_buffer_active) {
      if (!restore_previous_draw_buffer(g_camera_popup)) {
        g_camera_popup->draw_buffer_restore_pending = true;
        g_camera_popup->draw_buffer_restore_retry_at_ms = millis() + 2000;
      }
    }
    return;
  }
  if (strcmp(status, "error") == 0) {
    Serial.printf("[Camera] Bridge error: %s\n",
                  static_cast<const char*>(doc["error"] | ""));
    camera_popup_set_status(localize_camera_error(doc["error"] | ""), true);
    return;
  }
  if (strcmp(status, "stopped") == 0) {
    camera_popup_set_status(camera_text().camera_stream_stopped, false);
  }
}

void camera_popup_set_status(const char* text, bool error) {
  if (error && g_camera_popup) {
    g_camera_popup->waiting_for_bridge = false;
    g_camera_popup->bridge_response_deadline_ms = 0;
  }
  set_status_label(text, error);
  camera_stream_set_external_status(text, error);
}
