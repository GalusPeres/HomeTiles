#pragma once

// Privacy indicator of the built-in camera, and the first user of a status
// pill that later notifications can share. Its style is a Web Admin setting
// (experimental): none, the stripe only, or the stripe with the pill. While
// the sensor streams (plus a short hold after it stopped):
//   - a red stripe in the top grid margin whose ends fade into the dark
//     background; the part that meets the pill keeps the full pill colour.
//     It is static: an animation would redraw (and rotate) part of the screen
//     every frame while the camera shares the 2D-DMA with the display.
//   - a pill hanging from the stripe. Its lower part is a red 2x0.5 tile in
//     the top tile row (same bottom edge and content position as the half
//     tiles beside it: compact_sensor_layout, webcam icon in the tile disc,
//     title and hint); the top margin above it is red as well. Popup card
//     shadow. The top corners are square and flow into the stripe through
//     two concave fillets of half the tile radius; the bottom corners use the
//     full tile radius. Both follow the radius setting live.
//   - with the global tile border enabled, a slightly brighter border along
//     the outer contour: bottom and sides, then through the fillets, where it
//     fades out towards the stripe. A tap ends the running live stream and
//     hides the pill; nothing is paused or stored, Home Assistant can open the
//     camera again.
// The stripe and fillets lie above the pill. The stripe lies above every
// screen, popup and the screensaver. The pill belongs to the tile grids: it
// hides on the Settings tab and while a popup, the PIN pad or the screensaver
// is open on the top layer, so it never covers them.
// Header-only; ui_manager.cpp owns the single instance.

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <cmath>

#include "src/core/config/config_manager.h"
#include "src/core/config/tile_radius.h"
#include "src/core/i18n/i18n.h"
#include "src/devices/device.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_geometry.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/ui_manager.h"
#include "src/video/local_camera/local_camera.h"

