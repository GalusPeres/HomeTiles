#pragma once

#include <stdint.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/calendar/calendar_model.h"

namespace week_start {

// The panel's first day of the week (tm_wday: 0 Sunday, 1 Monday, 6
// Saturday): Web Admin Settings > Localization, under Automatic the
// language's (LocaleProfile::first_weekday).
inline uint8_t panel() {
  const DeviceConfig& cfg = configManager.getConfig();
  return calendar_model::resolve_week_start(cfg.week_start, i18n::locale(cfg.language).first_weekday);
}

}  // namespace week_start
