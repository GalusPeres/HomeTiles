// PR #120 (issue #41): on the Waveshare 7B a Wi-Fi join takes 6-7 s, and a new
// WiFi.begin() every 5 s aborted each attempt (ASSOC_LEAVE), so the panel
// never got an IP. Each join now gets 30 s; a join the station gives up on
// is retried 5 s after the failure, so a router restart stays quick. The
// station event log names the disconnect reason, and each kind of line prints
// at most once a minute: with the router off the events repeat for as long
// as it is off.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/network/network_manager.cpp'), 'utf8').replace(/\r\n/g, '\n');
const functions = cppFunctionDefinitions(source);
const body = name => functions.find(f => f.name === name)?.source ?? assert.fail(`${name} not found`);

// The join window and the retry after a failed join.
assert.match(source, /static constexpr uint32_t kWifiJoinWindowMs = 30000;/);
assert.match(source, /static constexpr uint32_t kWifiRetryAfterFailureMs = 5000;/);
const connect = body('HomeTilesNetworkManager::connectWifi');
assert.match(connect, /^\s*wifi_retry_at = millis\(\) \+ 5000UL;.*\n\s*wifi_join_window_open = false;/m,
  'an attempt that returns early leaves no join window open');
assert.match(connect, /wifi_join_window_open = true;\n\s*WiFi\.setScanMethod\(WIFI_ALL_CHANNEL_SCAN\);\n\s*WiFi\.setSortMethod\(WIFI_CONNECT_AP_BY_SIGNAL\);\n\s*WiFi\.begin\(cfg\.wifi_ssid, cfg\.wifi_pass\);\n\s*wifi_retry_at = millis\(\) \+ kWifiJoinWindowMs;/,
  'each WiFi.begin() gets the whole window');
assert.match(connect, /wifi_retry_at = millis\(\) \+ \(5000UL << shift\);\n\s*wifi_join_window_open = false;  \/\/ Keep the backoff\./,
  'a slow Hosted RPC keeps its backoff');
