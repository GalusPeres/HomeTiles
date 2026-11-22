// While the Sensor popup is open its numeric graph follows the time (user
// 2026-10-31): it asks for its range again once per step of that range, at
// least every minute (1H and 6H every minute, 24H every five minutes, 7D
// hourly), also when the value stays the same and no new state comes. Never
// while a finger reads the graph, and a kept cursor stays where it is.
// Compiles the popup's real refresh rule (sensor_popup.cpp) on the host.
import assert from 'node:assert/strict';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const popup = readRepoFile('src/ui/popups/sensor/sensor_popup.cpp').replace(/\r\n/g, '\n');
const fn = (name) => {
  const found = cppFunctionDefinitions(popup).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const constant = (name) => {
  const match = popup.match(new RegExp(`constexpr uint\\d+_t ${name} = [^;]+;`));
  assert.ok(match, name);
  return match[0];
};

// ---- Wiring -------------------------------------------------------------------
const queue = fn('process_sensor_popup_queue');
assert.ok(queue.includes('numeric_history_refresh_due(g_sensor_popup_ctx, millis())) {\n    request_history_for_context(g_sensor_popup_ctx);'),
  'the open popup asks again when a step is due');
assert.ok(queue.indexOf('!g_pending_history.valid && !g_sensor_first_frame.pending()') > queue.indexOf('apply_history_payload('),
  'not while an answer waits or the first frame draws');
assert.ok(fn('request_history_for_context').includes('ctx->numeric_history_asked_ms = millis();\n  mqttPublishHistoryRequest('),
  'every numeric request (opening, range switch, refresh) starts the next step');
assert.ok(fn('finish_sensor_popup_open').includes('g_sensor_popup_ctx->numeric_history_asked_ms = millis();'),
  'the opening counts as the first request');
const apply = fn('apply_history_payload');
assert.ok(apply.includes('lv_chart_refresh(ctx->chart);\n  refresh_sensor_readout(ctx);'),
  'a new answer shows the kept cursor again where it was');

// ---- The rule -----------------------------------------------------------------
const harness = String.raw`
#include <algorithm>
#include <cstdint>
#include <cstdio>
${['kHistoryHours6h', 'kHistoryPeriodMinutes6h', 'kHistoryPoints6h', 'kHistoryHours24h', 'kHistoryPeriodMinutes24h',
  'kHistoryPoints24h', 'kHistoryHours7d', 'kHistoryPeriodMinutes7d', 'kHistoryPoints7d', 'kHistoryHours1h',
  'kHistoryPeriodMinutes1h', 'kHistoryPoints1h', 'kHistoryHours30d', 'kHistoryPeriodMinutes30d',
  'kHistoryPoints30d'].map(constant).join('\n')}
enum class SensorHistoryRange { Hour1, Hours6, Day24, Day7, Day30 };
struct HistoryRangeConfig { uint16_t hours; uint16_t period_minutes; uint16_t points; };
${fn('get_history_range_config')}
struct EditableControl { bool interacting = false; };
bool editable_control_is_interacting(const EditableControl* c) { return c && c->interacting; }
struct Readout { bool touching = false; bool active() const { return touching; } };
struct SensorPopupContext {
  bool editable = false, binary_mode = false, state_history_mode = false;
  SensorHistoryRange history_range = SensorHistoryRange::Day24;
  Readout readout;
  EditableControl* control = nullptr;
  uint32_t numeric_history_asked_ms = 0;
};
${constant('kNumericHistoryRefreshSlackMs')}
${fn('numeric_history_refresh_due')}
int main() {
  SensorPopupContext ctx;
  const uint32_t asked = 1000;
  ctx.numeric_history_asked_ms = asked;
  auto due = [&](SensorHistoryRange range, uint32_t after_s) {
    ctx.history_range = range;
    return numeric_history_refresh_due(&ctx, asked + after_s * 1000) ? 1 : 0;
  };
  std::printf("hour1 %d %d\n", due(SensorHistoryRange::Hour1, 64), due(SensorHistoryRange::Hour1, 65));
  std::printf("hours6 %d %d\n", due(SensorHistoryRange::Hours6, 64), due(SensorHistoryRange::Hours6, 65));
  std::printf("day24 %d %d\n", due(SensorHistoryRange::Day24, 304), due(SensorHistoryRange::Day24, 305));
  std::printf("day7 %d %d\n", due(SensorHistoryRange::Day7, 3604), due(SensorHistoryRange::Day7, 3605));
  ctx.readout.touching = true;
  std::printf("finger %d\n", due(SensorHistoryRange::Hour1, 600));
  ctx.readout.touching = false;
  ctx.state_history_mode = true;
  const int state = due(SensorHistoryRange::Hour1, 600);
  ctx.state_history_mode = false;
  ctx.binary_mode = true;
  const int binary = due(SensorHistoryRange::Hour1, 600);
  ctx.binary_mode = false;
  ctx.editable = true;
  std::printf("others %d %d %d\n", state, binary, due(SensorHistoryRange::Hour1, 600));
  return 0;
}
`;
const output = compileAndRun({label: 'Sensor popup graph refresh', harness});
if (output !== null) {
  const rows = Object.fromEntries(output.trim().split('\n').map((line) => {
    const [key, ...rest] = line.trim().split(' ');
    return [key, rest];
  }));
  assert.deepEqual(rows.hour1, ['0', '1'], '1H: every minute (the replayed answer runs out first)');
  assert.deepEqual(rows.hours6, ['0', '1'], '6H: every minute');
  assert.deepEqual(rows.day24, ['0', '1'], '24H: every five minutes');
  assert.deepEqual(rows.day7, ['0', '1'], '7D: hourly');
  assert.deepEqual(rows.finger, ['0'], 'never while a finger reads the graph');
  assert.deepEqual(rows.others, ['0', '0', '0'], 'state, binary and editable histories keep their own refresh');
}
console.log('Sensor popup graph: follows the time once per step of its range, never under a finger, cursor kept');
