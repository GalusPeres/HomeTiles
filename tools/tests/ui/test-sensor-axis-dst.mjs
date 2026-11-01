// The Sensor popup's time axis across a daylight saving change. The midnight
// and hour boundaries kept the DST flag of the time they started from, so
// the day after a change began an hour early: 7D showed "Sun Sun" around
// 2026-10-25 and its day lines sat an hour off (user 2026-10-27). Compiles
// the real next_local_midnight_after and next_local_boundary_after
// (sensor_popup.cpp) on the host in a zone with DST, finds the day the
// clocks go back and checks both boundaries on and after it.
import assert from 'node:assert/strict';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const popup = readRepoFile('src/ui/popups/sensor/sensor_popup.cpp');
const fn = (name) => {
  const found = cppFunctionDefinitions(popup).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};

const harness = String.raw`
#include <cstdio>
#include <cstdlib>
#include <ctime>

${fn('next_local_midnight_after')}
${fn('next_local_boundary_after')}

static time_t local_midnight(int year, int month, int day) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month;
  t.tm_mday = day;
  t.tm_isdst = -1;
  return mktime(&t);
}
static struct tm local(time_t ts) {
  struct tm t;
#ifdef _WIN32
  localtime_s(&t, &ts);
#else
  localtime_r(&ts, &t);
#endif
  return t;
}

int main() {
#ifdef _WIN32
  _putenv_s("TZ", "EST5EDT");
  _tzset();
#else
  setenv("TZ", "EST5EDT", 1);
  tzset();
#endif
  // The day of 2026 with 25 hours: the clocks go back.
  time_t day = 0, next = 0;
  for (int d = 1; d < 365 && !day; ++d) {
    const time_t a = local_midnight(2026, 0, d), b = local_midnight(2026, 0, d + 1);
    if (b - a == 25 * 3600) day = a, next = b;
  }
  if (!day) {
    std::puts("nodst");
    return 0;
  }
  const time_t midnight = next_local_midnight_after(day);
  const struct tm m = local(midnight);
  std::printf("midnight %ld %d %d\n", static_cast<long>(midnight - next), m.tm_hour, m.tm_mday == local(next).tm_mday ? 1 : 0);
  // Every six hours from half past midnight, still in summer time: 06:00.
  const struct tm six = local(next_local_boundary_after(day + 1800, 6));
  std::printf("six %d\n", six.tm_hour);
  // The day before the change: its midnight is the change day's.
  std::printf("before %ld\n", static_cast<long>(next_local_midnight_after(day - 12 * 3600) - day));
  return 0;
}
`;
const output = compileAndRun({label: 'Sensor axis daylight saving', harness});
if (output !== null) {
  if (output.trim() === 'nodst') {
    console.log('SKIP: the host C library offers no DST zone for the axis test');
    process.exit(0);
  }
  const rows = Object.fromEntries(output.trim().split('\n').map((line) => {
    const [key, ...rest] = line.trim().split(' ');
    return [key, rest.map(Number)];
  }));
  assert.deepEqual(rows.midnight, [0, 0, 1], 'the day after the change starts at its midnight, not an hour early');
  assert.deepEqual(rows.six, [6], 'hour boundaries on the change day stay on the full hour');
  assert.deepEqual(rows.before, [0], 'the day before the change ends at the change day\'s midnight');
}
console.log('sensor axis: midnight and hour boundaries keep their place across a daylight saving change');
