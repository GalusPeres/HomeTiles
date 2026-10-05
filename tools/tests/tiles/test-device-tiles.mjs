// Lock (24), Alarm panel (25) and Fan (26) tiles (types/device): the Bridge's
// detail state is parsed defensively and keeps a missing state apart from a
// known one, the security rules of bridge-contract.md hold (sealed Lock and
// Alarm commands that state the Web Admin password, a 15 s deadline, no code
// kept), every text comes from the central translations, and the types are
// wired into every list a tile type needs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// Persisted IDs and policies.
const types = read('src/types/tile_type.h');
for (const [name, id] of [['LOCK', 24], ['ALARM', 25], ['FAN', 26]]) {
  assert.match(types, new RegExp(`TILE_${name} = ${id}`), `TILE_${name} keeps ID ${id}`);
}
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /tileTypeIsDeviceControl\(int type\) \{\s*return type == TILE_LOCK \|\| type == TILE_ALARM \|\| type == TILE_FAN;/);

// Security: Lock and Alarm leave the panel sealed; the body states the
// password and a deadline of at most 15 s; the plaintext body is wiped.
assert.match(read('src/network/secure/command_channel_core.h'), /"value", "fan", "lock", "alarm"\}/);
assert.match(read('docs-dev/command-encryption.md'), /`value`, `fan`, `lock`, `alarm`/);
const control = read('src/types/device/device_control.cpp');
for (const marker of [
  'command_channel::state() == command_channel::PairingState::Active && web_admin_auth::enabled()',
  'doc["deadline"] = static_cast<uint64_t>(now + 15);',
  'doc["web_auth"] = web_admin_auth::enabled();',
  'if (code && *code) doc["code"] = code;',
  "for (size_t i = 0; i < body.length(); ++i) body.setCharAt(i, '\\0');",
  'if (now < 1700000000)',
  // Only answers to this panel's own commands count.
  'if (!flight.id.length() || flight.id != id) continue;',
  'constexpr uint32_t kAnswerMs = 15000;',
  'constexpr uint32_t kPendingMs = 10000;',
]) assert.ok(control.includes(marker), `device_control: ${marker}`);
const popup = read('src/ui/popups/device/device_popup.cpp');
for (const marker of [
  'pin_access::secureClear(g_code.code, sizeof(g_code.code))',
  'init.back = !from_tile;',
  'init.dismissed = code_dismissed;',
  'if (!std::strcmp(status, "locked_out") || retry_after > 0)',
  'popup_shell_pulse_icon(pop.card',
  'viewNavigationPopupShown(pop.card',
  'hide_camera_popup();',
  // An open code entry follows its device (header, keys) and pulses while
  // its code waits for the answer.
  'init.context = &g_code;',
  'if (is_pin_popup_for(&g_code) && (entity == "*" || g_code.target.entity == entity)) {',
  'pin_popup_pulse_icon(true);',
]) assert.ok(popup.includes(marker), `device_popup: ${marker}`);
const pin = read('src/ui/popups/pin/pin_popup.cpp');
assert.match(pin, /void pin_popup_set_state\([^)]*\) \{[\s\S]*?style_keypad\(g_ctx\);\s*sync_popup_shell\(\);/);
assert.match(pin, /void resume_pin_popup_after_failed_success\(\) \{[\s\S]*?popup_shell_pulse_icon\(g_ctx->card, false\);/);

// The MQTT routes: the additive detail topic and the Bridge's answers.
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(mqtt, /tileTypeIsDeviceControl\(slot\.type\) \? "detail"/);
assert.match(mqtt, /route\.topic\.endsWith\("\/detail"\)\) \{\s*device_control::queue_detail/);
// Like Number/Select/Date, every offered Lock, Alarm panel and Fan keeps its
// detail subscribed, not only those on a tile (a newly chosen one showed
// its state only seconds after the save).
for (const list of ['locks_text', 'alarm_panels_text', 'fans_text']) {
  assert.ok(mqtt.includes(`{&cfg.${list}, "detail"}`), `${list} detail is subscribed up front`);
}
// The answers are read into the large buffer: the Bridge's answer for the
// sim alarm panel (V2, 02.10.) outgrew the small one, was cut off and the
// code entry waited for "No answer" although the alarm had armed.
assert.match(mqtt, /\{TopicKey::LOCK_STAT, handleLockResult, true\}/);
assert.match(mqtt, /\{TopicKey::ALARM_STAT, handleAlarmResult, true\}/);
const smallBuf = Number(mqtt.match(/SMALL_BUF = (\d+);/)[1]);
const largeBuf = Number(mqtt.match(/LARGE_BUF = (\d+);/)[1]);
const answer = entity => JSON.stringify({entity_id: entity, id: '0123456789abcdef', status: 'locked_out', retry_after: 3600});
assert.ok(answer('alarm_control_panel.hometiles_sim_alarmanlage').length >= smallBuf, 'the reported answer needs more than SMALL_BUF');
assert.ok(answer(`alarm_control_panel.${'x'.repeat(235)}`).length < Math.min(largeBuf, 512), 'the longest answer fits');
assert.ok(control.includes('[Device] Malformed Bridge answer ignored'), 'a dropped answer is logged');
assert.match(read('src/network/mqtt/mqtt_topics.cpp'), /\{TopicKey::LOCK_STAT, TopicDomain::State, "lock"\}/);

// Every other popup hides the device popup; the lifecycle knows it.
for (const type of ['camera', 'climate', 'cover', 'energy', 'light', 'media', 'sensor', 'weather', 'pin']) {
  assert.match(read(`src/ui/popups/${type}/${type}_popup.cpp`), /hide_device_popup\(\);/, `${type} hides the device popup`);
}
assert.match(read('src/ui/navigation/view_navigation.cpp'), /hide_device_popup\(\);\n\}/);
assert.match(read('src/ui/navigation/view_navigation.cpp'), /case TILE_LOCK: case TILE_ALARM: case TILE_FAN: return true;/);
assert.match(read('src/ui/ui_manager.cpp'), /preload_device_popup\(\);/);
assert.match(read('src/tiles/runtime/tile_icon_source.cpp'), /device_popup_follow_tile_color\(color\);/);
assert.match(read('src/tiles/runtime/tile_update_service.h'), /process_device_updates\(drain_all \? 0 : 4\);/);

