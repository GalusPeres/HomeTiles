// The panel's own temperature history keeps one sample every five minutes.
// A request for shorter buckets (the Sensor popup's 6H asks for 360 minutes)
// took one sample per bucket, so six hours showed the last 30 hours.
// Compiles the real build_external_temp_history_payload (mqtt_handlers.cpp)
// on the host: 6H spans the last 72 samples, five buckets per sample; 24H
// and 7D keep one and twelve samples per bucket.
import assert from 'node:assert/strict';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
const fn = (name) => {
  const found = cppFunctionDefinitions(handlers).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const constants = handlers.slice(handlers.indexOf('static constexpr uint16_t kExternalTempHistoryHoursMax'),
  handlers.indexOf('static uint8_t* g_external_history_storage'));
assert.ok(constants.includes('kExternalTempHistorySampleMinutes = 5;'), 'five-minute samples');

const harness = String.raw`
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// The few Arduino String members the builder uses.
struct String {
  std::string text;
  String() = default;
  String(const char* value) : text(value ? value : "") {}
  explicit String(unsigned value) : text(std::to_string(value)) {}
  String& operator=(const char* value) { text = value ? value : ""; return *this; }
  String& operator+=(const char* value) { text += value ? value : ""; return *this; }
  String& operator+=(const String& value) { text += value.text; return *this; }
  void reserve(size_t size) { text.reserve(size); }
  const char* c_str() const { return text.c_str(); }
};
char* dtostrf(double value, signed char, unsigned char precision, char* out) {
  std::snprintf(out, 24, "%.*f", precision, value);
  return out;
}
static const char* kEntityExternalTemperature = "sensor.panel_temperature";
static float g_external_temp_c = 20.0f;
static bool g_external_temp_valid = true;
${constants}
static float g_external_history_values[kExternalTempHistoryPoints];
static bool g_external_history_valid[kExternalTempHistoryPoints];
static uint16_t g_external_history_head = 0;
static uint16_t g_external_history_count = 0;

${fn('build_external_temp_history_payload')}

int main() {
  (void)kExternalTempHistorySampleMs;  // the sampler's interval, not built here
  // A full week of samples, oldest first: sample i holds the value i.
  for (uint16_t i = 0; i < kExternalTempHistoryPoints; ++i) {
    g_external_history_values[i] = static_cast<float>(i);
    g_external_history_valid[i] = true;
  }
  g_external_history_count = kExternalTempHistoryPoints;
  g_external_history_head = 0;
  std::printf("%s\n", build_external_temp_history_payload("sensor.panel_temperature", 6, 1, 360).c_str());
  std::printf("%s\n", build_external_temp_history_payload("sensor.panel_temperature", 24, 5, 288).c_str());
  std::printf("%s\n", build_external_temp_history_payload("sensor.panel_temperature", 168, 60, 168).c_str());
  return 0;
}
`;
const output = compileAndRun({label: 'Local temperature history buckets', harness});
if (output !== null) {
  const [six, day, week] = output.trim().split('\n').map((line) => JSON.parse(line));
  const last = 2016 - 1;
  // 6H: 360 one-minute buckets over the newest 72 samples, five per sample.
  assert.equal(six.hours, 6);
  assert.equal(six.values.length, 360);
  assert.equal(six.values[0], last - 71, '6H starts six hours back, not 30');
  assert.deepEqual(six.values.slice(0, 6), [1944, 1944, 1944, 1944, 1944, 1945], 'five buckets share a sample');
  assert.equal(six.values[359], last, '6H ends at the newest sample');
  // 24H: one sample per five-minute bucket.
  assert.equal(day.values.length, 288);
  assert.equal(day.values[0], last - 287);
  assert.equal(day.values[287], last);
  // 7D: the mean of twelve samples per hour.
  assert.equal(week.values.length, 168);
  assert.equal(week.values[0], 5.5);
  assert.equal(week.values[167], last - 5.5);
}
console.log('local temperature history: buckets follow the samples by time, 6H shows six hours');
