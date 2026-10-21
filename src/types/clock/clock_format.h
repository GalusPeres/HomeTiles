#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "src/core/i18n/i18n.h"

namespace clock_tile {

enum TimeFormat : uint8_t {
  TIME_FORMAT_AUTO = 0,
  TIME_FORMAT_24H = 1,
  TIME_FORMAT_12H = 2,
};

enum DateFormat : uint8_t {
  DATE_FORMAT_AUTO = 0,
  DATE_FORMAT_DMY = 1,
  DATE_FORMAT_MDY = 2,
  DATE_FORMAT_YMD = 3,
  // User 2026-10-11 (issue #13 and the clock "Mit Wochentag"): 21/08/2026,
  // 21.08., 21. August and 21. August 2026 (month names by language).
  DATE_FORMAT_DMY_SLASH = 4,
  DATE_FORMAT_DM = 5,
  DATE_FORMAT_D_MONTH = 6,
  DATE_FORMAT_D_MONTH_YEAR = 7,
};

// The clock's weekday (user 2026-10-11): none, the short name ("Sa") or the
// full one ("Samstag").
enum WeekdayMode : uint8_t {
  WEEKDAY_NONE = 0,
  WEEKDAY_SHORT = 1,
  WEEKDAY_LONG = 2,
};

inline uint8_t normalize_time_format(int raw) {
  switch (raw) {
    case TIME_FORMAT_24H:
    case TIME_FORMAT_12H:
      return static_cast<uint8_t>(raw);
    default:
      return TIME_FORMAT_AUTO;
  }
}

inline uint8_t normalize_date_format(int raw) {
  switch (raw) {
    case DATE_FORMAT_DMY:
    case DATE_FORMAT_MDY:
    case DATE_FORMAT_YMD:
    case DATE_FORMAT_DMY_SLASH:
    case DATE_FORMAT_DM:
    case DATE_FORMAT_D_MONTH:
    case DATE_FORMAT_D_MONTH_YEAR:
      return static_cast<uint8_t>(raw);
    default:
      return DATE_FORMAT_AUTO;
  }
}

// Clock font sizes the tile stores (key_code: time, key_modifier: date);
// anything else falls back. The date line stops at 72.
inline uint8_t normalize_font_size(uint8_t raw, uint8_t fallback) {
  switch (raw) {
    case 20:
    case 24:
    case 28:
    case 32:
    case 40:
    case 48:
    case 56:
    case 64:
    case 72:
    case 80:
    case 96:
      return raw;
    default:
      return fallback;
  }
}

inline uint8_t normalize_date_font_size(uint8_t raw, uint8_t fallback) {
  const uint8_t normalized = normalize_font_size(raw, fallback);
  return normalized > 72 ? 72 : normalized;
}

inline uint8_t default_time_format_for_language(const char* language_code) {
  return i18n::locale(language_code).default_time_format;
}

inline uint8_t default_date_format_for_language(const char* language_code) {
  return i18n::locale(language_code).default_date_format;
}

inline uint8_t resolve_time_format(int preferred_raw, int global_raw, const char* language_code) {
  const uint8_t preferred = normalize_time_format(preferred_raw);
  if (preferred != TIME_FORMAT_AUTO) return preferred;
  const uint8_t global = normalize_time_format(global_raw);
  if (global != TIME_FORMAT_AUTO) return global;
  return default_time_format_for_language(language_code);
}

inline uint8_t resolve_date_format(int preferred_raw, int global_raw, const char* language_code) {
  const uint8_t preferred = normalize_date_format(preferred_raw);
  if (preferred != DATE_FORMAT_AUTO) return preferred;
  const uint8_t global = normalize_date_format(global_raw);
  if (global != DATE_FORMAT_AUTO) return global;
  return default_date_format_for_language(language_code);
}

// Weekday name for tm_wday (0 = Sunday). The device locale is always "C",
// so strftime %A would always be English; use the i18n tables instead.
// The clock's time, "08:05" or "08:05 AM": two-digit hours in both formats
// like the head bar (user 2026-10-12), so the line keeps its width.
inline void format_time(char* out, size_t size, int hour, int minute, uint8_t format) {
  if (format == TIME_FORMAT_12H) {
    const int hour12 = hour % 12 ? hour % 12 : 12;
    snprintf(out, size, "%02d:%02d %s", hour12, minute, hour < 12 ? "AM" : "PM");
  } else {
    snprintf(out, size, "%02d:%02d", hour, minute);
  }
}

inline const char* weekday_name(int tm_wday, const char* language_code) {
  if (tm_wday < 0 || tm_wday > 6) return "";
  return i18n::locale(language_code).weekday_names[tm_wday];
}

// The short weekday ("Sa"), the same as the Weather tile's.
inline const char* weekday_short_name(int tm_wday, const char* language_code) {
  if (tm_wday < 0 || tm_wday > 6) return "";
  return i18n::locale(language_code).weather_weekdays_short[tm_wday];
}

// The clock tile's date options in one 32-bit value (Tile::sensor_gauge_max,
// never clamped for clocks; the Web Admin sends it as clock_date_format):
// bits 0-7 the date format, 8-9 the weekday, bit 10 rolling digits, 11-12
// the alignment (0 centered, 1 left, 2 right; user 2026-10-12), 13-15 the
// style (ClockStyle), 16-18 the wall clock's dial color, 19-20 its hands'
// color (clock_face kDialColors / kHandColors). Tiles saved before
// 2026-10-11 hold 0-3: the date format alone.
enum ClockStyle : uint8_t {
  STYLE_DIGITAL = 0,
  STYLE_ANALOG = 1,
  STYLE_WALL = 2,
  // 3 was the big date (b396-b402), removed (user 2026-10-12: buggy, not
  // liked): such tiles show the digital clock.
};
constexpr uint8_t kStyleCount = 3;
constexpr uint8_t kDialColorCount = 6;
constexpr uint8_t kHandColorCount = 4;

struct TileDateOptions {
  uint8_t date_format = DATE_FORMAT_AUTO;
  uint8_t weekday = WEEKDAY_NONE;
  bool roll = false;
  uint8_t align = 0;
  uint8_t style = STYLE_DIGITAL;
  uint8_t dial = 0;
  uint8_t hands = 0;
};

// The clock widget's alignment (0 left, 1 center, 2 right) of a tile's.
inline uint8_t tile_alignment(uint8_t align) { return align == 1 ? 0 : align == 2 ? 2 : 1; }

inline TileDateOptions unpack_date_options(int32_t raw) {
  TileDateOptions options;
  if (raw < 0) return options;
  options.date_format = normalize_date_format(raw & 0xFF);
  const uint8_t weekday = static_cast<uint8_t>((raw >> 8) & 0x03);
  options.weekday = weekday <= WEEKDAY_LONG ? weekday : WEEKDAY_NONE;
  options.roll = ((raw >> 10) & 0x01) != 0;
  const uint8_t align = static_cast<uint8_t>((raw >> 11) & 0x03);
  options.align = align <= 2 ? align : 0;
  const uint8_t style = static_cast<uint8_t>((raw >> 13) & 0x07);
  options.style = style < kStyleCount ? style : STYLE_DIGITAL;
  const uint8_t dial = static_cast<uint8_t>((raw >> 16) & 0x07);
  options.dial = dial < kDialColorCount ? dial : 0;
  options.hands = static_cast<uint8_t>((raw >> 19) & 0x03);
  return options;
}

inline int32_t pack_date_options(const TileDateOptions& options) {
  const uint8_t weekday = options.weekday <= WEEKDAY_LONG ? options.weekday : WEEKDAY_NONE;
  const uint8_t align = options.align <= 2 ? options.align : 0;
  const uint8_t style = options.style < kStyleCount ? options.style : STYLE_DIGITAL;
  const uint8_t dial = options.dial < kDialColorCount ? options.dial : 0;
  const uint8_t hands = options.hands < kHandColorCount ? options.hands : 0;
  return static_cast<int32_t>(normalize_date_format(options.date_format)) | (static_cast<int32_t>(weekday) << 8) |
         (options.roll ? (1 << 10) : 0) | (static_cast<int32_t>(align) << 11) | (static_cast<int32_t>(style) << 13) |
         (static_cast<int32_t>(dial) << 16) | (static_cast<int32_t>(hands) << 19);
}

// A stored or sent value made valid (unknown bits dropped).
inline int32_t normalize_date_options(int32_t raw) { return pack_date_options(unpack_date_options(raw)); }

// Fills a long date pattern's {d} (day), {M} (month) and {Y} (year).
inline void fill_date_pattern(char* out, size_t size, const char* pattern, const char* day, const char* month,
                              const char* year) {
  size_t used = 0;
  out[0] = '\0';
  for (const char* p = pattern; *p && used + 1 < size;) {
    const char* part = strncmp(p, "{d}", 3) == 0   ? day
                       : strncmp(p, "{M}", 3) == 0 ? month
                       : strncmp(p, "{Y}", 3) == 0 ? year
                                                   : nullptr;
    if (!part) {
      out[used++] = *p++;
      out[used] = '\0';
      continue;
    }
    const int written = snprintf(out + used, size - used, "%s", part);
    if (written < 0) break;
    used += static_cast<size_t>(written);
    if (used >= size) used = size - 1;
    p += 3;
  }
  out[used] = '\0';
}

// The locale's long date, with the year or without (its own order, Spanish
// "{d} de {M} de {Y}").
inline const char* long_date_pattern(const i18n::LocaleProfile& locale, bool with_year) {
  if (with_year) return locale.long_date_year_pattern ? locale.long_date_year_pattern : "{d} {M} {Y}";
  return locale.long_date_pattern ? locale.long_date_pattern : "{d} {M}";
}

// The date in a resolved `format` (not AUTO); `month` 0-11, `year` with the
// century. The day-month formats take the locale's month name as it stands
// in a date and its order ("21. August", "21 August", Polish "21 sierpnia").
inline void format_date(char* out, size_t size, int day, int month, int year, uint8_t format,
                        const char* language_code) {
  if (!out || size == 0) return;
  out[0] = '\0';
  switch (format) {
    case DATE_FORMAT_MDY:
      snprintf(out, size, "%02d/%02d/%04d", month + 1, day, year);
      return;
    case DATE_FORMAT_YMD:
      snprintf(out, size, "%04d/%02d/%02d", year, month + 1, day);
      return;
    case DATE_FORMAT_DMY_SLASH:
      snprintf(out, size, "%02d/%02d/%04d", day, month + 1, year);
      return;
    case DATE_FORMAT_DM:
      snprintf(out, size, "%02d.%02d.", day, month + 1);
      return;
    case DATE_FORMAT_D_MONTH:
    case DATE_FORMAT_D_MONTH_YEAR: {
      const i18n::LocaleProfile& locale = i18n::locale(language_code);
      const char* name = month >= 0 && month < 12 && locale.month_names_in_date[month] ? locale.month_names_in_date[month] : "";
      char day_text[12];
      char year_text[12];
      snprintf(day_text, sizeof(day_text), "%d", day);
      snprintf(year_text, sizeof(year_text), "%04d", year);
      fill_date_pattern(out, size, long_date_pattern(locale, format == DATE_FORMAT_D_MONTH_YEAR), day_text, name,
                        year_text);
      return;
    }
    default:
      snprintf(out, size, "%02d.%02d.%04d", day, month + 1, year);
      return;
  }
}

// The month formats as a pattern in the locale's order, like the list's
// other choices ("DD. MMMM", "DD MMMM YYYY"); a fixed example date (issue
// #13's 21 August 2026) read like a value (user 2026-10-14).
inline void format_date_pattern(char* out, size_t size, uint8_t format, const char* language_code) {
  if (!out || size == 0) return;
  fill_date_pattern(out, size, long_date_pattern(i18n::locale(language_code), format == DATE_FORMAT_D_MONTH_YEAR),
                    "DD", "MMMM", "YYYY");
}

// The clock's date line: "Samstag, 21. August", the weekday or the date
// alone, empty without both.
inline void format_date_line(char* out, size_t size, int tm_wday, int day, int month, int year, bool show_date,
                             uint8_t format, uint8_t weekday, const char* language_code) {
  if (!out || size == 0) return;
  char date[64] = "";
  if (show_date) format_date(date, sizeof(date), day, month, year, format, language_code);
  const char* name = weekday == WEEKDAY_LONG    ? weekday_name(tm_wday, language_code)
                     : weekday == WEEKDAY_SHORT ? weekday_short_name(tm_wday, language_code)
                                                : "";
  if (name[0] && date[0]) {
    snprintf(out, size, "%s, %s", name, date);
  } else {
    snprintf(out, size, "%s", name[0] ? name : date);
  }
}

}  // namespace clock_tile
