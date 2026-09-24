// Built-in camera image controls (brightness, contrast, saturation, red,
// blue): server-rendered sliders with translated labels, one validated POST
// extension of /api/local-camera, own NVS keys, live application by the
// capture worker and the delivered browser code (debounced save, immediate
// save on release, revert on a failed save, status updates).
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {readAdminDeliverySource, readRepoFile} from '../../lib/admin-source.mjs';

const html = readRepoFile('src/web/server/render/web_admin_html.cpp').replaceAll('\r\n', '\n');
const handler = readRepoFile('src/web/server/handlers/web_admin_local_camera.cpp').replaceAll('\r\n', '\n');
const service = readRepoFile('src/video/local_camera/local_camera.cpp').replaceAll('\r\n', '\n');
const publicHeader = readRepoFile('src/video/local_camera/local_camera.h');
const css = readRepoFile('src/web/assets/admin.css');

const IMAGE_KEYS = ['brightness', 'contrast', 'saturation', 'red', 'blue', 'gain'];

// --- Server-rendered sliders ---------------------------------------------------
const sliderStart = html.indexOf('static void appendLocalCameraImageSlider(');
assert.notEqual(sliderStart, -1, 'One shared slider helper renders every control');
const sliderHelper = html.slice(sliderStart, html.indexOf('\n}\n', sliderStart));
const sliderMarkup = [...sliderHelper.matchAll(/R"html\(([\s\S]*?)\)html"/g)].map(m => m[1]).join('');
assert.match(sliderMarkup, /<label class="local-camera-slider" for="local_camera_/);
assert.match(sliderMarkup, /<input type="range" id="local_camera_/);
assert.match(sliderMarkup, /oninput="localCameraImageInput\(this\)" onchange="localCameraImageChange\(this\)"/);
assert.match(sliderMarkup, /<output id="local_camera_/);
assert.doesNotMatch(sliderMarkup, /\bname=/, 'Sliders must not be submitted with the /mqtt settings form');
assert.equal(sliderMarkup.replace(/<[^>]*>/g, ' ').replace(/[\s:]+/g, ''), '',
  'No display text may be hard-coded in the slider markup');
assert.match(sliderHelper, /appendHtmlEscaped\(html, label\);/, 'Labels are escaped translations');
assert.match(sliderHelper, /data-saved="\)html";\s*html \+= String\(value\);/,
  'The last saved value travels with the slider for a revert');

const sectionStart = html.indexOf('static void appendLocalCameraSettingsHtml(');
const section = html.slice(sectionStart, html.indexOf('\n}\n', sectionStart));
assert.match(section, /const local_camera_contract::ImageSettings image = local_camera::imageSettings\(\);/);
const sliders = [...section.matchAll(
  /appendLocalCameraImageSlider\(html, "(\w+)", tr\.(\w+),\s*([\w:]+),\s*([\w:]+),\s*image\.(\w+),\s*"(%?)"\);/g)]
  .map(m => ({key: m[1], label: m[2], min: m[3], max: m[4], field: m[5], unit: m[6]}));
assert.deepEqual(sliders.map(s => s.key), IMAGE_KEYS, 'Six sliders in a fixed order');
for (const slider of sliders) {
  assert.equal(slider.field, slider.key);
  assert.equal(slider.label, `local_camera_${slider.key}`);
}
assert.deepEqual(sliders.map(s => [s.min.replace(/^.*::/, ''), s.max.replace(/^.*::/, '')]), [
  ['kImageAdjustMin', 'kImageAdjustMax'], ['kImageAdjustMin', 'kImageAdjustMax'],
  ['kSaturationMin', 'kSaturationMax'], ['kImageAdjustMin', 'kImageAdjustMax'],
  ['kImageAdjustMin', 'kImageAdjustMax'], ['kGainLimitMin', 'kGainLimitMax']]);
assert.deepEqual(sliders.map(s => s.unit), ['', '', '%', '%', '%', '%']);
assert.match(section, /<div class="network-settings-heading">\)html";\s*appendHtmlEscaped\(html, tr\.local_camera_image_section\);/,
  'The sub-block has a translated heading');
assert.match(section, /<button type="button" class="btn btn-secondary" id="local_camera_image_reset" onclick="resetLocalCameraImage\(\)">\)html";\s*appendHtmlEscaped\(html, tr\.local_camera_image_reset\);/);
assert.match(css, /\.local-camera-slider \{ display:grid;/);

// --- Endpoint: validated integers, one POST, reset -----------------------------
assert.match(handler, /readImageArg\(server, "brightness", kImageAdjustMin, kImageAdjustMax, &brightness\)/);
assert.match(handler, /readImageArg\(server, "contrast", kImageAdjustMin, kImageAdjustMax, &contrast\)/);
assert.match(handler, /readImageArg\(server, "saturation", kSaturationMin, kSaturationMax, &saturation\)/);
assert.match(handler, /readImageArg\(server, "red", kImageAdjustMin, kImageAdjustMax, &red\)/);
assert.match(handler, /readImageArg\(server, "blue", kImageAdjustMin, kImageAdjustMax, &blue\)/);
// Max. gain (hardware 2026-09-25: night images far too noisy at full gain).
assert.match(handler, /readImageArg\(server, "gain", kGainLimitMin, kGainLimitMax, &gain\)/);
assert.match(handler, /makeImageSettings\(brightness, contrast, saturation, red, blue, gain\)/);
assert.match(handler, /!local_camera_contract::parseImageInteger\(text\.c_str\(\), &parsed\) \|\|\s*parsed < min_value \|\| parsed > max_value/,
  'Non-integers and out-of-range values are invalid');
assert.match(handler, /if \(value != "1" && value != "true" && value != "on"\) return ImageArgs::Invalid;\s*base = ImageSettings\{\};/,
  'reset=1 starts from the defaults');
const post = handler.slice(handler.indexOf('if (server.method() == HTTP_POST) {'));
const invalidAt = post.indexOf('"Invalid image value"');
assert.ok(invalidAt > 0 && invalidAt < post.indexOf('local_camera::setStreamMode(') &&
  invalidAt < post.indexOf('local_camera::setMirror(') && invalidAt < post.indexOf('local_camera::setEnabled('),
  'Invalid image values are rejected before any setting is saved');
assert.match(post, /sendJsonError\(server, 400, "Invalid image value"\)/);
// A combined POST such as brightness=10&mirror=x must not persist the valid
// part: every 400 rejection precedes the first saving call.
const firstSave = Math.min(...['local_camera::setImageSettings(', 'local_camera::setStreamMode(',
  'local_camera::setMirror(', 'local_camera::setEnabled('].map(call => {
  const at = post.indexOf(call);
  assert.ok(at > 0, `${call} is present`);
  return at;
}));
for (const message of ['"Invalid image value"', '"Missing enabled value"', '"Invalid mode value"',
  '"Invalid mirror value"', '"Invalid enabled value"']) {
  const at = post.indexOf(message);
  assert.ok(at > 0 && at < firstSave, `${message} is rejected before any setting is saved`);
  assert.equal(post.indexOf(message, at + 1), -1, `${message} is reported once`);
}
assert.match(post, /if \(!has_enabled && !has_mode && !has_mirror && !has_indicator && !has_custom &&\s*image_args == ImageArgs::None\)/);
assert.match(post, /image_args == ImageArgs::Valid && !local_camera::setImageSettings\(image\)/);

// Reference model of the handler arguments against the real contract ranges.
const contract = readRepoFile('src/video/local_camera/local_camera_contract.h');
const constant = name => Number(contract.match(new RegExp(`constexpr int ${name} = (-?\\d+);`))[1]);
const ranges = {
  brightness: [constant('kImageAdjustMin'), constant('kImageAdjustMax')],
  contrast: [constant('kImageAdjustMin'), constant('kImageAdjustMax')],
  saturation: [constant('kSaturationMin'), constant('kSaturationMax')],
  red: [constant('kImageAdjustMin'), constant('kImageAdjustMax')],
  blue: [constant('kImageAdjustMin'), constant('kImageAdjustMax')],
  gain: [constant('kGainLimitMin'), constant('kGainLimitMax')],
};
assert.deepEqual(ranges.gain, [0, 100]);
assert.equal(constant('kGainLimitDefault'), 100, 'The default keeps the full gain range');
assert.deepEqual(ranges.brightness, [-50, 50]);
assert.deepEqual(ranges.saturation, [0, 200]);
assert.equal(constant('kSaturationDefault'), 100);

// --- Persistence and status ------------------------------------------------------
const prefKeys = {
  kPrefsBrightnessKey: 'lcam_bright', kPrefsContrastKey: 'lcam_contrast',
  kPrefsSaturationKey: 'lcam_sat', kPrefsRedKey: 'lcam_red', kPrefsBlueKey: 'lcam_blue',
  kPrefsGainKey: 'lcam_gain'};
const allKeys = [...service.matchAll(/constexpr char kPrefs\w+Key\[\] = "([^"]+)";/g)].map(m => m[1]);
for (const [name, key] of Object.entries(prefKeys)) {
  assert.ok(service.includes(`constexpr char ${name}[] = "${key}";`), `${name} must be "${key}"`);
  assert.ok(key.length <= 15, `NVS key ${key} must fit 15 characters`);
}
assert.equal(new Set(allKeys).size, allKeys.length, 'Camera NVS keys must be unique');
assert.match(service, /makeImageSettings\(\s*prefs\.getChar\(kPrefsBrightnessKey, 0\), prefs\.getChar\(kPrefsContrastKey, 0\),\s*prefs\.getUChar\(kPrefsSaturationKey, kSaturationDefault\),\s*prefs\.getChar\(kPrefsRedKey, 0\), prefs\.getChar\(kPrefsBlueKey, 0\),\s*prefs\.getUChar\(kPrefsGainKey, kGainLimitDefault\)\)/,
  'Missing keys load the defaults; stored values are clamped');
for (const [name, put] of [['kPrefsBrightnessKey', 'putChar'], ['kPrefsContrastKey', 'putChar'],
  ['kPrefsSaturationKey', 'putUChar'], ['kPrefsRedKey', 'putChar'], ['kPrefsBlueKey', 'putChar'],
  ['kPrefsGainKey', 'putUChar']]) {
  assert.ok(service.includes(`prefs.${put}(${name}, wanted.`), `${name} is written with ${put}`);
}
const setter = service.slice(service.indexOf('bool setImageSettings('));
assert.match(setter, /if \(sameImageSettings\(wanted, current\)\) return true;/, 'Unchanged values write nothing');
assert.match(setter, /BatchedNvsWrite::finish\(prefs\) \|\| !written\) \{[\s\S]*?return false;[\s\S]*?g_image = wanted;[\s\S]*?g_image_generation\.fetch_add\(1\);/,
  'Only a successful save changes the live settings');
assert.match(publicHeader, /bool setImageSettings\(const local_camera_contract::ImageSettings& settings\);/);
assert.match(service, /",\\"image\\":\{\\"brightness\\":%d,\\"contrast\\":%d,\\"saturation\\":%u,"\s*"\\"red\\":%d,\\"blue\\":%d,\\"gain\\":%u\}"/,
  'The status JSON returns every value');

// --- Live application on the capture worker -----------------------------------
const body = name => {
  const start = service.indexOf(name);
  assert.notEqual(start, -1, name);
  return service.slice(start, service.indexOf('\n}\n', start));
};
assert.match(body('void applyColorCorrection() {'), /buildImageCcm\(kBaseCcm, g_gains, currentImageSettings\(\), ccm\.matrix\);/);
assert.match(body('uint32_t gammaCurve(uint32_t x) {'), /gammaLutValue\(x, kMode\.black_level, kGammaExponent, g_gamma_curve_contrast,\s*g_gamma_curve_gain\)/);
assert.match(body('uint32_t aeTarget() {'), /adjustedAeTarget\(g_pipe\.ae_target, currentImageSettings\(\)\.brightness\)/);
const loader = body('esp_err_t loadGammaCurve(int contrast, uint8_t digital_step) {');
assert.doesNotMatch(loader, /esp_isp_gamma_disable/, 'Gamma is reconfigured while enabled (no linear frame)');
for (const channel of ['R', 'G', 'B']) {
  assert.match(loader, new RegExp(`esp_isp_gamma_configure\\(g_pipe\\.isp, COLOR_COMPONENT_${channel}, &curve\\)`));
}
const applier = body('void applyImageSettingsIfChanged() {');
assert.match(applier, /if \(generation == g_pipe\.image_generation\) return;/);
assert.match(applier, /applyColorCorrection\(\);\s*if \(image\.contrast == g_pipe\.gamma_contrast &&\s*image\.brightness == g_gamma_curve_brightness\) \{\s*return;\s*\}\s*const esp_err_t err = loadGammaCurve\(image\.contrast, g_pipe\.gamma_digital_step\);/);
assert.doesNotMatch(service, /g_pipe\.ae_target, 12/, 'Every AE step uses the brightness-adjusted target');
assert.equal([...service.matchAll(/aeTarget\(\), 12/g)].length, 3, 'Snapshot, settle and stream AE use aeTarget()');
assert.match(service, /g_pipe\.image_generation = g_image_generation\.load\(\);\s*const ImageSettings image = currentImageSettings\(\);[\s\S]*?buildImageCcm\(kBaseCcm, g_gains, image, ccm\.matrix\);[\s\S]*?err = loadGammaCurve\(image\.contrast, g_digital_step\);\s*if \(err == ESP_OK\) err = esp_isp_gamma_enable\(g_pipe\.isp\);/,
  'A new pipeline starts with the current settings');
assert.match(body('ErrorCode captureJpeg('), /applyImageSettingsIfChanged\(\);\s*applyColorCorrection\(\);\s*g_sensor\.setExposure/,
  'Snapshots apply changed settings before the sensor starts');
const streamLoop = service.slice(service.indexOf('    for (;;) {\n      reason = streamStopCondition();'));
assert.match(streamLoop.slice(0, streamLoop.indexOf('streamCaptureFrame(run);')),
  /applyImageSettingsIfChanged\(\);/, 'The stream applies changes before its next frame');

// --- Delivered browser code -----------------------------------------------------
const delivered = readAdminDeliverySource();
const start = delivered.indexOf('let localCameraSaveSequence');
const end = delivered.indexOf('document.addEventListener', start);
const moduleCode = delivered.slice(start, end);
assert.match(moduleCode, /const LOCAL_CAMERA_IMAGE_DEBOUNCE_MSs*=s*300;/);
assert.doesNotMatch(moduleCode, /'[A-Z][a-z]+ [a-z]+/, 'Browser code must not contain display sentences');

function createHarness(fetchImpl, initial = {}) {
  const elements = new Map();
  for (const slider of sliders) {
    const value = String(initial[slider.key] ?? (slider.key === 'saturation' ? 100 : 0));
    elements.set(`local_camera_${slider.key}`, {
      value, dataset: {imageKey: slider.key, unit: slider.unit, saved: value}});
    elements.set(`local_camera_${slider.key}_value`, {textContent: value + slider.unit});
  }
  elements.set('local_camera_status', {dataset: {label: 'Status', stateReady: 'Bereit'}, textContent: ''});
  const timers = [];
  const requests = [];
  const notifications = [];
  const context = {
    document: {getElementById: id => elements.get(id) || null, addEventListener: () => {}},
    fetch: async (url, options = {}) => {
      requests.push({url, options});
      return fetchImpl(url, options, requests.length);
    },
    setTimeout: (fn, ms) => { timers.push({fn, ms, cleared: false}); return timers.length; },
    clearTimeout: id => { if (id && timers[id - 1]) timers[id - 1].cleared = true; },
    showNotification: (message, ok) => notifications.push({message, ok}),
    t: key => `t:${key}`,
  };
  vm.createContext(context);
  vm.runInContext(moduleCode, context);
  const slider = key => elements.get(`local_camera_${key}`);
  const output = key => elements.get(`local_camera_${key}_value`).textContent;
  return {context, elements, timers, requests, notifications, slider, output};
}

const statusWith = image => ({ok: true, status: 200, json: async () => ({
  supported: true, enabled: true, state: 'ready', image})});
const defaults = {brightness: 0, contrast: 0, saturation: 100, red: 0, blue: 0, gain: 100};

{
  // Dragging: the value shows at once, the save waits 300 ms and only the
  // last value of the drag is sent.
  const harness = createHarness(async () => statusWith({...defaults, brightness: 20}));
  const brightness = harness.slider('brightness');
  brightness.value = '10';
  harness.context.localCameraImageInput(brightness);
  brightness.value = '20';
  harness.context.localCameraImageInput(brightness);
  assert.equal(harness.output('brightness'), '20');
  assert.equal(harness.requests.length, 0, 'No POST while the debounce runs');
  const live = harness.timers.filter(timer => !timer.cleared);
  assert.equal(live.length, 1);
  assert.equal(live[0].ms, 300);
  await live[0].fn();
  assert.equal(harness.requests.length, 1);
  assert.equal(harness.requests[0].url, '/api/local-camera');
  assert.equal(harness.requests[0].options.method, 'POST');
  assert.equal(harness.requests[0].options.headers['Content-Type'], 'application/x-www-form-urlencoded');
  assert.equal(harness.requests[0].options.body, 'brightness=20');
  assert.equal(brightness.dataset.saved, '20');
}
{
  // Release saves immediately; percent values keep their unit.
  const harness = createHarness(async () => statusWith({...defaults, saturation: 140}));
  const saturation = harness.slider('saturation');
  saturation.value = '140';
  await harness.context.localCameraImageChange(saturation);
  assert.equal(harness.requests.length, 1);
  assert.equal(harness.requests[0].options.body, 'saturation=140');
  assert.equal(harness.output('saturation'), '140%');
  assert.equal(saturation.dataset.saved, '140');
}
{
  // A failed save restores the last saved value and reports the error.
  const harness = createHarness(async () => ({ok: false, status: 400, json: async () => ({})}), {red: 5});
  const red = harness.slider('red');
  red.value = '30';
  await harness.context.localCameraImageChange(red);
  assert.equal(harness.requests[0].options.body, 'red=30');
  assert.equal(red.value, '5');
  assert.equal(harness.output('red'), '5%');
  assert.deepEqual(harness.notifications, [{message: 't:networkErrorSave', ok: false}]);
}
{
  // A network error behaves the same.
  const harness = createHarness(async () => { throw new Error('offline'); }, {contrast: -10});
  const contrast = harness.slider('contrast');
  contrast.value = '25';
  await harness.context.localCameraImageChange(contrast);
  assert.equal(contrast.value, '-10');
  assert.equal(harness.notifications.length, 1);
}
{
  // Reset restores every default in one POST and shows the device values.
  const harness = createHarness(async () => statusWith(defaults),
    {brightness: 15, contrast: 20, saturation: 60, red: -10, blue: 30});
  await harness.context.resetLocalCameraImage();
  assert.equal(harness.requests.length, 1);
  assert.equal(harness.requests[0].options.body, 'reset=1');
  for (const key of IMAGE_KEYS) {
    assert.equal(harness.slider(key).value, String(defaults[key]));
    assert.equal(harness.slider(key).dataset.saved, String(defaults[key]));
  }
  assert.equal(harness.output('saturation'), '100%');
}
{
  // A failed reset puts every slider back to its saved value.
  const harness = createHarness(async () => ({ok: false, status: 500, json: async () => ({})}),
    {brightness: 15, blue: 30});
  harness.slider('brightness').value = '0';
  await harness.context.resetLocalCameraImage();
  assert.equal(harness.slider('brightness').value, '15');
  assert.equal(harness.slider('blue').value, '30');
}
{
  // One POST at a time: a change during a running save is sent afterwards,
  // and the running save's reply does not move the slider under the user.
  let release;
  const replies = [
    () => new Promise(resolve => { release = () => resolve(statusWith({...defaults, blue: 10})); }),
    () => Promise.resolve(statusWith({...defaults, blue: 25})),
  ];
  const harness = createHarness(async (url, options, count) => replies[count - 1]());
  const blue = harness.slider('blue');
  blue.value = '10';
  const first = harness.context.localCameraImageChange(blue);
  assert.equal(harness.requests.length, 1);
  blue.value = '25';
  harness.context.localCameraImageInput(blue);
  const debounce = harness.timers.filter(timer => !timer.cleared).at(-1);
  await debounce.fn();
  assert.equal(harness.requests.length, 1, 'The second value waits for the running save');
  release();
  await first;
  assert.equal(harness.requests.length, 2);
  assert.equal(harness.requests[1].options.body, 'blue=25');
  assert.equal(blue.value, '25');
  assert.equal(blue.dataset.saved, '25');
}
{
  // A status refresh (reload, other setting) shows the stored values but
  // never overwrites a value that has not been sent yet.
  const harness = createHarness(async () => statusWith({...defaults, brightness: -20, red: 12}));
  const red = harness.slider('red');
  red.value = '40';
  harness.context.localCameraImageInput(red);
  await harness.context.refreshLocalCameraStatus();
  assert.equal(harness.slider('brightness').value, '-20');
  assert.equal(harness.output('brightness'), '-20');
  assert.equal(red.value, '40', 'The pending drag value stays');
  assert.equal(red.dataset.saved, '12');
}
{
  // Unknown keys and non-numeric values are ignored.
  const harness = createHarness(async () => statusWith(defaults));
  harness.context.localCameraImageChange({value: '5', dataset: {imageKey: 'gamma'}});
  const brightness = harness.slider('brightness');
  brightness.value = 'abc';
  harness.context.localCameraImageChange(brightness);
  assert.equal(harness.requests.length, 0);
}

console.log('Local camera image controls: rendering, endpoint, persistence, live application and browser saves passed.');
