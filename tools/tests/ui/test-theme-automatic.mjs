// The global Design "Automatic" (user 2026-10-10: "einen Schalter mit dunkel
// hell automatisch"): light by day, dark by night. The day runs from sunrise
// to sunset of the Bridge's weather payloads, else from 7:00 to 19:00 local
// time; until the time zone is set the panel keeps the theme Automatic showed
// last. Runs the production day rule and checks every place the setting
// passes through.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const fn = (file, name) => {
  const found = cppFunctionDefinitions(read(file)).find(f => f.name === name);
  assert(found, `${file}: ${name}`);
  return found.source;
};

// Stored as theme 2; a boot starts with the last automatic theme.
const config = read('src/core/config/config_manager.cpp');
assert.ok(config.includes('  if (config.theme > 2) config.theme = 0;\n  config.theme_auto_last = prefs.getUChar("theme_al", 0) == 1 ? 1 : 0;'));
assert.match(fn('src/core/config/config_manager.cpp', 'ConfigManager::saveThemeAutoLast'), /prefs\.putUChar\("theme_al", theme\)/);
const schedule = read('src/ui/shared/theme_schedule.cpp');
assert.match(fn('src/ui/shared/theme_schedule.cpp', 'decide'),
  /if \(!g_time_zone \|\| !getLocalTime\(&now, 0\) \|\| now\.tm_year < 2020 - 1900\) \{\s*\*why = [^;]*;\s*return configManager\.getConfig\(\)\.theme_auto_last;/);
assert.match(fn('src/ui/shared/theme_schedule.cpp', 'effective'), /return decide\(&why\);/);
assert.ok(read('HomeTiles.ino').includes('ui_theme::set(theme_schedule::at_boot(configManager.getConfig().theme));'));
const ino = read('HomeTiles.ino');
assert.ok(ino.indexOf('theme_schedule::poll();') >= 0 && ino.indexOf('theme_schedule::poll();') < ino.lastIndexOf('uiManager.processThemeChange();'),
  'the loop asks before it applies');
const poll = fn('src/ui/shared/theme_schedule.cpp', 'poll');
assert.match(poll, /if \(\(theme == 1\) != ui_theme::theme_light\(\)\) uiManager\.requestThemeChange\(\);/);
// One log line whenever the decision or its source changes (user
// 2026-10-23: the S3 stayed dark after sunrise until a restart).
assert.match(poll, /if \(logged_theme != theme \|\| logged_why != why\) \{[\s\S]*Serial\.printf\("\[Theme\] Automatic: %s \(%s\)\\n"/);
for (const why of ['time zone not set, last shown', 'clock not set, last shown', 'sun times',
  '07:00-19:00, no sun times for today']) {
  assert.ok(schedule.includes('"' + why + '"'), why);
}
const ui = read('src/ui/ui_manager.cpp');
assert.ok(ui.includes('  const uint8_t theme = theme_schedule::effective(stored);\n  if ((theme == 1) == ui_theme::theme_light()) return;') &&
  ui.includes('if (stored == theme_schedule::kAutomatic) configManager.saveThemeAutoLast(theme);') &&
  ui.includes('  tz_configured = true;\n  theme_schedule::note_time_zone();'), 'processThemeChange and the time zone');
// Sun times from the weather tile and popup.
assert.ok(read('src/tiles/runtime/tile_renderer.cpp').includes('theme_schedule::note_sun(sun);'));
assert.ok(read('src/ui/popups/weather/weather_popup.cpp').includes('theme_schedule::note_sun(ctx->sun);'));
// The Web Admin: a third choice; the answer names the theme shown now.
const handler = fn('src/web/server/handlers/web_admin_handlers.cpp', 'WebAdminServer::handleSaveTheme');
assert.ok(handler.includes('if (value != "0" && value != "1" && value != "2") {') &&
  handler.includes('const uint8_t shown = theme_schedule::effective(stored);') &&
  handler.includes(',\\"effective\\":'), 'theme handler');
const html = read('src/web/server/render/web_admin_html.cpp');
assert.ok(html.includes('for (uint8_t theme = 0; theme < 3; ++theme) {') &&
  html.includes('theme == 2 ? tr.theme_auto : theme ? tr.theme_light : tr.theme_dark'), 'three choices');
const i18n = read('src/core/i18n/i18n.cpp');
for (const words of [['"Hell",', '"Automatisch",'], ['"Light",', '"Automatic",'], ['"Clair",', '"Automatique",'],
  ['"Jasny",', '"Automatyczny",']]) {
  assert.ok(i18n.includes(words.map(word => '    ' + word).join('\n')), 'Automatic: ' + words[1]);
}
const saveTheme = read('src/web/admin/settings/display-borders.js');
assert.ok(saveTheme.includes("if (theme !== '2') applyPanelThemePreview(theme);") &&
  saveTheme.includes("const shown = String(answer?.effective ?? (theme === '2' ? previousShown : theme));"),
  'the preview shows the theme the panel picked');

// The day rule, as compiled for the panel.
const host = await lvglHost(root);
if (!host) { console.log('SKIP: the automatic theme day rule needs a C++ compiler'); process.exit(0); }
const out = path.join(root, 'build/tests/theme-automatic');
fs.mkdirSync(out, {recursive: true});
const icons = read('src/types/weather/weather_icons.h');
const structs = icons.slice(icons.indexOf('struct SunDay {'), icons.indexOf('// Reads the "sun" list'));
const cpp = `#include <cstdint>
#include <cstdio>
#include <cstring>
namespace weather_icons {
${structs}
${fn('src/types/weather/weather_icons.cpp', 'is_night')}
}
namespace theme_schedule {
inline constexpr int kFallbackDayStart = 7 * 60;
inline constexpr int kFallbackDayEnd = 19 * 60;
${fn('src/ui/shared/theme_schedule.cpp', 'has_sun_for')}
${fn('src/ui/shared/theme_schedule.cpp', 'day_at')}
}
static int day(const weather_icons::SunTimes& sun, const char* date, int h, int m) {
  return theme_schedule::day_at(sun, date, h * 60 + m) ? 1 : 0;
}
int main() {
  weather_icons::SunTimes none;
  weather_icons::SunTimes sun;
  std::strcpy(sun.days[0].date, "2026-10-10"); sun.days[0].rise = 7 * 60 + 41; sun.days[0].set = 18 * 60 + 33;
  std::strcpy(sun.days[1].date, "2026-06-21"); sun.days[1].up = 1;
  std::strcpy(sun.days[2].date, "2026-12-21"); sun.days[2].up = 0;
  sun.count = 3;
  std::printf("%d%d%d%d ", day(none, "2026-10-10", 6, 59), day(none, "2026-10-10", 7, 0),
              day(none, "2026-10-10", 18, 59), day(none, "2026-10-10", 19, 0));
  std::printf("%d%d%d%d ", day(sun, "2026-10-10", 7, 40), day(sun, "2026-10-10", 7, 41),
              day(sun, "2026-10-10", 18, 32), day(sun, "2026-10-10", 18, 33));
  std::printf("%d%d ", day(sun, "2026-10-11", 6, 30), day(sun, "2026-10-11", 12, 0));
  std::printf("%d%d\\n", day(sun, "2026-06-21", 23, 30), day(sun, "2026-12-21", 12, 0));
  return 0;
}
`;
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, cpp);
let result = spawnSync(host.cxx, ['-std=c++17', file, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stderr);
// Without sun times: 7:00 to 19:00. With them: sunrise to sunset; another
// date falls back to the hours; polar day and night.
assert.equal(result.stdout.trim(), '0110 0110 01 10');
console.log('Automatic theme: stored, applied at boot and in the loop, sun times or 7-19 h, Web Admin choice in four languages');
