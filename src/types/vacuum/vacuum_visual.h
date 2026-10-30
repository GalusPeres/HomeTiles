#pragma once

#include <Arduino.h>

#include <cstdio>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/device/device_visual.h"
#include "src/types/vacuum/vacuum_detail.h"

// The state look of the Vacuum tile and popup (build/design-mockups/vacuum):
// the vacuum's blue in every reported state, red on an error, the theme's
// unavailable grey only while Home Assistant cannot reach it. The state
// line adds the battery ("Docked · 84 %").
namespace vacuum_visual {

// The mockup's icon color (--icon).
constexpr uint32_t kColor = 0x29B6F6;

inline const char* language() { return configManager.getConfig().language; }
inline const char* text(i18n::VacuumLabel label) { return i18n::vacuum_label(language(), label); }

struct Visual {
  String label;
  const char* icon = "robot-vacuum";
  uint32_t color = kColor;
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
    v.color = device_visual::grey();
    return v;
  }
  if (vacuum_detail::is(d, "error")) {
    v.icon = "robot-vacuum-alert";
    v.color = device_visual::kRed;
  }
  if (d.has_battery) {
    char battery[16];
    snprintf(battery, sizeof(battery), " · %u %%", static_cast<unsigned>(d.battery));
    v.label += battery;
  }
  return v;
}

// The automatic icon: Home Assistant's icon of the entity (what the Web
// Admin shows as "Automatic", e.g. the integration's sleep icon while
// docked), else the state icon.
inline String icon_name(const String& ha_icon, const Visual& v) {
  const String icon = normalizeMdiIconName(ha_icon);
  return icon.length() ? icon : String(v.icon);
}

}  // namespace vacuum_visual