const update = body('HomeTilesNetworkManager::update');
assert.match(update, /const uint32_t join_failed_ms = wifi_join_failed_ms\.exchange\(0\);\n\s*if \(join_failed_ms != 0 && wifi_join_window_open\.load\(\)\) \{\n\s*const uint32_t early = join_failed_ms \+ kWifiRetryAfterFailureMs;\n\s*if \(\(int32_t\)\(wifi_retry_at - early\) > 0\) wifi_retry_at = early;/,
  'a failed join is retried 5 s after the failure, never later than the window');
assert.match(update, /wifi_start_failures = 0;\n\s*wifi_join_window_open = false;/, 'a later drop of a live connection is no failed join');

// The event log: the failure is recorded before the rate limit, and every
// line goes through it.
const log = body('logWifiEvent');
const allow = log.indexOf('wifi_event_log_limiter.allow(');
assert.ok(allow > 0, 'the event log uses the rate limit');
assert.ok(log.indexOf('wifi_join_failed_ms = millis() | 1u;') < allow, 'a failed join counts even when its line is left out');
assert.match(log, /reason != WIFI_REASON_ASSOC_LEAVE && wifi_join_window_open\.load\(\)/, 'ASSOC_LEAVE is no failed join');
assert.doesNotMatch(log.slice(0, allow), /Serial\./, 'nothing prints before the rate limit');
assert.equal((log.slice(allow).match(/Serial\.printf\(/g) || []).length, 2, 'one line, with or without the left-out count');
assert.match(log, /wifi_event_log::key\(static_cast<uint32_t>\(event\), reason\)/, 'each disconnect reason is its own kind');
assert.match(body('HomeTilesNetworkManager::init'), /if \(!wifi_event_log_registered\) \{\n\s*WiFi\.onEvent\(logWifiEvent\);/, 'registered once');

// The rate limit itself, compiled on the host.
const output = compileAndRun({
  label: 'Wi-Fi event log rate limit',
  harness: String.raw`
#include "src/network/wifi_event_log_limit.h"
#include <cstdio>
#include <initializer_list>

using wifi_event_log::Limiter;
using wifi_event_log::key;

static int failures = 0;
static void expect(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

int main() {
  const uint32_t kStarted = key(2, 0), kAssociated = key(4, 0), kGotIp = key(7, 0);
  const uint32_t kNoAp = key(5, 201), kHandshake = key(5, 15);
  uint32_t skipped = 99;

  // A first join logs every step.
  Limiter first;
  expect(first.allow(kStarted, 1000, &skipped) && skipped == 0, "station started");
  expect(first.allow(kAssociated, 5000, &skipped) && skipped == 0, "associated");
  expect(first.allow(kGotIp, 8000, &skipped) && skipped == 0, "got IP");

  // Router off for an hour: "access point not found" twice per attempt, an
  // attempt every 7.5 s. One line a minute, each with the left-out count.
  Limiter router_off;
  int printed = 0;
  uint32_t total_skipped = 0, attempts = 0;
  for (uint32_t t = 2500; t <= 3600000; t += 7500, ++attempts) {
    for (uint32_t again : {0u, 2400u}) {
      if (router_off.allow(kNoAp, t + again, &skipped)) {
        ++printed;
        total_skipped += skipped;
      }
    }
  }
  expect(printed >= 55 && printed <= 61, "about one line a minute");
  expect(total_skipped + printed <= attempts * 2 && total_skipped + printed >= attempts * 2 - 20,
         "every left-out line is counted");
  // The router is back: association and IP print at once, the same reason
  // a minute later says how many were left out.
  expect(router_off.allow(kAssociated, 3610000, &skipped) && skipped == 0, "back: associated");
  expect(router_off.allow(kGotIp, 3613000, &skipped) && skipped == 0, "back: got IP");

  // Wrong password: associated, handshake timeout, alternating. Both kinds
  // are limited on their own.
  Limiter wrong_password;
  int associated = 0, handshake = 0;
  for (uint32_t t = 0; t < 600000; t += 5000) {
    associated += wrong_password.allow(kAssociated, t + 3000, &skipped);
    handshake += wrong_password.allow(kHandshake, t + 4500, &skipped);
  }
  expect(associated == 10 && handshake == 10, "two kinds, ten minutes, ten lines each");
  expect(wrong_password.allow(kHandshake, 604500 + 60000, &skipped) && skipped == 11, "left-out count of the last minute");

  // More kinds than slots: the one printed longest ago gives way, and a kind
  // seen again after its window prints with a fresh count.
  Limiter many;
  for (uint32_t reason = 1; reason <= 12; ++reason) {
    expect(many.allow(key(5, reason), reason * 10, &skipped) && skipped == 0, "new kinds always print");
  }
  expect(many.allow(key(5, 12), 200, &skipped) == false, "the newest kind stays limited");
  expect(many.allow(key(5, 1), 300, &skipped) && skipped == 0, "an evicted kind prints again");

  // millis() wraps after 49.7 days.
  Limiter wrap;
  expect(wrap.allow(kNoAp, 0xFFFFF000u, &skipped), "before the wrap");
  expect(!wrap.allow(kNoAp, 0x00001000u, &skipped), "8 s later, across the wrap");
  expect(wrap.allow(kNoAp, 0x0000F000u, &skipped) && skipped == 1, "a minute later, across the wrap");

  std::printf("%s printed=%d skipped=%u\n", failures ? "FAILED" : "OK", printed, total_skipped);
  return failures ? 1 : 0;
}
`,
});

if (output !== null) {
  assert.match(output, /^OK printed=\d+ skipped=\d+/m, output);
  console.log(`Wi-Fi join window and event log rate limit (${output.trim()})`);
} else {
  console.log('Wi-Fi join window wiring checked; rate limit harness skipped');
}
