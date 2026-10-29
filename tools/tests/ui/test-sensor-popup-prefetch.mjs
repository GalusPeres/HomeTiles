// The Sensor popup's graph still came after the popup (user 2026-10-29,
// b611 S3 log): the Bridge answered 124-127 ms after the popup asked, the
// popup showed after ~110 ms. The finger rests 43-138 ms before the tap, so
// the history now goes out when it comes down on a sensor tile or a power
// flow node with a numeric graph; the popup opened by the tap replays the
// answer or waits for the request on its way instead of asking again.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = rel => readRepoFile(rel).replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

// The finger comes down: the watched press of a tile.
const ui = read('src/ui/ui_manager.cpp');
const down = ui.slice(ui.indexOf('self->touch_press_ms = lv_tick_get();'));
assert.ok(down.indexOf('sensor_tile_touch_down(pressed_object);') > 0 &&
  down.indexOf('sensor_tile_touch_down(pressed_object);') < down.indexOf('}'), 'sensor tiles hear the finger');
assert.ok(down.indexOf('power_flow_touch_down(pressed_object);') > 0 &&
  down.indexOf('power_flow_touch_down(pressed_object);') < down.indexOf('}'), 'so do the power flow nodes');

// Sensor tile: the same decisions as the tap that opens the popup.
const sensor = read('src/types/sensor/renderer.cpp');
assert.ok(sensor.includes('lv_obj_add_event_cb(card, sensor_popup_click_cb, popup_event, data);'));
const click = fn(sensor, 'sensor_popup_click_cb');
assert.ok(click.includes('init.unit = popup_unit(*data);') &&
  click.includes('init.state_history_mode = popup_state_history(data->entity_id, init.value, init.unit);'));
const touch = fn(sensor, 'sensor_tile_touch_down');
assert.ok(touch.includes('lv_event_dsc_get_cb(dsc) != sensor_popup_click_cb'), 'the tile is the one the tap opens');
assert.ok(touch.includes('data->editable ||') && touch.includes('popup_state_history(data->entity_id, haBridgeConfig.findSensorInitialValue(data->entity_id),') &&
  touch.includes('popup_unit(*data)'), 'only a numeric graph: editable values and state histories ask on opening');
assert.ok(touch.includes('sensor_popup_prefetch_history(data->entity_id);'));

// Power flow node: the node popup's own decision.
const flow = read('src/types/power_flow/renderer.cpp');
assert.ok(fn(flow, 'open_node_popup').includes('init.state_history_mode = node_state_history(entity);'));
const flowTouch = fn(flow, 'power_flow_touch_down');
assert.ok(flowTouch.includes('lv_event_dsc_get_cb(dsc) != node_click_cb') &&
  flowTouch.includes('node_state_history(st->entities[role])') &&
  flowTouch.includes('sensor_popup_prefetch_history(st->entities[role]);'));

// The popup's range: what request_history_early asks.
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const prefetch = fn(popup, 'sensor_popup_prefetch_history');
assert.ok(prefetch.includes('if (!should_request_history(entity_id)) return;') &&
  prefetch.includes('get_history_range_config(SensorHistoryRange::Day24)') &&
  prefetch.includes('mqttPrefetchHistoryRequest(entity_id.c_str(), cfg.hours, cfg.period_minutes, cfg.points);'));
assert.ok(fn(popup, 'request_history_early').includes('get_history_range_config(SensorHistoryRange::Day24)'));

// The request: nothing when a fresh answer is stored or the same one is on
// its way, and never a replay (nothing reaches a popup or a tile graph).
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const early = fn(mqtt, 'mqttPrefetchHistoryRequest');
assert.ok(early.includes('if (fresh_history_replay(entity_id, hours, period_minutes, points) ||') &&
  early.includes('history_request_on_its_way(entity_id, hours, period_minutes, points)'));
assert.ok(!early.includes('queue_sensor_popup_history') && !early.includes('replay_history_response'));
assert.ok(early.includes('send_history_request(history_topic, entity_id, hours, period_minutes, points);'));
// The popup's own request replays or waits for it.
const ask = fn(mqtt, 'mqttPublishHistoryRequest');
const replayAt = ask.indexOf('replay_history_response(entity_id, hours, period_minutes, points)');
const waitAt = ask.indexOf('history_request_on_its_way(entity_id, hours, period_minutes, points)');
const clearAt = ask.indexOf('// Discard previous pending entries for the same entity.');
assert.ok(replayAt >= 0 && replayAt < waitAt && waitAt < clearAt, 'a stored answer first, then the one on its way');
const onWay = fn(mqtt, 'history_request_on_its_way');
assert.ok(onWay.includes('request->hours == hours && request->period_minutes == period_minutes &&') &&
  onWay.includes('request->points == points'), 'only the same range');

console.log('Sensor graphs ask for their history when the finger comes down');
