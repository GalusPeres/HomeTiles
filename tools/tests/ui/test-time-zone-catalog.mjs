// Every time zone of the catalog works everywhere: a stored code survives the
// load, it has a POSIX rule for the clock, and every language names it in the
// catalog's order. An unknown code silently falls back to Berlin, and a list one
// label short shows a null name. Alaska was missing, and a panel could not take
// Home Assistant's zone (issue #145).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n/g, '\n');
const i18n = read('src/core/i18n/i18n.cpp');
const header = read('src/core/i18n/i18n.h');
const config = read('src/core/config/config_manager.cpp');
const configHeader = read('src/core/config/config_manager.h');
const ui = read('src/ui/ui_manager.cpp');
const handlers = read('src/network/mqtt/mqtt_handlers.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
const fn = (source, name) => cppFunctionDefinitions(source)
  .find(item => item.name === name || item.name === `ConfigManager::${name}`)?.source ?? '';

const table = i18n.slice(i18n.indexOf('static const TimezoneOptionInfo kTimezoneOptions'));
const codes = [...table.slice(0, table.indexOf('};')).matchAll(/\{\d, "([a-z_]+)"\}/g)].map(match => match[1]);
assert.ok(codes.includes('anchorage'), 'Alaska is in the catalog');
assert.equal(codes[1], 'auto', 'the automatic zone in the Global group, beside UTC');
assert.match(table, /\{0, "utc"\},\n\s*\{0, "auto"\},\n\s*\{1, "london"\}/, 'one group, in order');

const count = Number(header.match(/kTimezoneOptionCount = (\d+);/)?.[1]);
assert.equal(codes.length, count, 'catalog size');
assert.equal(Number(header.match(/timezone_labels\[(\d+)\];/)?.[1]), count, 'label array size');

const known = config.slice(config.indexOf('kKnownTimezones[] = {'));
const specs = ui.slice(ui.indexOf('static const char* timezone_spec_for_code'));
const specBody = specs.slice(0, specs.indexOf('\n}\n'));
for (const code of codes) {
  assert.ok(known.slice(0, known.indexOf('};')).includes(`{"${code}"}`), `${code} survives the configuration's load`);
  assert.ok(specBody.includes(`"${code}") == 0)`), `${code} has a clock rule`);
}

// Alaska: UTC-9, UTC-8 in summer, on the US dates like the other US zones.
assert.ok(specBody.includes('"anchorage") == 0) return "AKST9AKDT,M3.2.0/02,M11.1.0/02";'), 'the Alaska rule');

// Every language lists the zones in catalog order.
const lists = [...i18n.matchAll(/\{"UTC\+0 - UTC"[^}]*\}/g)].map(match => [...match[0].matchAll(/"([^"]*)"/g)].map(item => item[1]));
assert.equal(lists.length, 8, 'eight languages');
const alaska = codes.indexOf('anchorage');
for (const labels of lists) {
  assert.equal(labels.length, count, `${labels[0]}: one name per zone`);
  assert.match(labels[1], /^(Auto|Авто) \(Home Assistant\)$/, 'the automatic zone\'s name');
  assert.match(labels[alaska - 1], /^UTC-10 - /, 'Honolulu before Alaska');
  assert.match(labels[alaska], /^UTC-9 \/ UTC-8 - (Anchorage|Анкоридж)$/, 'Alaska with both offsets');
  assert.match(labels[alaska + 1], /^UTC-8 \/ UTC-7 - /, 'Los Angeles after Alaska');
}

// The Settings list finds Berlin by its code, wherever the catalog has it.
assert.ok(!/= 2;\s*\/\/ berlin|return 2;\s*\/\/ berlin/.test(settings), 'no fixed Berlin index');
assert.match(fn(settings, 'settings_timezone_index'), /"berlin"\) == 0\) berlin = i;/);
assert.match(fn(settings, 'selected_timezone_code'), /return "berlin";/);

