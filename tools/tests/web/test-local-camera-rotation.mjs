// Built-in camera rotation and red/blue swap: own NVS keys with validated
// values, the rotation split (180 degrees as sensor flips, the remaining
// quarter turn announced as "rotate" for the Bridge) for every board and user
// combination, the live status republish, the red/blue swap as the Bayer
// counterpart at the next pipeline build, the collapsed Advanced block of the
// Web Admin with its browser saves, and the translations of every language.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';

import {readAdminDeliverySource, readRepoFile, repoRoot} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const lf = text => text.replaceAll('\r\n', '\n');
const service = lf(readRepoFile('src/video/local_camera/local_camera.cpp'));
const publicHeader = lf(readRepoFile('src/video/local_camera/local_camera.h'));
const contract = lf(readRepoFile('src/video/local_camera/local_camera_contract.h'));
const handler = lf(readRepoFile('src/web/server/handlers/web_admin_local_camera.cpp'));
const html = lf(readRepoFile('src/web/server/render/web_admin_html.cpp'));
const header = lf(readRepoFile('src/core/i18n/i18n.h'));
const i18n = lf(readRepoFile('src/core/i18n/i18n.cpp'));
const css = lf(readRepoFile('src/web/assets/admin.css'));

const definitions = cppFunctionDefinitions(service);
const svc = name => {
  const found = definitions.find(item => item.name === name);
  assert.ok(found, `${name} must exist in local_camera.cpp`);
  return found;
};

// --- Persistence: own keys, validated values ----------------------------------------
assert.match(service, /constexpr char kPrefsRotationKey\[\] = "lcam_rot";/);
assert.match(service, /constexpr char kPrefsRbSwapKey\[\] = "lcam_rbswap";/);
{
  const keys = [...service.matchAll(/constexpr char kPrefs\w+Key\[\] = "([^"]+)";/g)].map(m => m[1]);
  assert.equal(new Set(keys).size, keys.length, 'Camera NVS keys must be unique');
  for (const key of keys) assert.ok(key.length <= 15, `NVS key ${key} must fit 15 characters`);
}
assert.match(contract, /constexpr uint8_t kRotationMax = 3;/);
const begin = svc('begin').body;
assert.match(begin, /const uint8_t rotation = prefs\.getUChar\(kPrefsRotationKey, 0\);\s*g_rotation\.store\(rotation <= kRotationMax \? rotation : 0\);/,
  'A missing key is 0; an unknown stored value falls back to 0');
assert.match(begin, /g_rb_swap\.store\(prefs\.getBool\(kPrefsRbSwapKey, false\)\);/, 'Red/blue swap is off by default');
const setRotation = svc('setRotation').body;
assert.match(setRotation, /if \(quarter_turns > kRotationMax\) return false;/, 'Only 0..3 quarter turns');
assert.match(setRotation, /prefs\.putUChar\(kPrefsRotationKey, quarter_turns\)/);
assert.match(setRotation, /BatchedNvsWrite::finish\(prefs\) \|\| !written\) \{[\s\S]*?return false;[\s\S]*?g_rotation\.store\(quarter_turns\);/,
  'Only a successful save changes the live value');
// Live: the retained status is republished with the new "rotate"; the worker
// reads the value for every orientation check (per stream frame).
assert.match(setRotation, /g_rotation\.store\(quarter_turns\);[\s\S]*?publishStatus\(\);\s*return true;/,
  'A rotation change republishes the retained status');
assert.match(svc('publishStatus').body, /buildStatusJson\(payload, sizeof\(payload\), currentStatusFields\(\)\)/);
assert.match(svc('currentStatusFields').body, /fields\.rotate = statusRotate\(\);/);
assert.match(svc('statusRotate').body,
  /statusRotateDegrees\(\s*imageTurn\(false, local_camera_board::kMode\.quarter_turn, g_rotation\.load\(\)\)\)/);