// The card's other gesture switches a Fan or a Lock like the Switch tile;
// the Alarm panel has none (user 02.10.).
const tile = read('src/types/device/device_tile.cpp');
assert.match(tile, /if \(view->type != TILE_ALARM\) \{\s*lv_obj_add_event_cb\(card, on_card_toggle,\s*popup_event == LV_EVENT_SHORT_CLICKED \? LV_EVENT_LONG_PRESSED : LV_EVENT_SHORT_CLICKED/);
assert.match(tile, /if \(\*target && std::strcmp\(target, d\.state\) != 0\) return;\s*device_request\(popup_target\(view\), device_visual::lock_on\(d\) \? "unlock" : "lock", true\);/);

// The lock shows only reported states (user 02.10.: a code the lock itself
// rejected, answered ok by Home Assistant, left an orange thumb on the
// target over the red lock as if it had worked). A sent command waits with
// the icon pulsing; a target that runs out unanswered says so in the popup.
assert.ok(!tile.includes('thumb_sent'), 'the tile bar draws no sent target');
assert.ok(!tile.includes('slot_sent'), 'the alarm bar lights no sent mode');

// Every key, pill and switch shows a press one control step up, like the PIN
// keys (user 02.10.: none showed a press).
assert.match(popup, /void control_pressed\(lv_obj_t\* obj, bool lit, lv_color_t lit_color\) \{[\s\S]*?popup_nav_style::fill_raised\(/);
for (const call of ['control_pressed(button, lit, bg);', 'control_pressed(key, selected, bg);',
                    'control_pressed(power, on, accent);', 'control_pressed(item, false, fill);',
                    'control_pressed(track, false, rest);']) {
  assert.ok(popup.includes(call), `device_popup: ${call}`);
}
assert.ok(!/set_bg\((?:button|key|power|item), [^;]*LV_STATE_PRESSED\)/.test(popup), 'no press in the rest color');
assert.match(tile, /int8_t pressed_part_at\(const View\* view\)/);
assert.match(tile, /i == view->pressed_part \? pressed_button_rgb\(base\) : button_rgb\(base\)/);
assert.match(tile, /if \(view->pressed_part == 0\) level_bar::draw_rect\(layer, area, lv_color_hex\(button_rgb\(base\)\), radius\);/);
assert.match(popup, /const char\* shown = d\.state;\s*const uint32_t lit = v\.color;/);
assert.match(popup, /const bool busy = device_visual::alarm_disarm_only\(d\) \|\| device_detail::is\(d, "disarming"\);/);
assert.match(popup, /const bool up = device_visual::lock_on\(d\);\s*const uint32_t color = v\.color;/);
assert.match(popup, /make_icon\(thumb, lv_color_hex\(pop\.card_rgb\), v\.icon\);\s*lv_obj_center\(symbol\);\s*if \(moving\) pulse\(symbol\);/);
assert.match(control, /pending = Pending\{\};\s*device_popup_on_no_reaction\(entity\);/);
assert.match(popup, /void device_popup_on_no_reaction\(const String& entity\) \{[\s\S]*?set_notice\(text\(DeviceLabel::NoReaction\)\);/);

// Every pulse reads one clock (user 02.10.: the popup's middle and its
// header pulsed apart): none keeps an eased animation of its own.
for (const file of ['src/types/device/device_tile.cpp', 'src/ui/popups/device/device_popup.cpp',
                    'src/ui/popups/popup_shell.cpp']) {
  const text = read(file);
  assert.ok(text.includes('ui_pulse::opa_now()'), `${file} pulses from the shared clock`);
  assert.ok(!/lv_anim_set_values\(&\w+, LV_OPA_COVER, LV_OPA_TRANSP\)/.test(text), `${file} keeps no clock of its own`);
}
assert.match(read('src/ui/shared/ui_pulse.h'), /lv_tick_get\(\) % \(2 \* kHalfMs\)/);

// Registry and Web Admin.
const registry = read('src/types/types_registry.cpp');
for (const [type, prefix] of [['LOCK', 'Lock'], ['ALARM', 'Alarm'], ['FAN', 'Fan']]) {
  assert.match(registry, new RegExp(`TILE_${type},[\\s\\S]{0,80}"device",[\\s\\S]{0,40}"device",\\s*nullptr,\\s*"load${prefix}Fields"`));
  assert.match(registry, new RegExp(`case TILE_${type}: return i18n::device_label\\(language, i18n::DeviceLabel::Type`));
}
const admin = read('src/types/device/admin.js');
for (const fn of ['Lock', 'Alarm', 'Fan']) {
  for (const verb of ['load', 'save', 'reset']) assert.match(admin, new RegExp(`function ${verb}${fn}Fields\\(`));
}
assert.match(read('src/web/admin/bundle.json'), /"src\/types\/device\/admin\.js"/);
// The preview counts the alarm slots again when the bar's width changes
// (V2 02.10.: a 2x1 tile kept the three slots of 1x1 and lit none).
assert.match(admin, /onResize = \(\) => \{\s*if \(fitNow\(\) !== fit\) applyDevicePreview\(tileElem, kind, d, halfHeight\);/);
assert.match(admin, /bar\.__onResize = onResize;/);
assert.match(read('src/types/switch/admin.js'), /if \(typeof bar\.__onResize === 'function'\) bar\.__onResize\(\);\s*drawSwitchPreviewFill\(bar\);/);
assert.match(read('src/web/server/handlers/web_admin_tiles.cpp'), /\\"device_values\\"/);
// The entity field is the shared picker of the Bridge's released list.
assert.match(read('src/types/device/web.cpp'),
  /appendEntityPickerField\(html, tab_id, entity_field\.c_str\(\), entity_label\(type\),\s*type == TILE_LOCK \? "locks" : type == TILE_ALARM \? "alarm_panels" : "fans"\);/);
for (const key of ['locks', 'alarm_panels', 'fans']) {
  assert.ok(read('src/web/server/handlers/web_admin_tiles.cpp').includes(`appendList("${key}"`), key);
}

// Translations: one label per DeviceLabel in every language; the states in
// the tables' documented order.
const header = read('src/core/i18n/i18n.h');
const labelEnum = header.match(/enum class DeviceLabel : uint8_t \{([\s\S]*?)\n\};/)[1];
const labels = labelEnum.split('\n').map(line => line.replace(/\/\/.*$/, '').trim().replace(/,$/, '')).filter(Boolean);
assert.equal(labels.at(-1), 'Count');
assert.match(header, new RegExp(`const char\\* device_labels\\[${labels.length - 1}\\];`));
const i18n = read('src/core/i18n/i18n.cpp');
for (const [lang, first, lock, alarm] of [
  ['de', '"Schloss", "Alarmanlage", "Ventilator"', '"Verriegelt", "Entriegelt"', '"Unscharf", "Scharf zu Hause"'],
  ['en', '"Lock", "Alarm panel", "Fan"', '"Locked", "Unlocked"', '"Disarmed", "Armed home"'],
  ['fr', '"Serrure", "Alarme", "Ventilateur"', '"Verrouillé", "Déverrouillé"', '"Désarmé", "Armé (domicile)"'],
]) {
  assert.ok(i18n.includes(first), `${lang} device labels`);
  assert.ok(i18n.includes(lock), `${lang} lock states`);
  assert.ok(i18n.includes(alarm), `${lang} alarm states`);
}

// The parser on the host with the firmware's ArduinoJson.
const jsonInclude = [process.env.ARDUINOJSON_INCLUDE, path.join(os.homedir(), 'Documents/Arduino/libraries/ArduinoJson/src')]
  .filter(Boolean).find(p => fs.existsSync(path.join(p, 'ArduinoJson.h')));
const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
if (!jsonInclude || !compiler) {
  console.log('SKIP: device detail parser needs ArduinoJson and a host C++ compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/device-tiles');
fs.mkdirSync(out, {recursive: true});
const source = `
#include <cassert>
#include <cstring>
#include "src/types/device/device_detail.h"
using namespace device_detail;
int main() {
  // A lock with its code rules.
  Detail d = parse(R"({"state":"locked","supported_features":1,"code_format":"number","lock_code":false,"unlock_code":true,"unlock_allowed":false})");
  assert(d.valid && d.exists && d.available && is(d, "locked"));
  assert(d.features == kLockOpen && d.code_number && !d.code_text && !d.lock_code && d.unlock_code && !d.unlock_allowed);
  // A missing entity (state null) is neither unknown nor unavailable; unknown stays available.
  d = parse(R"({"state":null,"supported_features":0})");
  assert(d.valid && !d.exists && !d.available && d.state[0] == 0);
  d = parse(R"({"state":"unknown"})");
  assert(d.valid && d.exists && d.available && d.unlock_allowed && d.disarm_allowed);
  d = parse(R"({"state":"unavailable"})");
  assert(d.valid && d.exists && !d.available);
  // Text codes are reported apart (the keypad takes digits only).
  d = parse(R"({"state":"disarmed","code_format":"text","arm_code":true})");
  assert(d.code_text && !d.code_number && d.arm_code && !d.disarm_code);
  // A fan with three speeds and presets.
  d = parse(R"({"state":"on","supported_features":63,"percentage":66,"percentage_step":33.33,"preset_mode":"sleep","preset_modes":["auto","sleep",5,"","this_preset_name_is_longer_than_32_bytes"],"oscillating":false,"direction":"forward"})");
  assert(d.has_percentage && d.percentage == 66 && fan_speed_count(d) == 3 && fan_speed_of(d, 66) == 2);
  assert(fan_percentage_of(d, 1) == 33 && fan_percentage_of(d, 3) == 100 && fan_percentage_of(d, 0) == 0);
  assert(d.preset_count == 2 && !strcmp(d.presets[1], "sleep") && !strcmp(d.preset_mode, "sleep"));
  assert(d.has_oscillating && !d.oscillating && !strcmp(d.direction, "forward"));
  // Missing numbers stay missing; out-of-range values are clamped or dropped.
  d = parse(R"({"state":"off","percentage":null,"percentage_step":0,"direction":"sideways"})");
  assert(!d.has_percentage && fan_speed_count(d) == 100 && d.direction[0] == 0 && !d.has_oscillating);
  d = parse(R"({"state":"on","percentage":250})");
  assert(d.percentage == 100);
  // Malformed payloads are ignored.
  assert(!parse("locked").valid && !parse("[]").valid && !parse(R"({"state":5})").valid);
  assert(!parse(nullptr).valid && !parse("").valid);
  return 0;
}
`;
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, source);
const compile = spawnSync(compiler, ['-std=c++17', '-Wall', '-I', root, '-I', jsonInclude, file, '-o', binary],
                          {encoding: 'utf8'});
assert.equal(compile.status, 0, `device detail parser did not compile:\n${compile.stdout}${compile.stderr}`);
const run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `device detail parser contract failed:\n${run.stdout}${run.stderr}`);
console.log('Device tiles: detail parser, sealed Lock/Alarm commands, translations and wiring');
