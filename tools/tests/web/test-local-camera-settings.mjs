// Web Admin opt-in for the built-in camera: rendered only on the exact camera
// profile, every visible text comes from the central translations, the toggle
// and the live-stream mode select save through their own endpoint (never
// through the /mqtt settings form) and the delivered browser code maps the
// status JSON to the server-provided texts.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {readAdminDeliverySource, readRepoFile} from '../../lib/admin-source.mjs';

const html = readRepoFile('src/web/server/render/web_admin_html.cpp').replaceAll('\r\n', '\n');
const header = readRepoFile('src/core/i18n/i18n.h').replaceAll('\r\n', '\n');
const i18n = readRepoFile('src/core/i18n/i18n.cpp').replaceAll('\r\n', '\n');
const routes = readRepoFile('src/web/server/web_admin.cpp');
const handler = readRepoFile('src/web/server/handlers/web_admin_local_camera.cpp');
const bundle = JSON.parse(readRepoFile('src/web/admin/bundle.json'));

// --- Central translations for every supported language -----------------------
const keys = ['local_camera_section', 'local_camera_enable', 'local_camera_note',
  'local_camera_status_label', 'local_camera_status_disabled',
  'local_camera_status_probing', 'local_camera_status_ready',
  'local_camera_status_not_found', 'local_camera_status_error',
  'local_camera_stream_mode', 'local_camera_stream_mode_auto', 'local_camera_mirror',
  'local_camera_image_section', 'local_camera_brightness', 'local_camera_contrast',
  'local_camera_saturation', 'local_camera_red', 'local_camera_blue',
  'local_camera_image_reset', 'local_camera_indicator_active', 'local_camera_indicator_end',
  'local_camera_indicator_line', 'local_camera_indicator_pill', 'local_camera_indicator_note',
  'local_camera_stream_mode_custom', 'local_camera_custom_fps', 'local_camera_custom_quality',
  'local_camera_indicator_section', 'local_camera_stream_section', 'local_camera_gain'];
const members = [...header.matchAll(/^\s*const char\* (\w+);/gm)].map(match => match[1]);
const stringsMembers = members.slice(0, members.indexOf('settings_tile_parking') + keys.length + 1);
for (const key of keys) {
  assert.ok(stringsMembers.includes(key), `${key} must be part of i18n::Strings`);
}
// The positional tables end with the new keys, in declaration order.
const lastKeys = stringsMembers.slice(-keys.length);
assert.deepEqual(lastKeys, keys);
const tails = {};
for (const table of ['kStringsDe', 'kStringsEn', 'kStringsFr']) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  assert.notEqual(start, -1, table);
  const end = i18n.indexOf('};', start);
  const values = [...i18n.slice(start, end).matchAll(/"((?:\\.|[^"\\])*)"/g)].map(match => match[1]);
  tails[table] = values.slice(-keys.length);
  for (const [index, value] of tails[table].entries()) {
    assert.ok(value.trim().length > 0, `${table}.${keys[index]} must not be empty`);
  }
}
assert.notDeepEqual(tails.kStringsDe, tails.kStringsEn, 'German must be translated');
assert.notDeepEqual(tails.kStringsFr, tails.kStringsEn, 'French must be translated');
assert.equal(tails.kStringsEn[0], 'Built-in camera');
assert.equal(tails.kStringsDe[0], 'Integrierte Kamera');
const byKey = table => Object.fromEntries(keys.map((key, index) => [key, tails[table][index]]));
assert.equal(byKey('kStringsEn').local_camera_mirror, 'Mirror image');
assert.equal(byKey('kStringsDe').local_camera_mirror, 'Bild spiegeln');
// Indicator sub-block: its heading is marked experimental in every language.
assert.match(byKey('kStringsDe').local_camera_indicator_section, /experimentell/);
assert.match(byKey('kStringsEn').local_camera_indicator_section, /experimental/);
assert.match(byKey('kStringsFr').local_camera_indicator_section, /expérimental/);
assert.equal(byKey('kStringsDe').local_camera_indicator_line, 'Rote Linie oben anzeigen');
// Image controls: every language has its own labels.
assert.deepEqual(
  ['image_section', 'brightness', 'contrast', 'saturation', 'red', 'blue', 'image_reset']
    .map(name => byKey('kStringsEn')[`local_camera_${name}`]),
  ['Image', 'Brightness', 'Contrast', 'Saturation', 'Red', 'Blue', 'Reset']);
