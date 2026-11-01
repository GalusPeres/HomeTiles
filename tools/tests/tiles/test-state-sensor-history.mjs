import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relativePath => fs
  .readFileSync(path.join(repoRoot, relativePath), 'utf8')
  .replace(/\r\n?/g, '\n');

const popupHeader = read('src/ui/popups/sensor/sensor_popup.h');
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const renderer = read('src/types/sensor/renderer.cpp');
const bridgeHeader = read('src/network/bridge/ha_bridge_config.h');
const bridgeConfig = read('src/network/bridge/ha_bridge_config.cpp');
const mqttHeader = read('src/network/mqtt/mqtt_handlers.h');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');

assert.ok(
  popupHeader.includes('bool state_history_mode = false;') &&
    popupHeader.includes('sensor_popup_should_use_state_history'),
  'The Sensor popup contract must distinguish numeric and textual HA sensors',
);
assert.match(
  renderer,
  /findSensorStateKind\((?:data->)?entity_id\)[\s\S]*state_kind == "state"[\s\S]*state_kind == "number"[\s\S]*sensor_popup_should_use_state_history/,
  'Bridge metadata must be authoritative with a backward-compatible strict-value fallback',
);
for (const marker of [
  'sensor_state_kinds_map',
  'state_kinds_index_',
  'findSensorStateKind',
  'extractStringField(object, "state_kind", state_kind)',
]) {
  assert.ok(
    bridgeHeader.includes(marker) || bridgeConfig.includes(marker),
    `Sensor state-kind metadata path is missing: ${marker}`,
  );
}

assert.ok(
  mqttHeader.includes('mqttPublishStateHistoryRequest'),
  'The textual-state history request API must be declared',
);
assert.match(
  mqtt,
  /payload \+= kind;[\s\S]*mqttPublishDiscreteHistoryRequest\(entity_id, "state", "StateHistory"/,
  'Textual sensors must use the bounded versioned state-history request path',
);
assert.match(
  mqtt,
  /const bool is_state = response_kind\.equalsIgnoreCase\("state"\);[\s\S]*const bool is_discrete = is_binary \|\| is_state;[\s\S]*if \(!is_discrete\)/,
  'State-history responses must reach only the popup and never the numeric tile graph',
);

for (const marker of [
  'strcmp(kind, "state") == 0',
  'apply_state_history_payload(ctx, doc)',
  'palette4-hex',
  'doc["palette"]',
  'decode_state_timeline',
  'kStateHistoryMaxPaletteEntries = 16',
  'format_state_history_label',
  'state_history_color',
  'mbedtls_sha256',
  'kMaxHistoryStateBytes = 32',
  'kDigestHexChars = 8',
  'doc["palette_complete"] | true',
  'overflow is shown as unknown',
]) {
  assert.ok(popup.includes(marker), `Textual state popup path is missing: ${marker}`);
}
assert.match(
  popup,
  /expected_chars =[\s\S]*points[\s\S]*binary_timeline_hex_nibble\(data\[index\]\)[\s\S]*code\) >= palette\.size\(\)/,
  'The palette timeline decoder must reject malformed or out-of-range data',
);
assert.match(
  popup,
  /if \(ctx->state_history_mode\)[\s\S]*mqttPublishBinaryHistoryRequest[\s\S]*mqttPublishStateHistoryRequest[\s\S]*mqttPublishHistoryRequest/,
  'Binary, textual-state, and numeric history requests must remain separate',
);
assert.match(
  popup,
  /if \(g_sensor_popup_ctx->state_history_mode\)[\s\S]*prepend_state_history_activity[\s\S]*schedule_state_history_refresh/,
  'Live textual state changes must update Activity immediately and refresh Recorder data later',
);
assert.match(
  popup,
  /const String normalized = ctx->binary_mode[\s\S]*normalize_state_history_value\(value\);/,
  'Activity entries must use the same bounded canonical state as the Bridge palette',
);
assert.match(
  popup,
  /const String next_value =[\s\S]*normalize_state_live_value\(g_pending_value\.value\);[\s\S]*if \(next_value != previous_value\)/,
  'Live comparison must preserve the display state while Activity uses the bounded Bridge canonical state',
);
assert.match(
  popup,
  /const bool categorical_state = ctx->state_history_mode && !ctx->binary_mode;[\s\S]*if \(!categorical_state &&[\s\S]*apply_decimals/,
  'Textual states must never be rewritten by numeric decimal localization',
);
assert.match(
  popup,
  /binary_activity_date[\s\S]*format_state_history_date[\s\S]*local_date_key/,
  'Activity must show a localized date header while scrolling',
);
assert.match(
  popup,
  /lv_obj_set_style_radius\(ctx->binary_timeline, 0, 0\);[\s\S]*lv_obj_set_style_clip_corner\(ctx->binary_timeline, false, 0\)/,
  'The discrete timeline must remain square in loading and loaded states',
);
assert.match(
  popup,
  /binary_time_labels\[index\][\s\S]*lv_obj_set_style_text_opa\(label, LV_OPA_COVER, 0\)/,
  'Timeline labels must remain fully opaque',
);
assert.match(
  popup,
  /JsonArray values = doc\["values"\]\.as<JsonArray>\(\)[\s\S]*lv_chart_set_point_count/,
  'Numeric Sensor entities must retain the established graph path',
);

console.log('Textual Sensor state-history contract: PASS');