namespace camera_indicator {
namespace detail {

// Short enough that a popup or the screensaver opened above never hides the
// indicator for long.
constexpr uint32_t kPollMs = 100;
// A darker red than the Material 600 tone: calmer on the dark UI.
constexpr uint32_t kRed = 0xC62828;
// Stripe: the top margin minus the one-pixel gap to the tiles.
constexpr int kStripeHeight = GRID_PAD_TOP > 1 ? GRID_PAD_TOP - 1 : 1;
// The whole stripe, pill included, spans this share of the screen width.
constexpr int kStripePermille = 600;
// Each wing fades in from its outer end up to this gradient position (of
// 255) and is solid from there to the pill, wider than the largest fillet.
constexpr int32_t kFadeStop = 180;
// The pill is a 2x0.5 tile.
constexpr float kPillSpan = 2.0f;
// Concave fillets between the stripe and the pill sides: half the tile
// radius, at most half the largest radius the setting allows.
constexpr int kMaxFillet = (tile_radius::maximum(GRID_CELL_H, GRID_GAP) + 1) / 2;
// Icon disc on the red pill (~25% white; tiles use ~15%).
constexpr lv_opa_t kDiscOpa = 64;
// Contour border: as bright as the disc (the tile border's ~20% white is hard
// to see on red).
constexpr lv_opa_t kBorderOpa = kDiscOpa;
// Coverage samples per pixel axis for the fillet edge (anti-aliasing).
constexpr int kFilletSamples = 4;
// Tabs 0-2 are the tile grids; the pill never shows on Settings.
constexpr uint8_t kSettingsTab = 3;

struct Objects {
  lv_obj_t* pill = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* hint = nullptr;
  lv_obj_t* card = nullptr;   // The 2x0.5 tile part of the pill.
  lv_obj_t* bar = nullptr;    // Solid stripe section along the pill.
  lv_obj_t* left = nullptr;   // Fading wings outside the pill.
  lv_obj_t* right = nullptr;
  lv_obj_t* fillet_left = nullptr;
  lv_obj_t* fillet_right = nullptr;
  // Contour border (global tile border setting): sides and bottom.
  lv_obj_t* frame_clip = nullptr;
  lv_obj_t* frame = nullptr;
  uint8_t* fillet_buffers[2] = {nullptr, nullptr};
  int pill_x = 0;
  int pill_width = 0;
  int pill_height = 0;
  int wing_width = 0;
  // Shape the parts are laid out for (updateShape()).
  int radius = -1;
  int fillet = 0;
  bool border = false;
  bool visible = false;    // Stripe shown.
  bool with_pill = false;  // Pill, fillets and border shown as well.
};

inline Objects& objects() {
  static Objects instance;
  return instance;
}

inline int screenWidth() {
  return static_cast<int>(lv_display_get_horizontal_resolution(lv_display_get_default()));
}

inline int pillWidth() {
  return tile_geometry::extent(0.0f, kPillSpan, GRID_CELL_W, GRID_GAP);
}

inline void onPillClicked(lv_event_t*) {
  // endStream() logs; without a live stream (snapshot only) nothing happens.
  local_camera::endStream();
}

inline lv_obj_t* createStripePart(int x, int width) {
  lv_obj_t* part = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(part);
  lv_obj_set_pos(part, x, 0);
  lv_obj_set_size(part, width, kStripeHeight);
  lv_obj_set_style_bg_color(part, lv_color_hex(kRed), 0);
  lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
  lv_obj_remove_flag(part, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(part, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN));
  return part;
}

// The current tile radius (Settings value or the Web Admin live preview),
// the same the pill's bottom corners use; the fillets use half of it.
inline int tileRadius() { return ui_surface_style::radius(tile_layout::scale_480(22)); }
inline int filletRadius() { return std::clamp(tileRadius() / 2, 1, kMaxFillet); }
inline bool bordersEnabled() { return configManager.getConfig().tile_borders; }

inline lv_obj_t* createFilletCanvas() {
  lv_obj_t* canvas = lv_canvas_create(lv_layer_top());
  lv_obj_remove_flag(canvas, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(canvas, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN));
  return canvas;
}

// Concave fillet: red outside a circle of radius r whose centre lies r away
// from the pill side and r below the stripe, so the curve runs from the
// stripe's lower edge down into the pill side. With the border, the first
// pixel ring of the red side of the curve carries the border colour: full
// where the curve meets the pill side, fading out towards the stripe.
inline void drawFillet(lv_obj_t* canvas, uint8_t* buffer, int r, int x, bool left_side,
                       bool border) {
  lv_canvas_set_buffer(canvas, buffer, r, r, LV_COLOR_FORMAT_ARGB8888);
  const lv_color_t red = lv_color_hex(kRed);
  constexpr int kSamples = kFilletSamples * kFilletSamples;
  constexpr float kQuarterTurnInv = 2.0f / 3.14159265f;
  const float radius2 = static_cast<float>(r) * r;
  const float ring2 = static_cast<float>(r + 1) * (r + 1);
  for (int y = 0; y < r; ++y) {
    for (int x_px = 0; x_px < r; ++x_px) {
      // Distance to the pill side: r at the outer column, 0 at the pill.
      const int column = left_side ? x_px : r - 1 - x_px;
      int covered = 0;
      float ring = 0.0f;
      for (int sy = 0; sy < kFilletSamples; ++sy) {
        for (int sx = 0; sx < kFilletSamples; ++sx) {
          const float dx = column + (sx + 0.5f) / kFilletSamples;
          const float dy = r - (y + (sy + 0.5f) / kFilletSamples);
          const float d2 = dx * dx + dy * dy;
          if (d2 >= radius2) ++covered;
          // Angle 0 at the pill side, a quarter turn at the stripe.
          if (d2 >= radius2 && d2 < ring2) ring += 1.0f - std::atan2(dy, dx) * kQuarterTurnInv;
        }
      }
      const lv_color_t color =
          border ? lv_color_mix(lv_color_white(), red,
                                static_cast<uint8_t>(ring * kBorderOpa / kSamples + 0.5f))
                 : red;
      lv_canvas_set_px(canvas, x_px, y, color, static_cast<lv_opa_t>(covered * 255 / kSamples));
    }
  }
  lv_obj_set_pos(canvas, x, kStripeHeight);
}

// Horizontal gradient of `color` from `main_opa` (left end) to `grad_opa`
// (right end) between the two stops (0..255 of the object width).
inline void setFadeGradient(lv_obj_t* obj, lv_color_t color, lv_opa_t main_opa,
                            lv_opa_t grad_opa, int32_t main_stop, int32_t grad_stop) {
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_grad_color(obj, color, 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_main_opa(obj, main_opa, 0);
  lv_obj_set_style_bg_grad_opa(obj, grad_opa, 0);
  lv_obj_set_style_bg_main_stop(obj, main_stop, 0);
  lv_obj_set_style_bg_grad_stop(obj, grad_stop, 0);
}

// Lays the fillets and the contour border out for the current tile radius
// and border setting (also on first use). The border along the sides and
// bottom is an outline-only frame whose rounded top corners lie above its
// clip area, so it starts where the fillets end; its outline follows the
// global tile border setting here.
inline void updateShape(Objects& ui) {
  const int radius = tileRadius();
  const int fillet = filletRadius();
  const bool border = bordersEnabled();
  if (radius == ui.radius && fillet == ui.fillet && border == ui.border) return;
  // PSRAM: the internal RAM stays with the SDIO link and the UI.
  constexpr size_t kBufferBytes =
      LV_CANVAS_BUF_SIZE(kMaxFillet, kMaxFillet, 32, LV_DRAW_BUF_STRIDE_ALIGN);
  for (uint8_t*& buffer : ui.fillet_buffers) {
    if (!buffer) {
      buffer = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
  }
  if (!ui.fillet_buffers[0] || !ui.fillet_buffers[1]) return;
  ui.radius = radius;
  ui.fillet = fillet;
  ui.border = border;
  drawFillet(ui.fillet_left, ui.fillet_buffers[0], fillet, ui.pill_x - fillet, true, border);
  drawFillet(ui.fillet_right, ui.fillet_buffers[1], fillet, ui.pill_x + ui.pill_width, false,
             border);

  const int frame_top = kStripeHeight + fillet;
  const int frame_height = std::max(1, ui.pill_height - frame_top);
  lv_obj_set_pos(ui.frame_clip, ui.pill_x, frame_top);
  lv_obj_set_size(ui.frame_clip, ui.pill_width, frame_height);
  lv_obj_set_pos(ui.frame, 0, -radius);
  lv_obj_set_size(ui.frame, ui.pill_width, frame_height + radius);
  lv_obj_set_style_outline_width(ui.frame, border ? 1 : 0, 0);
}


inline void create(Objects& ui) {
  const int screen = screenWidth();
  const int pill_width = pillWidth();
  const int pill_x = (screen - pill_width) / 2;
  const int wing_width = std::max(0, (screen * kStripePermille / 1000 - pill_width) / 2);
  // From the screen top down to the bottom edge of the half tiles in the top
  // tile row, with the same rounding as their fractional geometry.
  const int tile_height = tile_geometry::extent(0.0f, 0.5f, GRID_CELL_H, GRID_GAP);
  const int pill_height = GRID_PAD_TOP + tile_height;
  ui.pill_x = pill_x;
  ui.pill_width = pill_width;
  ui.pill_height = pill_height;
  ui.wing_width = wing_width;

  // Pill first: the stripe parts created after it lie above its shadow.
  ui.pill = lv_button_create(lv_layer_top());
  lv_obj_remove_style_all(ui.pill);
  lv_obj_set_pos(ui.pill, pill_x, 0);
  lv_obj_set_size(ui.pill, pill_width, pill_height);
  ui_surface_style::apply_radius(ui.pill, tile_layout::scale_480(22), 0);
  // No pressed colour: pill, stripe and fillets form one uniform shape; the
  // pill disappearing confirms the tap.
  lv_obj_set_style_bg_color(ui.pill, lv_color_hex(kRed), 0);
  lv_obj_set_style_bg_opa(ui.pill, LV_OPA_COVER, 0);
  // Same shadow as the popup cards.
  lv_obj_set_style_shadow_width(ui.pill, popup_layout::scale480(28), 0);
  lv_obj_set_style_shadow_color(ui.pill, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(ui.pill, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(ui.pill, popup_layout::scale480(2), 0);
  lv_obj_remove_flag(ui.pill, LV_OBJ_FLAG_SCROLLABLE);
  disable_pressed_button_animation(ui.pill);
  lv_obj_add_flag(ui.pill, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN));
  lv_obj_add_event_cb(ui.pill, onPillClicked, LV_EVENT_CLICKED, nullptr);

  // Square top corners: a red block fills the upper half (at least the
  // largest tile radius); the bottom corners stay rounded.
  lv_obj_t* top = lv_obj_create(ui.pill);
  lv_obj_remove_style_all(top);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_size(top, pill_width, pill_height / 2);
  lv_obj_set_style_bg_color(top, lv_color_hex(kRed), 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
  lv_obj_remove_flag(top, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  // The 2x0.5 tile: exactly where a half tile of the top row sits, so the
  // content lines up with the half tiles beside it. The pill's red shows
  // through; taps fall through to the pill.
  ui.card = lv_obj_create(ui.pill);
  lv_obj_remove_style_all(ui.card);
  lv_obj_set_pos(ui.card, 0, GRID_PAD_TOP);
  lv_obj_set_size(ui.card, pill_width, tile_height);
  lv_obj_remove_flag(ui.card, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  ui.icon = lv_label_create(ui.card);
  if (FONT_MDI_ICONS != nullptr) set_label_style(ui.icon, lv_color_white(), FONT_MDI_ICONS);
  const String icon = getMdiChar("webcam");
  lv_label_set_text(ui.icon, icon.c_str());
  ui.title = lv_label_create(ui.card);
  set_label_style(ui.title, lv_color_white(), compact_sensor_layout::title_font());
  ui.hint = lv_label_create(ui.card);
  set_label_style(ui.hint, lv_color_white(), compact_sensor_layout::value_font());
  compact_sensor_layout::apply_content(ui.card, ui.icon, ui.title, ui.hint, pill_width);
  // The tile disc (15% white) is hard to see on red: stronger on the pill.
  if (lv_obj_t* disc = lv_obj_get_parent(ui.icon); disc && disc != ui.card) {
    lv_obj_set_style_bg_opa(disc, kDiscOpa, 0);
  }

  // Border along the sides and bottom: the global tile border geometry (1 px
  // outline, -1 px pad) with the brighter pill opacity, on an outline-only
  // frame inside a clip container. updateShape() switches it with the setting.
  ui.frame_clip = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ui.frame_clip);
  lv_obj_remove_flag(ui.frame_clip, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(ui.frame_clip, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN));
  ui.frame = lv_obj_create(ui.frame_clip);
  lv_obj_remove_style_all(ui.frame);
  lv_obj_remove_flag(ui.frame, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  ui_surface_style::apply_radius(ui.frame, tile_layout::scale_480(22), 0);
  lv_obj_set_style_outline_pad(ui.frame, -1, 0);
  lv_obj_set_style_outline_color(ui.frame, lv_color_white(), 0);
  lv_obj_set_style_outline_opa(ui.frame, kBorderOpa, 0);
  lv_obj_set_style_outline_width(ui.frame, 0, 0);

  ui.bar = createStripePart(pill_x, pill_width);
  ui.left = createStripePart(pill_x - wing_width, wing_width);
  // Left wing: transparent at its outer end, full red from kFadeStop on.
  setFadeGradient(ui.left, lv_color_hex(kRed), LV_OPA_TRANSP, LV_OPA_COVER, 0, kFadeStop);
  ui.right = createStripePart(pill_x + pill_width, wing_width);
  setFadeGradient(ui.right, lv_color_hex(kRed), LV_OPA_COVER, LV_OPA_TRANSP, 255 - kFadeStop, 255);
  ui.fillet_left = createFilletCanvas();
  ui.fillet_right = createFilletCanvas();
  updateShape(ui);
}

inline void setShown(lv_obj_t* obj, bool shown) {
  if (shown) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

// visible: the stripe; with_pill: the pill, its fillets and border as well.
inline void setVisible(Objects& ui, bool visible, bool with_pill) {
  with_pill = visible && with_pill;
  if (visible == ui.visible && with_pill == ui.with_pill) return;
  ui.visible = visible;
  ui.with_pill = with_pill;
  if (with_pill) {
    // The language may have changed since the last capture.
    const auto& strings = i18n::strings(configManager.getConfig().language);
    lv_label_set_text(ui.title, strings.local_camera_indicator_active);
    lv_label_set_text(ui.hint, strings.local_camera_indicator_end);
  }
  for (lv_obj_t* part : {ui.bar, ui.left, ui.right}) setShown(part, visible);
  for (lv_obj_t* part : {ui.pill, ui.frame_clip, ui.fillet_left, ui.fillet_right}) {
    setShown(part, with_pill);
  }
}

// The pill only on a tile grid without a popup (tile popups and the PIN pad
// all open through popup_shell) or the screensaver above it. Closed popups
// stay parked, not hidden, on the top layer, so the layer's children say
// nothing about an open popup; the shell and the screensaver do.
inline bool pillAllowed() {
  const uint8_t tab = uiManager.activeTab();
  return tab != UINT8_MAX && tab != kSettingsTab && !popup_shell_active() &&
         !image_screensaver_covers_ui();
}

// Popups and the screensaver are added to the same layer later: keep the pill
// and, above it, the stripe and fillets on top.
inline void keepOnTop(Objects& ui) {
  lv_obj_t* const order[] = {ui.pill, ui.frame_clip, ui.bar, ui.left, ui.right,
                             ui.fillet_left, ui.fillet_right};
  constexpr uint32_t kCount = sizeof(order) / sizeof(order[0]);
  lv_obj_t* layer = lv_layer_top();
  const uint32_t children = lv_obj_get_child_count(layer);
  bool on_top = children >= kCount;
  for (uint32_t i = 0; on_top && i < kCount; ++i) {
    on_top = lv_obj_get_child(layer, static_cast<int32_t>(children - kCount + i)) == order[i];
  }
  if (on_top) return;
  for (lv_obj_t* part : order) lv_obj_move_foreground(part);
}

inline void refresh(Objects& ui) {
  const local_camera::IndicatorStyle style = local_camera::indicatorStyle();
  const bool visible =
      style != local_camera::IndicatorStyle::None && local_camera::indicatorActive();
  if (!ui.pill) {
    if (!visible) return;
    create(ui);
  }
  setVisible(ui, visible, style == local_camera::IndicatorStyle::Pill && pillAllowed());
  if (!visible) return;
  updateShape(ui);
  keepOnTop(ui);
}

// A popup or the screensaver overlay opened or closed on the top layer: hide
// or restore the pill and keep the stripe above it at once, not at the next
// poll. The indicator's own changes re-enter here; busy stops that.
inline void onTopLayerChanged(lv_event_t*) {
  static bool busy = false;
  Objects& ui = objects();
  if (busy || !ui.pill || !ui.visible) return;
  busy = true;
  refresh(ui);
  busy = false;
}

inline void poll(lv_timer_t*) { refresh(objects()); }

}  // namespace detail

// Tab switches: apply the pill rule before the switch is drawn; the poll
// would show or hide the pill one poll later, visibly over the new page.
// Call with display invalidation enabled, so a hidden pill's area is redrawn.
// LVGL task only.
inline void refreshNow() {
  if (!local_camera::supported()) return;
  detail::refresh(detail::objects());
}

// The Settings switch clears the framebuffer and redraws only its own
// controls: redraw the visible indicator parts as well, or the stripe keeps
// only the pieces above those controls. LVGL task only.
inline void invalidateVisible() {
  detail::Objects& ui = detail::objects();
  if (!ui.pill || !ui.visible) return;
  for (lv_obj_t* part : {ui.bar, ui.left, ui.right}) lv_obj_invalidate(part);
  if (!ui.with_pill) return;
  for (lv_obj_t* part : {ui.pill, ui.frame_clip, ui.fillet_left, ui.fillet_right}) {
    lv_obj_invalidate(part);
  }
}

// Starts the poll timer on devices with a built-in camera. LVGL task only.
inline void init() {
  static bool started = false;
  if (started || !local_camera::supported()) return;
  started = true;
  lv_timer_create(detail::poll, detail::kPollMs, nullptr);
  lv_obj_add_event_cb(lv_layer_top(), detail::onTopLayerChanged, LV_EVENT_CHILD_CREATED, nullptr);
  lv_obj_add_event_cb(lv_layer_top(), detail::onTopLayerChanged, LV_EVENT_CHILD_CHANGED, nullptr);
}

}  // namespace camera_indicator
