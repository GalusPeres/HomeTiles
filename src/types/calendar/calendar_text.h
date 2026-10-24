#pragma once

#include <Arduino.h>
#include <time.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/types/calendar/calendar_model.h"
#include "src/types/clock/clock_format.h"

// Words and times of the Calendar tile and popup in the panel's language and
// time format (the Clock tile's: global setting, else the language's).
namespace calendar_text {

using calendar_events::Event;
using i18n::CalendarLabel;

// The panel's local day and minute; `valid` false until the clock is set.
struct Now {
  bool valid = false;
  int32_t today = 0;
  int minute = 0;
};

inline Now now() {
  Now out;
  const time_t epoch = time(nullptr);
  // The Web Admin's own lower bound for a set clock (web_admin_auth_core.h).
  if (epoch < static_cast<time_t>(1700000000)) return out;
  struct tm local;
  localtime_r(&epoch, &local);
  out.valid = true;
  out.today = calendar_events::days_from_civil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                                               static_cast<unsigned>(local.tm_mday));
  out.minute = local.tm_hour * 60 + local.tm_min;
  return out;
}

inline const char* language() { return configManager.getConfig().language; }

inline const char* label(CalendarLabel which) { return i18n::calendar_label(language(), which); }

inline uint8_t time_format() {
  return clock_tile::resolve_time_format(0, configManager.getConfig().global_time_format, language());
}

// "10:00" or "10:00 AM"; 24:00 (an event ending at midnight) as 00:00.
inline String clock(int minute) {
  char text[16];
  minute = ((minute % (24 * 60)) + 24 * 60) % (24 * 60);
  clock_tile::format_time(text, sizeof(text), minute / 60, minute % 60, time_format());
  return String(text);
}

inline String printf_label(CalendarLabel which, unsigned a, unsigned b = 0) {
  char text[64];
  snprintf(text, sizeof(text), label(which), a, b);
  return String(text);
}

// The first day of the week of a tile (Tile::sensor_decimals).
inline uint8_t week_start(uint8_t stored) {
  return calendar_model::resolve_week_start(stored, i18n::locale(language()).first_weekday);
}

inline const char* weekday_short(int32_t day) {
  return i18n::locale(language()).weather_weekdays_short[calendar_events::weekday(day)];
}

// "Today", "Tomorrow" or "Mon 26 Oct".
inline String day_label(int32_t day, const Now& now) {
  if (now.valid && day == now.today) return String(i18n::weather_today_label(language()));
  if (now.valid && day == now.today + 1) return String(i18n::weather_tomorrow_label(language()));
  int y;
  unsigned m, d;
  calendar_events::civil_from_days(day, y, m, d);
  return String(weekday_short(day)) + " " +
         i18n::format_short_date(language(), static_cast<int>(d), i18n::weather_month_short(language(), m));
}

// "October 2026".
inline String month_title(int year, unsigned month) {
  return String(i18n::month_name(language(), static_cast<int>(month))) + " " + String(year);
}

// An event's time on `day`: "All day", "10:00 – 11:30", or for an event
// across midnight the part on that day ("22:00 – 00:00", "00:00 – 02:00").
inline String time_range(const Event& e, int32_t day) {
  if (e.all_day()) return String(label(CalendarLabel::AllDay));
  const int start = e.start_day == day ? e.start_min : 0;
  const int end = e.end_day == day ? e.end_min : 24 * 60;
  if (end == start) return clock(start);
  return clock(start) + " \xE2\x80\x93 " + clock(end);
}

// The start column of a list row: "All day" or "10:00" (the start on that day).
inline String start_text(const Event& e, int32_t day) {
  if (e.all_day()) return String(label(CalendarLabel::AllDay));
  return clock(e.start_day == day ? e.start_min : 0);
}

// How far away a later start is: "in 45 min", "in 2 h 30 min", "in 3 days".
inline String countdown(int32_t minutes, int32_t days_ahead) {
  if (days_ahead >= 2) return printf_label(CalendarLabel::InDays, static_cast<unsigned>(days_ahead));
  if (minutes < 60) return printf_label(CalendarLabel::InMinutes, static_cast<unsigned>(minutes < 1 ? 1 : minutes));
  const unsigned hours = static_cast<unsigned>(minutes / 60);
  const unsigned rest = static_cast<unsigned>(minutes % 60);
  return rest ? printf_label(CalendarLabel::InHoursMinutes, hours, rest)
              : printf_label(CalendarLabel::InHours, hours);
}

// The line under a "next" event (Up next tile, half-height chip):
//   running all-day        "All day"            (multi-day: "until Sun 1 Nov")
//   running timed          "until 11:30"
//   later today            "10:00 · in 1 h 30 min" ("10:00" when `brief`)
//   tomorrow               "Tomorrow · 10:00"
//   within a week          "Mon · 10:00" / "Mon"
//   later                  "Mon 2 Nov · in 9 days"
inline String when_line(const calendar_model::Next& next, const Now& now, bool brief) {
  if (!next.found) return String();
  const Event& e = *next.item.event;
  const String dot = " \xC2\xB7 ";
  if (next.ongoing) {
    if (e.all_day()) {
      if (e.end_day > now.today) {
        char text[64];
        snprintf(text, sizeof(text), label(CalendarLabel::Until), day_label(e.end_day, now).c_str());
        return String(text);
      }
      return String(label(CalendarLabel::AllDay));
    }
    const String end = e.end_day == now.today ? clock(e.end_min) : day_label(e.end_day, now);
    char text[64];
    snprintf(text, sizeof(text), label(CalendarLabel::Until), end.c_str());
    return String(text);
  }
  const int32_t days = next.day - now.today;
  const String time = e.all_day() ? String() : clock(e.start_min);
  if (days == 0) {
    const int32_t minutes = calendar_model::minutes_until(next, now.today, now.minute);
    if (brief || e.all_day()) return e.all_day() ? String(label(CalendarLabel::AllDay)) : time;
    return time + dot + countdown(minutes, 0);
  }
  if (days == 1) return day_label(next.day, now) + (time.length() ? dot + time : String());
  if (days < 7) return String(weekday_short(next.day)) + (time.length() ? dot + time : String());
  return brief ? day_label(next.day, now) : day_label(next.day, now) + dot + countdown(0, days);
}

}  // namespace calendar_text
