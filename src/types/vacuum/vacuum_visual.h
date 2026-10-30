#pragma once

#include <Arduino.h>

#include <cstdio>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/device/device_visual.h"
#include "src/types/vacuum/vacuum_detail.h"
#include "src/ui/shared/tone_color.h"

// The state look of the Vacuum tile and popup, like the Home Assistant
// frontend's active state color: cleaning, returning and paused in the
// device cyan, error red, docked and idle grey. The state line adds the
// battery ("Cleaning · 84 %").
namespace vacuum_visual {

inline const char* language() { return configManager.getConfig().language; }
inline const char* text(i18n::VacuumLabel label) { return i18n::vacuum_label(language(), label); }

struct Visual {
  String label;
  const char* icon = "robot-vacuum";
  uint32_t color = device_visual::grey();
};

inline Visual visual(const vacuum_detail::Detail& d) {
  Visual v;
  if (!d.valid) {
    v.label = "--";
    return v;
  }
  const char* state = d.available ? d.state : "unavailable";
  v.label = i18n::vacuum_state_label(language(), state);
  if (!d.available) {
    v.icon = "robot-vacuum-off";
    return v;
  }
  if (vacuum_detail::is(d, "error")) {
    v.icon = "robot-vacuum-alert";
    v.color = device_visual::kRed;
  } else if (vacuum_detail::is(d, "cleaning") || vacuum_detail::is(d, "returning") ||
             vacuum_detail::is(d, "paused")) {
    v.color = device_visual::kCyan;
  }
  if (d.has_battery) {
    char battery[16];
    snprintf(battery, sizeof(battery), " · %u %%", static_cast<unsigned>(d.battery));
    v.label += battery;
  }
  return v;
}

// The main button's look on its fill (the icon color): a dark step of the
// same hue for its glyph and text, like the mockup.
inline uint32_t on_accent(uint32_t accent) {
  const tone_color::Oklch c = tone_color::from_rgb(accent & 0xFFFFFF);
  return tone_color::to_rgb(0.25f, c.C < 0.06f ? c.C : 0.06f, c.h);
}

}  // namespace vacuum_visual
