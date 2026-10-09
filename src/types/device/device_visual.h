#pragma once

#include <Arduino.h>

#include <cctype>
#include <cmath>
#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/device/device_detail.h"
#include "src/types/tile_type.h"
#include "src/ui/shared/ui_theme.h"

// The state look of Lock, Alarm panel and Fan tiles and their popup, like
// the Home Assistant frontend: lock locked green, unlocked, open and jammed
// red, transitions orange; alarm armed green, arming/pending/disarming
// orange, triggered red, disarmed grey; fan on cyan, off grey. `waiting`: a
// command runs or the device needs attention, its icon pulses
// (state-control-styles).
namespace device_visual {

constexpr uint32_t kGreen = 0x4CAF50;
constexpr uint32_t kRed = 0xF44336;
constexpr uint32_t kOrange = 0xFF9800;
// The device grey: the inactive grey of the theme (ui_theme::icon_inactive).
inline uint32_t grey() { return ui_theme::icon_inactive(); }
constexpr uint32_t kCyan = 0x00BCD4;

struct Visual {
  String label;
  const char* icon = "";
  uint32_t color = grey();
  bool waiting = false;
};

inline const char* language() { return configManager.getConfig().language; }
inline const char* text(i18n::DeviceLabel label) { return i18n::device_label(language(), label); }

using device_detail::Detail;
using device_detail::is;

// Home Assistant shows Lock and Unlock buttons instead of the toggle for a
// lock without a toggle position (unknown, jammed).
inline bool lock_buttons(const Detail& d) {
  return !(is(d, "locked") || is(d, "locking") || is(d, "unlocked") || is(d, "unlocking") || is(d, "open") ||
           is(d, "opening"));
}
// The toggle is on (locked side) while locked or locking (Home Assistant's
// _isOn).
inline bool lock_on(const Detail& d) { return is(d, "locked") || is(d, "locking"); }
inline bool lock_moving(const Detail& d) { return is(d, "locking") || is(d, "unlocking") || is(d, "opening"); }

// Home Assistant offers only Disarm while an alarm is arming, pending or
// triggered.
inline bool alarm_disarm_only(const Detail& d) { return is(d, "arming") || is(d, "pending") || is(d, "triggered"); }

inline bool fan_on(const Detail& d) { return is(d, "on"); }
inline bool fan_has_speed(const Detail& d) { return (d.features & device_detail::kFanSetSpeed) != 0; }
// Up to four fixed speeds show as segments (Home Assistant's fan speed
// buttons); more keep the slider.
inline bool fan_segmented(const Detail& d) {
  const int count = device_detail::fan_speed_count(d);
  return fan_has_speed(d) && count >= 2 && count <= 4;
}

// "auto_mode" shows as "Auto mode" (Home Assistant gives no translation here).
inline String preset_text(const char* raw) {
  String out(raw ? raw : "");
  out.replace('_', ' ');
  if (out.length()) out.setCharAt(0, static_cast<char>(toupper(out[0])));
  return out;
}

// The speed or level text: "Speed 2" for fixed speeds (`speed_count`
// device_detail::fan_speed_count), "66 %" otherwise.
inline String fan_level_text(int speed_count, int percentage) {
  char buffer[32];
  if (speed_count < 100) {
    const int speed = percentage <= 0 ? 0 : static_cast<int>(std::lround(percentage * speed_count / 100.0f));
    snprintf(buffer, sizeof(buffer), text(i18n::DeviceLabel::FanSpeed),
             static_cast<unsigned>(speed < 1 ? 1 : speed > speed_count ? speed_count : speed));
  } else {
    snprintf(buffer, sizeof(buffer), "%u %%", static_cast<unsigned>(percentage));
  }
  return buffer;
}

inline Visual visual(TileType type, const Detail& d) {
  Visual v;
  const char* lang = language();
  const auto& tr = i18n::strings(lang);
  if (!d.valid) {
    v.label = "--";
    v.icon = type == TILE_LOCK ? "lock" : type == TILE_ALARM ? "shield" : "fan-off";
    return v;
  }
  if (type == TILE_LOCK) {
    v.icon = "lock";
    if (!d.available) {
      v.label = i18n::lock_state_label(lang, "unavailable");
      return v;
    }
    v.label = i18n::lock_state_label(lang, d.state);
    if (is(d, "locked")) {
      v.color = kGreen;
    } else if (is(d, "unlocked") || is(d, "open")) {
      v.icon = "lock-open-variant";
      v.color = kRed;
    } else if (lock_moving(d)) {
      v.icon = "lock-clock";
      v.color = kOrange;
      v.waiting = true;
    } else if (is(d, "jammed")) {
      v.icon = "lock-alert";
      v.color = kRed;
      v.waiting = true;
    }
    return v;
  }
  if (type == TILE_ALARM) {
    v.icon = "shield";
    if (!d.available) {
      v.label = i18n::alarm_state_label(lang, "unavailable");
      return v;
    }
    v.label = i18n::alarm_state_label(lang, d.state);
    static const struct {
      const char* state;
      const char* icon;
      uint32_t color;
      bool waiting;
    } kLooks[] = {
        {"disarmed", "shield-off", grey(), false},
        {"armed_home", "shield-home", kGreen, false},
        {"armed_away", "shield-lock", kGreen, false},
        {"armed_night", "shield-moon", kGreen, false},
        {"armed_vacation", "shield-airplane", kGreen, false},
        {"armed_custom_bypass", "security", kGreen, false},
        {"arming", "shield", kOrange, true},
        {"pending", "shield-outline", kOrange, true},
        {"disarming", "shield", kOrange, true},
        {"triggered", "bell-ring", kRed, true},
    };
    for (const auto& look : kLooks) {
      if (!is(d, look.state)) continue;
      v.icon = look.icon;
      v.color = look.color;
      v.waiting = look.waiting;
    }
    return v;
  }
  // Fan.
  v.icon = "fan-off";
  if (!d.available) {
    v.label = i18n::entity_state_label(lang, "unavailable");
    return v;
  }
  if (is(d, "unknown")) {
    v.label = i18n::entity_state_label(lang, "unknown");
    return v;
  }
  if (!fan_on(d)) {
    v.label = tr.light_off;
    return v;
  }
  v.icon = "fan";
  v.color = kCyan;
  if (d.preset_mode[0]) {
    v.label = preset_text(d.preset_mode);
  } else if (fan_has_speed(d) && d.has_percentage) {
    v.label = fan_level_text(device_detail::fan_speed_count(d), d.percentage);
  } else {
    v.label = tr.light_on;
  }
  return v;
}

}  // namespace device_visual