// ---- The automatic zone ------------------------------------------------------
// The clock takes the stored rule, UTC before a Bridge sent any.
assert.match(specBody, /"auto"\) == 0\) \{\n\s*const char\* rule = configManager\.getConfig\(\)\.timezone_rule;\n\s*return rule\[0\] \? rule : "UTC0";/);
// Stored across a start, written only when it changed, and no save of the
// other settings can lose it.
assert.match(configHeader, /#define CONFIG_TIMEZONE_RULE_MAX 48/);
assert.match(config, /prefs\.getString\("tz_rule", config\.timezone_rule, CONFIG_TIMEZONE_RULE_MAX\);/);
const save = fn(config, 'saveTimeZoneRule');
assert.match(save, /if \(strcmp\(config\.timezone_rule, rule\) == 0\) return false;/, 'no flash write for the same rule');
assert.match(save, /prefs\.putString\("tz_rule", rule\)/);
assert.match(fn(config, 'save'), /memcpy\(normalized\.timezone_rule, config\.timezone_rule, sizeof\(normalized\.timezone_rule\)\);/);
// A new rule from the Bridge sets the clock when the panel is on "auto"; a
// configuration without one (an older Bridge) changes nothing.
assert.match(handlers, /time_zone_rule::extract\(cfg_buf, zone_rule, sizeof\(zone_rule\)\) &&\n\s*configManager\.saveTimeZoneRule\(zone_rule\) &&\n\s*strcasecmp\(configManager\.getConfig\(\)\.timezone, "auto"\) == 0\) \{/);
assert.match(handlers, /Time zone from Home Assistant: %s\\n", zone_rule\);\n\s*uiManager\.scheduleNtpSync\(0\);/);
assert.ok(handlers.indexOf('time_zone_rule::extract') > handlers.indexOf('bool applied = haBridgeConfig.applyJson(cfg_buf'), 'after the configuration applied');

// The real parser on what a Bridge sends (Python's json.dumps writes ": ").
const output = compileAndRun({
  label: 'Time zone rule',
  harness: String.raw`
#include <cstdio>
#include "src/network/bridge/time_zone_rule.h"
static void show(const char* name, const char* json) {
  char out[48] = "kept";
  const bool found = time_zone_rule::extract(json, out, sizeof(out));
  std::printf("%s|%d|%s\n", name, found ? 1 : 0, out);
}
int main() {
  show("alaska", R"({"device_id": "x", "ha_prefix": "ha", "time_zone_rule": "STD9DST,M3.2.0/2,M11.1.0/2", "sensors": []})");
  show("compact", R"({"time_zone_rule":"STD-5:30"})");
  show("half_hour", R"({"time_zone_rule": "STD-10:30DST-11,M10.1.0/2,M4.1.0/2"})");
  show("older_bridge", R"({"device_id": "x", "sensors": []})");
  show("none", R"({"time_zone_rule": null})");
  show("empty", R"({"time_zone_rule": ""})");
  show("not_a_rule", R"({"time_zone_rule": "STD9;reboot"})");
  show("starts_wrong", R"({"time_zone_rule": "9STD"})");
  show("cut_off", R"({"time_zone_rule": "STD9DST,M3)");
  show("too_long", R"({"time_zone_rule": "STD9DST,M3.2.0/2,M11.1.0/2,M3.2.0/2,M11.1.0/2,M3.2.0/2"})");
  show("an_entity", R"({"sensors": ["time_zone_rule", "x"], "time_zone_rule": "STD7"})");
  return 0;
}
`,
});
if (output !== null) {
  const seen = Object.fromEntries(output.trim().split('\n').map(line => {
    const [name, found, value] = line.split('|');
    return [name, {found: found === '1', value}];
  }));
  assert.deepEqual(seen.alaska, {found: true, value: 'STD9DST,M3.2.0/2,M11.1.0/2'});
  assert.deepEqual(seen.compact, {found: true, value: 'STD-5:30'});
  assert.deepEqual(seen.half_hour, {found: true, value: 'STD-10:30DST-11,M10.1.0/2,M4.1.0/2'});
  assert.deepEqual(seen.an_entity, {found: true, value: 'STD7'}, 'an entity of this name is a value');
  for (const name of ['older_bridge', 'none', 'empty', 'not_a_rule', 'starts_wrong', 'cut_off', 'too_long']) {
    assert.deepEqual(seen[name], {found: false, value: 'kept'}, `${name}: the panel keeps what it has`);
  }
}

console.log(`Time zone catalog: ${count} zones with a stored code, a clock rule and a name in all ${lists.length} languages; Home Assistant's zone reaches the clock`);