assert.deepEqual(
  ['image_section', 'brightness', 'contrast', 'saturation', 'red', 'blue', 'image_reset']
    .map(name => byKey('kStringsDe')[`local_camera_${name}`]),
  ['Bild', 'Helligkeit', 'Kontrast', 'Sättigung', 'Rot', 'Blau', 'Zurücksetzen']);
assert.deepEqual(
  ['image_section', 'brightness', 'contrast', 'saturation', 'red', 'blue', 'image_reset']
    .map(name => byKey('kStringsFr')[`local_camera_${name}`]),
  ['Image', 'Luminosité', 'Contraste', 'Saturation', 'Rouge', 'Bleu', 'Réinitialiser']);

// --- Server-rendered section --------------------------------------------------
const helperStart = html.indexOf('static void appendLocalCameraSettingsHtml(');
assert.notEqual(helperStart, -1);
const helper = html.slice(helperStart, html.indexOf('\n}\n', helperStart));
// The mirror checkbox is server-rendered, saves immediately and the handler
// accepts it on its own.
assert.match(helper, /id="local_camera_mirror" onchange="saveLocalCameraMirror\(this\.checked\)"/);
assert.match(helper, /if \(local_camera::mirror\(\)\) html \+= " checked";/);
assert.match(handler, /const bool has_mirror = server\.hasArg\("mirror"\);/);
assert.match(handler, /local_camera::setMirror\(mirror\)/);
// Indicator style: two server-rendered checkboxes (line, pill) that save
// immediately as indicator=0|1|2; the pill checkbox is disabled while the
// line is off. The handler validates before anything is saved.
assert.match(helper, /id="local_camera_indicator_line" onchange="saveLocalCameraIndicator\(\)"/);
// Two columns in the settings grid: the live stream group (heading, mode,
// Custom sliders, mirror) left, the indicator group (heading, toggles, note)
// right, the image controls below over the full width. Each group has a
// sub-heading like the image block; the pill toggle dims while disabled.
assert.match(helper, /<div class="local-camera-group" id="local_camera_stream">\s*<div class="network-settings-heading">\)html";[\s\S]*?appendHtmlEscaped\(html, tr\.local_camera_stream_section\);/);
assert.match(helper, /<div class="local-camera-group local-camera-indicator" id="local_camera_indicator">\s*<div class="network-settings-heading">\)html";[\s\S]*?appendHtmlEscaped\(html, tr\.local_camera_indicator_section\);/);
{
  const streamAt = helper.indexOf('id="local_camera_stream"');
  const indicatorAt = helper.indexOf('id="local_camera_indicator"');
  const imageAt = helper.indexOf('id="local_camera_image"');
  assert.ok(streamAt < helper.indexOf('id="local_camera_stream_mode"') &&
            helper.indexOf('id="local_camera_custom"') < helper.indexOf('id="local_camera_mirror"') &&
            helper.indexOf('id="local_camera_mirror"') < indicatorAt && indicatorAt < imageAt,
    'Stream group (mode, Custom, mirror), then the indicator group, then the image block');
  assert.doesNotMatch(helper.slice(streamAt, helper.indexOf('<div class="settings-full local-camera-image"')), /settings-full/,
    'Both groups take one column each');
  assert.match(helper, /<div class="settings-full local-camera-image" id="local_camera_image">/);
}
const css = readRepoFile('src/web/assets/admin.css');
assert.match(css, /\.local-camera-group \{ display:flex; flex-direction:column; gap:12px; min-width:0; \}/);
assert.match(css, /\.local-camera-indicator \.settings-checkbox:has\(input:disabled\) \{ opacity:\.45; \}/);
assert.equal(byKey('kStringsDe').local_camera_stream_mode, 'Modus');
assert.equal(byKey('kStringsDe').local_camera_stream_section, 'Live-Stream');
assert.match(helper, /id="local_camera_indicator_pill" onchange="saveLocalCameraIndicator\(\)"/);
assert.match(helper, /const local_camera::IndicatorStyle indicator = local_camera::indicatorStyle\(\);/);
assert.match(helper, /if \(indicator != local_camera::IndicatorStyle::None\) html \+= " checked";/);
assert.match(helper, /if \(indicator != local_camera::IndicatorStyle::Line\) html \+= " checked";\s*if \(indicator == local_camera::IndicatorStyle::None\) html \+= " disabled";/);
assert.match(handler, /const bool has_indicator = server\.hasArg\("indicator"\);/);
assert.match(handler, /"Invalid indicator value"/);
assert.ok(handler.indexOf('"Invalid indicator value"') < handler.indexOf('local_camera::setImageSettings(image)'),
  'The indicator value is validated before anything is saved');
