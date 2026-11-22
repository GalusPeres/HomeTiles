// Energy tiles and an open Energy popup follow Home Assistant's 5-minute statistics
// by themselves: one request per step, a minute after the Recorder wrote it (user
// 2026-11-04: they only moved after a tap; asked once an hour the newest bar stayed
// as small as its first minutes; nothing may swallow it). An answer without hours
// never blanks them. Runs the production functions over simulated days and real
// payloads.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/types/energy/energy_data.cpp').replace(/\r\n/g, '\n');
const header = readRepoFile('src/types/energy/energy_data.h').replace(/\r\n/g, '\n');
const popup = readRepoFile('src/ui/popups/energy/energy_popup.cpp').replace(/\r\n/g, '\n');
const functions = cppFunctionDefinitions(source);
const fn = name => {
  const found = functions.find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};
const popupFn = name => {
  const found = cppFunctionDefinitions(popup).find(item => item.name === name);
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

assert.match(source, /#include <time\.h>/, 'time comes from time.h on the firmware');

// ---- The open popup --------------------------------------------------------------
const openPeriod = popupFn('energy_popup_open_period');
assert.ok(openPeriod.includes('popup_visible(g_energy_popup_ctx)') && openPeriod.includes('shown_period == EnergyPeriod::Day'),
  'only an open popup asks along, and Today gets the tiles\' answer');
assert.ok(popupFn('process_energy_popup_queue').includes('readout.active()) return;'),
  'new chart data waits while a finger reads a bar');

// ---- When it asks ------------------------------------------------------------------
const periodicHarness = String.raw`
#include <cstdint>
#include <cstdio>
#include <ctime>
static uint32_t g_millis = 1000;
static time_t g_wall = 0;
static uint32_t millis() { return g_millis; }
static time_t fake_time(time_t*) { return g_wall; }
#define time(x) fake_time(x)
static int64_t g_asked_step = -1;
${constant('kEnergyStepS')}
${constant('kEnergyStepDelayS')}
${constant('kClockSetEpoch')}
// 2026-11-04 00:00 UTC (a whole number of days).
static const time_t kMidnight = static_cast<time_t>(1793750400LL);

static bool g_online = true, g_section = false, g_tiles = true, g_queued = true, g_answers = true;
static const char* g_popup_period = nullptr;
static int g_lost = 0, g_requests = 0, g_off_step = 0, g_popup_requests = 0;

// Home Assistant as the Bridge reads it: the Recorder writes a step at second 10 of
// every fifth minute, plus its own time. The tile has the step of its last answer.
static long g_recorder_seconds = 2;
static time_t g_tile_has = 0;
static long g_behind = 0, g_most_behind = 0;
static time_t ha_written() { return (g_wall - 10 - g_recorder_seconds) / 300 * 300; }

struct Network { bool isMqttConnected() const { return g_online; } } networkManager;
struct Bridge { bool hasEnergyEntries() const { return g_section; } } haBridgeConfig;
struct RequestState { uint32_t last_attempt_ms = 0; };
static RequestState g_day_request, g_week_request, g_month_request, g_year_request;
static void service_energy_retry(const char*, RequestState&, uint32_t) {}
static bool energy_tiles_alive() { return g_tiles; }
static const char* energy_popup_open_period() { return g_popup_period; }
static bool energy_request_period(const char*, bool) {
  ++g_popup_requests;
  return true;
}
static bool energy_request_day_for_tiles(bool) {
  if (!g_queued) return false;
  ++g_requests;
  g_day_request.last_attempt_ms = g_millis;
  if (g_wall >= kClockSetEpoch && static_cast<uint32_t>(g_wall % kEnergyStepS) != kEnergyStepDelayS) ++g_off_step;
  if (g_lost > 0) {
    --g_lost;
    return true;
  }
  if (g_answers) g_tile_has = ha_written();
  return true;
}

${fn('energy_service_periodic')}

static void reset(time_t wall) {
  g_asked_step = -1;
  g_day_request = RequestState{};
  g_requests = g_off_step = g_lost = g_popup_requests = 0;
  g_wall = wall;
  g_online = g_tiles = g_queued = g_answers = true;
  g_section = false;
  g_popup_period = nullptr;
  g_recorder_seconds = 2;
  g_tile_has = ha_written();
  g_behind = g_most_behind = 0;
}

// Whole seconds, with several loop passes in each.
static void run(long seconds) {
  for (long i = 0; i < seconds; ++i) {
    for (int pass = 0; pass < 4; ++pass) {
      energy_service_periodic();
      g_millis += 250;
    }
    // How long a tap would have found newer statistics than the tile has.
    g_behind = ha_written() != g_tile_has ? g_behind + 1 : 0;
    if (g_behind > g_most_behind) g_most_behind = g_behind;
    ++g_wall;
  }
}

int main() {
  const long day = 24L * 3600L;
  const time_t first_step = kMidnight + 60;

  reset(first_step);
  run(day);
  std::printf("day=%d off=%d behind=%ld popup_closed=%d\n", g_requests, g_off_step, g_most_behind, g_popup_requests);

  // A busy Recorder writes a step minutes late: the next step's request has it.
  reset(first_step);
  g_recorder_seconds = 4 * 60;
  run(day);
  std::printf("busy=%d busy_behind=%ld\n", g_requests, g_most_behind);

  // A start inside a step asks at once, then with the next step.
  reset(kMidnight + 10 * 3600 + 30 * 60 + 20);  // 10:30:20
  run(30);
  const int started = g_requests;
  run(15);  // 10:31:05
  std::printf("start=%d next=%d\n", started, g_requests);

  // An answer lost on the way costs one step, then the tile has the newest again.
  reset(first_step);
  run(290);  // up to 00:05:50
  g_lost = 1;
  run(320);  // 00:06 lost, 00:11 answered
  std::printf("lost=%d lost_behind=%ld lost_caught=%d\n", g_requests, g_most_behind, g_tile_has == ha_written() ? 1 : 0);

  // No answer at all: still one request per step, no more.
  reset(first_step);
  g_answers = false;
  run(day);
  std::printf("never=%d\n", g_requests);

  // An open popup on Week, Month or Year asks along, once per step.
  reset(first_step);
  g_popup_period = "week";
  run(3600);
  std::printf("popup_day=%d popup_open=%d\n", g_requests, g_popup_requests);

  // Nothing to show, nothing to ask for.
  reset(first_step);
  g_tiles = false;
  run(day);
  std::printf("no_tile=%d\n", g_requests);

  // An energy section alone still asks.
  reset(first_step);
  g_tiles = false;
  g_section = true;
  run(day);
  std::printf("section=%d\n", g_requests);

  // Offline: no request.
  reset(first_step);
  g_online = false;
  run(day);
  std::printf("offline=%d\n", g_requests);

  // A full send queue tries again at once when it has room.
  reset(first_step);
  g_queued = false;
  run(32 * 60);  // up to 00:33
  g_queued = true;
  run(10);
  std::printf("queued=%d\n", g_requests);

  // No time server: the same rate since the last try.
  reset(static_cast<time_t>(1000));
  run(day);
  std::printf("no_clock=%d\n", g_requests);

  // The clock arrives: one request at once, then with the steps.
  reset(static_cast<time_t>(1000));
  run(600);
  const int before_clock = g_requests;
  g_wall = kMidnight + 10 * 60;  // 00:10:00
  run(5);
  const int at_clock = g_requests;
  run(30);
  std::printf("before_clock=%d at_clock=%d after_clock=%d\n", before_clock, at_clock, g_requests);
  return 0;
}
`;

const periodicOutput = compileAndRun({label: 'Energy refresh', harness: periodicHarness});
if (periodicOutput !== null) {
  const seen = values(periodicOutput);
  assert.equal(seen.day, 288, 'one request per 5-minute step');
  assert.equal(seen.off, 0, 'each a minute into its step');
  assert.ok(seen.behind <= 60, `a new step reaches the tile within a minute, without a tap (${seen.behind} s)`);
  assert.equal(seen.popup_closed, 0, 'a closed popup asks for nothing');
  assert.equal(seen.busy, 288, 'a busy Recorder costs no extra request');
  assert.ok(seen.busy_behind <= 120, `and its step still comes without a tap (${seen.busy_behind} s)`);
  assert.equal(seen.start, 1, 'a start inside a step asks at once');
  assert.equal(seen.next, 2, 'then with the next step');
  assert.equal(seen.lost, 3, 'a lost answer: the next step asks again');
  assert.ok(seen.lost_behind <= 360, `a lost answer costs one step (${seen.lost_behind} s)`);
  assert.equal(seen.lost_caught, 1, 'and the tile has the newest again');
  assert.equal(seen.never, 288, 'without any answer never more than one request per step');
  assert.equal(seen.popup_day, 12, 'twelve day requests an hour');
  assert.equal(seen.popup_open, 12, 'an open popup asks along with each');
  assert.equal(seen.no_tile, 0, 'no tile, no section: no request');
  assert.equal(seen.section, 288, 'an energy section alone keeps its refresh');
  assert.equal(seen.offline, 0, 'offline: no request');
  assert.equal(seen.queued, 1, 'a full queue does not lose the step');
  assert.equal(seen.no_clock, 288, 'without a clock the same rate');
  assert.equal(seen.before_clock, 2, 'every five minutes without a clock');
  assert.equal(seen.at_clock, 3, 'the clock arriving asks at once');
  assert.equal(seen.after_clock, 3, 'then waits for the next step');
}

// ---- What an answer does -----------------------------------------------------------
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')]
  .filter(Boolean).find(folder => fs.existsSync(path.join(folder, 'ArduinoJson.h')));
if (!jsonInclude) {
  console.log('SKIP: the Energy answers need ArduinoJson');
} else {
  const parseHarness = String.raw`
// The firmware keeps ArduinoJson's compatibility class for its documents.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
static uint32_t millis() { return 1000; }
${header.match(/static constexpr uint8_t ENERGY_VALUES_MAX = \d+;/)[0]}
${header.match(/struct EnergyEntryData \{[\s\S]*?\n\};/)[0]}
struct Bridge {
  String findSensorName(const String&) const { return String(); }
  String findSensorUnit(const String&) const { return String(); }
} haBridgeConfig;
struct EnergyRequestState { uint32_t last_attempt_ms = 0; };
static EnergyRequestState g_day_request, g_week_request, g_month_request, g_year_request;
static std::vector<EnergyEntryData> g_day_entries, g_week_entries, g_month_entries, g_year_entries;
static int g_tile_values = 0, g_tile_bars = 0, g_last_bar_hours = -1, g_popup_refreshes = 0;
static void queue_energy_tile_update_for_entry(const EnergyEntryData&) { ++g_tile_values; }
static void energy_tile_bars_update(const EnergyEntryData& entry) {
  ++g_tile_bars;
  g_last_bar_hours = entry.value_count;
}
static String energy_source_icon(const String&) { return String("icon"); }
static void tiles_request_icon_refresh() {}
static void queue_energy_popup_refresh(const char*) { ++g_popup_refreshes; }
${fn('normalize_period')}
${fn('request_state_for_period')}
${fn('cache_for_period')}
${fn('cached_unit_for_entry')}
${fn('apply_energy_sign')}
${fn('answer_newest_hour')}
${fn('parse_energy_response')}

static void show(const char* name, const char* payload) {
  g_tile_values = g_tile_bars = g_popup_refreshes = 0;
  parse_energy_response(payload);
  const float total = g_day_entries.empty() ? -1.0f : g_day_entries[0].total;
  std::printf("%s_values=%d %s_bars=%d %s_total=%d %s_day=%d %s_popup=%d\n", name, g_tile_values, name, g_tile_bars,
              name, static_cast<int>(total * 10.0f), name, static_cast<int>(g_day_entries.size()), name, g_popup_refreshes);
}

int main() {
  show("good", R"({"period":"day","entries":[
    {"id":"sensor.pv","values":[0,0,1.5],"total":1.5},{"id":"sensor.grid","values":[1,1,1],"total":3}]})");
  std::printf("good_hours=%d\n", g_last_bar_hours);

  // Nothing here may touch the tiles, the stored answer or the open popup.
  show("no_hours", R"({"period":"day","entries":[
    {"id":"sensor.pv","values":[],"total":0},{"id":"sensor.grid","values":[],"total":0}]})");
  show("null_hours", R"({"period":"day","entries":[{"id":"sensor.pv","values":[null,null],"total":0}]})");
  show("no_values", R"({"period":"day","entries":[{"id":"sensor.pv","total":0}]})");
  show("broken", R"({"period":"day","entries":[{"id":"sensor.pv","val)");

  // An open popup's Week reaches the popup, not the tiles or the stored day.
  show("week", R"({"period":"week","entries":[{"id":"sensor.pv","values":[9],"total":9}]})");

  // One source with hours is a real answer, also for the source without.
  show("mixed", R"({"period":"day","entries":[
    {"id":"sensor.pv","values":[2],"total":2},{"id":"sensor.new","values":[],"total":0}]})");

  // An Energy dashboard without sources is an answer too.
  show("no_sources", R"({"period":"day","entries":[]})");
  return 0;
}
`;
  const parseOutput = compileAndRun({label: 'Energy answers', harness: parseHarness, includes: [jsonInclude]});
  if (parseOutput !== null) {
    const seen = values(parseOutput);
    assert.equal(seen.good_values, 2, 'an answer with hours reaches the totals');
    assert.equal(seen.good_bars, 2, 'and the bars');
    assert.equal(seen.good_popup, 1, 'and an open popup');
    assert.equal(seen.good_hours, 3);
    assert.equal(seen.good_total, 15);
    for (const name of ['no_hours', 'null_hours', 'no_values', 'broken']) {
      assert.equal(seen[`${name}_values`], 0, `${name}: the totals stay`);
      assert.equal(seen[`${name}_bars`], 0, `${name}: the bars stay`);
      assert.equal(seen[`${name}_total`], 15, `${name}: the stored day stays`);
      assert.equal(seen[`${name}_day`], 2, `${name}: the stored day stays`);
      assert.equal(seen[`${name}_popup`], 0, `${name}: the popup stays`);
    }
    assert.equal(seen.week_values, 0, 'a week answer leaves the tiles alone');
    assert.equal(seen.week_bars, 0);
    assert.equal(seen.week_total, 15, 'and the stored day');
    assert.equal(seen.week_popup, 1, 'and reaches the open popup');
    assert.equal(seen.mixed_bars, 2, 'one source with hours is a real answer');
    assert.equal(seen.mixed_day, 2);
    assert.equal(seen.no_sources_day, 0, 'no sources: nothing left to show');
  }
}

console.log('Energy refresh: one request per 5-minute step a minute after the Recorder, tiles and an open popup follow without a tap, an answer without hours keeps them');