assert.match(svc('applyOrientation').body,
  /const ImageTurn turn = imageTurn\(imageRotated180\(\), kQuarterTurn, g_rotation\.load\(\)\);\s*const SensorOrientation wanted =\s*desiredOrientation\(turn\.rotated_180, g_mirror\.load\(\), turn\.quarter_turn\);/,
  'The 180 degree part and the mirror become sensor flips');
{
  const run = svc('runStream').body;
  const loop = run.slice(run.indexOf('for (;;) {'));
  assert.match(loop.slice(0, loop.indexOf('streamCaptureFrame(run);')), /if \(!applyOrientation\(true\)\)/,
    'A running stream turns the sensor readout before its next frame');
}
const setSwap = svc('setRedBlueSwap').body;
assert.match(setSwap, /prefs\.putBool\(kPrefsRbSwapKey, swap\)/);
assert.match(setSwap, /BatchedNvsWrite::finish\(prefs\) \|\| !written\) \{[\s\S]*?return false;[\s\S]*?g_rb_swap\.store\(swap\);/);
assert.match(publicHeader, /bool setRotation\(uint8_t quarter_turns\);/);
assert.match(publicHeader, /bool setRedBlueSwap\(bool swap\);/);
// Status JSON for the Web Admin.
const statusJson = svc('appendStatusJson').body;
assert.match(statusJson, /json \+= ",\\"rotation\\":";\s*json \+= String\(static_cast<unsigned>\(g_rotation\.load\(\)\)\);/);
assert.match(statusJson, /json \+= ",\\"rb_swap\\":";\s*json \+= g_rb_swap\.load\(\) \? "true" : "false";/);
// The picture size Home Assistant shows follows the Bridge turn.
assert.match(svc('imageWidth').body, /statusRotate\(\) != 0 \? kImageHeight : kImageWidth/);
assert.match(svc('imageHeight').body, /statusRotate\(\) != 0 \? kImageWidth : kImageHeight/);

// --- Red/blue swap: Bayer counterpart when the pipeline is built ------------------
const pipeline = svc('ensurePipeline').body;
const boardOrderAt = pipeline.indexOf('isp_config.bayer_order = kMode.bayer_order;');
const swapAt = pipeline.indexOf('if (g_rb_swap.load()) isp_config.bayer_order = redBlueSwapped(isp_config.bayer_order);');
assert.ok(boardOrderAt > 0 && swapAt > boardOrderAt &&
  swapAt < pipeline.indexOf('esp_isp_new_processor(&isp_config, &g_pipe.isp)'),
  'The ISP processor is created with the swapped board order');
