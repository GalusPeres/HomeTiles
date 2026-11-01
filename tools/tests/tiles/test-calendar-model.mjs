// Calendar tile data: the Bridge's events payload (calendar_events.h) and what
// the tile and popup derive from it (calendar_model.h), run on the host with
// the firmware's ArduinoJson.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')]
  .filter(Boolean).find(p => fs.existsSync(path.join(p, 'ArduinoJson.h')));
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (!jsonInclude || !compiler) {
  console.log('SKIP: calendar model needs ArduinoJson and a host C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/calendar-model');
fs.mkdirSync(out, {recursive: true});
const source = String.raw`
#include <cassert>
#include <cstring>
#include <string>
#include "src/types/calendar/calendar_events.h"
#include "src/types/calendar/calendar_model.h"
using namespace calendar_events;
using namespace calendar_model;

static int32_t D(int y, unsigned m, unsigned d) { return days_from_civil(y, m, d); }
static Calendar P(const std::string& text) { return parse(text.c_str(), text.size()); }

int main() {
  // Civil dates and weekdays (1970-01-01 was a Thursday, 2026-10-24 a Saturday).
  assert(D(1970, 1, 1) == 0 && weekday(0) == 4);
  assert(weekday(D(2026, 10, 24)) == 6 && weekday(D(2026, 10, 26)) == 1);
  assert(weekday(-1) == 3 && weekday(-5) == 6 && weekday(-11) == 0);
  for (int32_t day = -800; day < 30000; day += 37) {
    int y; unsigned m, d;
    civil_from_days(day, y, m, d);
    assert(D(y, m, d) == day);
  }
  assert(days_in_month(2028, 2) == 29 && days_in_month(2026, 2) == 28 && days_in_month(2100, 2) == 28);

  // Stamps: a date alone is all-day; times keep their minute; nonsense fails.
  int32_t day; int16_t minute;
  assert(parse_stamp("2026-10-24", day, minute) && day == D(2026, 10, 24) && minute == kAllDay);
  assert(parse_stamp("2026-10-24T10:05", day, minute) && minute == 605);
  assert(parse_stamp("2026-10-24 23:59:30", day, minute) && minute == 1439);
  assert(!parse_stamp("2026-02-30", day, minute) && !parse_stamp("2026-10-24T24:00", day, minute));
  assert(!parse_stamp("24.10.2026", day, minute) && !parse_stamp(nullptr, day, minute));

  const std::string family = R"({"v":1,"state":"on","from":"2026-09-24","to":"2026-12-31","e":[
    {"s":"2026-10-24T19:00","e":"2026-10-24T22:00","t":"Dinner at Anna's","l":"Anna"},
    {"s":"2026-10-27T08:00","e":"2026-10-27T08:45","t":"Dentist"},
    {"s":"2026-10-30","e":"2026-11-02","t":"Weekend in Hamburg"},
    {"s":"2026-10-25T23:00","e":"2026-10-26T00:00","t":"Late call"},
    {"s":"2026-10-28T22:00","e":"2026-10-29T02:00","t":"Night shift"},
    {"s":"2026-10-24","e":"2026-10-24T10:00","t":"mixed kinds are dropped"},
    {"s":"bad","e":"2026-10-24","t":"broken"},
    7
  ]})";
  Calendar a = P(family);
  assert(a.valid && a.available && a.from_day == D(2026, 9, 24) && a.to_day == D(2026, 12, 31));
  assert(a.events.size() == 5);
  // Sorted by start; all-day ends are exclusive; midnight ends belong to the day before.
  assert(!strcmp(a.events[0].title, "Dinner at Anna's") && !strcmp(a.events[0].location, "Anna"));
  const Event& late = a.events[1];
  assert(!strcmp(late.title, "Late call") && late.end_day == D(2026, 10, 25) && late.end_min == 24 * 60);
  const Event& trip = a.events[4];
  assert(!strcmp(trip.title, "Weekend in Hamburg") && trip.all_day() && trip.end_day == D(2026, 11, 1));
  const Event& night = a.events[3];
  assert(night.start_day == D(2026, 10, 28) && night.end_day == D(2026, 10, 29) && night.end_min == 120);

  const std::string waste = R"({"v":1,"state":"off","from":"2026-09-24","to":"2026-12-24","e":[
    {"s":"2026-10-24","e":"2026-10-25","t":"Bio waste"},
    {"s":"2026-10-26","e":"2026-10-27","t":"Paper"}]})";
  Calendar b = P(waste);
  assert(b.valid && b.available && b.events.size() == 2);

  // Unavailable calendars and broken payloads.
  Calendar gone = P(R"({"v":1,"state":"unavailable","from":"2026-10-01","to":"2026-10-31","e":[]})");
  assert(gone.valid && !gone.available && gone.events.empty());
  assert(!P("[]").valid && !P(R"({"v":2,"from":"2026-10-01","to":"2026-10-31"})").valid);
  assert(!P(R"({"v":1,"from":"2026-10-31","to":"2026-10-01"})").valid);
  assert(!parse(nullptr, 0).valid);

  // Titles are bounded and cut on a character boundary; control characters go.
  std::string long_title(200, 'x');
  long_title.replace(61, 2, "\xC3\xA4");  // "ä" across the 63-byte cut
  Calendar c = P(std::string(R"({"v":1,"state":"on","from":"2026-10-01","to":"2026-10-31","e":[{"s":"2026-10-02","e":"2026-10-03","t":")") +
                 long_title + R"(\nend"}]})");
  assert(c.events.size() == 1 && strlen(c.events[0].title) <= kTitleBytes);
  assert((static_cast<unsigned char>(c.events[0].title[strlen(c.events[0].title) - 1]) & 0xC0) != 0xC0);
  // "ä" ends exactly at the cut: it stays (the cut dropped a whole character
  // here before).
  assert(strlen(c.events[0].title) == kTitleBytes && !strcmp(c.events[0].title + 61, "\xC3\xA4"));
  // "ä" across the cut: it goes as a whole.
  std::string across(200, 'x');
  across.replace(62, 2, "\xC3\xA4");
  Calendar a2 = P(std::string(R"({"v":1,"state":"on","from":"2026-10-01","to":"2026-10-31","e":[{"s":"2026-10-02","e":"2026-10-03","t":")") +
                  across + R"("}]})");
  assert(strlen(a2.events[0].title) == 62 && a2.events[0].title[61] == 'x');
  Calendar ctrl = P(R"({"v":1,"state":"on","from":"2026-10-01","to":"2026-10-31","e":[{"s":"2026-10-02","e":"2026-10-03","t":"a\nb"}]})");
  assert(!strcmp(ctrl.events[0].title, "a b"));

  // Merging: all-day first, then by start; both calendars counted.
  Sources s;
  s.calendars[0] = &a;
  s.calendars[1] = &b;
  s.count = 2;
  Item items[8];
  const int32_t today = D(2026, 10, 24);
  size_t n = items_on(s, today, items, 8);
  assert(n == 2 && !strcmp(items[0].event->title, "Bio waste") && items[0].calendar == 1);
  assert(!strcmp(items[1].event->title, "Dinner at Anna's"));
  // A limit keeps the earliest ones and still counts all.
  n = items_on(s, today, items, 1);
  assert(n == 2 && !strcmp(items[0].event->title, "Bio waste"));
  assert(items_on(s, D(2026, 10, 26), items, 8) == 1 && !strcmp(items[0].event->title, "Paper"));
  // A multi-day event shows on each of its days; a timed one over midnight too.
  assert(items_on(s, D(2026, 11, 1), items, 8) == 1 && items_on(s, D(2026, 11, 2), items, 8) == 0);
  assert(items_on(s, D(2026, 10, 29), items, 8) == 1 && start_on(*items[0].event, D(2026, 10, 29)) == 0);
  assert(day_mask(s, today) == 3 && day_mask(s, D(2026, 10, 27)) == 1 && day_mask(s, D(2026, 10, 23)) == 0);

  // Next: today's all-day event runs now; a running timed event comes first.
  Next next = next_event(s, today, 8 * 60 + 30, today + 30);
  assert(next.found && next.ongoing && !strcmp(next.item.event->title, "Bio waste"));
  Next evening = next_event(s, today, 19 * 60 + 10, today + 30);
  assert(evening.ongoing && !strcmp(evening.item.event->title, "Dinner at Anna's"));
  // Without today's events the next start counts down.
  Sources only_family;
  only_family.calendars[0] = &a;
  only_family.count = 1;
  next = next_event(only_family, today, 8 * 60, today + 30);
  assert(next.found && !next.ongoing && next.day == today && upcoming_timed(next, today, 8 * 60));
  assert(minutes_until(next, today, 8 * 60) == 11 * 60);
  next = next_event(only_family, today, 22 * 60, today + 30);
  assert(!strcmp(next.item.event->title, "Late call") && minutes_until(next, today, 22 * 60) == 25 * 60);
  // Nothing within the horizon.
  assert(!next_event(only_family, D(2026, 11, 2), 0, D(2026, 11, 20)).found);
  // An empty source list has no data.
  Sources none;
  assert(!none.any_valid() && !next_event(none, today, 0, today + 30).found);

  // Window: the days every calendar covers.
  int32_t from, to;
  assert(s.window(from, to) && from == D(2026, 9, 24) && to == D(2026, 12, 24));

  // Month grids: October 2026 starts on a Thursday.
  assert(month_grid_start(2026, 10, 1) == D(2026, 9, 28));
  assert(month_grid_start(2026, 10, 0) == D(2026, 9, 27));
  assert(month_grid_start(2026, 10, 6) == D(2026, 9, 26));
  assert(month_grid_start(2026, 2, 0) == D(2026, 2, 1));
  assert(month_rows(2026, 10, 1) == 5 && month_rows(2026, 8, 1) == 6 && month_rows(2026, 2, 0) == 4);
  assert(week_start_day(today, 1) == D(2026, 10, 19) && week_start_day(today, 0) == D(2026, 10, 18));
  assert(week_start_day(today, 6) == today);
  int y = 2026; unsigned m = 12;
  add_months(y, m, 1);
  assert(y == 2027 && m == 1);
  add_months(y, m, -13);
  assert(y == 2025 && m == 12);

  // Today's events left: all-day and running ones count, finished ones not.
  assert(remaining_today(s, today, 8 * 60) == 2);
  assert(remaining_today(s, today, 21 * 60) == 2);
  assert(remaining_today(s, today, 22 * 60) == 1);
  assert(remaining_today(s, D(2026, 10, 23), 0) == 0 && remaining_today(none, today, 0) == 0);
  // A night shift from yesterday still running counts until it ends.
  assert(remaining_today(only_family, D(2026, 10, 29), 60) == 1 && remaining_today(only_family, D(2026, 10, 29), 120) == 0);
  // Week strips: one row starts today, taller ones on the first weekday.
  assert(strip_first_day(today, 1, true) == today && strip_first_day(today, 1, false) == D(2026, 10, 19));
  assert(strip_first_day(today, 0, false) == D(2026, 10, 18));

  // Tile options.
  assert(normalize_view(3) == kViewToday && normalize_view(4) == kViewAgenda);
  assert(normalize_view(2) == kViewMonth && normalize_view(9) == kViewAgenda);
  assert(normalize_week_start(0xFF) == kWeekStartAuto && normalize_week_start(3) == kWeekStartAuto);
  assert(resolve_week_start(kWeekStartAuto, 1) == 1 && resolve_week_start(0, 1) == 0);
  assert(resolve_week_start(kWeekStartAuto, 9) == 1);
  assert(calendar_color(0, false) != calendar_color(1, false) && calendar_color(4, true) == calendar_color(0, true));
  return 0;
}
`;
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, source);
const compile = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-I', root, '-I', jsonInclude, file, '-o', binary],
                          {encoding: 'utf8'});
assert.equal(compile.status, 0, `calendar model did not compile:\n${compile.stdout}${compile.stderr}`);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `calendar model contract failed:\n${run.stdout}${run.stderr}`);
console.log('Calendar: events payload, merged days, next event, today count, strips, month grid and options');
