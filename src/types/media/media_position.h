#pragma once

#include <stdint.h>

// Home Assistant's media position is the position at media_position_updated_at,
// not at the time a state arrives: a volume change sends the old position
// again, and the panel jumped back to the last play or pause (user
// 2026-10-22, V2). The Bridge sends the time as UTC ISO 8601
// ("2026-10-22T15:04:05.123456+00:00").
namespace media_progress {

// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil).
inline int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Seconds since 1970 (UTC) of "YYYY-MM-DD[T ]HH:MM:SS[.fraction][Z|+HH:MM|-HH:MM]";
// false for anything else.
inline bool parse_utc(const char* text, double& out) {
  if (!text) return false;
  auto digits = [&](int count, int& value) {
    value = 0;
    for (int i = 0; i < count; ++i, ++text) {
      if (*text < '0' || *text > '9') return false;
      value = value * 10 + (*text - '0');
    }
    return true;
  };
  int year, month, day, hour, minute, second;
  if (!digits(4, year) || *text++ != '-' || !digits(2, month) || *text++ != '-' || !digits(2, day)) return false;
  if (*text != 'T' && *text != ' ') return false;
  ++text;
  if (!digits(2, hour) || *text++ != ':' || !digits(2, minute) || *text++ != ':' || !digits(2, second)) return false;
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return false;
  double fraction = 0.0;
  if (*text == '.') {
    double scale = 0.1;
    for (++text; *text >= '0' && *text <= '9'; ++text, scale /= 10) fraction += (*text - '0') * scale;
  }
  int offset = 0;
  if (*text == '+' || *text == '-') {
    const int sign = *text == '-' ? -1 : 1;
    ++text;
    int oh, om;
    if (!digits(2, oh) || *text++ != ':' || !digits(2, om)) return false;
    offset = sign * (oh * 3600 + om * 60);
  } else if (*text == 'Z') {
    ++text;
  }
  if (*text != '\0') return false;
  const int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  out = static_cast<double>(days * 86400 + hour * 3600 + minute * 60 + second - offset) + fraction;
  return true;
}

// The position now: the reported one plus the time since it was taken while
// playing (now and updated in seconds since 1970). Without a clock or an
// updated time (or an implausible one) the reported position.
inline float now_position(float reported, float duration, bool playing, double now, bool has_updated, double updated) {
  float position = reported;
  if (playing && has_updated && now >= 1700000000.0) {
    const double since = now - updated;
    if (since > 0.0 && since < 24.0 * 3600.0) position += static_cast<float>(since);
  }
  if (position < 0.0f) position = 0.0f;
  if (duration > 0.0f && position > duration) position = duration;
  return position;
}

}  // namespace media_progress
