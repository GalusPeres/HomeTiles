#include "src/ui/timers/timer_ui.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "src/audio/speaker.h"
#include "src/audio/timer_chime.h"
#include "src/core/config/config_manager.h"
#include "src/core/display/display_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/core/power/power_manager.h"
#include "src/tiles/config/grid_layout.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/timer/renderer.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/screensaver/image_screensaver.h"
#include "src/ui/shared/tabular_digits.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/tabs/settings/settings_style.h"
#include "src/ui/tabs/tiles/home_bar.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/timers/voice_timers.h"

namespace timer_ui {
namespace {

using voice_timer::Timer;

constexpr const char* kLogPrefix = "[Timers]";
// The timer amber; its circle and pill take tones of it like the camera's red.
constexpr uint32_t kAmber = 0xF5A524;
constexpr uint32_t kOnAmber = 0x1A1206;
constexpr uint32_t kTickMs = 250;
// Ringing: rounds of the chime twice, a pause, for at most two minutes.
constexpr uint32_t kRingPauseMs = 2000;
constexpr uint32_t kRingLimitMs = 120000;
// The user's own sound plays from the Bridge; a round waits at least this.
constexpr uint32_t kBridgeRoundMs = 4000;
// Home Assistant speaks after the first round; its clip starts within this.
constexpr uint32_t kSpeakStartMs = 2500;
// The longest a spoken sentence may hold the next round.
constexpr uint32_t kSpeechWaitMs = 15000;
constexpr uint32_t kActivityRefreshMs = 5000;
// The Timers popup and the alert card dim the screen behind them, so the
// tiles do not compete with the countdowns.
constexpr lv_opa_t kBackdropOpa = LV_OPA_60;
constexpr uint32_t kPresetMinutes[] = {1, 5, 10, 15, 30};
constexpr size_t kPresetCount = sizeof(kPresetMinutes) / sizeof(kPresetMinutes[0]);

const i18n::Strings& tr() { return i18n::strings(configManager.getConfig().language); }

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return obj;
}

lv_obj_t* icon_label(lv_obj_t* parent, const char* icon, uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, getMdiChar(icon).c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(label, FONT_MDI_ICONS, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

int text_width(const lv_font_t* font, const char* text) {
  lv_point_t size{};
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

const lv_font_t* countdown_font() { return home_bar::time_font(); }

// A name to show: the timer's own, else its length ("15 min timer").
struct TimerName {
  char text[voice_timer::kMaxNameBytes + 32];
  explicit TimerName(const Timer& timer) { timer_label(timer, text, sizeof(text)); }
};

// ---------------------------------------------------------------------------
// Head pill
// ---------------------------------------------------------------------------

// The countdown labels of every built head (one per cached grid).
constexpr size_t kMaxPills = 12;
lv_obj_t* g_pill_labels[kMaxPills] = {};
// What the built pills show; another shape builds the heads again.
struct PillShape {
  uint8_t count = 0;
  bool hours = false;
  bool operator!=(const PillShape& other) const { return count != other.count || hours != other.hours; }
};
PillShape g_built_shape;
char g_pill_text[12] = "";

PillShape current_shape() {
  PillShape shape;
  Timer timer;
  uint32_t left = 0;
  shape.count = voice_timers::count();
  if (voice_timers::nextTimer(&timer, &left)) shape.hours = left >= 3600;
  return shape;
}

void pill_text(char* out, size_t size) {
  Timer timer;
  uint32_t left = 0;
  if (!voice_timers::nextTimer(&timer, &left)) {
    snprintf(out, size, "0:00");
    return;
  }
  voice_timer::formatClock(left, out, size);
}

void forget_pill_label(lv_event_t* event) {
  lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_target(event));
  for (lv_obj_t*& slot : g_pill_labels) {
    if (slot == label) slot = nullptr;
  }
}

void remember_pill_label(lv_obj_t* label) {
  for (lv_obj_t*& slot : g_pill_labels) {
    if (slot) continue;
    slot = label;
    lv_obj_add_event_cb(label, forget_pill_label, LV_EVENT_DELETE, nullptr);
    return;
  }
  Serial.printf("%s No free pill label slot\n", kLogPrefix);
}

void refresh_pills() {
  char text[sizeof(g_pill_text)];
  pill_text(text, sizeof(text));
  if (strcmp(text, g_pill_text) == 0) return;
  memcpy(g_pill_text, text, sizeof(g_pill_text));
  for (lv_obj_t* label : g_pill_labels) {
    if (label) lv_label_set_text(label, g_pill_text);
  }
}

void on_pill(lv_event_t*) { open_popup(); }

// ---------------------------------------------------------------------------
// Timers popup
// ---------------------------------------------------------------------------

struct Row {
  lv_obj_t* box = nullptr;
  lv_obj_t* name = nullptr;
  lv_obj_t* left = nullptr;
  lv_obj_t* pause = nullptr;
  lv_obj_t* pause_icon = nullptr;
  lv_obj_t* add = nullptr;
  lv_obj_t* cancel = nullptr;
  char id[voice_timer::kMaxIdChars + 1] = {};
  bool active = true;
};

struct Popup {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  lv_obj_t* list = nullptr;
  lv_obj_t* empty = nullptr;
  lv_obj_t* presets = nullptr;
  lv_obj_t* preset_buttons[kPresetCount] = {};
  Row rows[voice_timer::kMaxTimers];
  uint32_t shown_generation = UINT32_MAX;
};
Popup* g_popup = nullptr;

void style_button(lv_obj_t* button, bool primary) {
  lv_color_t card = lv_obj_get_style_bg_color(g_popup->card, LV_PART_MAIN);
  lv_color_t icon = lv_color_hex(kAmber);
  lv_color_t fill, raised;
  lv_opa_t opa, pressed;
  popup_nav_style::fill(card, icon, fill, opa);
  popup_nav_style::fill_raised(card, icon, raised, pressed);
  if (primary) {
    popup_nav_style::set_bg(button, lv_color_hex(kAmber), LV_OPA_COVER, LV_PART_MAIN);
    popup_nav_style::set_bg(button, lv_color_hex(0xFFC35C), LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
  } else {
    popup_nav_style::set_bg(button, fill, opa, LV_PART_MAIN);
    popup_nav_style::set_bg(button, raised, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
  }
  popup_nav_style::no_press_filter(button, LV_PART_MAIN | LV_STATE_PRESSED);
}

lv_obj_t* make_button(lv_obj_t* parent, int w, int h, lv_event_cb_t cb, void* user_data) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  disable_pressed_button_animation(button);
  lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
  return button;
}

void on_popup_close(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  hide_popup();
}

// Another popup took the shell: it hid this card already.
void on_popup_dismissed() {}

void on_preset(lv_event_t* event) {
  const uint32_t minutes = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  voice_timers::requestStart(minutes * 60u);
}

void on_row_pause(lv_event_t* event) {
  Row* row = static_cast<Row*>(lv_event_get_user_data(event));
  if (row && row->id[0]) voice_timers::requestAction(row->active ? "pause" : "resume", row->id);
}

void on_row_add(lv_event_t* event) {
  Row* row = static_cast<Row*>(lv_event_get_user_data(event));
  if (row && row->id[0]) voice_timers::requestAdd(row->id, 60);
}

void on_row_cancel(lv_event_t* event) {
  Row* row = static_cast<Row*>(lv_event_get_user_data(event));
  if (row && row->id[0]) voice_timers::requestAction("cancel", row->id);
}

void on_popup_delete(lv_event_t*) {
  delete g_popup;
  g_popup = nullptr;
}

void build_row(Row& row, int width, int height) {
  const uint32_t text = ui_theme::text();
  row.box = plain(g_popup->list);
  lv_obj_set_size(row.box, width, height);
  ui_surface_style::apply_radius(row.box, popup_layout::kCardRadius, 0);
  lv_obj_set_style_pad_hor(row.box, height / 5, 0);
  const int button = height * 7 / 10;
  const int gap = height / 6;

  row.cancel = make_button(row.box, button, button, on_row_cancel, &row);
  lv_obj_align(row.cancel, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_center(icon_label(row.cancel, "close", text));
  row.add = make_button(row.box, button * 3 / 2, button, on_row_add, &row);
  lv_obj_align_to(row.add, row.cancel, LV_ALIGN_OUT_LEFT_MID, -gap, 0);
  lv_obj_t* add_label = lv_label_create(row.add);
  lv_obj_set_style_text_font(add_label, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(add_label, lv_color_hex(text), 0);
  lv_label_set_text(add_label, tr().timer_add_minute);
  lv_obj_center(add_label);
  row.pause = make_button(row.box, button, button, on_row_pause, &row);
  lv_obj_align_to(row.pause, row.add, LV_ALIGN_OUT_LEFT_MID, -gap, 0);
  row.pause_icon = icon_label(row.pause, "pause", text);
  lv_obj_center(row.pause_icon);

  const int text_w = width - 2 * (height / 5) - 3 * gap - button * 7 / 2;
  row.name = lv_label_create(row.box);
  lv_obj_set_style_text_font(row.name, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(row.name, lv_color_hex(ui_theme::text_soft()), 0);
  lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(row.name, text_w > 1 ? text_w : 1);
  lv_obj_align(row.name, LV_ALIGN_LEFT_MID, 0, -lv_font_get_line_height(countdown_font()) / 2);
  row.left = lv_label_create(row.box);
  lv_obj_set_style_text_font(row.left, countdown_font(), 0);
  lv_obj_set_style_text_color(row.left, lv_color_hex(text), 0);
  lv_obj_align(row.left, LV_ALIGN_LEFT_MID, 0, lv_font_get_line_height(popup_layout::font20()) / 2);
  lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
}

void style_popup() {
  lv_color_t card = lv_obj_get_style_bg_color(g_popup->card, LV_PART_MAIN);
  lv_color_t fill;
  lv_opa_t opa;
  popup_nav_style::fill(card, lv_color_hex(kAmber), fill, opa);
  for (Row& row : g_popup->rows) {
    lv_obj_set_style_bg_color(row.box, fill, 0);
    lv_obj_set_style_bg_opa(row.box, static_cast<lv_opa_t>(opa / 2), 0);
    for (lv_obj_t* button : {row.pause, row.add, row.cancel}) style_button(button, false);
  }
  for (lv_obj_t* button : g_popup->preset_buttons) style_button(button, false);
}

void build_popup() {
  g_popup = new Popup();
  const PopupShellParts parts = create_popup_body(on_popup_close, g_popup);
  g_popup->overlay = parts.overlay;
  g_popup->card = parts.card;
  g_popup->title = parts.title;
  g_popup->icon = parts.icon;
  g_popup->close = parts.close;
  disable_pressed_button_animation(parts.close);
  lv_obj_add_event_cb(g_popup->overlay, on_popup_delete, LV_EVENT_DELETE, nullptr);
  // The shell copies its owner's background: a dark backdrop for this popup only.
  lv_obj_set_style_bg_color(g_popup->overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_popup->overlay, kBackdropOpa, 0);
  hometiles_title::set(g_popup->title, tr().timers_title);
  lv_label_set_text(g_popup->icon, getMdiChar("timer-outline").c_str());
  lv_obj_set_style_text_color(g_popup->icon, lv_color_hex(kAmber), 0);
  popup_layout::alignHeader(g_popup->card, g_popup->title, g_popup->icon);

  const int width = popup_layout::kContentWidth;
  const int inner_h = popup_layout::kCardHeight - 2 * popup_layout::kCardPad;
  const int header_bottom = popup_layout::kHeaderHeight - popup_layout::kCardPad;
  const int gap = Device::kGridGap;

  // Presets along the bottom: "Start a timer" and five pills.
  const int preset_h = popup_layout::kNavHeight * 3 / 4;
  g_popup->presets = plain(g_popup->card);
  lv_obj_set_size(g_popup->presets, width, preset_h);
  lv_obj_align(g_popup->presets, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(g_popup->presets, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_popup->presets, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const int preset_w = (width - (static_cast<int>(kPresetCount) - 1) * gap) / static_cast<int>(kPresetCount);
  for (size_t i = 0; i < kPresetCount; ++i) {
    lv_obj_t* button = make_button(g_popup->presets, preset_w, preset_h, on_preset,
                                   reinterpret_cast<void*>(static_cast<uintptr_t>(kPresetMinutes[i])));
    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, popup_layout::font24(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ui_theme::text()), 0);
    lv_label_set_text_fmt(label, tr().timer_minutes, static_cast<int>(kPresetMinutes[i]));
    lv_obj_center(label);
    g_popup->preset_buttons[i] = button;
  }
  lv_obj_t* heading = lv_label_create(g_popup->card);
  lv_obj_set_style_text_font(heading, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(heading, lv_color_hex(ui_theme::text_soft()), 0);
  lv_label_set_text(heading, tr().timer_start);
  const int heading_h = lv_font_get_line_height(popup_layout::font20());
  lv_obj_align(heading, LV_ALIGN_BOTTOM_LEFT, 0, -(preset_h + gap / 2));

  // The timers above, scrolling when they do not fit.
  const int list_y = header_bottom + gap;
  const int list_h = inner_h - list_y - preset_h - heading_h - 2 * gap;
  g_popup->list = plain(g_popup->card);
  lv_obj_add_flag(g_popup->list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_popup->list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_popup->list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_size(g_popup->list, width, list_h > 1 ? list_h : 1);
  lv_obj_set_pos(g_popup->list, 0, list_y);
  lv_obj_set_flex_flow(g_popup->list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_popup->list, gap, 0);
  const int row_h = popup_layout::scale(104);
  for (Row& row : g_popup->rows) build_row(row, width, row_h);
  g_popup->empty = lv_label_create(g_popup->card);
  lv_obj_set_style_text_font(g_popup->empty, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(g_popup->empty, lv_color_hex(ui_theme::text_soft()), 0);
  lv_label_set_text(g_popup->empty, tr().timer_none);
  lv_obj_align(g_popup->empty, LV_ALIGN_TOP_MID, 0, list_y + list_h / 2 - heading_h);

  lv_obj_move_foreground(g_popup->icon);
  lv_obj_move_foreground(g_popup->title);
  lv_obj_move_foreground(g_popup->close);
  style_popup();
}

// Rows follow the list; the countdowns every tick.
void refresh_popup(bool force) {
  if (!popup_visible()) return;
  const uint32_t generation = voice_timers::generation();
  const bool rebuild = force || generation != g_popup->shown_generation;
  g_popup->shown_generation = generation;
  const uint8_t count = voice_timers::count();
  for (uint8_t i = 0; i < voice_timer::kMaxTimers; ++i) {
    Row& row = g_popup->rows[i];
    Timer timer;
    uint32_t left = 0;
    const bool shown = voice_timers::timerAt(i, &timer, &left);
    if (rebuild) {
      lv_obj_set_flag(row.box, LV_OBJ_FLAG_HIDDEN, !shown);
      if (!shown) {
        row.id[0] = '\0';
        continue;
      }
      memcpy(row.id, timer.id, sizeof(row.id));
      row.active = timer.active;
      if (timer.active) {
        lv_label_set_text(row.name, TimerName(timer).text);
      } else {
        lv_label_set_text_fmt(row.name, tr().timer_paused_name, TimerName(timer).text);
      }
      lv_label_set_text(row.pause_icon, getMdiChar(timer.active ? "pause" : "play").c_str());
    }
    if (!shown) continue;
    char text[12];
    voice_timer::formatClock(left, text, sizeof(text));
    if (strcmp(lv_label_get_text(row.left), text) != 0) lv_label_set_text(row.left, text);
  }
  if (rebuild) lv_obj_set_flag(g_popup->empty, LV_OBJ_FLAG_HIDDEN, count > 0);
}

// ---------------------------------------------------------------------------
// Alert card
// ---------------------------------------------------------------------------

constexpr size_t kMaxRinging = 4;

struct Alert {
  lv_obj_t* backdrop = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* halo = nullptr;
  lv_obj_t* disc = nullptr;
  lv_obj_t* bell = nullptr;
  lv_obj_t* name = nullptr;
  lv_obj_t* line = nullptr;
  lv_obj_t* also = nullptr;
  lv_obj_t* also_label = nullptr;
  int halo_size = 0;
  bool animating = false;
};
Alert g_alert;

struct Ringing {
  bool active = false;
  Timer timers[kMaxRinging];
  uint8_t count = 0;
  uint32_t started_ms = 0;
  uint32_t next_ms = 0;
  uint32_t first_round_end_ms = 0;
  uint8_t rounds = 0;
  bool spoke = false;
  // The timer the spoken sentence names: the newest one that finished.
  uint8_t speak_at = 0;
  // Until then a clip playing is taken for the spoken sentence and awaited.
  uint32_t speech_until_ms = 0;
  uint32_t activity_ms = 0;
};
Ringing g_ring;

// The bell's swing (degrees x10) over one 1.6 s period: a few decaying
// swings, then rest.
int32_t bell_angle(int32_t t_ms) {
  static const int16_t kAt[] = {0, 128, 256, 384, 512, 640, 768, 960, 1600};
  static const int16_t kAngle[] = {0, 180, -160, 120, -90, 50, -20, 0, 0};
  for (size_t i = 1; i < sizeof(kAt) / sizeof(kAt[0]); ++i) {
    if (t_ms > kAt[i]) continue;
    const int32_t span = kAt[i] - kAt[i - 1];
    return (kAngle[i - 1] + (kAngle[i] - kAngle[i - 1]) * (t_ms - kAt[i - 1]) / span) * 10;
  }
  return 0;
}

void bell_anim(void* obj, int32_t t_ms) {
  lv_obj_set_style_transform_rotation(static_cast<lv_obj_t*>(obj), bell_angle(t_ms), 0);
}

void halo_anim(void* obj, int32_t t_ms) {
  // 0.85 -> 1.25 of the disc, fading out.
  lv_obj_t* halo = static_cast<lv_obj_t*>(obj);
  const int size = g_alert.halo_size * (85 + 40 * t_ms / 1600) / 100;
  lv_obj_set_size(halo, size, size);
  lv_obj_center(halo);
  lv_obj_set_style_bg_opa(halo, static_cast<lv_opa_t>(230 - 230 * t_ms / 1600), 0);
}

void start_animation(bool on) {
  if (!g_alert.bell || on == g_alert.animating) return;
  g_alert.animating = on;
  lv_anim_delete(g_alert.bell, bell_anim);
  lv_anim_delete(g_alert.halo, halo_anim);
  if (!on) {
    lv_obj_set_style_transform_rotation(g_alert.bell, 0, 0);
    lv_obj_add_flag(g_alert.halo, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(g_alert.halo, LV_OBJ_FLAG_HIDDEN);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_values(&a, 0, 1600);
  lv_anim_set_duration(&a, 1600);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_var(&a, g_alert.bell);
  lv_anim_set_exec_cb(&a, bell_anim);
  lv_anim_start(&a);
  lv_anim_set_var(&a, g_alert.halo);
  lv_anim_set_exec_cb(&a, halo_anim);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

void stop_ringing() {
  speaker::stopChime();
  start_animation(false);
}

void hide_alert() {
  stop_ringing();
  g_ring = Ringing{};
  if (g_alert.backdrop) lv_obj_add_flag(g_alert.backdrop, LV_OBJ_FLAG_HIDDEN);
}

void on_stop(lv_event_t*) {
  Serial.printf("%s Alert stopped\n", kLogPrefix);
  hide_alert();
}

void on_add_minute(lv_event_t*) {
  // The finished timer is gone in Home Assistant: a new one with its name.
  if (g_ring.count) {
    const Timer& timer = g_ring.timers[0];
    voice_timers::requestStart(60, timer.name[0] ? timer.name : nullptr);
  }
  hide_alert();
}

void build_alert() {
  const int screen_w = lv_display_get_horizontal_resolution(lv_display_get_default());
  const int screen_h = lv_display_get_vertical_resolution(lv_display_get_default());
  const bool upright = screen_h > screen_w;
  g_alert.backdrop = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_alert.backdrop);
  lv_obj_set_size(g_alert.backdrop, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(g_alert.backdrop, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_alert.backdrop, kBackdropOpa, 0);
  lv_obj_add_flag(g_alert.backdrop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g_alert.backdrop, LV_OBJ_FLAG_SCROLLABLE);

  const int card_w = popup_layout::kCardWidth * 9 / 10;
  const int card_h = upright ? popup_layout::kCardHeight : popup_layout::kCardHeight * 8 / 10;
  g_alert.card = plain(g_alert.backdrop);
  lv_obj_set_size(g_alert.card, card_w, card_h);
  lv_obj_center(g_alert.card);
  lv_obj_set_style_bg_color(g_alert.card, lv_color_hex(ui_theme::popup_card()), 0);
  lv_obj_set_style_bg_opa(g_alert.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_alert.card, lv_color_hex(kAmber), 0);
  lv_obj_set_style_border_width(g_alert.card, 2, 0);
  ui_surface_style::apply_radius(g_alert.card, popup_layout::kCardRadius + Device::kGridGap, 0);
  const int pad = popup_layout::kCardPad;
  lv_obj_set_style_pad_all(g_alert.card, pad, 0);
  lv_obj_add_flag(g_alert.card, LV_OBJ_FLAG_CLICKABLE);

  // The bell: an amber disc in a darker ring, a fading halo behind it.
  const int disc = popup_layout::scale(96);
  const int ring = disc * 130 / 100;
  g_alert.halo_size = disc * 160 / 100;
  lv_obj_t* stage = plain(g_alert.card);
  lv_obj_set_size(stage, g_alert.halo_size * 5 / 4, g_alert.halo_size * 5 / 4);
  lv_obj_align(stage, LV_ALIGN_TOP_MID, 0, 0);
  const uint32_t deep = settings_style::tone(ui_theme::popup_card(), kAmber).disc;
  g_alert.halo = plain(stage);
  lv_obj_set_style_radius(g_alert.halo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_alert.halo, lv_color_hex(deep), 0);
  lv_obj_set_size(g_alert.halo, g_alert.halo_size, g_alert.halo_size);
  lv_obj_center(g_alert.halo);
  lv_obj_t* outer = plain(stage);
  lv_obj_set_size(outer, ring, ring);
  lv_obj_set_style_radius(outer, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(outer, lv_color_hex(deep), 0);
  lv_obj_set_style_bg_opa(outer, LV_OPA_COVER, 0);
  lv_obj_center(outer);
  g_alert.disc = plain(stage);
  lv_obj_set_size(g_alert.disc, disc, disc);
  lv_obj_set_style_radius(g_alert.disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_alert.disc, lv_color_hex(kAmber), 0);
  lv_obj_set_style_bg_opa(g_alert.disc, LV_OPA_COVER, 0);
  lv_obj_center(g_alert.disc);
  g_alert.bell = icon_label(g_alert.disc, "bell-ring", kOnAmber);
  lv_obj_center(g_alert.bell);
  // Swings from its top, like a hanging bell.
  lv_obj_set_style_transform_pivot_x(g_alert.bell, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(g_alert.bell, LV_PCT(12), 0);

  const int stage_bottom = g_alert.halo_size * 5 / 4;
  g_alert.name = lv_label_create(g_alert.card);
  lv_obj_set_style_text_font(g_alert.name, popup_layout::font40(), 0);
  lv_obj_set_style_text_color(g_alert.name, lv_color_hex(ui_theme::text()), 0);
  lv_obj_set_style_text_align(g_alert.name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_alert.name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_alert.name, card_w - 2 * pad);
  lv_obj_align(g_alert.name, LV_ALIGN_TOP_MID, 0, stage_bottom);
  g_alert.line = lv_label_create(g_alert.card);
  lv_obj_set_style_text_font(g_alert.line, popup_layout::font24(), 0);
  lv_obj_set_style_text_color(g_alert.line, lv_color_hex(ui_theme::text_soft()), 0);
  lv_obj_set_style_text_align(g_alert.line, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_alert.line, card_w - 2 * pad);
  lv_obj_align_to(g_alert.line, g_alert.name, LV_ALIGN_OUT_BOTTOM_MID, 0, Device::kGridGap / 2);

  // "Eggs is done too"
  g_alert.also = plain(g_alert.card);
  lv_obj_set_size(g_alert.also, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(g_alert.also, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_alert.also, lv_color_hex(deep), 0);
  lv_obj_set_style_bg_opa(g_alert.also, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(g_alert.also, Device::kGridGap, 0);
  lv_obj_set_style_pad_ver(g_alert.also, Device::kGridGap / 3, 0);
  g_alert.also_label = lv_label_create(g_alert.also);
  lv_obj_set_style_text_font(g_alert.also_label, popup_layout::font20(), 0);
  lv_obj_set_style_text_color(g_alert.also_label, lv_color_hex(ui_theme::text()), 0);
  lv_label_set_long_mode(g_alert.also_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(g_alert.also_label, card_w - 4 * pad, 0);
  lv_obj_align_to(g_alert.also, g_alert.line, LV_ALIGN_OUT_BOTTOM_MID, 0, Device::kGridGap);
  lv_obj_add_flag(g_alert.also, LV_OBJ_FLAG_HIDDEN);

  // Stop (amber) and +1 min side by side; stacked on an upright screen.
  const int button_h = popup_layout::scale(84);
  const int gap = Device::kGridGap;
  const int inner_w = card_w - 2 * pad;
  const int stop_w = upright ? inner_w : (inner_w - gap) * 3 / 5;
  const int add_w = upright ? inner_w : inner_w - gap - stop_w;
  lv_obj_t* stop = lv_button_create(g_alert.card);
  lv_obj_remove_style_all(stop);
  lv_obj_set_size(stop, stop_w, button_h);
  lv_obj_set_style_radius(stop, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(stop, lv_color_hex(kAmber), 0);
  lv_obj_set_style_bg_color(stop, lv_color_hex(0xFFC35C), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(stop, LV_OPA_COVER, 0);
  lv_obj_add_flag(stop, LV_OBJ_FLAG_PRESS_LOCK);
  disable_pressed_button_animation(stop);
  lv_obj_add_event_cb(stop, on_stop, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* stop_label = lv_label_create(stop);
  lv_obj_set_style_text_font(stop_label, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(stop_label, lv_color_hex(kOnAmber), 0);
  lv_label_set_text(stop_label, tr().timer_stop);
  lv_obj_center(stop_label);
  lv_obj_t* add = lv_button_create(g_alert.card);
  lv_obj_remove_style_all(add);
  lv_obj_set_size(add, add_w, button_h);
  lv_obj_set_style_radius(add, LV_RADIUS_CIRCLE, 0);
  lv_color_t fill, raised;
  lv_opa_t opa, pressed;
  popup_nav_style::fill(lv_color_hex(ui_theme::popup_card()), lv_color_hex(kAmber), fill, opa);
  popup_nav_style::fill_raised(lv_color_hex(ui_theme::popup_card()), lv_color_hex(kAmber), raised, pressed);
  popup_nav_style::set_bg(add, fill, opa, LV_PART_MAIN);
  popup_nav_style::set_bg(add, raised, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(add, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_flag(add, LV_OBJ_FLAG_PRESS_LOCK);
  disable_pressed_button_animation(add);
  lv_obj_add_event_cb(add, on_add_minute, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* add_label = lv_label_create(add);
  lv_obj_set_style_text_font(add_label, popup_layout::font28(), 0);
  lv_obj_set_style_text_color(add_label, lv_color_hex(ui_theme::text()), 0);
  lv_label_set_text(add_label, tr().timer_add_minute);
  lv_obj_center(add_label);
  if (upright) {
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_align_to(stop, add, LV_ALIGN_OUT_TOP_MID, 0, -gap);
  } else {
    lv_obj_align(stop, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(add, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  }
  lv_obj_add_flag(g_alert.backdrop, LV_OBJ_FLAG_HIDDEN);
}

void show_ringing() {
  if (!g_alert.backdrop) build_alert();
  const Timer& first = g_ring.timers[0];
  lv_label_set_text(g_alert.name, first.name[0] ? TimerName(first).text : tr().timer_done_title);
  char length[16];
  voice_timer::formatClock(first.total_s, length, sizeof(length));
  lv_label_set_text_fmt(g_alert.line, tr().timer_set_for, length);
  if (g_ring.count > 1) {
    // Room for every other name in full (kMaxRinging - 1 of them).
    char names[(kMaxRinging - 1) * (voice_timer::kMaxNameBytes + 2)] = "";
    for (uint8_t i = 1; i < g_ring.count; ++i) {
      if (i > 1) strncat(names, ", ", sizeof(names) - strlen(names) - 1);
      strncat(names, TimerName(g_ring.timers[i]).text, sizeof(names) - strlen(names) - 1);
    }
    lv_label_set_text_fmt(g_alert.also_label, tr().timer_done_too, names);
    lv_obj_remove_flag(g_alert.also, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_alert.also, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(g_alert.backdrop, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_alert.backdrop);
}

// A finished timer: the card (or one more name on it) and the ringing.
void take_finished() {
  Timer timer;
  bool added = false;
  while (voice_timers::takeFinished(&timer)) {
    if (g_ring.count < kMaxRinging) {
      g_ring.speak_at = g_ring.count;
      g_ring.timers[g_ring.count++] = timer;
    }
    added = true;
  }
  if (!added) return;
  const uint32_t now = millis();
  // Every newly finished timer rings in full, also one that finished after
  // the card of an earlier one stopped ringing.
  g_ring.active = true;
  g_ring.started_ms = now;
  g_ring.next_ms = now;
  g_ring.first_round_end_ms = now;
  g_ring.speech_until_ms = now;
  g_ring.rounds = 0;
  g_ring.spoke = false;
  // The display wakes and the screensaver goes for the card. A camera in
  // full screen draws straight into the framebuffer, over the card: it closes.
  powerManager.wakeFromDisplaySleep("timer alert");
  hide_image_screensaver();
  if (camera_popup_is_full_screen()) hide_camera_popup();
  displayManager.resetActivityTimer();
  g_ring.activity_ms = now;
  show_ringing();
  start_animation(true);
  Serial.printf("%s Alert for %s\n", kLogPrefix, timer.id);
}

void ring_step() {
  if (!g_ring.active) return;
  const uint32_t now = millis();
  const bool ringing = now - g_ring.started_ms < kRingLimitMs;
  // The display stays on while it rings; afterwards it may sleep as usual
  // with the card kept for whoever comes back.
  if (ringing && now - g_ring.activity_ms >= kActivityRefreshMs) {
    g_ring.activity_ms = now;
    displayManager.resetActivityTimer();
  }
  // Popups opened meanwhile stay below the card.
  lv_obj_t* layer = lv_layer_top();
  const uint32_t children = lv_obj_get_child_count(layer);
  if (children && lv_obj_get_child(layer, static_cast<int32_t>(children - 1)) != g_alert.backdrop) {
    lv_obj_move_foreground(g_alert.backdrop);
  }
  if (!ringing) {
    if (g_alert.animating) {
      Serial.printf("%s Ringing ended after %lu s\n", kLogPrefix, static_cast<unsigned long>(kRingLimitMs / 1000));
      stop_ringing();
    }
    return;
  }
  const uint8_t sound = voice_timers::sound();
  // Sound off or the timer volume at 0: the card and the spoken sentence only.
  const bool silent = sound == voice_timer::kSoundOff || voice_timers::volumePercent() == 0;
  // Home Assistant says which timer finished once the first round ended.
  if (!g_ring.spoke && (g_ring.rounds > 0 || silent) &&
      static_cast<int32_t>(now - g_ring.first_round_end_ms) >= 0 && !speaker::chimePlaying()) {
    g_ring.spoke = true;
    voice_timers::requestAction("speak", g_ring.timers[g_ring.speak_at].id);
    g_ring.next_ms = now + kSpeakStartMs;
    g_ring.speech_until_ms = now + kSpeakStartMs + kSpeechWaitMs;
    return;
  }
  if (silent || static_cast<int32_t>(now - g_ring.next_ms) < 0) return;
  // A chime still playing, or the spoken sentence, finishes first. Music or
  // a camera's sound does not hold the alert: the chime ends that clip.
  if (speaker::chimePlaying() ||
      (speaker::outputBusy() && static_cast<int32_t>(now - g_ring.speech_until_ms) < 0)) {
    g_ring.next_ms = now + 500;
    return;
  }
  uint32_t round_ms = kBridgeRoundMs;
  if (sound < timer_chime::kChimeCount) {
    if (!speaker::playChime(sound, 2, voice_timers::volumePercent() / 100.0f)) {
      g_ring.next_ms = now + 1000;
      return;
    }
    round_ms = 2 * timer_chime::lengthMs(sound) + timer_chime::kRepeatGapMs;
  } else {
    voice_timers::requestAction("play_sound", g_ring.timers[0].id);
  }
  if (g_ring.rounds == 0) g_ring.first_round_end_ms = now + round_ms;
  ++g_ring.rounds;
  g_ring.next_ms = now + round_ms + kRingPauseMs;
}

uint32_t g_tick_ms = 0;

}  // namespace

void timer_label(const Timer& timer, char* out, size_t size) {
  if (!out || !size) return;
  if (timer.name[0]) {
    snprintf(out, size, "%s", timer.name);
    // Voice names arrive in lower case as heard; show them like a title.
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return;
  }
  char length[24];
  if (timer.total_s % 60u == 0 && timer.total_s < 3600u) {
    snprintf(length, sizeof(length), tr().timer_minutes, static_cast<int>(timer.total_s / 60u));
  } else {
    voice_timer::formatClock(timer.total_s, length, sizeof(length));
  }
  snprintf(out, size, tr().timer_unnamed, length);
}

int add_head_pill(lv_obj_t* bar, int right, int center_y, int height, uint32_t base) {
  const PillShape shape = current_shape();
  g_built_shape = shape;
  if (!bar || shape.count == 0 || !voice_timers::supported()) return right;
  const settings_style::Tone tone = settings_style::tone(base, kAmber);
  const lv_font_t* font = countdown_font();
  char widest[12];
  snprintf(widest, sizeof(widest), shape.hours ? "0:00:00" : "00:00");
  const int pad = height / 4;
  const int icon_w = height * 3 / 5;
  const int gap = height / 8;
  const int text_w = text_width(font, widest);
  char badge_text[8] = "";
  if (shape.count > 1) snprintf(badge_text, sizeof(badge_text), "+%u", static_cast<unsigned>(shape.count - 1));
  const int badge_h = height * 2 / 3;
  const lv_font_t* badge_font = settings_style::small_font();
  const int badge_w = shape.count > 1 ? LV_MAX(badge_h, text_width(badge_font, badge_text) + badge_h / 2) : 0;
  const int width = pad + icon_w + gap + text_w + (badge_w ? gap + badge_w : 0) + pad;
  const int x = right - width;

  lv_obj_t* pill = lv_button_create(bar);
  lv_obj_remove_style_all(pill);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(pill, x, center_y - height / 2);
  lv_obj_set_size(pill, width, height);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pill, lv_color_hex(tone.disc), 0);
  lv_obj_set_style_bg_color(pill, lv_color_hex(tone.control), LV_STATE_PRESSED);
  disable_pressed_button_animation(pill);
  lv_obj_add_event_cb(pill, on_pill, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* icon = icon_label(pill, "timer-outline", tone.icon);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, pad, 0);
  lv_obj_t* label = lv_label_create(pill);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(ui_theme::text()), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(label, text_w, lv_font_get_line_height(font));
  lv_obj_align(label, LV_ALIGN_LEFT_MID, pad + icon_w + gap, 0);
  pill_text(g_pill_text, sizeof(g_pill_text));
  lv_label_set_text(label, g_pill_text);
  remember_pill_label(label);
  if (badge_w) {
    lv_obj_t* badge = plain(pill);
    lv_obj_set_size(badge, badge_w, badge_h);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(tone.control), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -pad, 0);
    lv_obj_t* badge_label = lv_label_create(badge);
    lv_obj_set_style_text_font(badge_label, badge_font, 0);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(ui_theme::text()), 0);
    lv_label_set_text(badge_label, badge_text);
    lv_obj_center(badge_label);
  }
  return x;
}

bool head_time_opens_popup() { return voice_timers::supported(); }

void open_popup() {
  if (!voice_timers::supported()) return;
  const ui_theme::SurfaceScope popup_scope = popup_surface::scope();
  if (!g_popup) build_popup();
  if (!g_popup || !g_popup->card) return;
  popup_shell_use_no_tile_disc();
  lv_obj_set_style_bg_color(g_popup->card, lv_color_hex(ui_theme::popup_card()), 0);
  style_popup();
  lv_obj_remove_flag(g_popup->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_popup->overlay, LV_OBJ_FLAG_CLICKABLE);
  show_popup_shell(g_popup->overlay, g_popup->card, g_popup->title, g_popup->icon, g_popup->close,
                   on_popup_dismissed);
  refresh_popup(true);
}

void hide_popup() {
  if (!g_popup || !g_popup->card) return;
  hide_popup_shell(g_popup->card);
  cancel_popup_open(g_popup->card);
  lv_obj_add_flag(g_popup->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_popup->overlay, LV_OBJ_FLAG_CLICKABLE);
}

void theme_changed() {
  if (!g_alert.backdrop) return;
  const bool shown = !lv_obj_has_flag(g_alert.backdrop, LV_OBJ_FLAG_HIDDEN);
  const bool animating = g_alert.animating;
  start_animation(false);
  lv_obj_delete(g_alert.backdrop);
  g_alert = Alert{};
  if (!shown || !g_ring.count) return;
  show_ringing();
  start_animation(animating);
}

bool popup_visible() {
  return g_popup && g_popup->card && !lv_obj_has_flag(g_popup->card, LV_OBJ_FLAG_HIDDEN) &&
         popup_shell_body() == g_popup->card;
}

void service() {
  if (!voice_timers::supported()) return;
  voice_timers::checkExpired();
  take_finished();
  ring_step();
  const uint32_t now = millis();
  if (now - g_tick_ms < kTickMs) return;
  g_tick_ms = now;
  // A pill came, went or changed its shape: every head again (only the head
  // bar layout has heads).
  if (!grid_layout::head_bar()) {
    g_built_shape = current_shape();
  } else if (current_shape() != g_built_shape) {
    tiles_rebuild_heads(false);
  } else if (g_built_shape.count) {
    refresh_pills();
  }
  refresh_popup(false);
  timer_tile::refresh();
}

bool serviceSleep() {
  // The awake loop takes the finished timer and shows its card.
  if (!voice_timers::supported()) return false;
  voice_timers::checkExpired();
  if (!voice_timers::hasFinished()) return false;
  powerManager.wakeFromDisplaySleep("timer alert");
  return true;
}

}  // namespace timer_ui
