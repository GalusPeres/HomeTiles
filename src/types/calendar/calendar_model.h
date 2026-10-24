#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>

#include "src/types/calendar/calendar_events.h"

// What a Calendar tile and its popup show, from up to kMaxCalendars parsed
// calendars and the panel's local day and minute. Pure functions: no LVGL,
// no clock reads, no allocation, so host tests drive them with fixed dates.
namespace calendar_model {

using calendar_events::Calendar;
using calendar_events::Event;

// A tile merges up to four Home Assistant calendars, each in its own color
// (the order of the tile's calendar fields).
constexpr uint8_t kMaxCalendars = 4;

// The tile's view (Tile::sensor_display_mode): the agenda adapts to the
// tile's size (next event, countdown, list); the week strip and the month
// need room. Today counts today's events on a half-height tile and is the
// agenda on larger ones.
enum View : uint8_t { kViewAgenda = 0, kViewWeek = 1, kViewMonth = 2, kViewToday = 3 };
constexpr uint8_t kViewCount = 4;

// The first day of the week (Tile::sensor_decimals): a tm_wday (0 Sunday ..
// 6 Saturday), or kWeekStartAuto for the language's (LocaleProfile).
constexpr uint8_t kWeekStartAuto = 0xFF;

inline uint8_t normalize_view(int raw) {
  return raw >= 0 && raw < kViewCount ? static_cast<uint8_t>(raw) : kViewAgenda;
}

inline uint8_t normalize_week_start(int raw) {
  return raw == 0 || raw == 1 || raw == 6 ? static_cast<uint8_t>(raw) : kWeekStartAuto;
}

inline uint8_t resolve_week_start(uint8_t stored, uint8_t language_default) {
  const uint8_t normalized = normalize_week_start(stored);
  if (normalized != kWeekStartAuto) return normalized;
  return language_default <= 6 ? language_default : 1;
}

// One event shown on one day, with the calendar it comes from.
struct Item {
  const Event* event = nullptr;
  uint8_t calendar = 0;
};

// The tile's calendars in field order; null entries (no payload yet) skip.
struct Sources {
  const Calendar* calendars[kMaxCalendars] = {};
  uint8_t count = 0;

