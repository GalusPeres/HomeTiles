// Home Assistant's theme and rotation selects (user 2026-10-23: the device
// page had no Dark / Light / Automatic choice, and its rotation was the old
// Tab5 switch for 180 degrees while the panel turns 0/90/180/270 or 0/180).
// The panel announces both (capabilities display_theme, display_rotation),
// takes {base}/cmnd/display_theme ("dark", "light", "auto") and
// {base}/cmnd/display_rotation (degrees of its Settings row), and keeps the
// retained states current; the old display_rotate switch topic stays for
// older Bridges.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

const topics = readRepoFile('src/network/mqtt/mqtt_topics.cpp');
for (const [key, domain, leaf] of [['DISPLAY_THEME_CMND', 'Command', 'display_theme'],
  ['DISPLAY_THEME_STAT', 'State', 'display_theme'], ['DISPLAY_ROTATION_CMND', 'Command', 'display_rotation'],
  ['DISPLAY_ROTATION_STAT', 'State', 'display_rotation']]) {
  assert.ok(topics.includes(`{TopicKey::${key}, TopicDomain::${domain}, "${leaf}"},`), key);
}

const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
// Subscribed with every route; the old switch topic stays.
for (const route of ['{TopicKey::DISPLAY_ROTATE_CMND, handleDisplayRotateCommand, false},',
  '{TopicKey::DISPLAY_ROTATION_CMND, handleDisplayRotationCommand, false},',
  '{TopicKey::DISPLAY_THEME_CMND, handleDisplayThemeCommand, false},']) {
  assert.ok(handlers.includes(route), route);
}
assert.match(fn(handlers, 'display_theme_id'),
  /return theme == theme_schedule::kAutomatic \? "auto" : theme == 1 \? "light" : "dark";/);
const theme = fn(handlers, 'handleDisplayThemeCommand');
assert.match(theme, /value\.trim\(\);\s*value\.toLowerCase\(\);/);
assert.match(theme, /if \(value == display_theme_id\(theme\)\) \{\s*theme_schedule::store\(theme\);/,
  'stored like the panel\'s Settings and the Web Admin');

const degrees = fn(handlers, 'display_rotation_degrees');
assert.match(degrees, /return static_cast<int>\(\(quarters - Device::kRotationDefault\) & 0x03\) \* 90;/);
assert.match(degrees, /return quarters == Device::kRotationFlipped \? 180 : 0;/);
const rotation = fn(handlers, 'handleDisplayRotationCommand');
assert.match(rotation, /if \(end == payload \|\| degrees < 0 \|\| degrees % 90 != 0\) return;/);
assert.match(rotation, /if \(degrees > 270 \|\| \(!quarter_turns && degrees % 180 != 0\)\) return;/,
  'only the steps this panel offers');
assert.match(rotation, /settings_select_display_rotation\(static_cast<uint8_t>\(degrees \/ \(quarter_turns \? 90 : 180\)\)\);/);
const tabSettings = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
assert.match(fn(tabSettings, 'settings_select_display_rotation'),
  /settings_model::rotation_selected\(index\);\s*settings_screen::sync_rotation\(\);/, 'applied like a tap, the row follows');

// Retained states: the theme setting and the rotation with its steps.
const publish = fn(handlers, 'mqttPublishDeviceSettings');
assert.match(publish, /"\{\\"degrees\\":%d,\\"options\\":%s\}", display_rotation_degrees\(cfg\),\s*Device::supportsQuarterTurnRotation\(\) \? "\[0,90,180,270\]" : "\[0,180\]"/);
assert.match(publish, /publish_state\(TopicKey::DISPLAY_ROTATION_STAT, rotation\);/);
assert.match(publish, /publish_state\(TopicKey::DISPLAY_THEME_STAT, display_theme_id\(cfg\.theme\)\);/);
assert.match(publish, /publish_state\(TopicKey::DISPLAY_ROTATE_STAT, cfg\.display_rotated_180 \? "ON" : "OFF"\);/);

// The announcement: older Bridges drop unknown capability keys.
assert.ok(readRepoFile('src/network/bridge/ha_bridge_config.cpp')
  .includes('json += ",\\"display_theme\\":true,\\"display_rotation\\":true";'));
console.log('Home Assistant theme and rotation selects: topics, commands within the panel\'s steps, retained states, capabilities');
