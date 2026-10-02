// The Web Admin preview polls /api/sensor_values, which now carries the
// cached weather and media payloads (user 2026-10-02: the preview showed no
// weather and no media state). The hourly forecast and embedded artwork only
// the device needs are cut out first; what remains must still be valid JSON
// with every other member intact.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const cases = [
  // Bridge order: daily forecast before the hourly one.
  {state: 'cloudy', temperature: 18.5, forecast: [{condition: 'rainy', temperature: 20}],
   forecast_hourly: [{condition: 'sunny', datetime: '2026-10-02T12:00:00', note: 'a ] and } inside'}], sun: []},
  // The hourly forecast first and last.
  {forecast_hourly: [{temperature: 1}], state: 'sunny'},
  {state: 'fog', forecast_hourly: []},
  // Embedded artwork with escaped quotes and a URL beside it.
  {state: 'playing', media_title: 'Song "Live"', entity_picture_data: 'abc\\"def"=', entity_picture: 'http://ha:8123/x?token=1',
   source: 'TV'},
  // Neither member: unchanged.
  {state: 'paused', media_title: 'Quiet'},
];
const inputs = cases.map(item => JSON.stringify(item));
// Also the bridge's json.dumps separators with spaces.
inputs.push(JSON.stringify(cases[0], null, 1).replace(/\n\s*/g, ' '));

const harness = `
#include <cstdio>
#include <iostream>
#include <string>
#include "src/web/server/handlers/preview_payload.h"
int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    for (const char* key : {"forecast_hourly", "entity_picture_data"}) {
      int from = 0, to = 0;
      if (preview_payload::member_span(line.c_str(), static_cast<int>(line.size()), key, &from, &to)) {
        line.erase(from, to - from);
      }
    }
    std::cout << line << "\\n";
  }
  return 0;
}
`;
const output = compileAndRun({label: 'Preview payload trim', harness, input: inputs.join('\n') + '\n'});
if (output !== null) {
  const lines = output.trim().split('\n');
  assert.equal(lines.length, inputs.length);
  lines.forEach((line, index) => {
    const parsed = JSON.parse(line);
    const expected = JSON.parse(inputs[index]);
    delete expected.forecast_hourly;
    delete expected.entity_picture_data;
    assert.deepEqual(parsed, expected, `case ${index}: ${line}`);
  });
}

// The handler cuts both members from the weather and media payloads it adds.
const handler = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp').replace(/\r\n/g, '\n');
assert.match(handler, /for \(const char\* heavy : \{"forecast_hourly", "entity_picture_data"\}\)/);
assert.match(handler, /append_preview_payloads\("weather_values", ha\.weathers_text\);/);
assert.match(handler, /append_preview_payloads\("media_values", ha\.media_players_text\);/);
console.log('Preview payloads: hourly forecast and artwork data cut out, the rest valid and unchanged');
