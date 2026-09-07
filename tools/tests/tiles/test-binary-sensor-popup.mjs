import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relativePath => fs
  .readFileSync(path.join(repoRoot, relativePath), 'utf8')
  .replace(/\r\n?/g, '\n');

const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const wrapper = read('src/ui/popups/binary_sensor/binary_sensor_popup.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const mqttHeader = read('src/network/mqtt/mqtt_handlers.h');
const i18nHeader = read('src/core/i18n/i18n.h');
const i18n = read('src/core/i18n/i18n.cpp');

assert.match(
  wrapper,
  /sensor_init\.binary_mode = true;[\s\S]*show_sensor_popup\(sensor_init\);/,
  'Binary Sensor must reuse the established Sensor popup surface and lifecycle',
);
assert.match(
  popup,
  /hide_light_popup\(\);[\s\S]*hide_weather_popup\(\);[\s\S]*hide_media_popup\(\);/,
  'Opening the shared popup must keep the existing mutual-exclusion path',
);

for (const marker of [
  'kHistoryHours24h = 24',
  'kHistoryHours7d = 168',
  'kBinaryMaxSegments = 96',
  'kBinaryMaxTimelineBins = 768',
  'kBinaryMaxActivityEntries = 96',
  'i18n::binary_sensor_label(',
  'binary_history_title',
  'binary_timeline',
  'binary_activity_title',
  'binary_activity_status',
  'binary_activity_viewport',
  'binary_activity_spacer',
  'binary_activity_lines',
  'binary_activity_rows',
  'DEVICE_LAYOUT_480X480',
  'DEVICE_LAYOUT_1024X600',
]) {
  assert.ok(popup.includes(marker), `Binary popup layout is missing: ${marker}`);
}
assert.match(
  popup,
  /constexpr int kBinaryActivityPoolRows =[\s\S]*?kBinaryActivityViewportLimit/,
  'The bounded virtual activity row pool must cover the available viewport height',
);
assert.match(
  popup,
  /lv_obj_set_scroll_dir\(viewport, LV_DIR_VER\);[\s\S]*lv_obj_set_scrollbar_mode\(viewport, LV_SCROLLBAR_MODE_AUTO\);[\s\S]*LV_EVENT_SCROLL/,
  'Activity must expose a vertically scrollable viewport with a visible scrollbar',
);
assert.match(
  popup,
  /lv_obj_get_scroll_y\(ctx->binary_activity_viewport\)[\s\S]*binary_activity_first_row[\s\S]*static_cast<int32_t>\(activity_index\) \*\s*kBinaryActivityRowHeight/,
  'Activity must virtualize all bounded entries instead of truncating to visible rows',
);
assert.match(
  popup,
  /binary_activity\.size\(\) < kBinaryMaxActivityEntries[\s\S]*lv_obj_set_height\(ctx->binary_activity_spacer, content_height\)/,
  'All bounded activity entries must contribute to the scroll extent',
);
assert.match(
  popup,
  /lv_obj_set_style_bg_opa\(dot, LV_OPA_COVER, 0\)/,
  'State dots must be opaque and visible',
);
assert.match(
  popup,
  /lv_obj_set_style_radius\(ctx->binary_timeline, 0, 0\)/,
  'The timeline must stay square before and after history loads',
);
assert.match(
  popup,
  /axis_y \+ lv_font_get_line_height\(popup_layout::font20\(\)\) \+[\s\S]*popup_layout::scale\(12\)/,
  'Activity spacing must account for the rendered time-axis line height',
);
assert.match(
  popup,
  /doc\["segments"\]\.as<JsonArrayConst>\(\)[\s\S]*doc\["activity"\]\.as<JsonArrayConst>\(\)/,
  'The real binary response branch must consume timeline and activity arrays',
);
assert.match(
  popup,
  /static_cast<int>\(activity\.size\(\)\) - 1[\s\S]*--source_index/,
  'Activity must be displayed newest-first like Home Assistant',
);
for (const timelineField of [
  'timeline_points',
  'timeline_encoding',
  'timeline_data',
  '2bit-hex',
]) {
  assert.ok(
    popup.includes(timelineField),
    `Compact full-range timeline support is missing: ${timelineField}`,
  );
}
assert.match(
  popup,
  /points > kBinaryMaxTimelineBins[\s\S]*strlen\(data\) != expected_chars[\s\S]*output\.clear\(\);\s*return false;/,
  'The compact timeline decoder must reject oversized, truncated, and invalid hex safely',
);
assert.match(
  popup,
  /strcmp\(kind, "binary"\) == 0[\s\S]*apply_binary_history_payload\(ctx, doc\)/,
  'Binary history must use its real dedicated parser branch',
);
assert.match(
  popup,
  /DynamicJsonDocument doc\(24576\)/,
  'The popup document must fit the bounded 96+96 binary response contract',
);
assert.match(
  popup,
  /kStateHistoryRefreshMinMs24h = 60000[\s\S]*kStateHistoryRefreshMinMs7d = 300000[\s\S]*state_history_refresh_pending/,
  'Live state transitions must strongly rate-limit full Recorder refreshes without losing the final refresh',
);
assert.match(
  popup,
  /prepend_state_history_activity\([\s\S]*binary_activity\.insert\(ctx->binary_activity\.begin\(\), entry\)[\s\S]*schedule_state_history_refresh/,
  'Live transitions must update Activity immediately while the Recorder refresh stays throttled',
);
assert.match(
  popup,
  /binary_state_priority\(timeline_state\) > binary_state_priority\([\s\S]*first_bin \+ 1U[\s\S]*binary_activity_status[\s\S]*LV_OBJ_FLAG_HIDDEN/,
  'Optimistic updates must preserve an active partial timeline bin and clear the empty-activity label',
);
assert.match(
  popup,
  /const bool missing = !state\.length\(\);[\s\S]*lv_label_set_text\(ctx->value_label, "--"\)/,
  'A missing HA entity must stay distinct from unknown and unavailable in the popup',
);
assert.match(
  popup,
  /doc\.containsKey\("current"\) && current_variant\.isNull\(\)[\s\S]*\? String\(\)/,
  'A null current history state must clear stale popup state',
);

assert.ok(
  mqttHeader.includes('mqttPublishBinaryHistoryRequest'),
  'The binary popup request API must be declared',
);
for (const marker of [
  'mqttPublishDiscreteHistoryRequest(entity_id, "binary", "BinaryHistory"',
  '\\"max_transitions\\":',
  'kDiscreteHistoryHaResponseTimeoutMs = 8000',
  'if (max_transitions < 2) max_transitions = 2;',
  'queue_discrete_history_unavailable(',
  'clear_pending_discrete_history_request(',
]) {
  assert.ok(mqtt.includes(marker), `Binary history MQTT path is missing: ${marker}`);
}
assert.match(
  mqtt,
  /const bool is_discrete = is_binary \|\| is_state;[\s\S]*queue_sensor_popup_history\(nullptr, large_buf, copy_len\);[\s\S]*if \(!is_discrete\) \{\s*queue_tile_graph_history/,
  'A binary response must reach the popup but never the numeric tile graph',
);

assert.ok(
  i18nHeader.includes('binary_sensor_labels[8]') &&
    i18nHeader.includes('binary_sensor_states[36]'),
  'Binary popup and state strings must live in the central locale schema',
);
for (const locale of ['kLocaleDe', 'kLocaleEn', 'kLocaleFr']) {
  assert.ok(i18n.includes(locale), `Missing central locale: ${locale}`);
}
assert.equal(
  (i18n.match(/"24H", "7D"/g) || []).length,
  3,
  'All locales must use the exact Sensor-popup range abbreviations',
);
for (const deviceClass of [
  'battery', 'battery_charging', 'carbon_monoxide', 'cold', 'connectivity',
  'door', 'garage_door', 'gas', 'heat', 'light', 'lock', 'moisture',
  'motion', 'moving', 'occupancy', 'opening', 'plug', 'power', 'presence',
  'problem', 'running', 'safety', 'smoke', 'sound', 'tamper', 'update',
  'vibration', 'window',
]) {
  assert.ok(
    i18n.includes(`device_class == "${deviceClass}"`) ||
      i18n.includes(`device_class == "${deviceClass}" ||`),
    `Localized HA state mapping is missing device class ${deviceClass}`,
  );
}

console.log('Binary Sensor popup and history contract: PASS');
