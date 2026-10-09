#include "src/ui/popups/device/device_popup.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/config/pin_access.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/tiles/runtime/tile_icon_source.h"
#include "src/tiles/runtime/tile_renderer_shared.h"
#include "src/types/device/device_control.h"
#include "src/types/device/device_tile.h"
#include "src/types/device/device_visual.h"
#include "src/ui/navigation/view_navigation.h"
#include "src/ui/popups/camera/camera_popup.h"
#include "src/ui/popups/climate/climate_popup.h"
#include "src/ui/popups/cover/cover_popup.h"
#include "src/ui/popups/energy/energy_popup.h"
#include "src/ui/popups/light/light_popup.h"
#include "src/ui/popups/media/media_popup.h"
#include "src/ui/popups/pin/pin_keypad_geometry.h"
#include "src/ui/popups/pin/pin_popup.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/popups/popup_nav_style.h"
#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_shell.h"
#include "src/ui/popups/popup_surface.h"
#include "src/ui/popups/sensor/sensor_popup.h"
#include "src/ui/popups/weather/weather_popup.h"
#include "src/ui/shared/command_pacer.h"
#include "src/ui/shared/title_label.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_pulse.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/ui_theme.h"

namespace {

using device_detail::Detail;
using device_visual::Visual;
using i18n::DeviceLabel;

const char* text(DeviceLabel label) { return device_visual::text(label); }

// The Light popup's vertical control (light_popup.cpp kVerticalSlider*).
int track_w() { return popup_layout::contentScale(160); }
int track_h() { return popup_layout::contentScale(340); }
int track_radius() { return popup_layout::contentScale(32); }

constexpr uint32_t kRemoteBlockMs = 3000;
constexpr uint32_t kNoticeMs = 5000;
constexpr uint32_t kOpenConfirmMs = 5000;
constexpr uint32_t kOpenDoneMs = 2000;
constexpr int kDragThreshold = popup_layout::scale(10);

enum class FanMenu : uint8_t { Preset, Oscillation, Direction };
struct FanPill {
  const char* caption = "";
  String value;
  FanMenu menu = FanMenu::Preset;
  uint8_t option_count = 0;
  int8_t selected = 0;
};

struct Popup {
  lv_obj_t* overlay = nullptr;
  lv_obj_t* card = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* close = nullptr;
  // Hidden header value: the Alarm's state (key popups keep the state in the
  // header like the PIN entry); empty for Lock and Fan.
  lv_obj_t* value = nullptr;
  lv_obj_t* body = nullptr;
  DevicePopupTarget target;
  uint32_t card_rgb = popup_surface::default_card();
  uint32_t icon_rgb = device_visual::grey();
  bool open = false;
  // Lock Open like Home Assistant: "Really open?" for 5 s, "Door open" 2 s.
  uint32_t open_confirm_until = 0;
  uint32_t open_done_until = 0;
  // The last command's answer below the controls.
  String notice;
  uint32_t notice_until = 0;
  lv_timer_t* expiry_timer = nullptr;
  // Fan slider.
  lv_obj_t* fan_track = nullptr;
  lv_obj_t* fan_fill = nullptr;
  lv_obj_t* fan_value = nullptr;
  lv_obj_t* fan_segments[4] = {};
  uint8_t fan_segment_count = 0;
  bool fan_dragging = false;
  bool fan_moved = false;
  lv_point_t fan_press = {0, 0};
  uint8_t fan_drag = 0;
  uint8_t fan_hold = 0;
  uint32_t fan_hold_until = 0;
  // Three tones like the Light popup (user 02.10.): the card, the circle fill
  // for the rail and the unlit speeds, the state color for what is lit.
  lv_color_t fan_accent = lv_color_hex(0x00BCD4);
  lv_color_t fan_rest = lv_color_hex(0x3A3A3A);
  lv_opa_t fan_rest_opa = LV_OPA_COVER;
  // Fan menus (the Climate popup's control menu).
  FanPill pills[3];
  lv_obj_t* pill_objects[3] = {};
  uint8_t pill_count = 0;
  lv_obj_t* fan_menu = nullptr;
  int8_t fan_menu_index = -1;
  bool refresh_due = false;
} pop;

command_pacer::Pacer g_pacer;
lv_timer_t* g_live_timer = nullptr;

// A code entry for a Lock or Alarm command through the PIN popup.
struct CodeEntry {
  bool open = false;
  bool from_tile = false;
  bool in_flight = false;
  DevicePopupTarget target;
  char action[24] = {};
  String command_id;
  char code[pin_access::kInputMaxDigits + 1] = {};
  // The Bridge's block after too many wrong codes, for this entity.
  String lockout_entity;
  uint32_t lockout_until = 0;
  lv_timer_t* lockout_timer = nullptr;
} g_code;

// The last command sent without a code: its answer may ask for one.
struct LastCommand {
  String id;
  DevicePopupTarget target;
  char action[24] = {};
  bool from_tile = false;
} g_last;

bool time_before(uint32_t until) { return until && static_cast<int32_t>(until - millis()) > 0; }

int content_width() { return popup_layout::kCardWidth - 2 * lv_obj_get_style_pad_top(pop.card, LV_PART_MAIN); }
int content_height() { return popup_layout::kCardHeight - 2 * lv_obj_get_style_pad_top(pop.card, LV_PART_MAIN); }

void refresh(bool show);

void expiry_timer_cb(lv_timer_t*) {
  pop.expiry_timer = nullptr;
  if (pop.open) refresh(false);
}

// Refreshes the popup once `until` has passed (confirm, done and notice times).
void refresh_at(uint32_t until) {
  if (!until) return;
  const int32_t wait = static_cast<int32_t>(until - millis()) + 50;
  if (pop.expiry_timer) lv_timer_delete(pop.expiry_timer);
  pop.expiry_timer = lv_timer_create(expiry_timer_cb, static_cast<uint32_t>(std::max<int32_t>(wait, 50)), nullptr);
  if (pop.expiry_timer) lv_timer_set_repeat_count(pop.expiry_timer, 1);
}

lv_obj_t* make_label(lv_obj_t* parent, lv_color_t color, const lv_font_t* font, const char* value) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_label_set_text(label, value);
  return label;
}

lv_obj_t* make_icon(lv_obj_t* parent, lv_color_t color, const char* icon) {
  lv_obj_t* label = make_label(parent, color, FONT_MDI_ICONS, getMdiChar(icon).c_str());
  popup_layout::applyIconScale(label);
  return label;
}

lv_obj_t* make_box(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return box;
}

lv_obj_t* make_button(lv_obj_t* parent, int x, int y, int w, int h, int radius) {
  lv_obj_t* button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  ui_surface_style::apply_radius(button, radius, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_outline_width(button, 0, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_style_anim_duration(button, 0, 0);
  lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_opa(button, LV_OPA_30, LV_STATE_DISABLED);
  disable_pressed_button_animation(button);
  return button;
}

void control_fill(lv_color_t& fill, lv_opa_t& opa) {
  popup_nav_style::fill(lv_color_hex(pop.card_rgb), lv_color_hex(pop.icon_rgb), fill, opa);
}

// A pressed control: one control step up, like the PIN keys (user 02.10.:
// the keys, pills and the switch showed no press at all). A lit control
// presses a step lighter in its own color.
void control_pressed(lv_obj_t* obj, bool lit, lv_color_t lit_color) {
  lv_color_t raised;
  lv_opa_t opa;
  popup_nav_style::fill_raised(lv_color_hex(pop.card_rgb), lv_color_hex(pop.icon_rgb), raised, opa);
  if (lit) {
    raised = lv_color_hex(brighten_rgb_color(lv_color_to_u32(lit_color) & 0xFFFFFF, 0x10));
    opa = LV_OPA_COVER;
  }
  popup_nav_style::set_bg(obj, raised, opa, LV_PART_MAIN | LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(obj, LV_PART_MAIN | LV_STATE_PRESSED);
}

// Opacity 1 -> 0 -> 1 in 1 s for a control's symbol while a command runs,
// in step with the header icon although a refresh rebuilds the symbol
// (ui_pulse.h).
void pulse_exec(void* obj, int32_t) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), ui_pulse::opa_now(), 0);
}