  bool any_valid() const {
    for (uint8_t i = 0; i < count; ++i) {
      if (calendars[i] && calendars[i]->valid) return true;
    }
    return false;
  }
  // The days every calendar with data covers (inclusive), false without data.
  bool window(int32_t& from_day, int32_t& to_day) const {
    bool found = false;
    for (uint8_t i = 0; i < count; ++i) {
      const Calendar* c = calendars[i];
      if (!c || !c->valid) continue;
      from_day = found ? std::max(from_day, c->from_day) : c->from_day;
      to_day = found ? std::min(to_day, c->to_day) : c->to_day;
      found = true;
    }
    return found && to_day >= from_day;
  }
};

inline bool covers(const Event& e, int32_t day) { return e.start_day <= day && e.end_day >= day; }

// The minute the event starts on `day` (0 when it began before), all-day -1.
inline int16_t start_on(const Event& e, int32_t day) {
  if (e.all_day()) return calendar_events::kAllDay;
  return e.start_day == day ? e.start_min : 0;
}

inline bool item_before(const Item& a, const Item& b, int32_t day) {
  const int16_t sa = start_on(*a.event, day);
  const int16_t sb = start_on(*b.event, day);
  if (sa != sb) return sa < sb;
  if (a.calendar != b.calendar) return a.calendar < b.calendar;
  return strcmp(a.event->title, b.event->title) < 0;
}

// The events on `day`, all-day ones first, then by start; at most `max`.
// Returns the total count on that day (also beyond `max`).
inline size_t items_on(const Sources& sources, int32_t day, Item* out, size_t max) {
  size_t total = 0;
  size_t kept = 0;
  for (uint8_t c = 0; c < sources.count; ++c) {
    const Calendar* calendar = sources.calendars[c];
    if (!calendar || !calendar->valid) continue;
    for (const Event& e : calendar->events) {
      if (e.start_day > day) break;  // sorted by start
      if (!covers(e, day)) continue;
      ++total;
      const Item item{&e, c};
      if (kept < max) {
        out[kept++] = item;
      } else if (max && item_before(item, out[max - 1], day)) {
        out[max - 1] = item;
      } else {
        continue;
      }
      // Keep the kept part sorted (a few items: insertion).
      for (size_t i = (kept < max ? kept : max) - 1; i > 0 && item_before(out[i], out[i - 1], day); --i) {
        std::swap(out[i], out[i - 1]);
      }
    }
  }
  return total;
}

// Bit c set when calendar c has an event on `day` (the month's dots).
inline uint8_t day_mask(const Sources& sources, int32_t day) {
  uint8_t mask = 0;
  for (uint8_t c = 0; c < sources.count; ++c) {
    const Calendar* calendar = sources.calendars[c];
    if (!calendar || !calendar->valid) continue;
    for (const Event& e : calendar->events) {
      if (e.start_day > day) break;
      if (covers(e, day)) {
        mask |= static_cast<uint8_t>(1u << c);
        break;
      }
    }
  }
  return mask;
}

// The next thing to show on a small tile: an event running now or the next
// one to start, from today on. All-day events of today count as running.
struct Next {
  bool found = false;
  bool ongoing = false;
  int32_t day = 0;  // the day it starts (today when it runs now)
  Item item;
};

inline Next next_event(const Sources& sources, int32_t today, int now_min, int32_t until_day) {
  Next best;
  for (uint8_t c = 0; c < sources.count; ++c) {
    const Calendar* calendar = sources.calendars[c];
    if (!calendar || !calendar->valid) continue;
    for (const Event& e : calendar->events) {
      if (e.start_day > until_day) break;
      // Over before now.
      if (e.end_day < today) continue;
      if (!e.all_day() && e.end_day == today && e.end_min <= now_min) continue;
      const bool started = e.start_day < today || (e.start_day == today && (e.all_day() || e.start_min <= now_min));
      Next candidate;
      candidate.found = true;
      candidate.ongoing = started;
      candidate.day = started ? today : e.start_day;
      candidate.item = Item{&e, c};
      if (!best.found) {
        best = candidate;
        continue;
      }
      // Running timed events first (they end soonest), then all-day ones of
      // today, then whatever starts next.
      auto rank = [&](const Next& n) {
        if (n.ongoing && !n.item.event->all_day()) return 0;
        if (n.ongoing) return 1;
        return 2;
      };
      const int rc = rank(candidate);
      const int rb = rank(best);
      if (rc != rb) {
        if (rc < rb) best = candidate;
        continue;
      }
      if (rc == 2) {
        if (candidate.day != best.day ? candidate.day < best.day
                                      : item_before(candidate.item, best.item, candidate.day)) {
          best = candidate;
        }
      } else if (rc == 0) {
        const Event& a = *candidate.item.event;
        const Event& b = *best.item.event;
        if (a.end_day != b.end_day ? a.end_day < b.end_day : a.end_min < b.end_min) best = candidate;
      } else if (item_before(candidate.item, best.item, today)) {
        best = candidate;
      }
    }
  }
  return best;
}

// A timed event that starts later than now on today or a later day is the
// one a "next" line counts down to; all-day and running ones show no time.
inline bool upcoming_timed(const Next& next, int32_t today, int now_min) {
  return next.found && !next.ongoing && !next.item.event->all_day() &&
         (next.day > today || next.item.event->start_min > now_min);
}

// Minutes from now until a later start (0 when not later).
inline int32_t minutes_until(const Next& next, int32_t today, int now_min) {
  if (!next.found || next.ongoing) return 0;
  const int32_t start_min = next.item.event->all_day() ? 0 : next.item.event->start_min;
  const int32_t delta = (next.day - today) * 24 * 60 + start_min - now_min;
  return delta > 0 ? delta : 0;
}

// Today's events that are not over yet: all-day ones, running and later
// timed ones (the half-height Today tile).
inline size_t remaining_today(const Sources& sources, int32_t today, int now_min) {
  size_t count = 0;
  for (uint8_t c = 0; c < sources.count; ++c) {
    const Calendar* calendar = sources.calendars[c];
    if (!calendar || !calendar->valid) continue;
    for (const Event& e : calendar->events) {
      if (e.start_day > today) break;
      if (!covers(e, today)) continue;
      if (!e.all_day() && e.end_day == today && e.end_min <= now_min) continue;
      ++count;
    }
  }
  return count;
}

// The first day of a week strip: a one-row tile shows the seven days from
// today, a taller one the calendar week holding today.
inline int32_t strip_first_day(int32_t today, uint8_t week_start, bool rolling) {
  return rolling ? today : today - (calendar_events::weekday(today) - week_start + 7) % 7;
}

// The month grid: six weeks from the week holding the 1st, starting on
// `week_start` (tm_wday).
inline int32_t month_grid_start(int year, unsigned month, uint8_t week_start) {
  const int32_t first = calendar_events::days_from_civil(year, month, 1);
  const int offset = (calendar_events::weekday(first) - week_start + 7) % 7;
  return first - offset;
}

// The week holding `day`, starting on `week_start`.
inline int32_t week_start_day(int32_t day, uint8_t week_start) {
  return day - (calendar_events::weekday(day) - week_start + 7) % 7;
}

// Six or five rows: a month that fits five weeks keeps the sixth row empty
// only when the grid starts late. The grid always has six rows on the
// popup; tiles with little room show the rows the month needs.
inline int month_rows(int year, unsigned month, uint8_t week_start) {
  const int32_t start = month_grid_start(year, month, week_start);
  const int32_t last = calendar_events::days_from_civil(year, month, calendar_events::days_in_month(year, month));
  return static_cast<int>((last - start) / 7 + 1);
}

// Moves a (year, month) pair by delta months.
inline void add_months(int& year, unsigned& month, int delta) {
  int index = year * 12 + static_cast<int>(month) - 1 + delta;
  year = index / 12;
  month = static_cast<unsigned>(index % 12 + 1);
}

// Each calendar's color, dark and light theme (the mockup's palette: blue,
// teal, purple, orange; they differ in lightness as well as hue).
inline uint32_t calendar_color(uint8_t index, bool light_theme) {
  static const uint32_t kDark[kMaxCalendars] = {0x5B9BF0, 0x4DB6AC, 0xB388FF, 0xFFA65C};
  static const uint32_t kLight[kMaxCalendars] = {0x2F6FD0, 0x00897B, 0x7E57C2, 0xE07020};
  return (light_theme ? kLight : kDark)[index % kMaxCalendars];
}

}  // namespace calendar_model