assert.match(handler, /local_camera::setIndicatorStyle\(indicator\)/);
// Custom stream mode: an extra select entry and two sliders (fps, quality)
// shown only while it is selected; the handler validates both before saving
// and stores them before switching the mode.
assert.deepEqual(['stream_mode_custom', 'custom_fps', 'custom_quality'].map(name => byKey('kStringsDe')[`local_camera_${name}`]),
  ['Benutzerdefiniert', 'Bilder pro Sekunde', 'JPEG-Qualität']);
assert.deepEqual(['stream_mode_custom', 'custom_fps', 'custom_quality'].map(name => byKey('kStringsEn')[`local_camera_${name}`]),
  ['Custom', 'Frames per second', 'JPEG quality']);
assert.match(helper, /html \+= String\(static_cast<unsigned>\(local_camera_stream::kModeCustom\)\);\s*html \+= "\\"";\s*if \(selected_mode == local_camera_stream::kModeCustom\) html \+= " selected";/);
assert.match(helper, /id="local_camera_custom" data-mode="\)html";/);
assert.match(helper, /if \(selected_mode != local_camera_stream::kModeCustom\) html \+= " hidden";/);
assert.match(helper, /appendLocalCameraCustomSlider\(html, "fps", tr\.local_camera_custom_fps,\s*local_camera_stream::kCustomMinFps,\s*local_camera_stream::kCustomMaxFps, custom\.fps\);/);
// The Custom quality reaches the oversize floor (10), so a noisy night
// stream can be kept small on purpose.
assert.match(helper, /appendLocalCameraCustomSlider\(html, "quality", tr\.local_camera_custom_quality,\s*local_camera_stream::kCustomMinQuality,\s*local_camera_stream::kMaxQuality, custom\.quality\);/);
assert.match(html, /oninput="localCameraCustomInput\(this\)" onchange="localCameraCustomChange\(this\)"/);
assert.match(handler, /"Invalid custom stream value"/);
assert.ok(handler.indexOf('"Invalid custom stream value"') < handler.indexOf('local_camera::setImageSettings(image)'),
  'Custom values are validated before anything is saved');
assert.ok(handler.indexOf('local_camera::setCustomMode(') < handler.indexOf('local_camera::setStreamMode('),
  'Custom values are stored before the mode switches to them');
assert.match(readRepoFile('src/web/assets/admin.css'), /\.local-camera-custom\[hidden\] \{ display:none; \}/,
  'The grid display must not override the hidden attribute');
{
  const service = readRepoFile('src/video/local_camera/local_camera.cpp');
  assert.match(service, /kPrefsCustomFpsKey\[\] = "lcam_cfps";/);
  assert.match(service, /kPrefsCustomQualityKey\[\] = "lcam_cq";/);
  assert.match(service, /json \+= ",\\"custom\\":\{\\"fps\\":";/);
  assert.match(service, /kImageWidth, kImageHeight, &settings,\s*currentCustomMode\(\)\)/);
}
{
  const service = readRepoFile('src/video/local_camera/local_camera.cpp');
  assert.match(service, /kPrefsIndicatorKey\[\] = "lcam_ind";/, 'Own NVS key, Settings record unchanged');
  assert.match(service, /prefs\.putUChar\(kPrefsIndicatorKey, value\)/);
  assert.match(service, /json \+= ",\\"indicator\\":";/);
}
assert.match(helper, /if \(!local_camera::supported\(\) \|\| !Device::kCapabilities\.has_builtin_camera\) \{\s*return;/,
  'The section must only exist on the exact camera profile');
// The pill texts belong to the on-display indicator, not to the Web Admin.
const deviceOnlyKeys = ['local_camera_indicator_active', 'local_camera_indicator_end'];
for (const key of keys.filter(name => !deviceOnlyKeys.includes(name))) {
  assert.ok(helper.includes(`tr.${key}`), `The section must render tr.${key}`);
}
const markup = [...helper.matchAll(/R"html\(([\s\S]*?)\)html"/g)].map(match => match[1]).join('');
assert.match(markup, /<input type="checkbox" id="local_camera_enabled" onchange="saveLocalCameraEnabled\(this\.checked\)"/);
// Live-stream mode: server-rendered select, Auto from the translations, the
// numeric mode labels from the core mode table, saved immediately.
assert.match(markup, /<select id="local_camera_stream_mode" onchange="saveLocalCameraStreamMode\(this\.value\)">/);
assert.doesNotMatch(markup, /id="local_camera_stream_mode"[^>]*\bname=/,
  'The stream mode must not be submitted with the /mqtt settings form');
assert.match(helper, /<option value="0"\)html";\s*if \(selected_mode == local_camera_stream::kModeAuto\) html \+= " selected";\s*html \+= ">";\s*appendHtmlEscaped\(html, tr\.local_camera_stream_mode_auto\);/);
assert.match(helper, /for \(const local_camera_stream::ModeEntry& mode : local_camera_stream::kModes\)[\s\S]*formatModeLabel\(label, sizeof\(label\), mode,\s*local_camera::imageWidth\(\),\s*local_camera::imageHeight\(\)\)[\s\S]*if \(mode\.id == selected_mode\) html \+= " selected";/);
assert.match(helper, /const uint8_t selected_mode = local_camera::streamMode\(\);/);
assert.doesNotMatch(markup, /id="local_camera_enabled"[^>]*\bname=/,
  'The opt-in must not be submitted with the /mqtt settings form');
const visibleText = markup.replace(/<[^>]*>/g, ' ').replace(/[\s:]+/g, '');
assert.equal(visibleText, '', 'No display text may be hard-coded in the section markup');
assert.match(html, /\)html";\n  appendLocalCameraSettingsHtml\(html, tr\);\n  html \+= R"html\(\n          <div class="settings-section">\n            <div class="section-title">\)html";\n  html \+= tr\.admin_settings_screenshot;/);
assert.match(html, /if \(local_camera::supported\(\) && Device::kCapabilities\.has_builtin_camera\) \{\n    json \+= ",\\"local_camera\\":";\n    local_camera::appendStatusJson\(json\);\n  \}/,
  '/api/status exposes the camera status only on the exact camera profile');

// The status line carries every translated state as a data attribute. Map the
// attribute names the server really renders to the i18n member they print.
const statusAttributes = new Map([...helper.matchAll(
  /data-([a-z-]+)="\)html";\s*appendHtmlEscaped\(html, tr\.(\w+)\);/g)]
  .map(match => [match[1].replace(/-([a-z])/g, (_, c) => c.toUpperCase()), match[2]]));
assert.ok(statusAttributes.size >= 6, 'The status line must render its translated data attributes');

// --- Endpoint -------------------------------------------------------------------
assert.match(routes, /server\.on\("\/api\/local-camera", HTTP_GET,/);
assert.match(routes, /server\.on\("\/api\/local-camera", HTTP_POST,/);
assert.match(handler, /if \(!local_camera::supported\(\) \|\| !Device::kCapabilities\.has_builtin_camera\) \{\s*web_admin_handlers::sendJsonError\(server, 404,/);
assert.match(handler, /local_camera::setEnabled\(enable\)/);
assert.match(handler, /"Invalid enabled value"/);
assert.match(handler, /server\.hasArg\("mode"\)/);
assert.match(handler, /!local_camera_stream::isKnownMode\(static_cast<uint8_t>\(mode\)\)\) \{\s*web_admin_handlers::sendJsonError\(server, 400, "Invalid mode value"\);/,
  'Unknown mode ids are rejected before anything is persisted');
assert.match(handler, /local_camera::setStreamMode\(static_cast<uint8_t>\(mode\)\)/);
const service = readRepoFile('src/video/local_camera/local_camera.cpp');
assert.match(service, /kPrefsStreamModeKey\[\] = "lcam_mode";/, 'Own NVS key, Settings record unchanged');
assert.match(service, /prefs\.getUChar\(kPrefsStreamModeKey, local_camera_stream::kModeAuto\)/);
assert.match(service, /prefs\.putUChar\(kPrefsStreamModeKey, mode\)/);
assert.match(service, /json \+= ",\\"stream_mode\\":";/, 'GET returns the stored mode');

// --- Delivered browser code -----------------------------------------------------
assert.ok(bundle.sources.includes('src/web/admin/settings/local-camera.js'));
const delivered = readAdminDeliverySource();
const start = delivered.indexOf('let localCameraSaveSequence');
assert.notEqual(start, -1, 'The camera module must be part of the delivered Admin asset');
const end = delivered.indexOf('document.addEventListener', start);
const moduleCode = delivered.slice(start, end);
assert.doesNotMatch(moduleCode, /'[A-Z][a-z]+ [a-z]+/,
  'Browser code must not contain display sentences; texts come from data attributes');

// Every dataset key the browser code reads must be rendered by the server.
const keyProbe = {document: {getElementById: () => null, addEventListener: () => {}}};
vm.createContext(keyProbe);
vm.runInContext(`${moduleCode}\n;globalThis.stateKeys = LOCAL_CAMERA_STATE_KEYS;`, keyProbe);
assert.ok(keyProbe.stateKeys, 'LOCAL_CAMERA_STATE_KEYS must be delivered');
const readKeys = ['label', ...Object.values(keyProbe.stateKeys)];
assert.equal(readKeys.length, 6);
for (const key of readKeys) {
  assert.ok(statusAttributes.has(key), `The status line must render data-* for dataset.${key}`);
}
// German texts in the server's attribute order, from the real translation table.
const germanByKey = Object.fromEntries(keys.map((key, index) => [key, tails.kStringsDe[index]]));

function createHarness({fetchImpl, withModeSelect = false, withIndicator = false, withCustom = false}) {
  const dataset = {state: 'probing'};
  for (const [attribute, key] of statusAttributes) {
    assert.ok(key in germanByKey, `tr.${key} must be a camera translation`);
    dataset[attribute] = germanByKey[key];
  }
  const note = {dataset, textContent: ''};
  const toggle = {checked: false};
  const modeSelect = withModeSelect ? {value: '0', dataset: {saved: '0'}} : null;
  const indicatorLine = withIndicator ? {checked: true, dataset: {saved: '2'}} : null;
  const indicatorPill = withIndicator ? {checked: true, disabled: false} : null;
  const customBlock = withCustom ? {hidden: true, dataset: {mode: '100'}} : null;
  const customFps = withCustom ? {value: '1', dataset: {customKey: 'fps', saved: '1'}} : null;
  const customQuality = withCustom ? {value: '65', dataset: {customKey: 'quality', saved: '65'}} : null;
  const customFpsValue = {textContent: ''};
  const customQualityValue = {textContent: ''};
  const notifications = [];
  const timers = [];
  const requests = [];
  const context = {
    document: {
      getElementById: id => (id === 'local_camera_status' ? note
        : id === 'local_camera_enabled' ? toggle
        : id === 'local_camera_stream_mode' ? modeSelect
        : id === 'local_camera_indicator_line' ? indicatorLine
        : id === 'local_camera_indicator_pill' ? indicatorPill
        : id === 'local_camera_custom' ? customBlock
        : id === 'local_camera_custom_fps' ? customFps
        : id === 'local_camera_custom_quality' ? customQuality
        : id === 'local_camera_custom_fps_value' && withCustom ? customFpsValue
        : id === 'local_camera_custom_quality_value' && withCustom ? customQualityValue : null),
      addEventListener: () => {},
    },
    fetch: async (url, options = {}) => {
      requests.push({url, options});
      return fetchImpl(url, options);
    },
    setTimeout: (fn, ms) => { timers.push({fn, ms}); return timers.length; },
    clearTimeout: () => {},
    showNotification: (message, ok) => notifications.push({message, ok}),
    t: key => `t:${key}`,
  };
  vm.createContext(context);
  vm.runInContext(moduleCode, context);
  return {context, note, toggle, modeSelect, indicatorLine, indicatorPill, customBlock, customFps,
    customQuality, customFpsValue, notifications, timers, requests};
}

const json = body => ({ok: true, status: 200, json: async () => body});

{
  const harness = createHarness({fetchImpl: async () => json({
    supported: true, enabled: true, state: 'ready', sensor: 'ov02c10', chip_id: '0x5602'})});
  await harness.context.saveLocalCameraEnabled(true);
  assert.equal(harness.requests[0].url, '/api/local-camera');
  assert.equal(harness.requests[0].options.method, 'POST');
  assert.equal(harness.requests[0].options.body, 'enabled=1');
  assert.equal(harness.note.textContent, 'Status: Sensor erkannt (OV02C10, 0x5602)');
  assert.equal(harness.toggle.checked, true);
  assert.equal(harness.timers.length, 0);
}
{
  const harness = createHarness({fetchImpl: async () => json({
    supported: true, enabled: true, state: 'probing'})});
  await harness.context.saveLocalCameraEnabled(true);
  assert.equal(harness.note.textContent, 'Status: Sensor wird geprüft...');
  assert.equal(harness.timers.length, 1, 'A running probe is followed briefly');
  assert.equal(harness.timers[0].ms, 1000);
}
{
  const harness = createHarness({fetchImpl: async () => json({
    supported: true, enabled: true, state: 'not_found', detail: 'sensor_not_found'})});
  await harness.context.refreshLocalCameraStatus();
  assert.equal(harness.requests[0].options.method, undefined);
  assert.equal(harness.note.textContent, 'Status: Kein Kamerasensor gefunden',
    'Internal detail codes are diagnostic-only and never shown');
}
{
  const harness = createHarness({fetchImpl: async () => json({
    supported: true, enabled: false, state: 'disabled', detail: ''})});
  harness.toggle.checked = true;
  await harness.context.saveLocalCameraEnabled(false);
  assert.equal(harness.requests[0].options.body, 'enabled=0');
  assert.equal(harness.note.textContent, 'Status: Deaktiviert');
  assert.equal(harness.toggle.checked, false);
}
{
  const harness = createHarness({fetchImpl: async () => ({ok: false, status: 500, json: async () => ({})})});
  harness.toggle.checked = true;
  await harness.context.saveLocalCameraEnabled(true);
  assert.equal(harness.toggle.checked, false, 'A failed save restores the previous toggle state');
  assert.deepEqual(harness.notifications, [{message: 't:networkErrorSave', ok: false}]);
}
{
  const harness = createHarness({fetchImpl: async () => json({state: 'error', detail: 'no_frames', enabled: true})});
  await harness.context.refreshLocalCameraStatus();
  assert.equal(harness.note.textContent, 'Status: Kamerafehler');
}
// Stream mode: POST mode=<id>, the fresh status sets the select; a failed save
// restores the last saved value.
{
  const harness = createHarness({withModeSelect: true, fetchImpl: async () => json({
    supported: true, enabled: true, state: 'ready', stream_mode: 3})});
  harness.modeSelect.value = '3';
  await harness.context.saveLocalCameraStreamMode('3');
  assert.equal(harness.requests[0].url, '/api/local-camera');
  assert.equal(harness.requests[0].options.method, 'POST');
  assert.equal(harness.requests[0].options.body, 'mode=3');
  assert.equal(harness.modeSelect.value, '3');
  assert.equal(harness.modeSelect.dataset.saved, '3');
}
{
  const harness = createHarness({withModeSelect: true,
    fetchImpl: async () => ({ok: false, status: 400, json: async () => ({})})});
  harness.modeSelect.value = '5';
  await harness.context.saveLocalCameraStreamMode('5');
  assert.equal(harness.modeSelect.value, '0', 'A failed save restores the saved mode');
  assert.deepEqual(harness.notifications, [{message: 't:networkErrorSave', ok: false}]);
}
{
  // A status refresh (e.g. after a reload) shows the persisted mode.
  const harness = createHarness({withModeSelect: true, fetchImpl: async () => json({
    supported: true, enabled: true, state: 'ready', stream_mode: 2})});
  await harness.context.refreshLocalCameraStatus();
  assert.equal(harness.modeSelect.value, '2');
}
// Indicator style: line off posts 0 and disables the pill checkbox; line on
// with the pill off posts 1; a failed save restores the saved style.
{
  const harness = createHarness({withIndicator: true, fetchImpl: async (url, options) => json({
    supported: true, enabled: true, state: 'ready',
    indicator: Number(String(options.body).split('=')[1])})});
  harness.indicatorLine.checked = false;
  await harness.context.saveLocalCameraIndicator();
  assert.equal(harness.requests[0].options.body, 'indicator=0');
  assert.equal(harness.indicatorPill.disabled, true);
  assert.equal(harness.indicatorPill.checked, true, 'The pill choice is kept while the line is off');
  assert.equal(harness.indicatorLine.dataset.saved, '0');
  harness.indicatorLine.checked = true;
  harness.indicatorPill.checked = false;
  await harness.context.saveLocalCameraIndicator();
  assert.equal(harness.requests[1].options.body, 'indicator=1');
  assert.equal(harness.indicatorPill.disabled, false);
  assert.equal(harness.indicatorLine.checked, true);
  assert.equal(harness.indicatorPill.checked, false);
}
{
  const harness = createHarness({withIndicator: true,
    fetchImpl: async () => ({ok: false, status: 500, json: async () => ({})})});
  harness.indicatorLine.checked = false;
  await harness.context.saveLocalCameraIndicator();
  assert.equal(harness.indicatorLine.checked, true, 'A failed save restores the saved style');
  assert.equal(harness.indicatorPill.checked, true);
  assert.equal(harness.indicatorPill.disabled, false);
  assert.deepEqual(harness.notifications, [{message: 't:networkErrorSave', ok: false}]);
}
// Custom mode: selecting it shows the sliders at once, the fresh status keeps
// them and the stored values; another mode hides them again.
{
  const harness = createHarness({withModeSelect: true, withCustom: true, fetchImpl: async () => json({
    supported: true, enabled: true, state: 'ready', stream_mode: 100, custom: {fps: 2, quality: 70}})});
  harness.modeSelect.value = '100';
  const pending = harness.context.saveLocalCameraStreamMode('100');
  assert.equal(harness.customBlock.hidden, false, 'Shown right away');
  await pending;
  assert.equal(harness.requests[0].options.body, 'mode=100');
  assert.equal(harness.customBlock.hidden, false);
  assert.equal(harness.customFps.value, '2');
  assert.equal(harness.customFps.dataset.saved, '2');
  assert.equal(harness.customQuality.value, '70');
  assert.equal(harness.customFpsValue.textContent, '2');
}
{
  const harness = createHarness({withModeSelect: true, withCustom: true, fetchImpl: async () => json({
    supported: true, enabled: true, state: 'ready', stream_mode: 5, custom: {fps: 1, quality: 65}})});
  harness.customBlock.hidden = false;
  await harness.context.saveLocalCameraStreamMode('5');
  assert.equal(harness.customBlock.hidden, true);
}
// A slider saves its own key on release; a failed save restores the value.
{
  const harness = createHarness({withCustom: true, fetchImpl: async () => json({
    supported: true, enabled: true, state: 'ready', stream_mode: 100, custom: {fps: 3, quality: 65}})});
  harness.customFps.value = '3';
  harness.context.localCameraCustomInput(harness.customFps);
  assert.equal(harness.customFpsValue.textContent, '3');
  await harness.context.localCameraCustomChange(harness.customFps);
  assert.equal(harness.requests[0].options.body, 'custom_fps=3');
  assert.equal(harness.customFps.dataset.saved, '3');
}
{
  const harness = createHarness({withCustom: true,
    fetchImpl: async () => ({ok: false, status: 400, json: async () => ({})})});
  harness.customQuality.value = '85';
  await harness.context.localCameraCustomChange(harness.customQuality);
  assert.equal(harness.requests[0].options.body, 'custom_quality=85');
  assert.equal(harness.customQuality.value, '65', 'A failed save restores the saved value');
  assert.deepEqual(harness.notifications, [{message: 't:networkErrorSave', ok: false}]);
}
console.log('Local camera Web Admin settings, stream modes, indicator, translations and status mapping passed.');