void pulse(lv_obj_t* obj) { ui_pulse::start(obj, pulse_exec); }

// A pill in the bottom row (the Light popup's controls row): the circle fill;
// `lit` in the state color (Open's confirm step).
lv_obj_t* pill(int x, int y, int w, int h, const char* icon, const char* label, bool lit, bool enabled,
               lv_event_cb_t cb, uint32_t lit_rgb, void* user_data = nullptr) {
  lv_color_t fill;
  lv_opa_t opa;
  control_fill(fill, opa);
  const lv_color_t bg = lit ? lv_color_hex(lit_rgb) : fill;
  lv_obj_t* button = make_button(pop.body, x, y, w, h, h / 2);
  lv_obj_set_style_radius(button, h / 2, 0);
  popup_nav_style::set_bg(button, bg, lit ? LV_OPA_COVER : opa, LV_PART_MAIN);
  control_pressed(button, lit, bg);
  lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(button, popup_layout::scale(10), 0);
  const lv_color_t fg = lit ? lv_color_hex(pop.card_rgb) : lv_color_hex(ui_theme::text());
  make_icon(button, fg, icon);
  make_label(button, fg, popup_layout::font24(), label);
  if (!enabled) {
    lv_obj_add_state(button, LV_STATE_DISABLED);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
  } else if (cb) {
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
  }
  return button;
}

// Why the panel may not operate the device, or the last answer, centered.
lv_obj_t* hint(int y, const char* value) {
  const int content_w = content_width();
  lv_obj_t* label = make_label(pop.body, lv_color_hex(tone_color::off_icon()), popup_layout::font20(), value);
  lv_obj_set_width(label, content_w - 2 * popup_layout::scale(40));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(label, popup_layout::scale(40), y);
  return label;
}

// The value above the control, at the Light popup's place and size.
lv_obj_t* value_label(const char* value) {
  lv_obj_t* box = make_box(pop.body);
  lv_obj_set_size(box, content_width(), popup_layout::kValueHeight);
  lv_obj_set_pos(box, 0, popup_layout::kValueY);
  lv_obj_t* label = make_label(box, lv_color_hex(ui_theme::text()), popup_layout::font40(), value);
  lv_obj_center(label);
  return label;
}

const char* hint_text(const Detail& d) {
  if (time_before(pop.notice_until)) return pop.notice.c_str();
  return device_control::blocked_reason(pop.target.type, d, nullptr);
}

const char* result_text(TileType type, const char* status) {
  if (!std::strcmp(status, "not_allowed")) return text(DeviceLabel::ResultNotAllowed);
  if (!std::strcmp(status, "busy")) return text(DeviceLabel::ResultBusy);
  if (!std::strcmp(status, "not_secured")) {
    // The Bridge sees the panel unsecured even if the panel does not (yet).
    const char* reason = device_control::blocked_reason(type, Detail{}, nullptr);
    return reason ? reason : text(DeviceLabel::NeedPairAndPassword);
  }
  if (!std::strcmp(status, "unsupported")) return text(DeviceLabel::ResultUnsupported);
  if (!std::strcmp(status, "unavailable")) return text(DeviceLabel::ResultUnavailable);
  if (!std::strcmp(status, "expired")) return text(DeviceLabel::ResultExpired);
  if (!std::strcmp(status, "no_answer")) return text(DeviceLabel::ResultNoAnswer);
  return text(DeviceLabel::ResultFailed);
}

void set_notice(const char* value) {
  pop.notice = value ? value : "";
  pop.notice_until = millis() + kNoticeMs;
  refresh_at(pop.notice_until);
}

// ---------------------------------------------------------------------------
// Lock

void lock_open_tapped(lv_event_t*) {
  if (time_before(pop.open_confirm_until)) {
    pop.open_confirm_until = 0;
    device_request(pop.target, "open", false);
    return;
  }
  // Like Home Assistant: Open needs a second tap within 5 s.
  pop.open_confirm_until = millis() + kOpenConfirmMs;
  refresh_at(pop.open_confirm_until);
  refresh(false);
}

void lock_toggle_tapped(lv_event_t*) {
  const Detail d = device_control::detail(pop.target.entity);
  device_request(pop.target, device_visual::lock_on(d) ? "unlock" : "lock", false);
}

void lock_button_tapped(lv_event_t* e) {
  device_request(pop.target, lv_event_get_user_data(e) ? "lock" : "unlock", false);
}

