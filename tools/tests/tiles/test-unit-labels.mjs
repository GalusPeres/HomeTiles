// The unit Home Assistant sends for the wind speed is spelled by the panel's
// language: "m/s" in every language but Russian, which reads "м/с". The label
// lives in the central Strings schema, and both the panel (sensor tile and
// popup) and the Web Admin use it.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n?/g, '\n');

// The central label.
const header = read('src/core/i18n/i18n.h');
assert.match(header, /const char\* unit_meters_per_second;/);
assert.match(header, /String unit_label\(const char\* language_code, const String& unit\);/);
const i18n = read('src/core/i18n/i18n.cpp');
assert.match(i18n,
  /String unit_label\(const char\* language_code, const String& unit\) \{\s*String key = unit;\s*key\.trim\(\);\s*if \(key\.equalsIgnoreCase\("m\/s"\)\) return strings\(language_code\)\.unit_meters_per_second;/,
  'unit_label maps m/s');
assert.match(i18n, /static const Strings kStringsRu = \{[\s\S]{0,400000}?"м\/с"/,
  'Russian reads м/с');
// The unit sits before the media block: the schema must keep ending with
// media..., week_start_label, settings_system (test-media-layout-choice), and
// every table line holds one string (the settings tests read line-based).
assert.match(i18n, /^\s*"m\/s",\s*$/m, 'the unit has its own line in the tables');
const structBody = header.slice(header.indexOf('struct Strings {'),
  header.indexOf('\n};', header.indexOf('struct Strings {')));
const declared = [...structBody.matchAll(/const char\* (\w+);/g)].map((m) => m[1]);
const unitAt = declared.indexOf('unit_meters_per_second');
const mediaAt = declared.findIndex((name) => name.startsWith('media_'));
assert.ok(unitAt >= 0 && mediaAt >= 0 && unitAt < mediaAt,
  'the unit field precedes the media block');

// The panel: the sensor tile and the sensor popup.
assert.match(read('src/tiles/runtime/tile_renderer.cpp'),
  /unit_text = i18n::unit_label\(configManager\.getConfig\(\)\.language, unit_text\);/,
  'the sensor tile translates its unit');
const popup = read('src/ui/popups/sensor/sensor_popup.cpp');
const popupUses = (popup.match(/i18n::unit_label\(/g) || []).length;
assert.ok(popupUses >= 4, `the popup translates every unit it stores (found ${popupUses})`);

// The Web Admin: the exported table and the value formatting.
assert.match(read('src/types/weather/web_scripts.cpp'), /units: Object\.freeze\(\{/);
assert.match(read('src/types/sensor/admin-format.js'), /WEATHER_I18N\.units\[match\[2\]\]/,
  'the Web Admin sensor preview translates the unit');

console.log('Unit labels: m/s localized for the panel and the Web Admin');
