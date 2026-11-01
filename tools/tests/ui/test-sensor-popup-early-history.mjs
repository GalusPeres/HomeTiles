// The coloured Sensor graphs came later than the old ones (user 2026-10-28,
// clearly on the S3): the popup asked for the history only after its first
// frame (finish_sensor_popup_open), so the graph waited a whole frame (~80 ms
// on the S3) plus the Bridge's answer. A numeric graph now asks when the
// popup opens; its answer (also a stored one that comes back inside the
// request) waits in g_pending_history until the body is bound, and the first
// frame's pass does not ask again.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const source = readRepoFile('src/ui/popups/sensor/sensor_popup.cpp').replace(/\r\n?/g, '\n');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

const early = fn('request_history_early');
assert.ok(early.includes('if (init.editable || init.binary_mode || init.state_history_mode || !should_request_history(init.entity_id)) {'),
  'numeric graphs only; editable values and state histories keep their request');
assert.ok(early.includes('get_history_range_config(SensorHistoryRange::Day24)'), 'the range the popup opens with');
assert.ok(early.indexOf('g_sensor_early_history = true;') < early.indexOf('mqttPublishHistoryRequest('),
  'the flag is set before a stored answer can come back inside the request');

const show = fn('show_sensor_popup');
assert.ok(show.indexOf('set_sensor_popup_visible(ctx, true);') < show.indexOf('request_history_early(init);') &&
  show.indexOf('g_pending_history.valid = false;') < show.indexOf('request_history_early(init);'),
  'asked once the popup is visible and its old pending history is dropped');

const finish = fn('finish_sensor_popup_open');
assert.ok(finish.includes('if (!fresh_history && !g_sensor_early_history) request_history_for_context(g_sensor_popup_ctx);') &&
  finish.includes('g_sensor_early_history = false;'), 'the first frame pass does not ask again');
// b610: an answer that is already here is applied with the controls, so the
// graph is in their frame instead of the next one (user: still later than
// the popup); the popup's own first frame stays as it is.
const applyAt = finish.indexOf('apply_history_payload(g_sensor_popup_ctx, g_pending_history.payload.c_str());');
assert.ok(applyAt >= 0 && applyAt < finish.indexOf('g_sensor_first_frame.begin();') &&
  finish.slice(0, applyAt).includes('if (g_sensor_early_history && g_pending_history.valid &&'),
  'the early answer goes into the controls\' frame');

const queue = fn('queue_sensor_popup_history');
assert.ok(queue.includes('if (g_sensor_open_pending && !g_sensor_early_history) return;'),
  'while opening only the early answer is kept');
assert.ok(queue.includes('const String& shown_entity = g_sensor_open_pending ? g_pending_sensor_init.entity_id : g_sensor_popup_ctx->entity_id;'),
  'for the entity the popup opens with');

for (const name of ['hide_sensor_popup', 'set_sensor_popup_visible']) {
  assert.ok(fn(name).includes('g_sensor_early_history = false;'), `${name} drops the flag`);
}
console.log('Sensor popup asks for its graph history when it opens');