for (const name of ['captureJpeg', 'runStream']) {
  assert.match(svc(name).body, /releaseUsedPipeline\(\);\s*if \(!ensurePipeline\(\)/,
    `${name} builds a fresh pipeline, so a changed swap applies from the next start`);
}

// --- Web Admin endpoint: validated before anything is saved ----------------------
const post = handler.slice(handler.indexOf('if (server.method() == HTTP_POST) {'));
assert.match(post, /const ImageArgs rotation_arg = readImageArg\(\s*server, "rotation", 0, local_camera_contract::kRotationMax, &rotation\);/);
assert.match(post, /const bool has_rb_swap = server\.hasArg\("rb_swap"\);/);
assert.match(post, /if \(has_rb_swap && !readSwitchArg\(server, "rb_swap", &rb_swap\)\)/);
assert.match(handler, /!local_camera_contract::parseImageInteger\(text\.c_str\(\), &parsed\) \|\|\s*parsed < min_value \|\| parsed > max_value/,
  'readImageArg accepts strict integers inside the range only');
{
  const firstSave = Math.min(...['local_camera::setImageSettings(', 'local_camera::setCustomMode(',
    'local_camera::setStreamMode(', 'local_camera::setMirror(', 'local_camera::setRotation(',
    'local_camera::setRedBlueSwap(', 'local_camera::setIndicatorStyle(', 'local_camera::setEnabled(']
    .map(call => {
      const at = post.indexOf(call);
      assert.ok(at > 0, `${call} is present`);
      return at;
    }));
  for (const message of ['"Invalid rotation value"', '"Invalid red/blue swap value"', '"Missing enabled value"']) {
    const at = post.indexOf(message);
    assert.ok(at > 0 && at < firstSave, `${message} is rejected before any setting is saved`);
  }
}
assert.match(post, /has_rotation && !local_camera::setRotation\(static_cast<uint8_t>\(rotation\)\)/);
assert.match(post, /has_rb_swap && !local_camera::setRedBlueSwap\(rb_swap\)/);

// --- Host: rotation split, status field, validation and the Bayer swap ------------
function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const redBlueSwapped = svc('redBlueSwapped').source;
const hostProgram = String.raw`
#include "src/video/local_camera/local_camera_contract.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace local_camera_contract;

// The ESP-IDF enum (hal/color_types.h) for the pasted firmware function.
typedef enum {
  COLOR_RAW_ELEMENT_ORDER_BGGR,
  COLOR_RAW_ELEMENT_ORDER_GBRG,
  COLOR_RAW_ELEMENT_ORDER_GRBG,
  COLOR_RAW_ELEMENT_ORDER_RGGB,
} color_raw_element_order_t;

${redBlueSwapped}

static const char* pattern(color_raw_element_order_t order) {
  switch (order) {
    case COLOR_RAW_ELEMENT_ORDER_BGGR: return "BGGR";
    case COLOR_RAW_ELEMENT_ORDER_GBRG: return "GBRG";
    case COLOR_RAW_ELEMENT_ORDER_GRBG: return "GRBG";
    case COLOR_RAW_ELEMENT_ORDER_RGGB: return "RGGB";
  }
  return "";
}

struct Image {
  int w;
  int h;
  std::vector<int> px;
  int at(int x, int y) const { return px[static_cast<size_t>(y * w + x)]; }
};

static Image turnCw(const Image& in) {
  Image out{in.h, in.w, std::vector<int>(in.px.size())};
  for (int y = 0; y < out.h; ++y) {
    for (int x = 0; x < out.w; ++x) out.px[static_cast<size_t>(y * out.w + x)] = in.at(y, in.h - 1 - x);
  }
  return out;
}

static Image turnCw(Image image, int quarter_turns) {
  for (int i = 0; i < (quarter_turns & 3); ++i) image = turnCw(image);
  return image;
}

static Image mirrored(const Image& in) {
  Image out = in;
  for (int y = 0; y < in.h; ++y) {
    for (int x = 0; x < in.w; ++x) out.px[static_cast<size_t>(y * in.w + x)] = in.at(in.w - 1 - x, y);
  }
  return out;
}

// Sensor readout of the unflipped frame S with the mirror/flip registers.
static Image readout(const Image& s, const SensorOrientation& o) {
  Image out = s;
  for (int y = 0; y < s.h; ++y) {
    for (int x = 0; x < s.w; ++x) {
      out.px[static_cast<size_t>(y * s.w + x)] = s.at(o.mirror ? s.w - 1 - x : x, o.flip ? s.h - 1 - y : y);
    }
  }
  return out;
}

static bool same(const Image& a, const Image& b) {
  return a.w == b.w && a.h == b.h && a.px == b.px;
}

int main() {
  Image sensor{5, 3, {}};
  for (int i = 0; i < sensor.w * sensor.h; ++i) sensor.px.push_back(i);

  for (int quarter = 0; quarter < 2; ++quarter) {
    for (int rotated = 0; rotated < 2; ++rotated) {
      for (int user = 0; user <= kRotationMax; ++user) {
        for (int mirror = 0; mirror < 2; ++mirror) {
          const ImageTurn turn = imageTurn(rotated != 0, quarter != 0, static_cast<uint8_t>(user));
          const uint16_t rotate = statusRotateDegrees(turn);
          // Only 0 or 90 reaches the Bridge: 180 degrees are sensor flips.
          assert(rotate == 0 || rotate == 90);
          const SensorOrientation o = desiredOrientation(turn.rotated_180, mirror != 0, turn.quarter_turn);
          // Delivered: the flipped readout, turned by the Bridge.
          const Image delivered = turnCw(readout(sensor, o), rotate / 90);
          // Wanted: the board's default picture, turned clockwise by the user
          // rotation, then mirrored like the displayed image.
          Image wanted = turnCw(turnCw(sensor, quarter + 2 * rotated), user);
          if (mirror) wanted = mirrored(wanted);
          assert(same(delivered, wanted));
          if (user == 0) {
            // No user rotation: exactly the former orientation and status.
            const SensorOrientation before = desiredOrientation(rotated != 0, mirror != 0, quarter != 0);
            assert(orientationCode(o) == orientationCode(before));
            assert(rotate == (quarter ? 90 : 0));
          }
          if (rotated == 0 && mirror == 0) {
            StatusFields fields;
            fields.width = 1280;
            fields.height = 720;
            fields.rotate = rotate;
            char json[288];
            assert(buildStatusJson(json, sizeof(json), fields) > 0);
            std::printf("status quarter_turn=%d user=%d %s\n", quarter, user,
                        std::strstr(json, ",\"rotate\":90") ? "rotate=90" : "no-rotate");
          }
          // The display rotation (180 degree steps) never changes the status.
          assert(statusRotateDegrees(imageTurn(rotated == 0, quarter != 0, static_cast<uint8_t>(user))) == rotate);
        }
      }
    }
  }

  // Web Admin rotation argument: a strict integer within 0..kRotationMax.
  const char* inputs[] = {"0", "1", "2", "3", "4", "-1", "", "abc", "1.5", "90", "270", "2x"};
  for (const char* text : inputs) {
    long value = -1;
    const bool accepted = parseImageInteger(text, &value) && value >= 0 && value <= kRotationMax;
    std::printf("rotation \"%s\" %s\n", text, accepted ? "accepted" : "rejected");
  }

  // Red/blue swap: the counterpart keeps the green sites and exchanges R and B.
  const color_raw_element_order_t orders[] = {COLOR_RAW_ELEMENT_ORDER_BGGR, COLOR_RAW_ELEMENT_ORDER_GBRG,
                                              COLOR_RAW_ELEMENT_ORDER_GRBG, COLOR_RAW_ELEMENT_ORDER_RGGB};
  for (const color_raw_element_order_t order : orders) {
    const color_raw_element_order_t swapped = redBlueSwapped(order);
    assert(redBlueSwapped(swapped) == order);
    std::printf("bayer %s -> %s\n", pattern(order), pattern(swapped));
  }
  return 0;
}
`;

const compiler = findCompiler();
let hostChecked = false;
if (compiler) {
  const out = path.join(repoRoot, 'build/tests/local-camera-rotation');
  fs.mkdirSync(out, {recursive: true});
  const cpp = path.join(out, 'rotation.cpp');
  const exe = path.join(out, process.platform === 'win32' ? 'rotation.exe' : 'rotation');
  fs.writeFileSync(cpp, hostProgram);
  const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', repoRoot, cpp, '-o', exe],
    {encoding: 'utf8'});
  assert.equal(build.status, 0, `rotation host program did not compile:\n${build.stdout}${build.stderr}`);
  const run = spawnSync(exe, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, `rotation host program failed:\n${run.stdout}${run.stderr}`);
  const lines = run.stdout.trim().split(/\r?\n/);
  // "rotate" for every board mounting and user rotation.
  assert.deepEqual(lines.filter(line => line.startsWith('status ')), [
    'status quarter_turn=0 user=0 no-rotate',
    'status quarter_turn=0 user=1 rotate=90',
    'status quarter_turn=0 user=2 no-rotate',
    'status quarter_turn=0 user=3 rotate=90',
    'status quarter_turn=1 user=0 rotate=90',
    'status quarter_turn=1 user=1 no-rotate',
    'status quarter_turn=1 user=2 rotate=90',
    'status quarter_turn=1 user=3 no-rotate',
  ]);
  assert.deepEqual(lines.filter(line => line.startsWith('rotation ')), [
    'rotation "0" accepted', 'rotation "1" accepted', 'rotation "2" accepted', 'rotation "3" accepted',
    'rotation "4" rejected', 'rotation "-1" rejected', 'rotation "" rejected', 'rotation "abc" rejected',
    'rotation "1.5" rejected', 'rotation "90" rejected', 'rotation "270" rejected', 'rotation "2x" rejected',
  ]);
  const bayer = Object.fromEntries(lines.filter(line => line.startsWith('bayer '))
    .map(line => line.slice(6).split(' -> ')));
  assert.deepEqual(bayer, {BGGR: 'RGGB', GBRG: 'GRBG', GRBG: 'GBRG', RGGB: 'BGGR'});
  const exchange = text => text.replace(/[RB]/g, c => (c === 'R' ? 'B' : 'R'));
  for (const [order, swapped] of Object.entries(bayer)) {
    assert.equal(swapped, exchange(order), `${order}: green sites stay, red and blue exchange`);
  }
  hostChecked = true;
}

