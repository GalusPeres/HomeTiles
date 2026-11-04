// Energy tiles refresh by themselves, sparingly and for certain: once an hour,
// 20 minutes past the hour, when Home Assistant's own Energy dashboard refreshes
// (user 2026-11-04: they only moved after a tap; every five minutes is too much;
// and nothing may swallow it). The hour counts only once an answer reached the
// tiles, and an answer without hours never blanks them. Runs the production
// functions over simulated days and real payloads.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/types/energy/energy_data.cpp').replace(/\r\n/g, '\n');
const header = readRepoFile('src/types/energy/energy_data.h').replace(/\r\n/g, '\n');
const functions = cppFunctionDefinitions(source);
const fn = name => {
  const found = functions.find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};
const constant = name => {
  const line = source.match(new RegExp(`constexpr [^\\n]* ${name} = [^\\n]*;`));
  assert.ok(line, name);
  return line[0];
};
const values = output => Object.fromEntries(
  [...output.matchAll(/(\w+)=(-?\d+)/g)].map(match => [match[1], Number(match[2])]));

assert.match(source, /#include <time\.h>/, 'time and localtime_r come from time.h on the firmware');

// The simulated clock and the refresh's own state and constants.
const clock = String.raw`
static uint32_t g_millis = 1000;
static time_t g_wall = 0;
static uint32_t millis() { return g_millis; }
static time_t fake_time(time_t*) { return g_wall; }
static struct tm* fake_localtime(const time_t* wall, struct tm* out) {
  const long long seconds = static_cast<long long>(*wall);
  out->tm_sec = static_cast<int>(seconds % 60);
  out->tm_min = static_cast<int>(seconds / 60 % 60);
  out->tm_hour = static_cast<int>(seconds / 3600 % 24);
  return out;
}
#define time(x) fake_time(x)
#define localtime_r(a, b) fake_localtime(a, b)
static int g_refreshed_hour = -1;
static int g_asked_hour = -1;
static uint32_t g_day_applied_ms = 0;
static uint32_t g_refresh_checked_ms = 0;
${constant('kEnergyRefreshMinute')}
${constant('kEnergyRefreshMs')}
${constant('kEnergyUnansweredMs')}
${constant('kClockSetEpoch')}
// 2026-11-04 00:00 in the fake clock's own count (a whole number of days).
static const time_t kMidnight = static_cast<time_t>(1793750400LL);
${fn('read_local_clock')}
${fn('note_day_answer_applied')}
`;

// ---- When it asks ------------------------------------------------------------
const periodicHarness = String.raw`
#include <cstdint>
#include <cstdio>
#include <ctime>
${clock}
static bool g_online = true, g_section = false, g_tiles = true, g_queued = true, g_answers = true;
static int g_lost = 0, g_requests = 0, g_wrong_minute = 0;
static time_t g_last_request_wall = 0;

struct Network { bool isMqttConnected() const { return g_online; } } networkManager;
struct Bridge { bool hasEnergyEntries() const { return g_section; } } haBridgeConfig;
struct RequestState { uint32_t last_attempt_ms = 0; };
static RequestState g_day_request, g_week_request, g_month_request, g_year_request;
static void service_energy_retry(const char*, RequestState&, uint32_t) {}
static bool energy_tiles_alive() { return g_tiles; }
static bool energy_request_day_for_tiles(bool) {
  if (!g_queued) return false;
  ++g_requests;
  g_last_request_wall = g_wall;
  g_day_request.last_attempt_ms = g_millis;
  // A set clock asks from minute 20 on.
  if (g_wall >= kClockSetEpoch && (g_wall / 60 % 60) < kEnergyRefreshMinute) ++g_wrong_minute;
  if (g_lost > 0) --g_lost;
  else if (g_answers) note_day_answer_applied();
  return true;
}

${fn('energy_service_periodic')}

static void reset(time_t wall) {
  g_refreshed_hour = g_asked_hour = -1;
  g_day_applied_ms = g_refresh_checked_ms = 0;
  g_day_request = RequestState{};
  g_requests = g_wrong_minute = g_lost = 0;
  g_wall = wall;
  g_online = g_tiles = g_queued = g_answers = true;
  g_section = false;
}

// Whole seconds, with several loop passes in each.
static void run(long seconds) {
  for (long i = 0; i < seconds; ++i) {
    for (int pass = 0; pass < 4; ++pass) {
      energy_service_periodic();
      g_millis += 250;
    }
    ++g_wall;
  }
}

int main() {
  const long day = 24L * 3600L;

  reset(kMidnight);
  run(day);
  std::printf("day=%d wrong=%d\n", g_requests, g_wrong_minute);

  // A start at 10:30 asks for this hour once, then at 11:20.
  reset(kMidnight + 10 * 3600 + 30 * 60);
  run(30 * 60);  // up to 11:00
  const int after_start = g_requests;
  run(20 * 60 - 1);  // up to 11:19:59
  const int before_next = g_requests;
  run(2);  // 11:20:01
  std::printf("start=%d before=%d next=%d\n", after_start, before_next, g_requests);

  // An answer lost on the way: asked again five minutes later, then done.
  reset(kMidnight);
  g_lost = 1;
  run(3600);
  std::printf("lost=%d lost_minute=%d\n", g_requests, static_cast<int>(g_last_request_wall / 60 % 60));

  // No answer at all: every five minutes from 20 past, no more than that.
  reset(kMidnight);
  g_answers = false;
  run(day);
  std::printf("never=%d\n", g_requests);

  // An answer another way (a popup opened at 10:22) counts for the hour.
  reset(kMidnight + 10 * 3600);
  g_answers = false;
  run(22 * 60);
  const int before_popup = g_requests;
  note_day_answer_applied();
  run(38 * 60);
  std::printf("popup_before=%d popup=%d\n", before_popup, g_requests);

  // An answer from before 20 past (a tile built at 10:10) does not.
  reset(kMidnight + 10 * 3600 + 10 * 60);
  note_day_answer_applied();
  run(11 * 60);
  std::printf("early=%d\n", g_requests);

  // Nothing to show, nothing to ask for.
  reset(kMidnight);
  g_tiles = false;
  run(day);
  std::printf("no_tile=%d\n", g_requests);

  // An energy section alone still asks.
  reset(kMidnight);
  g_tiles = false;
  g_section = true;
  run(day);
  std::printf("section=%d\n", g_requests);

  // Offline: no request.
  reset(kMidnight);
  g_online = false;
  run(day);
  std::printf("offline=%d\n", g_requests);

  // A full send queue tries again at once when it has room.
  reset(kMidnight);
  g_queued = false;
  run(30 * 60);  // up to 00:30
  g_queued = true;
  run(60);
  std::printf("queued=%d\n", g_requests);

  // No time server: an hour after the last answer.
  reset(static_cast<time_t>(1000));
  run(day);
  std::printf("no_clock=%d\n", g_requests);
  return 0;
}
`;

const periodicOutput = compileAndRun({label: 'Energy hourly refresh', harness: periodicHarness});
if (periodicOutput !== null) {
  const seen = values(periodicOutput);
  assert.equal(seen.day, 24, 'one request an hour');
  assert.equal(seen.wrong, 0, 'never before 20 past');
  assert.equal(seen.start, 1, 'a start late in the hour asks once');
  assert.equal(seen.before, 1, 'and not again before the next hour\'s minute');
  assert.equal(seen.next, 2, 'the next one at 20 past');
  assert.equal(seen.lost, 2, 'a lost answer is asked for again, then it stops');
  assert.equal(seen.lost_minute, 25, 'five minutes later');
  assert.equal(seen.never, 8 * 24, 'without any answer eight requests an hour at most');
  assert.equal(seen.popup_before, 1, 'asked at 20 past');
  assert.equal(seen.popup, 1, 'a popup\'s answer counts for the hour');
  assert.equal(seen.early, 1, 'an answer before 20 past does not');
  assert.equal(seen.no_tile, 0, 'no tile, no section: no request');
  assert.equal(seen.section, 24, 'an energy section alone keeps its refresh');
  assert.equal(seen.offline, 0, 'offline: no request');
  assert.equal(seen.queued, 1, 'a full queue does not lose the hour');
  assert.equal(seen.no_clock, 24, 'without a clock once an hour too');
}

// ---- What an answer does -----------------------------------------------------
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')]
  .filter(Boolean).find(folder => fs.existsSync(path.join(folder, 'ArduinoJson.h')));
if (!jsonInclude) {
  console.log('SKIP: the Energy answers need ArduinoJson');
} else {
  const parseHarness = String.raw`
// The firmware keeps ArduinoJson's compatibility class for its documents.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// The shared clock block also holds what only the periodic harness uses.
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <math.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& text) : std::string(text) {}
  bool equalsIgnoreCase(const String& other) const { return *this == other; }
};
struct TestSerial {
  template <class... T> void printf(const char*, T...) {}
  void println(const char*) {}
} Serial;
${header.match(/static constexpr uint8_t ENERGY_VALUES_MAX = \d+;/)[0]}
${header.match(/struct EnergyEntryData \{[\s\S]*?\n\};/)[0]}
${clock}
struct Bridge {
  String findSensorName(const String&) const { return String(); }
  String findSensorUnit(const String&) const { return String(); }
} haBridgeConfig;
struct EnergyRequestState { uint32_t last_attempt_ms = 0; };
static EnergyRequestState g_day_request, g_week_request, g_month_request, g_year_request;
static std::vector<EnergyEntryData> g_day_entries, g_week_entries, g_month_entries, g_year_entries;
static int g_tile_values = 0, g_tile_bars = 0, g_last_bar_hours = -1;
static void queue_energy_tile_update_for_entry(const EnergyEntryData&) { ++g_tile_values; }
static void energy_tile_bars_update(const EnergyEntryData& entry) {
  ++g_tile_bars;
  g_last_bar_hours = entry.value_count;
}
static String energy_source_icon(const String&) { return String("icon"); }
static void tiles_request_icon_refresh() {}
static void queue_energy_popup_refresh(const char*) {}
${fn('normalize_period')}
${fn('request_state_for_period')}
${fn('cache_for_period')}
${fn('cached_unit_for_entry')}
${fn('apply_energy_sign')}
${fn('answer_has_hours')}
${fn('parse_energy_response')}

static void at(int hour, int minute) { g_wall = kMidnight + hour * 3600 + minute * 60; g_millis += 1000; }
static void show(const char* name, const char* payload) {
  g_tile_values = g_tile_bars = 0;
  parse_energy_response(payload);
  const float total = g_day_entries.empty() ? -1.0f : g_day_entries[0].total;
  std::printf("%s_values=%d %s_bars=%d %s_hour=%d %s_total=%d %s_day=%d\n", name, g_tile_values, name, g_tile_bars,
              name, g_refreshed_hour, name, static_cast<int>(total * 10.0f), name, static_cast<int>(g_day_entries.size()));
}

int main() {
  at(10, 25);
  show("good", R"({"period":"day","entries":[
    {"id":"sensor.pv","values":[0,0,1.5],"total":1.5},{"id":"sensor.grid","values":[1,1,1],"total":3}]})");
  std::printf("good_hours=%d\n", g_last_bar_hours);

  // Nothing here may touch the tiles, the stored answer or the hour.
  at(11, 25);
  show("no_hours", R"({"period":"day","entries":[
    {"id":"sensor.pv","values":[],"total":0},{"id":"sensor.grid","values":[],"total":0}]})");
  show("null_hours", R"({"period":"day","entries":[{"id":"sensor.pv","values":[null,null],"total":0}]})");
  show("no_values", R"({"period":"day","entries":[{"id":"sensor.pv","total":0}]})");
  show("broken", R"({"period":"day","entries":[{"id":"sensor.pv","val)");
  show("week", R"({"period":"week","entries":[{"id":"sensor.pv","values":[9],"total":9}]})");

  // One source with hours: the answer counts, also for the source without.
  at(11, 26);
  show("mixed", R"({"period":"day","entries":[
    {"id":"sensor.pv","values":[2],"total":2},{"id":"sensor.new","values":[],"total":0}]})");

  // An Energy dashboard without sources is an answer too: nothing to wait for.
  at(12, 21);
  show("no_sources", R"({"period":"day","entries":[]})");

  // Before 20 past it reaches the tiles, but the hour's refresh is still to come.
  at(13, 10);
  show("early", R"({"period":"day","entries":[{"id":"sensor.pv","values":[3],"total":3}]})");
  return 0;
}
`;
  const parseOutput = compileAndRun({label: 'Energy answers', harness: parseHarness, includes: [jsonInclude]});
  if (parseOutput !== null) {
    const seen = values(parseOutput);
    assert.equal(seen.good_values, 2, 'an answer with hours reaches the totals');
    assert.equal(seen.good_bars, 2, 'and the bars');
    assert.equal(seen.good_hours, 3);
    assert.equal(seen.good_hour, 10, 'and counts for the hour');
    assert.equal(seen.good_total, 15);
    for (const name of ['no_hours', 'null_hours', 'no_values', 'broken', 'week']) {
      assert.equal(seen[`${name}_values`], 0, `${name}: the totals stay`);
      assert.equal(seen[`${name}_bars`], 0, `${name}: the bars stay`);
      assert.equal(seen[`${name}_hour`], 10, `${name}: the hour is still to come`);
      assert.equal(seen[`${name}_total`], 15, `${name}: the stored day stays`);
      assert.equal(seen[`${name}_day`], 2, `${name}: the stored day stays`);
    }
    assert.equal(seen.mixed_bars, 2, 'one source with hours is a real answer');
    assert.equal(seen.mixed_hour, 11);
    assert.equal(seen.no_sources_hour, 12, 'no sources: nothing to wait for');
    assert.equal(seen.early_bars, 1, 'an early answer reaches the bars');
    assert.equal(seen.early_hour, 12, 'but does not count for its hour');
  }
}

console.log('Energy hourly refresh: 24 requests a day at 20 past the hour, a lost answer asked for again, an answer without hours keeps the tiles');
