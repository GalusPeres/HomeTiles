// A classic guard like ui_theme.h: HomeTiles.ino reaches headers under two
// path spellings, which #pragma once does not see as one file.
#ifndef HOMETILES_THEME_SCHEDULE_H
#define HOMETILES_THEME_SCHEDULE_H

#include <stdint.h>

// Without weather_icons.h: HomeTiles.ino includes this header.
namespace weather_icons {
struct SunTimes;
}

// The theme "Automatic" (ConfigManager theme 2, user 2026-10-10): light by
// day, dark by night. The day runs from sunrise to sunset as the Bridge sends
// them with the weather ("sun" list, weather_icons::parse_sun); without sun
// times for today from 7:00 to 19:00 local time. Until the time zone is set
// and the clock runs, the panel keeps the theme Automatic showed last.
namespace theme_schedule {

inline constexpr uint8_t kAutomatic = 2;
inline constexpr int kFallbackDayStart = 7 * 60;
inline constexpr int kFallbackDayEnd = 19 * 60;

// Day at local `date` (YYYY-MM-DD) and `minute` after midnight: the sun
// times for that date, else the fallback hours.
bool day_at(const weather_icons::SunTimes& sun, const char* date, int minute);

// The theme to show for a stored theme setting: 0 dark, 1 light.
uint8_t effective(uint8_t stored);

// The theme of this boot: Automatic starts with the theme it showed last.
uint8_t at_boot(uint8_t stored);

// The sun times of a weather payload (tile or popup).
void note_sun(const weather_icons::SunTimes& sun);

// The time zone is set (UIManager::serviceNtpSync): the clock tells the day.
void note_time_zone();

// From the loop: Automatic switches at sunrise and sunset.
void poll();

// Stores the theme setting (0 dark, 1 light, 2 Automatic) from the panel's
// Settings, the Web Admin or Home Assistant: Automatic remembers what it
// shows now (a boot before the clock runs starts with it), the loop applies
// it and Home Assistant's select hears it. False when it was not saved.
bool store(uint8_t theme);

}  // namespace theme_schedule

#endif  // HOMETILES_THEME_SCHEDULE_H
