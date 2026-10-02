#pragma once

#include <stdint.h>

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
inline const char* weekday_name(int tm_wday, const char* language_code) {
  if (tm_wday < 0 || tm_wday > 6) return "";
  return i18n::locale(language_code).weekday_names[tm_wday];
}

}  // namespace clock_tile
