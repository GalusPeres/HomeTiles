// Yandex Pogoda sends its own condition codes ("overcast", "wet-snow", ...)
// beside Home Assistant's fifteen; both are translated on the panel (Weather
// tile, Weather popup) and in the Web Admin, and a condition sensor tile
// translates its value too. The three key lists must stay in step: the
// LocaleProfile table, weather_condition_label's keys and the JS export.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n?/g, '\n');

const cpp = read('src/core/i18n/i18n.cpp');
const header = read('src/core/i18n/i18n.h');

// The shared key list of weather_condition_label and is_weather_condition.
const keysBlock = /kWeatherConditionKeys\[\] = \{([\s\S]*?)\};/.exec(cpp);
assert.ok(keysBlock, 'kWeatherConditionKeys exists');
const keys = [...keysBlock[1].matchAll(/"([^"]+)"/g)].map(m => m[1]);
assert.ok(keys.length >= 15, 'at least Home Assistant\'s conditions');
assert.ok(keys.includes('overcast'), 'the reported Yandex condition is a key');
// No key twice: the first match wins, a duplicate would shadow a translation.
assert.equal(new Set(keys).size, keys.length, 'every condition key is unique');

// The translations hold one entry per key, in the same order.
const size = /weather_conditions\[(\d+)\]/.exec(header);
assert.ok(size, 'LocaleProfile::weather_conditions declared');
assert.equal(Number(size[1]), keys.length, 'one translation per key');

const anchors = ['Partly cloudy', 'Partiellement nuageux', 'Częściowo pochmurno',
  'Polojasno', 'Parcialmente nublado', 'Polooblačno', 'Переменная облачность',
  'Teilw. bewölkt'];
for (const anchor of anchors) {
  const lists = [...cpp.matchAll(/\{([^{}]*)\}/g)]
    .map(m => [...m[1].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(s => s[1]))
    .filter(items => items.length === keys.length && items.includes(anchor));
  assert.equal(lists.length, 1, `one ${keys.length}-entry list holding "${anchor}"`);
}
assert.equal(anchors.length, 8, 'every registered language translates the Yandex codes');

// Home Assistant's fifteen first, Yandex Pogoda's own seventeen after them.
assert.deepEqual(keys.slice(0, 15),
  ['clear-night', 'cloudy', 'exceptional', 'fog', 'hail', 'lightning',
    'lightning-rainy', 'partlycloudy', 'pouring', 'rainy', 'snowy',
    'snowy-rainy', 'sunny', 'windy', 'windy-variant'],
  'Home Assistant\'s conditions stay first');
assert.deepEqual(keys.slice(15),
  ['clear', 'partly-cloudy', 'overcast', 'drizzle', 'light-rain', 'rain',
    'moderate-rain', 'heavy-rain', 'continuous-heavy-rain', 'showers',
    'wet-snow', 'light-snow', 'snow', 'snow-showers', 'thunderstorm',
    'thunderstorm-with-rain', 'thunderstorm-with-hail'],
  'Yandex Pogoda\'s codes follow');

// The Web Admin preview gets the same table (WEATHER_I18N.conditions).
const scripts = read('src/types/weather/web_scripts.cpp');
const jsBlock = /kConditions\[\] = \{([\s\S]*?)\};/.exec(scripts);
assert.ok(jsBlock, 'web_scripts kConditions exists');
const jsKeys = [...jsBlock[1].matchAll(/"([^"]+)"/g)].map(m => m[1]);
assert.deepEqual(jsKeys, keys, 'the JS export lists the same keys in order');

// The consumers: the Weather tile and popup translate, a condition sensor
// tile translates its value, and every Yandex code has an icon.
for (const file of ['src/tiles/runtime/tile_renderer.cpp',
                    'src/ui/popups/weather/weather_popup.cpp']) {
  const source = read(file);
  assert.match(source, /key == "overcast"\) return "mdi:weather-cloudy"/, `${file}: overcast icon`);
}
assert.match(read('src/ui/popups/sensor/sensor_popup.cpp'), /is_weather_condition\(value\)/,
  'the sensor popup translates a condition value');
assert.match(header, /bool is_weather_condition\(const String& condition\)/);
assert.match(read('src/types/sensor/admin-format.js'), /WEATHER_I18N\.conditions\[conditionName\]/,
  'the Web Admin sensor preview uses the same table');
assert.match(read('src/core/i18n/i18n.cpp'), /return profile\.weather_conditions\[i\]/,
  'the label reads the translations');

// The Yandex Pogoda integration sends underscored states (light_rain) and has
// two names for conditions the table carries under another key (sleet,
// snowfall). Every consumer normalizes the same way: the label, the condition
// check, both icon tables and the Web Admin lookups.
for (const fn of ['weather_condition_label', 'is_weather_condition']) {
  const body = new RegExp(`(?:String|bool) ${fn}\\([\\s\\S]*?\\n\\}`).exec(cpp)?.[0];
  assert.ok(body, `${fn} exists`);
  assert.ok(body.includes('key.replace("_", "-")'), `${fn} normalizes underscores`);
  assert.ok(body.includes('if (key == "sleet") key = "snowy-rainy";'),
    `${fn} maps sleet`);
  assert.ok(body.includes('else if (key == "snowfall") key = "snowy";'),
    `${fn} maps snowfall`);
}
for (const file of ['src/tiles/runtime/tile_renderer.cpp',
                    'src/ui/popups/weather/weather_popup.cpp']) {
  const source = read(file);
  assert.match(source, /weather_icon_from_condition[\s\S]{0,220}key\.replace\("_", "-"\)/,
    `${file}: the icon table normalizes underscores`);
}
assert.match(read('src/types/sensor/admin-format.js'),
  /const conditionKey = lower\.replace\(\/_\/g, '-'\)/,
  'the Web Admin sensor preview normalizes underscores');
assert.match(read('src/types/weather/admin.js'),
  /function weatherConditionKey\(value\) \{[\s\S]{0,220}if \(key === 'sleet'\) return 'snowy-rainy';/,
  'the Web Admin weather preview normalizes the same way');
// The Web mirror of the label translates the underscored states too.
assert.match(read('src/types/weather/admin.js'),
  /function weatherConditionLabel\(condition\) \{[\s\S]{0,200}weatherConditionKey\(condition\)/,
  'the Web condition label uses the normalized key');

console.log(`Weather conditions: ${keys.length} keys translated (Home Assistant + Yandex Pogoda)`);