// --- Web Admin markup: essential controls visible, fine-tuning collapsed ----------
const helperStart = html.indexOf('static void appendLocalCameraSettingsHtml(');
const helper = html.slice(helperStart, html.indexOf('\n}\n', helperStart));
const advancedStart = helper.indexOf('<details class="settings-full local-camera-advanced" id="local_camera_advanced">');
const advancedEnd = helper.indexOf('</details>');
assert.ok(advancedStart > 0 && advancedEnd > advancedStart, 'One Advanced block');
assert.doesNotMatch(helper.slice(advancedStart, helper.indexOf('>', advancedStart) + 1), /\bopen\b/,
  'The Advanced block is collapsed by default');
assert.match(helper.slice(advancedStart), /^<details[^\n]*\n\s*<summary class="network-settings-heading">\)html";[\s\S]*?appendHtmlEscaped\(html, tr\.local_camera_advanced\);\s*html \+= R"html\(<\/summary>/);
const inside = helper.slice(advancedStart, advancedEnd);
const outside = helper.slice(0, advancedStart) + helper.slice(advancedEnd);
for (const id of ['local_camera_enabled', 'local_camera_status', 'local_camera_stream_mode',
  'local_camera_custom', 'local_camera_indicator_line', 'local_camera_indicator_pill']) {
  assert.ok(outside.includes(`id="${id}"`) && !inside.includes(`id="${id}"`), `${id} stays visible`);
}
for (const id of ['local_camera_rotation', 'local_camera_mirror', 'local_camera_rb_swap',
  'local_camera_image', 'local_camera_image_reset']) {
  assert.ok(inside.includes(`id="${id}"`) && !outside.includes(`id="${id}"`), `${id} is in the Advanced block`);
}
assert.match(inside, /<select id="local_camera_rotation" onchange="saveLocalCameraRotation\(this\.value\)">\)html";/);
assert.match(inside, /const uint8_t rotation = local_camera::rotation\(\);\s*for \(uint8_t turns = 0; turns <= local_camera_contract::kRotationMax; \+\+turns\) \{[\s\S]*?html \+= String\(static_cast<unsigned>\(turns\)\);[\s\S]*?if \(turns == rotation\) html \+= " selected";[\s\S]*?html \+= String\(static_cast<unsigned>\(turns\) \* 90u\);\s*html \+= "\\xC2\\xB0<\/option>";/,
  'Options 0..3 show untranslated clockwise degrees, the stored one selected');
assert.match(inside, /appendHtmlEscaped\(html, tr\.local_camera_rotation\);\s*html \+= R"html\(:<\/label>/);
assert.match(inside, /<input type="checkbox" id="local_camera_rb_swap" onchange="saveLocalCameraRbSwap\(this\.checked\)"\)html";\s*if \(local_camera::redBlueSwap\(\)\) html \+= " checked";/);
for (const key of ['local_camera_rb_swap', 'local_camera_rb_swap_note', 'local_camera_mirror']) {
  assert.ok(inside.includes(`appendHtmlEscaped(html, tr.${key});`), `The Advanced block renders tr.${key}`);
}
const markup = [...helper.matchAll(/R"html\(([\s\S]*?)\)html"/g)].map(match => match[1]).join('');
assert.doesNotMatch(markup, /id="local_camera_(rotation|rb_swap)"[^>]*\bname=/,
  'The controls must not be submitted with the /mqtt settings form');
assert.match(css, /\.local-camera-advanced > summary \{[^}]*cursor:pointer;[^}]*list-style:none;/);
assert.match(css, /\.local-camera-advanced > summary::-webkit-details-marker \{ display:none; \}/);
assert.match(css, /\.local-camera-advanced\[open\] > summary::before \{ transform:rotate\(45deg\); \}/);

// --- Translations: every language table ends with the new texts ------------------
const newKeys = ['local_camera_advanced', 'local_camera_rotation', 'local_camera_rb_swap',
  'local_camera_rb_swap_note'];
{
  const members = [...header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')))
    .matchAll(/^\s*const char\* (\w+);/gm)].map(match => match[1]);
  assert.deepEqual(members.slice(-newKeys.length), newKeys, 'The new members close i18n::Strings');
}
const expected = {
  kStringsEn: ['Advanced', 'Rotation', 'Swap red and blue'],
  kStringsDe: ['Erweitert', 'Drehung', 'Rot und Blau tauschen'],
  kStringsFr: ['Avancé', 'Rotation', 'Inverser le rouge et le bleu'],
};
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.deepEqual([...tables].sort(), Object.keys(expected).sort(), 'Every registered language is checked');
const tails = {};
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  assert.notEqual(start, -1, table);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  tails[table] = values.slice(-newKeys.length);
  assert.deepEqual(tails[table].slice(0, 3), expected[table], `${table} labels`);
  assert.ok(tails[table][3].length > 20, `${table} has the red/blue swap note`);
}
assert.notEqual(tails.kStringsDe[3], tails.kStringsEn[3], 'German note must be translated');
assert.notEqual(tails.kStringsFr[3], tails.kStringsEn[3], 'French note must be translated');