void build_lock(const Detail& d, const Visual& v) {
  const int content_w = content_width();
  const bool usable = d.valid && d.available && device_control::panel_secured();
  const bool moving = device_visual::lock_moving(d);
  value_label(v.label.c_str());
  const int nav = popup_layout::kNavHeight;
  const int gap = popup_layout::scale(16);
  const int w = track_w();
  int h = track_h();
  int track_y = popup_layout::kBodyY + (popup_layout::kBodyHeight - h) / 2;
  const bool can_open = (d.features & device_detail::kLockOpen) != 0;
  // The hint right below the value; on small panels the switch moves down
  // and gets shorter so it stays clear of the hint and the Open pill.
  if (const char* reason = hint_text(d)) {
    lv_obj_t* label = hint(popup_layout::kValueY + popup_layout::kValueHeight, reason);
    lv_obj_update_layout(label);
    const int below_hint = lv_obj_get_y(label) + lv_obj_get_height(label) + gap;
    if (track_y < below_hint) {
      const int bottom = can_open ? content_height() - popup_layout::kNavBottomInset - nav - gap : track_y + h;
      track_y = below_hint;
      h = std::min(h, bottom - track_y);
    }
  }
  if (!d.valid || !d.available) {
    // Nothing to operate; the value says why.
  } else if (device_visual::lock_buttons(d)) {
    // Unknown or jammed: both directions as pills, Unlock left, Lock right.
    const int pw = popup_layout::scale(230);
    const int x0 = (content_w - 2 * pw - gap) / 2;
    const int y = track_y + (h - nav) / 2;
    // The event user data tells Lock (non-null) from Unlock.
    pill(x0, y, pw, nav, "lock-open-variant", text(DeviceLabel::Unlock), false, usable && d.unlock_allowed,
         lock_button_tapped, 0, nullptr);
    pill(x0 + pw + gap, y, pw, nav, "lock", text(DeviceLabel::Lock), false, usable, lock_button_tapped, 0,
         reinterpret_cast<void*>(1));
  } else {
    // The Light popup's vertical switch: up = locked (Home Assistant's lock
    // toggle). It shows only what the lock reports: a sent command waits
    // with the header icon pulsing and moves nothing (user 02.10.: a code the
    // lock itself rejected, which Home Assistant answers with ok, left the
    // thumb in orange on the target over the red lock as if it had worked).
    const char* target = device_control::pending_target(pop.target.entity);
    const bool sent = *target && std::strcmp(target, d.state) != 0;
    const bool up = device_visual::lock_on(d);
    const uint32_t color = v.color;
    lv_color_t rest;
    lv_opa_t rest_opa;
    control_fill(rest, rest_opa);
    // The rail in the circle fill like the Light popup's switch: card, circle
    // fill and state color, three tones of one color (user 02.10.).
    lv_obj_t* track = make_box(pop.body);
    lv_obj_set_size(track, w, h);
    lv_obj_set_pos(track, (content_w - w) / 2, track_y);
    lv_obj_set_style_radius(track, track_radius(), 0);
    lv_obj_set_style_bg_color(track, rest, 0);
    lv_obj_set_style_bg_opa(track, rest_opa, 0);
    lv_obj_t* thumb = make_box(track);
    lv_obj_set_size(thumb, w, h / 2);
    lv_obj_set_pos(thumb, 0, up ? 0 : h - h / 2);
    lv_obj_set_style_radius(thumb, track_radius(), 0);
    lv_obj_set_style_bg_color(thumb, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_t* symbol = make_icon(thumb, lv_color_hex(pop.card_rgb), v.icon);
    lv_obj_center(symbol);
    if (moving) pulse(symbol);
    if (!usable) {
      lv_obj_set_style_opa(track, LV_OPA_30, 0);
    } else if (!moving && !sent) {
      // A tap switches to the other side, like Home Assistant's toggle; the
      // rail shows the press.
      lv_obj_add_flag(track, LV_OBJ_FLAG_CLICKABLE);
      control_pressed(track, false, rest);
      lv_obj_add_event_cb(track, lock_toggle_tapped, LV_EVENT_CLICKED, nullptr);
    }
  }
  if (!can_open) return;
  const bool done = time_before(pop.open_done_until);
  const bool confirm = !done && time_before(pop.open_confirm_until);
  const bool at_open = device_detail::is(d, "open") || device_detail::is(d, "opening");
  const char* label = done ? text(DeviceLabel::DoorOpen)
                    : confirm ? text(DeviceLabel::ReallyOpen)
                              : text(DeviceLabel::OpenDoor);
  // The pill fits the longest of its labels beside the icon, so nothing is
  // cut and it keeps its width when the label changes (Polish "Na pewno
  // otworzyć?" ran past both ends of the 290 px pill).
  int pw = popup_layout::scale(290);
  lv_point_t icon_size;
  lv_text_get_size(&icon_size, getMdiChar("door-open").c_str(), FONT_MDI_ICONS, 0, 0,
                   LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  for (const DeviceLabel id : {DeviceLabel::OpenDoor, DeviceLabel::ReallyOpen, DeviceLabel::DoorOpen}) {
    lv_point_t size;
    lv_text_get_size(&size, text(id), popup_layout::font24(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    pw = std::max(pw, static_cast<int>(icon_size.x + popup_layout::scale(10) + size.x +
                                       2 * popup_layout::scale(16)));
  }
  pw = std::min(pw, content_w);
  pill((content_w - pw) / 2, content_height() - popup_layout::kNavBottomInset - nav, pw, nav,
       done ? "check" : "door-open", label, confirm,
       !done && usable && !moving && !at_open && d.unlock_allowed, lock_open_tapped, v.color);
}

// ---------------------------------------------------------------------------
// Alarm panel

void alarm_key_tapped(lv_event_t* e) {
  const int mode = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  device_request(pop.target,
                 mode < 0 ? "disarm" : device_control::alarm_mode(static_cast<size_t>(mode)).action, false);
}

// The supported modes as keys in two columns filling the PIN keypad's block
// (its width, gap and corner rounding in proportion), Disarm below over the
// full width; the current mode lit in the state color. Arming, disarming,
// pending and triggered: the mode keys give way to the state symbol pulsing
// in a circle, only the Disarm key stays at its place (user 01.10.). Only
// reported states show: a sent mode waits with the header icon pulsing and
// lights nothing (user 02.10.: a code the panel itself rejected, answered ok
// by Home Assistant, showed the arming look over the old state).
// A mode label keeps one line and its full word: it steps down from the
// 28 px key font while less than 12 px stays free on either side (French
// "Personnalisé" and Polish "Poza domem" touched the 7-inch and 480 keys).
const lv_font_t* alarm_key_font(const char* label, int key_w) {
  const lv_font_t* const sizes[] = {popup_layout::font28(), popup_layout::font24(), popup_layout::font20()};
  const int room = key_w - 2 * popup_layout::scale(12);
  for (const lv_font_t* font : sizes) {
    lv_point_t size;
    lv_text_get_size(&size, label, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= room) return font;
  }
  return sizes[2];
}

void build_alarm(const Detail& d, const Visual& v) {
  const int content_w = content_width();
  const bool usable = d.valid && d.available && device_control::panel_secured();
  const pin_keypad::KeypadGeometry g = pin_keypad::keypad_geometry(pop.card, popup_layout::font24());
  const int block_w = 3 * g.key_w + 2 * g.gap;
  const int block_h = 4 * g.key_h + 3 * g.gap;
  int8_t entries[device_control::kAlarmModeCount + 1];
  int count = 0;
  for (size_t i = 0; i < device_control::kAlarmModeCount; ++i) {
    if (d.features & device_control::alarm_mode(i).feature) entries[count++] = static_cast<int8_t>(i);
  }
  entries[count++] = -1;
  const int kw = (block_w - g.gap) / 2;
  const int rows = (count + 1) / 2;
  // The keys fill the PIN entry's area from its row of code circles to the
  // bottom of its keypad; a hint takes the prompt line above. Never taller
  // than wide.
  int area_top = g.dots_y + (g.prompt_h - g.dot) / 2;
  if (const char* reason = hint_text(d)) {
    lv_obj_t* label = hint(g.prompt_y, reason);
    lv_obj_update_layout(label);
    area_top = std::max(area_top, static_cast<int>(lv_obj_get_y(label) + lv_obj_get_height(label)) + g.gap);
  }
  if (!d.valid || !d.available) return;
  const int area_h = g.keys_y + block_h - area_top;
  const int kh = std::max(1, std::min(kw, (area_h - (rows - 1) * g.gap) / rows));
  const int key_radius = kh * pin_keypad::kKeyRadius / std::max(1, g.key_h);
  const int block = rows * kh + (rows - 1) * g.gap;
  const int y0 = area_top + (area_h - block) / 2;
  const char* target = device_control::pending_target(pop.target.entity);
  const bool sent = *target && std::strcmp(target, d.state) != 0;
  const char* shown = d.state;
  const uint32_t lit = v.color;
  lv_color_t fill;
  lv_opa_t opa;
  control_fill(fill, opa);
  const bool busy = device_visual::alarm_disarm_only(d) || device_detail::is(d, "disarming");
  if (busy) {
    const uint32_t color = v.color;
    const int top = y0;
    const int bottom = y0 + (rows - 1) * (kh + g.gap) - g.gap;
    const int diameter = std::min(track_w(), bottom - top);
    lv_obj_t* circle = diameter > 0 ? make_box(pop.body) : nullptr;
    if (circle) {
    lv_obj_set_size(circle, diameter, diameter);
    lv_obj_set_pos(circle, (content_w - diameter) / 2, (top + bottom - diameter) / 2);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    // The circle fill like the keys; only the symbol takes the state color.
    lv_obj_set_style_bg_color(circle, fill, 0);
    lv_obj_set_style_bg_opa(circle, opa, 0);
    lv_obj_t* symbol = make_icon(circle, lv_color_hex(color), v.icon);
    lv_obj_center(symbol);
    pulse(symbol);
    }
  }
  // A sent command greys the other modes until the panel reports; Disarm
  // stays possible (it aborts arming in Home Assistant).
  const bool modes_locked = sent || device_visual::alarm_disarm_only(d);
  for (int i = 0; i < count; ++i) {
    const int mode = entries[i];
    if (busy && mode >= 0) continue;
    const char* state = mode < 0 ? "disarmed" : device_control::alarm_mode(static_cast<size_t>(mode)).state;
    const bool selected = std::strcmp(shown, state) == 0;
    // An odd last key (Disarm) spans both columns.
    const bool wide = i == count - 1 && count % 2 == 1;
    const int x = g.keys_x + (wide ? 0 : (i % 2) * (kw + g.gap));
    const int y = y0 + (i / 2) * (kh + g.gap);
    lv_obj_t* key = make_button(pop.body, x, y, wide ? block_w : kw, kh, key_radius);
    const lv_color_t bg = selected ? lv_color_hex(lit) : fill;
    popup_nav_style::set_bg(key, bg, selected ? LV_OPA_COVER : opa, LV_PART_MAIN);
    control_pressed(key, selected, bg);
    lv_obj_set_flex_flow(key, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(key, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(key, kh / 16, 0);
    const lv_color_t fg = selected ? lv_color_hex(pop.card_rgb) : lv_color_hex(ui_theme::text());
    make_icon(key, fg, mode < 0 ? "shield-off" : device_control::alarm_mode(static_cast<size_t>(mode)).icon);
    // The Disarm key names the action unless the alarm is disarmed.
    const char* label =
        mode >= 0 ? i18n::device_label(device_visual::language(),
                                       static_cast<DeviceLabel>(
                                           device_control::alarm_mode(static_cast<size_t>(mode)).label))
        : selected ? i18n::alarm_state_label(device_visual::language(), "disarmed")
                   : text(DeviceLabel::Disarm);
    make_label(key, fg, alarm_key_font(label, wide ? block_w : kw), label);
    const bool disarming = device_detail::is(d, "disarming");
    const bool enabled = usable && !selected && !disarming && (mode < 0 ? d.disarm_allowed : !modes_locked);
    if (!enabled) {
      lv_obj_remove_flag(key, LV_OBJ_FLAG_CLICKABLE);
      if (!selected && (!usable || disarming || (mode < 0 ? !d.disarm_allowed : modes_locked))) {
        lv_obj_add_state(key, LV_STATE_DISABLED);
      }
    } else {
      lv_obj_add_event_cb(key, alarm_key_tapped, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(mode)));
    }
  }
}

// ---------------------------------------------------------------------------
// Fan

bool fan_held(uint8_t& value) {
  if (pop.fan_dragging) {
    value = pop.fan_drag;
    return true;
  }
  if (!time_before(pop.fan_hold_until)) return false;
  value = pop.fan_hold;
  return true;
}

void hold_fan(uint8_t value) {
  pop.fan_hold = value;
  pop.fan_hold_until = millis() + kRemoteBlockMs;
  refresh_at(pop.fan_hold_until);
}

String fan_value_text(const Detail& d, bool on, int percentage) {
  if (!d.valid || !d.available) return i18n::entity_state_label(device_visual::language(), "unavailable");
  const auto& tr = i18n::strings(device_visual::language());
  if (!on) return tr.light_off;
  // The preset stays in its own pill; the value above the slider is always
  // the speed (user 01.10.).
  if (!device_visual::fan_has_speed(d)) return tr.light_on;
  return device_visual::fan_level_text(device_detail::fan_speed_count(d), percentage);
}

void set_fan_fill(const Detail& d, int percentage) {
  if (pop.fan_value) {
    lv_label_set_text(pop.fan_value, fan_value_text(d, percentage > 0, percentage).c_str());
  }
  if (pop.fan_segment_count) {
    const int speed = device_detail::fan_speed_of(d, percentage);
    for (uint8_t i = 0; i < pop.fan_segment_count; ++i) {
      const bool lit = i < speed;
      lv_obj_set_style_bg_color(pop.fan_segments[i], lit ? pop.fan_accent : pop.fan_rest, 0);
      lv_obj_set_style_bg_opa(pop.fan_segments[i], lit ? LV_OPA_COVER : pop.fan_rest_opa, 0);
    }
    return;
  }
  if (!pop.fan_fill || !pop.fan_track) return;
  const int h = lv_obj_get_height(pop.fan_track);
  int fill = h * percentage / 100;
  if (fill < track_radius() * 2) fill = track_radius() * 2;
  lv_obj_set_size(pop.fan_fill, track_w(), fill);
  lv_obj_set_y(pop.fan_fill, h - fill);
  lv_obj_set_flag(pop.fan_fill, LV_OBJ_FLAG_HIDDEN, percentage <= 0);
}

void send_fan_level(uint8_t value) {
  device_control::fan_percentage(pop.target.entity, value);
  g_pacer.sent(millis(), value);
}

void fan_live_cb(lv_timer_t*) {
  g_live_timer = nullptr;
  if (pop.fan_dragging && pop.fan_moved) send_fan_level(pop.fan_drag);
}

uint8_t fan_level_at(lv_obj_t* track, const Detail& d) {
  lv_point_t point = {0, 0};
  if (lv_indev_t* indev = lv_indev_get_act()) lv_indev_get_point(indev, &point);
  lv_area_t area;
  lv_obj_get_coords(track, &area);
  const int h = std::max<int>(1, lv_area_get_height(&area));
  const float share = static_cast<float>(area.y2 - point.y) / h;
  if (pop.fan_segment_count) {
    const int count = pop.fan_segment_count;
    const int speed = std::max(1, std::min(count, static_cast<int>(share * count) + 1));
    return static_cast<uint8_t>(device_detail::fan_percentage_of(d, speed));
  }
  // The Light popup's brightness logic: 1 % at the bottom, 100 % at the top.
  return static_cast<uint8_t>(std::max(1, std::min(100, static_cast<int>(lroundf(share * 100)))));
}

void on_fan_track(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* track = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  const Detail d = device_control::detail(pop.target.entity);
  if (!device_visual::fan_has_speed(d)) {
    if (code != LV_EVENT_CLICKED) return;
    const bool on = device_visual::fan_on(d);
    if (!(d.features & (on ? device_detail::kFanTurnOff : device_detail::kFanTurnOn))) return;
    device_control::fan_action(pop.target.entity, on ? "turn_off" : "turn_on");
    hold_fan(on ? 0 : 100);
    refresh(false);
    return;
  }
  lv_point_t point = {0, 0};
  if (lv_indev_t* indev = lv_indev_get_act()) lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED) {
    pop.fan_dragging = true;
    pop.fan_moved = false;
    pop.fan_press = point;
    g_pacer.begin_gesture();
    // A press on an off fan shows it in the fan color right away; the rail
    // keeps the circle fill.
    pop.fan_accent = lv_color_hex(device_visual::kCyan);
    if (pop.fan_fill) lv_obj_set_style_bg_color(pop.fan_fill, pop.fan_accent, 0);
  }
  if (!pop.fan_dragging) return;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    if (!pop.fan_moved) {
      const int dx = point.x - pop.fan_press.x, dy = point.y - pop.fan_press.y;
      pop.fan_moved = dx * dx + dy * dy >= kDragThreshold * kDragThreshold;
    }
    const uint8_t value = fan_level_at(track, d);
    if (value != pop.fan_drag || code == LV_EVENT_PRESSED) {
      pop.fan_drag = value;
      set_fan_fill(d, value);
      if (pop.fan_moved && !g_live_timer && !pop.fan_segment_count) {
        const uint32_t wait = g_pacer.wait(millis());
        if (wait == 0) {
          send_fan_level(value);
        } else {
          g_live_timer = lv_timer_create(fan_live_cb, wait, nullptr);
          if (g_live_timer) lv_timer_set_repeat_count(g_live_timer, 1);
        }
      }
    }
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    pop.fan_dragging = false;
    if (g_live_timer) {
      lv_timer_delete(g_live_timer);
      g_live_timer = nullptr;
    }
    const bool repeat = g_pacer.final_redundant(pop.fan_drag);
    g_pacer.end_gesture();
    if (!repeat) send_fan_level(pop.fan_drag);
    hold_fan(pop.fan_drag);
    if (pop.refresh_due) refresh(false);
  }
}

void fan_power(lv_event_t*) {
  const Detail d = device_control::detail(pop.target.entity);
  const bool on = device_visual::fan_on(d);
  if (!(d.features & (on ? device_detail::kFanTurnOff : device_detail::kFanTurnOn))) return;
  device_control::fan_action(pop.target.entity, on ? "turn_off" : "turn_on");
  hold_fan(on ? 0 : (d.has_percentage && d.percentage ? d.percentage : 100));
  refresh(false);
}

void close_fan_menu() {
  if (pop.fan_menu) lv_obj_delete(pop.fan_menu);
  pop.fan_menu = nullptr;
  pop.fan_menu_index = -1;
}

void choose_fan_option(FanMenu menu, int index) {
  const Detail d = device_control::detail(pop.target.entity);
  if (menu == FanMenu::Preset) {
    // "None" returns to the plain speed, which ends the preset.
    if (index == 0) {
      device_control::fan_percentage(pop.target.entity, d.has_percentage && d.percentage ? d.percentage : 100);
    } else if (index - 1 < d.preset_count) {
      device_control::fan_preset(pop.target.entity, d.presets[index - 1]);
    }
  } else if (menu == FanMenu::Oscillation) {
    device_control::fan_oscillate(pop.target.entity, index == 0);
  } else {
    device_control::fan_direction(pop.target.entity, index == 0 ? "forward" : "reverse");
  }
}

void on_fan_option(lv_event_t* e) {
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int pill_index = pop.fan_menu_index;
  close_fan_menu();
  if (pill_index >= 0 && pill_index < pop.pill_count) choose_fan_option(pop.pills[pill_index].menu, index);
  if (pop.refresh_due) refresh(false);
}

String fan_option_text(const Detail& d, FanMenu menu, int index) {
  const auto& tr = i18n::strings(device_visual::language());
  if (menu == FanMenu::Preset) {
    return index == 0 ? String(text(DeviceLabel::FanNone)) : device_visual::preset_text(d.presets[index - 1]);
  }
  if (menu == FanMenu::Oscillation) return index == 0 ? tr.light_on : tr.light_off;
  return index == 0 ? text(DeviceLabel::FanForward) : text(DeviceLabel::FanReverse);
}

lv_obj_t* menu_shell(lv_obj_t* parent, int x, int y, int w, int h, int radius, lv_color_t color,
                     lv_border_side_t sides) {
  lv_obj_t* part = make_box(parent);
  lv_obj_set_pos(part, x, y);
  lv_obj_set_size(part, w, h);
  lv_obj_set_style_bg_color(part, color, 0);
  lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(part, 1, 0);
  lv_obj_set_style_border_color(part, lv_color_black(), 0);
  lv_obj_set_style_border_opa(part, LV_OPA_40, 0);
  lv_obj_set_style_border_side(part, sides, 0);
  lv_obj_set_style_radius(part, radius, 0);
  return part;
}

void pill_texts(lv_obj_t* parent, const char* caption, const char* value, lv_color_t color) {
  lv_obj_t* label = make_label(parent, color, popup_layout::font20(), caption);
  lv_obj_set_style_text_opa(label, LV_OPA_70, 0);
  make_label(parent, color, popup_layout::font24(), value);
}

// The Climate popup's control menu (climate_popup.cpp open_control_menu): the
// pill grows upwards into the option list, the selected option white with
// card-colored text, a separator line and the current pill at the bottom,
// which closes the menu again.
void open_fan_menu(int index) {
  if (pop.fan_menu && pop.fan_menu_index == index) {
    close_fan_menu();
    return;
  }
  close_fan_menu();
  if (index < 0 || index >= pop.pill_count) return;
  const Detail d = device_control::detail(pop.target.entity);
  const FanPill& source = pop.pills[index];
  lv_obj_t* anchor = pop.pill_objects[index];
  const int x = lv_obj_get_x(anchor), y = lv_obj_get_y(anchor);
  const int w = lv_obj_get_width(anchor), pill_h = lv_obj_get_height(anchor);
  const int option_h = popup_layout::scale(64);
  const int separator_area = popup_layout::scale(9);
  const int separator_inset = popup_layout::scale(4);
  const int count = source.option_count;
  const int choices_h = count * option_h;
  const int menu_h = choices_h + separator_area + pill_h;
  lv_color_t fill;
  lv_opa_t opa;
  control_fill(fill, opa);
  const lv_color_t pressed = lv_color_mix(lv_color_hex(ui_theme::text()), fill, 40);
  pop.fan_menu = make_box(pop.body);
  lv_obj_set_pos(pop.fan_menu, x, y + pill_h - menu_h);
  lv_obj_set_size(pop.fan_menu, w, menu_h);
  lv_obj_add_flag(pop.fan_menu, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  pop.fan_menu_index = static_cast<int8_t>(index);
  // One continuous silhouette: round top cap, straight middle, round bottom pill.
  menu_shell(pop.fan_menu, 0, 0, w, option_h, option_h / 2, fill,
             static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT));
  menu_shell(pop.fan_menu, 0, option_h / 2, w, menu_h - option_h / 2 - pill_h / 2, 0, fill,
             static_cast<lv_border_side_t>(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT));
  menu_shell(pop.fan_menu, 0, choices_h + separator_area, w, pill_h, pill_h / 2, fill,
             static_cast<lv_border_side_t>(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT));
  for (int i = 0; i < count; ++i) {
    const bool selected = i == source.selected;
    lv_obj_t* option = make_button(pop.fan_menu, 0, i * option_h, w, option_h, option_h / 2);
    lv_obj_set_style_radius(option, option_h / 2, 0);
    lv_obj_set_style_bg_color(option, selected ? lv_color_hex(ui_theme::icon()) : fill, 0);
    lv_obj_set_style_bg_opa(option, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(option, pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_STATE_PRESSED);
    popup_nav_style::no_press_filter(option, LV_PART_MAIN | LV_STATE_PRESSED);
    const String label_text = fan_option_text(d, source.menu, i);
    lv_obj_t* label = make_label(option, selected ? lv_color_hex(pop.card_rgb) : lv_color_hex(ui_theme::text()),
                                 popup_layout::font24(), label_text.c_str());
    lv_obj_set_width(label, w - 8);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(option, on_fan_option, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }
  lv_obj_t* line = make_box(pop.fan_menu);
  lv_obj_set_size(line, w - 24, 1);
  lv_obj_set_pos(line, 12, choices_h + separator_area - 1 - separator_inset);
  lv_obj_set_style_bg_color(line, lv_color_mix(lv_color_hex(ui_theme::icon()), fill, 62), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  // The current pill closes the menu.
  lv_obj_t* current = make_button(pop.fan_menu, 0, choices_h + separator_area, w, pill_h, pill_h / 2);
  lv_obj_set_style_radius(current, pill_h / 2, 0);
  lv_obj_set_style_bg_opa(current, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(current, pressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(current, LV_OPA_COVER, LV_STATE_PRESSED);
  popup_nav_style::no_press_filter(current, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_flex_flow(current, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(current, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  pill_texts(current, source.caption, source.value.c_str(), lv_color_hex(ui_theme::text()));
  lv_obj_add_event_cb(
      current,
      [](lv_event_t*) {
        close_fan_menu();
        if (pop.refresh_due) refresh(false);
      },
      LV_EVENT_CLICKED, nullptr);
  lv_obj_move_foreground(pop.fan_menu);
}

void build_fan(const Detail& d) {
  const int content_w = content_width();
  const int w = track_w(), radius = track_radius();
  uint8_t held = 0;
  const bool holding = fan_held(held);
  const bool on = holding ? held > 0 : device_visual::fan_on(d);
  const int percentage = holding ? held : (on ? (d.has_percentage ? d.percentage : 100) : 0);
  const bool usable = d.valid && d.available;
  // On: the fan color; off or unavailable: the grey off color, like the
  // Light popup.
  const lv_color_t accent = lv_color_hex(on && usable ? device_visual::kCyan : device_visual::grey());
  pop.fan_segment_count = 0;
  pop.fan_accent = accent;
  control_fill(pop.fan_rest, pop.fan_rest_opa);
  pop.fan_value = value_label(fan_value_text(d, on, percentage).c_str());
  // Speed slider (the Light popup's vertical slider), segments for up to four
  // fixed speeds, or the Light popup's simple switch for a fan without speeds.
  int h = track_h();
  int track_y = popup_layout::kBodyY + (popup_layout::kBodyHeight - h) / 2;
  if (time_before(pop.notice_until)) {
    lv_obj_t* label = hint(popup_layout::kValueY + popup_layout::kValueHeight, pop.notice.c_str());
    lv_obj_update_layout(label);
    const int below = lv_obj_get_y(label) + lv_obj_get_height(label) + popup_layout::scale(16);
    if (track_y < below) {
      h -= below - track_y;
      track_y = below;
    }
  }
  pop.fan_track = make_box(pop.body);
  lv_obj_set_size(pop.fan_track, w, h);
  lv_obj_set_pos(pop.fan_track, (content_w - w) / 2, track_y);
  lv_obj_set_style_radius(pop.fan_track, radius, 0);
  // The rail in the circle fill like the Light popup's slider.
  lv_obj_set_style_bg_color(pop.fan_track, pop.fan_rest, 0);
  lv_obj_set_style_bg_opa(pop.fan_track, pop.fan_rest_opa, 0);
  lv_obj_set_style_clip_corner(pop.fan_track, true, 0);
  if (!usable) {
    lv_obj_set_style_opa(pop.fan_track, LV_OPA_30, 0);
  } else {
    lv_obj_add_flag(pop.fan_track, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    for (const lv_event_code_t code :
         {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST, LV_EVENT_CLICKED}) {
      lv_obj_add_event_cb(pop.fan_track, on_fan_track, code, nullptr);
    }
  }
  pop.fan_fill = make_box(pop.fan_track);
  lv_obj_set_style_radius(pop.fan_fill, radius, 0);
  lv_obj_set_style_bg_opa(pop.fan_fill, LV_OPA_COVER, 0);
  if (device_visual::fan_segmented(d)) {
    // Segments from the bottom (speed 1) to the top, filled up to the speed.
    lv_obj_add_flag(pop.fan_fill, LV_OBJ_FLAG_HIDDEN);
    pop.fan_fill = nullptr;
    lv_obj_set_style_bg_opa(pop.fan_track, LV_OPA_TRANSP, 0);
    const int gap = popup_layout::contentScale(8);
    const int count = std::min(4, device_detail::fan_speed_count(d));
    const int segment_h = (h - gap * (count - 1)) / count;
    for (int i = 0; i < count; ++i) {
      lv_obj_t* segment = make_box(pop.fan_track);
      lv_obj_set_size(segment, w, segment_h);
      lv_obj_set_pos(segment, 0, h - (i + 1) * segment_h - i * gap);
      lv_obj_set_style_radius(segment, popup_layout::contentScale(24), 0);
      pop.fan_segments[i] = segment;
    }
    pop.fan_segment_count = static_cast<uint8_t>(count);
    lv_obj_update_layout(pop.fan_track);
    set_fan_fill(d, percentage);
  } else if (device_visual::fan_has_speed(d)) {
    lv_obj_set_style_bg_color(pop.fan_fill, accent, 0);
    lv_obj_update_layout(pop.fan_track);
    set_fan_fill(d, percentage);
    lv_obj_t* dash = make_box(pop.fan_fill);
    lv_obj_set_size(dash, popup_layout::contentScale(46), popup_layout::contentScale(4));
    lv_obj_set_style_radius(dash, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dash, lv_color_hex(pop.card_rgb), 0);
    lv_obj_set_style_bg_opa(dash, LV_OPA_COVER, 0);
    lv_obj_align(dash, LV_ALIGN_TOP_MID, 0, popup_layout::contentScale(28));
  } else {
    lv_obj_set_size(pop.fan_fill, w, h / 2);
    lv_obj_set_y(pop.fan_fill, on ? 0 : h / 2);
    // Off: the Light popup switch's off thumb, one step above the rail.
    lv_obj_set_style_bg_color(pop.fan_fill,
                              on ? accent : lv_color_hex(tone_color::switch_thumb_off(lv_color_to_u32(pop.fan_rest) & 0xFFFFFF)), 0);
    lv_obj_t* symbol = make_icon(pop.fan_fill, on ? lv_color_hex(pop.card_rgb) : lv_color_hex(tone_color::off_icon()),
                                 on ? "fan" : "fan-off");
    lv_obj_center(symbol);
  }

  // Bottom row: power like the Light popup, then Climate-style pills for the
  // supported extras (caption and value, all in the circle fill).
  const auto& tr = i18n::strings(device_visual::language());
  pop.pill_count = 0;
  if ((d.features & device_detail::kFanPresetMode) && d.preset_count) {
    FanPill& item = pop.pills[pop.pill_count++];
    item = FanPill{};
    item.caption = text(DeviceLabel::FanPreset);
    item.menu = FanMenu::Preset;
    item.option_count = static_cast<uint8_t>(1 + d.preset_count);
    item.value = d.preset_mode[0] ? device_visual::preset_text(d.preset_mode) : String(text(DeviceLabel::FanNone));
    for (uint8_t i = 0; i < d.preset_count; ++i) {
      if (std::strcmp(d.presets[i], d.preset_mode) == 0) item.selected = static_cast<int8_t>(i + 1);
    }
  }
  if ((d.features & device_detail::kFanOscillate) && d.has_oscillating) {
    FanPill& item = pop.pills[pop.pill_count++];
    item = FanPill{};
    item.caption = text(DeviceLabel::FanOscillate);
    item.menu = FanMenu::Oscillation;
    item.option_count = 2;
    item.value = d.oscillating ? tr.light_on : tr.light_off;
    item.selected = d.oscillating ? 0 : 1;
  }
  if ((d.features & device_detail::kFanDirection) && d.direction[0]) {
    FanPill& item = pop.pills[pop.pill_count++];
    item = FanPill{};
    item.caption = text(DeviceLabel::FanDirection);
    item.menu = FanMenu::Direction;
    item.option_count = 2;
    const bool forward = std::strcmp(d.direction, "forward") == 0;
    item.value = forward ? text(DeviceLabel::FanForward) : text(DeviceLabel::FanReverse);
    item.selected = forward ? 0 : 1;
  }
  const int nav = popup_layout::kNavHeight, gap = popup_layout::scale(8);
  const int y = content_height() - popup_layout::kNavBottomInset - nav;
  int pill_w = pop.pill_count ? (content_w - nav - gap * pop.pill_count) / pop.pill_count : 0;
  pill_w = std::min(pill_w, popup_layout::scale(200));
  const int row_w = nav + pop.pill_count * (gap + pill_w);
  int x = (content_w - row_w) / 2;
  lv_color_t fill;
  lv_opa_t opa;
  control_fill(fill, opa);
  lv_obj_t* power = make_button(pop.body, x, y, nav, nav, nav / 2);
  lv_obj_set_style_radius(power, LV_RADIUS_CIRCLE, 0);
  popup_nav_style::set_bg(power, on ? accent : fill, on ? LV_OPA_COVER : opa, LV_PART_MAIN);
  control_pressed(power, on, accent);
  lv_obj_t* power_icon = make_icon(power, on ? lv_color_hex(pop.card_rgb) : lv_color_hex(ui_theme::text()), "power");
  lv_obj_center(power_icon);
  const uint32_t power_feature = on ? device_detail::kFanTurnOff : device_detail::kFanTurnOn;
  if (!usable || !(d.features & power_feature)) {
    lv_obj_add_state(power, LV_STATE_DISABLED);
  } else {
    lv_obj_add_event_cb(power, fan_power, LV_EVENT_CLICKED, nullptr);
  }
  x += nav + gap;
  for (uint8_t i = 0; i < pop.pill_count; ++i, x += pill_w + gap) {
    lv_obj_t* item = make_button(pop.body, x, y, pill_w, nav, nav / 2);
    lv_obj_set_style_radius(item, nav / 2, 0);
    // One color for every pill (user 01.10.): the circle fill; the state is
    // only in the value text, and only Power lights up.
    popup_nav_style::set_bg(item, fill, opa, LV_PART_MAIN);
    control_pressed(item, false, fill);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    pill_texts(item, pop.pills[i].caption, pop.pills[i].value.c_str(), lv_color_hex(ui_theme::text()));
    pop.pill_objects[i] = item;
    if (!usable) {
      lv_obj_add_state(item, LV_STATE_DISABLED);
      continue;
    }
    lv_obj_add_event_cb(
        item, [](lv_event_t* e) { open_fan_menu(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)))); },
        LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }
}

// ---------------------------------------------------------------------------
// Popup object

void on_close(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) hide_device_popup();
}

void ensure_popup() {
  if (pop.card) return;
  const auto parts = create_popup_body(on_close, nullptr, pop.card_rgb);
  pop.overlay = parts.overlay;
  pop.card = parts.card;
  pop.title = parts.title;
  pop.icon = parts.icon;
  pop.close = parts.close;
  disable_pressed_button_animation(parts.close);
  // The shared header reads the Alarm's state line from this hidden label.
  pop.value = lv_label_create(pop.card);
  lv_obj_add_flag(pop.value, LV_OBJ_FLAG_HIDDEN);
  pop.body = make_box(pop.card);
  lv_obj_set_size(pop.body, LV_PCT(100), LV_PCT(100));
  lv_obj_move_foreground(pop.icon);
  lv_obj_move_foreground(pop.title);
  lv_obj_move_foreground(pop.close);
  lv_obj_add_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
}

// Refills the popup for `pop.target`; `show` presents it.
void refresh(bool show) {
  if (!pop.target.entity.length()) return;
  // A finger on the slider or an open menu keeps the controls; the state
  // follows once they are released.
  if (!show && (pop.fan_dragging || pop.fan_menu)) {
    pop.refresh_due = true;
    return;
  }
  pop.refresh_due = false;
  ensure_popup();
  const Detail d = device_control::detail(pop.target.entity);
  const Visual v = device_visual::visual(pop.target.type, d);
  pop.icon_rgb = v.color;
  lv_obj_set_style_bg_color(pop.card, lv_color_hex(pop.card_rgb), 0);
  const String icon = pop.target.icon_name.length() ? pop.target.icon_name : String(v.icon);
  lv_label_set_text(pop.icon, pop.target.icon_visible ? getMdiChar(icon).c_str() : "");
  lv_obj_set_style_text_color(pop.icon, lv_color_hex(v.color), 0);
  hometiles_title::set(pop.title, pop.target.title.c_str());
  // Key popups (Alarm) show the state in the header like the PIN entry;
  // slider and switch popups (Lock, Fan) show it big above the control.
  lv_label_set_text(pop.value, pop.target.type == TILE_ALARM ? v.label.c_str() : "");
  popup_layout::alignHeader(pop.card, pop.title, pop.icon);
  close_fan_menu();
  lv_obj_clean(pop.body);
  pop.fan_track = pop.fan_fill = pop.fan_value = nullptr;
  pop.fan_segment_count = 0;
  pop.pill_count = 0;
  if (pop.target.type == TILE_LOCK) {
    build_lock(d, v);
  } else if (pop.target.type == TILE_ALARM) {
    build_alarm(d, v);
  } else {
    build_fan(d);
  }
  if (show) {
    pop.open = true;
    lv_obj_remove_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pop.overlay, LV_OBJ_FLAG_CLICKABLE);
    viewNavigationPopupShown(pop.card, pop.target.entity.c_str());
    show_popup_shell(pop.overlay, pop.card, pop.title, pop.icon, pop.close, nullptr, pop.value);
  } else if (pop.open) {
    sync_popup_shell();
  }
  // Home Assistant pulses the state icon while a command runs or the device
  // needs attention; here the shared header's icon, also right after a tap
  // until the device reports.
  const char* target = device_control::pending_target(pop.target.entity);
  if (pop.open) popup_shell_pulse_icon(pop.card, v.waiting || (*target && std::strcmp(target, d.state) != 0));
}

// The popup's background: the opening tile's color while that card lives
// (tile_icon_source::popup_background also takes its header disc options).
void open_for(const DevicePopupTarget& target) {
  if (pop.target.entity != target.entity) {
    pop.open_confirm_until = 0;
    pop.open_done_until = 0;
    pop.fan_hold_until = 0;
  }
  pop.target = target;
  if (!device_tile_alive(target.source)) pop.target.source = nullptr;
  pop.card_rgb = pop.target.source ? tile_icon_source::popup_background(pop.target.source, tileDefaultBgColor())
                                   : popup_surface::card_or_default(tileDefaultBgColor());
  refresh(true);
}

// ---------------------------------------------------------------------------
// Code entry (the PIN popup with device texts)

void clear_code() { pin_access::secureClear(g_code.code, sizeof(g_code.code)); }

bool locked_out() {
  return g_code.lockout_entity == g_code.target.entity && time_before(g_code.lockout_until);
}

void show_lockout() {
  if (!locked_out()) {
    if (g_code.lockout_timer) {
      lv_timer_delete(g_code.lockout_timer);
      g_code.lockout_timer = nullptr;
    }
    // The block is over: the code prompt again.
    if (g_code.lockout_until) pin_popup_set_error(text(DeviceLabel::WrongCode), false);
    g_code.lockout_until = 0;
    return;
  }
  char line[96];
  const uint32_t seconds = (g_code.lockout_until - millis() + 999) / 1000;
  snprintf(line, sizeof(line), text(DeviceLabel::TooManyCodes), static_cast<unsigned>(seconds));
  pin_popup_set_error(line, true);
}

void lockout_timer_cb(lv_timer_t*) {
  if (!g_code.open || !is_pin_popup_visible()) {
    if (g_code.lockout_timer) lv_timer_delete(g_code.lockout_timer);
    g_code.lockout_timer = nullptr;
    return;
  }
  show_lockout();
}

void start_lockout(int seconds) {
  g_code.lockout_entity = g_code.target.entity;
  g_code.lockout_until = millis() + static_cast<uint32_t>(std::max(1, seconds)) * 1000;
  if (!g_code.lockout_timer) g_code.lockout_timer = lv_timer_create(lockout_timer_cb, 1000, nullptr);
  show_lockout();
}

bool capture_code(const char* code, void*) {
  // While locked out the keypad stays on its wait line.
  if (locked_out()) {
    show_lockout();
    return false;
  }
  strlcpy(g_code.code, code ? code : "", sizeof(g_code.code));
  return g_code.code[0] != '\0';
}

void send_code(void*) {
  const char* error = nullptr;
  const String id =
      device_control::send_access(g_code.target.type, g_code.target.entity, g_code.action, g_code.code, &error);
  clear_code();
  if (!id.length()) {
    resume_pin_popup_after_failed_success();
    pin_popup_set_error(error ? error : text(DeviceLabel::ResultFailed), true);
    return;
  }
  g_code.command_id = id;
  g_code.in_flight = true;
  pin_popup_pulse_icon(true);
  if (std::strcmp(g_code.action, "open") == 0) {
    pop.open_done_until = millis() + kOpenDoneMs;
  }
}

// Leaving the code entry without a code returns to the popup it came from,
// like Home Assistant's code dialog over its more-info dialog.
void code_dismissed(void*) {
  clear_code();
  const bool back = !g_code.from_tile;
  g_code.open = false;
  g_code.in_flight = false;
  if (back) open_for(g_code.target);
}

void ask_code(const DevicePopupTarget& target, const char* action, bool from_tile) {
  g_code.open = true;
  g_code.from_tile = from_tile;
  g_code.in_flight = false;
  g_code.target = target;
  if (!device_tile_alive(g_code.target.source)) g_code.target.source = nullptr;
  strlcpy(g_code.action, action, sizeof(g_code.action));
  clear_code();
  const Detail d = device_control::detail(target.entity);
  const Visual v = device_visual::visual(target.type, d);
  PinPopupInit init;
  init.title = target.title;
  init.icon_name = target.icon_name.length() ? target.icon_name : String(v.icon);
  init.bg_color = g_code.target.source ? tile_icon_source::popup_background(g_code.target.source, tileDefaultBgColor())
                                       : popup_surface::card_or_default(tileDefaultBgColor());
  init.icon_color = v.color;
  init.hide_on_success = false;
  init.verify = capture_code;
  init.success = send_code;
  init.prompt = text(DeviceLabel::EnterCode);
  init.error = text(DeviceLabel::WrongCode);
  init.state = v.label;
  init.back = !from_tile;
  init.dismissed = code_dismissed;
  // Marks the entry as this device's (device_popup_refresh follows it).
  init.context = &g_code;
  pop.open = false;
  show_pin_popup(init);
  if (locked_out()) {
    if (!g_code.lockout_timer) g_code.lockout_timer = lv_timer_create(lockout_timer_cb, 1000, nullptr);
    show_lockout();
  }
}

void send_without_code(const DevicePopupTarget& target, const char* action, bool from_tile) {
  const char* error = nullptr;
  const String id = device_control::send_access(target.type, target.entity, action, nullptr, &error);
  if (!id.length()) {
    set_notice(error ? error : text(DeviceLabel::ResultFailed));
    open_for(target);
    return;
  }
  g_last.id = id;
  g_last.target = target;
  strlcpy(g_last.action, action, sizeof(g_last.action));
  g_last.from_tile = from_tile;
  if (std::strcmp(action, "open") == 0) {
    pop.open_done_until = millis() + kOpenDoneMs;
    refresh_at(pop.open_done_until);
  }
  if (pop.open && pop.target.entity == target.entity) refresh(false);
}

}  // namespace

void device_request(const DevicePopupTarget& target, const char* action, bool from_tile) {
  if (!action || !target.entity.length()) return;
  const Detail d = device_control::detail(target.entity);
  // A blocked action explains itself in the popup.
  if (device_control::blocked_reason(target.type, d, action)) {
    open_for(target);
    return;
  }
  if (device_control::needs_code(target.type, d, action)) {
    ask_code(target, action, from_tile);
    return;
  }
  send_without_code(target, action, from_tile);
}

void device_popup_on_result(TileType type, const String& entity, const String& id, const char* status,
                            int retry_after) {
  const bool ok = !std::strcmp(status, "ok") || !std::strcmp(status, "pending");
  if (g_code.in_flight && g_code.command_id == id) {
    g_code.in_flight = false;
    if (ok) {
      g_code.open = false;
      hide_pin_popup();
      if (!g_code.from_tile) open_for(g_code.target);
      return;
    }
    if (!std::strcmp(status, "wrong_code") || !std::strcmp(status, "locked_out") ||
        !std::strcmp(status, "code_required")) {
      resume_pin_popup_after_failed_success();
      // The wrong code that starts a block carries retry_after.
      if (!std::strcmp(status, "locked_out") || retry_after > 0) {
        start_lockout(retry_after);
      } else {
        pin_popup_set_error(text(DeviceLabel::WrongCode), true);
      }
      return;
    }
    // Any other failure: back to the popup with the reason.
    g_code.open = false;
    hide_pin_popup();
    pop.open_done_until = 0;
    set_notice(result_text(type, status));
    open_for(g_code.target);
    return;
  }
  if (g_last.id != id) return;
  g_last.id = "";
  if (ok) return;
  if (!std::strcmp(status, "code_required")) {
    // Sent without a code although one is needed: ask now.
    ask_code(g_last.target, g_last.action, g_last.from_tile);
    return;
  }
  pop.open_done_until = 0;
  set_notice(result_text(type, status));
  open_for(g_last.target);
  (void)entity;
}

void device_popup_on_no_reaction(const String& entity) {
  // Lock and Alarm show only reported states, so this line is all that
  // tells a code the device rejected (Home Assistant answered ok) apart.
  if (!pop.open || (pop.target.type != TILE_LOCK && pop.target.type != TILE_ALARM) ||
      pop.target.entity != entity) {
    return;
  }
  set_notice(text(DeviceLabel::NoReaction));
}

void show_device_popup(const DevicePopupTarget& target) {
  if (!target.entity.length()) return;
  hide_pin_popup();
  hide_light_popup();
  hide_climate_popup();
  hide_cover_popup();
  hide_sensor_popup();
  hide_weather_popup();
  hide_energy_popup();
  hide_media_popup();
  hide_camera_popup();
  if (pop.target.entity != target.entity) pop.notice_until = 0;
  open_for(target);
}

void hide_device_popup() {
  if (!pop.card || !pop.overlay) return;
  pop.open = false;
  pop.fan_dragging = false;
  if (g_live_timer) {
    lv_timer_delete(g_live_timer);
    g_live_timer = nullptr;
  }
  close_fan_menu();
  hide_popup_shell(pop.card);
  cancel_popup_open(pop.card);
  lv_obj_add_flag(pop.card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(pop.overlay, LV_OBJ_FLAG_CLICKABLE);
}

void preload_device_popup() { ensure_popup(); }

void device_popup_follow_tile_color(uint32_t color) {
  if (!pop.open || !pop.card) return;
  pop.card_rgb = color;
  lv_obj_set_style_bg_color(pop.card, lv_color_hex(color), 0);
  popup_shell_follow_tile_color(color);
}

void device_popup_refresh(const String& entity) {
  // An open code entry follows its device: the card already took the tile's
  // new color, the header and the keys follow here (V2: the card turned
  // orange behind grey keys and the old state while the alarm armed).
  if (is_pin_popup_for(&g_code) && (entity == "*" || g_code.target.entity == entity)) {
    const Detail d = device_control::detail(g_code.target.entity);
    const Visual v = device_visual::visual(g_code.target.type, d);
    pin_popup_set_state(g_code.target.icon_name.length() ? g_code.target.icon_name : String(v.icon), v.color, v.label);
  }
  if (!pop.open || (entity != "*" && pop.target.entity != entity)) return;
  refresh(false);
}
