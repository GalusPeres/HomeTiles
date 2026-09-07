#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace value_editor {
// Work on HA's step grid, including fractional steps and off-grid live states.
inline double stepped(double value, double minimum, double maximum, double step, int direction) {
  const double position = (value - minimum) / step;
  const double last = std::floor((maximum - minimum) / step + 1e-9);
  const double next = direction > 0 ? std::floor(position + 1e-9) + 1 : std::ceil(position - 1e-9) - 1;
  return std::min(maximum, minimum + std::max(0.0, std::min(last, next)) * step);
}
struct Calendar {
  int values[6] = {1970, 1, 1, 0, 0, 0};
};
inline int days(int year, int month) {
  if (month == 2) return 28 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
  return month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}
inline void stepCalendar(Calendar& calendar, int field, int direction) {
  const int minimum[] = {1, 1, 1, 0, 0, 0};
  const int maximum[] = {9999, 12, days(calendar.values[0], calendar.values[1]), 23, 59, 59};
  int& value = calendar.values[field];
  value = std::max(minimum[field], std::min(maximum[field], value + direction));
  calendar.values[2] = std::min(calendar.values[2], days(calendar.values[0], calendar.values[1]));
}
inline bool parseCalendar(const char* text, bool date, bool clock, Calendar& calendar) {
  Calendar next;
  int used = 0;
  if (date) {
    if (std::sscanf(text, "%d-%d-%d%n", &next.values[0], &next.values[1], &next.values[2], &used) != 3) return false;
    text += used;
    if (clock) { if (*text != ' ' && *text != 'T') return false; ++text; }
  }
  if (clock) {
    used = 0;
    if (std::sscanf(text, "%d:%d%n", &next.values[3], &next.values[4], &used) != 2) return false;
    text += used;
    if (*text == ':') {
      used = 0;
      if (std::sscanf(text, ":%d%n", &next.values[5], &used) != 1) return false;
      text += used;
    }
  }
  if (*text || next.values[0] < 1 || next.values[0] > 9999 || next.values[1] < 1 || next.values[1] > 12 ||
      next.values[2] < 1 || next.values[2] > days(next.values[0], next.values[1]) ||
      next.values[3] < 0 || next.values[3] > 23 || next.values[4] < 0 || next.values[4] > 59 ||
      next.values[5] < 0 || next.values[5] > 59) return false;
  calendar = next;
  return true;
}
}  // namespace value_editor