// --- Delivered browser code ----------------------------------------------------------
const delivered = readAdminDeliverySource();
const moduleStart = delivered.indexOf('let localCameraSaveSequence');
const moduleCode = delivered.slice(moduleStart, delivered.indexOf('document.addEventListener', moduleStart));
assert.doesNotMatch(moduleCode, /'[A-Z][a-z]+ [a-z]+/, 'Browser code must not contain display sentences');

function createHarness(fetchImpl) {
  const elements = new Map([
    ['local_camera_status', {dataset: {label: 'Status', stateReady: 'Sensor erkannt'}, textContent: ''}],
    ['local_camera_rotation', {value: '0', dataset: {saved: '0'}}],
    ['local_camera_rb_swap', {checked: false}],
    ['local_camera_mirror', {checked: false}],
  ]);
  const requests = [];
  const notifications = [];
  const context = {
    document: {getElementById: id => elements.get(id) || null, addEventListener: () => {}},
    fetch: async (url, options = {}) => {
      requests.push({url, options});
      return fetchImpl(url, options);
    },
    setTimeout: () => 0,
    clearTimeout: () => {},
    showNotification: (message, ok) => notifications.push({message, ok}),
    t: key => `t:${key}`,
  };
  vm.createContext(context);
  vm.runInContext(moduleCode, context);
  return {context, requests, notifications, element: id => elements.get(id)};
}
const json = body => ({ok: true, status: 200, json: async () => body});
const failed = async () => ({ok: false, status: 400, json: async () => ({})});

{
  const harness = createHarness(async () => json({supported: true, enabled: true, state: 'ready', rotation: 2}));
  harness.element('local_camera_rotation').value = '2';
  await harness.context.saveLocalCameraRotation('2');
  assert.equal(harness.requests[0].url, '/api/local-camera');
  assert.equal(harness.requests[0].options.method, 'POST');
  assert.equal(harness.requests[0].options.headers['Content-Type'], 'application/x-www-form-urlencoded');
  assert.equal(harness.requests[0].options.body, 'rotation=2');
  assert.equal(harness.element('local_camera_rotation').value, '2');
  assert.equal(harness.element('local_camera_rotation').dataset.saved, '2');
}
{
  const harness = createHarness(failed);
  harness.element('local_camera_rotation').value = '3';
  await harness.context.saveLocalCameraRotation('3');
  assert.equal(harness.element('local_camera_rotation').value, '0', 'A failed save restores the saved rotation');
  assert.deepEqual(harness.notifications, [{message: 't:networkErrorSave', ok: false}]);
}
{
  const harness = createHarness(async () => json({supported: true, enabled: true, state: 'ready', rb_swap: true}));
  await harness.context.saveLocalCameraRbSwap(true);
  assert.equal(harness.requests[0].options.body, 'rb_swap=1');
  assert.equal(harness.element('local_camera_rb_swap').checked, true);
}
{
  const harness = createHarness(failed);
  harness.element('local_camera_rb_swap').checked = true;
  await harness.context.saveLocalCameraRbSwap(true);
  assert.equal(harness.requests[0].options.body, 'rb_swap=1');
  assert.equal(harness.element('local_camera_rb_swap').checked, false, 'A failed save restores the checkbox');
  assert.equal(harness.notifications.length, 1);
}
{
  // A status refresh (reload, another setting) shows the stored values.
  const harness = createHarness(async () => json({
    supported: true, enabled: true, state: 'ready', rotation: 3, rb_swap: true, mirror: true}));
  await harness.context.refreshLocalCameraStatus();
  assert.equal(harness.element('local_camera_rotation').value, '3');
  assert.equal(harness.element('local_camera_rotation').dataset.saved, '3');
  assert.equal(harness.element('local_camera_rb_swap').checked, true);
  assert.equal(harness.element('local_camera_mirror').checked, true);
}
{
  const harness = createHarness(async () => json({supported: true, enabled: true, state: 'ready', rotation: 'x'}));
  await harness.context.refreshLocalCameraStatus();
  assert.equal(harness.element('local_camera_rotation').value, '0', 'Non-integer values are ignored');
}

if (!hostChecked) console.log('SKIP: rotation and Bayer host checks need a host C++ compiler');
console.log('Local camera rotation, red/blue swap, Advanced block, translations and browser saves passed.');
