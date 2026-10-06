// The Settings Display page of the approved mockup
// (build/design-mockups/settings/settings-menu.html): sections Screen and
// Screensaver, slider rows without colons, the rotation segment, and the
// category lines. Every text comes from the central translations, in every
// language; sizes come from the mockup's per-class values.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const raw = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
const settings = maskCpp(raw);
const style = readRepoFile('src/ui/tabs/settings/settings_style.h');

// --- Translations ---------------------------------------------------------------------
const keys = ['settings_screen', 'settings_brightness', 'settings_sleep', 'settings_rotation',
  'settings_screensaver', 'settings_starts_after', 'settings_sleep_summary_fmt',
  'settings_not_connected', 'settings_access_point_on', 'settings_update_available'];
const expected = {
  kStringsEn: ['Screen', 'Brightness', 'Sleep', 'Rotation', 'Screensaver', 'Starts after', 'Sleep %s',
    'Not connected', 'Access point on', 'Update available'],
  kStringsDe: ['Bildschirm', 'Helligkeit', 'Standby', 'Drehung', 'Screensaver', 'Startet nach', 'Standby %s',
    'Nicht verbunden', 'Hotspot an', 'Update verfügbar'],
  kStringsFr: ['Écran', 'Luminosité', 'Veille', 'Rotation', 'Écran de veille', 'Démarre après', 'Veille %s',
    'Non connecté', "Point d'accès actif", 'Mise à jour disponible'],
  kStringsPl: ['Ekran', 'Jasność', 'Uśpienie', 'Obrót', 'Wygaszacz ekranu', 'Start po', 'Uśpienie %s',
    'Brak połączenia', 'Punkt dostępu włączony', 'Dostępna aktualizacja'],
};
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
assert.deepEqual(fields.slice(-keys.length), keys, 'the new Settings strings close the Strings struct');
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.deepEqual([...tables].sort(), Object.keys(expected).sort(), 'every registered language is checked');
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  assert.deepEqual(values.slice(-keys.length), expected[table], `${table} Settings strings`);
}

// --- Display page -----------------------------------------------------------------------
const page = settings.slice(settings.indexOf('static void build_display_page(lv_obj_t* page) {'),
  settings.indexOf('// Power Status Update (stub'));
for (const key of ['settings_screen', 'settings_brightness', 'settings_sleep', 'settings_rotation',
  'settings_screensaver', 'settings_starts_after']) {
  assert.match(page, new RegExp(`s\\.${key}\\b`), `the page shows ${key}`);
}
const rawPage = raw.slice(raw.indexOf('static void build_display_page(lv_obj_t* page) {'),
  raw.indexOf('// Power Status Update (stub'));
assert.doesNotMatch(rawPage, /"[A-Z][a-z]+/, 'no display text in the page code');
assert.match(page, /section\(page, s\.settings_screen, true\)/);
assert.match(page, /section\(page, s\.settings_screensaver, false\)/);
// Quarter turns on panels that support them, else normal and flipped.
assert.match(page, /if \(Device::supportsQuarterTurnRotation\(\)\) \{[\s\S]*?kQuarterTurns, 4,[\s\S]*?\} else \{[\s\S]*?kFlip, 2,/);
// The slider rows share one length: the longest name x 1.3 (mockup applyProfile).
const width = settings.slice(settings.indexOf('static int display_slider_width(int page_width) {'),
  settings.indexOf('static lv_obj_t* display_slider_row('));
assert.match(width, /name = name \* 13 \/ 10;/);
assert.match(width, /3 \* settings_style::kRowGap -\s*settings_style::kValueWidth - name/);
// Steps read like the mockup: "30 s", "5 min", "1 h", Never.
const step = raw.slice(raw.indexOf('static void format_sleep_step('),
  raw.indexOf('static void sync_display_rotation_state('));
assert.match(step, /seconds < 60[\s\S]*?"%u s"[\s\S]*?seconds < 3600[\s\S]*?"%u min"[\s\S]*?"%u h"/);

// --- Category lines -----------------------------------------------------------------------
const lines = settings.slice(settings.indexOf('static void settings_category_line('),
  settings.indexOf('static void update_settings_category_lines() {'));
for (const key of ['settings_sleep_summary_fmt', 'settings_access_point_on', 'settings_not_connected',
  'settings_update_available']) {
  assert.match(lines, new RegExp(`s\\.${key}\\b`), `the category lines use ${key}`);
}

// --- Mockup sizes (PROFILES.vars: 1280x800 values, x 5/6 on 1024x600, own 480 values) ---------
for (const [name, big, small] of [['kSectionTop', 22, 14], ['kSectionBottom', 10, 7], ['kSectionLeft', 24, 16],
  ['kGroupRadius', 28, 19], ['kRowHeight', 88, 59], ['kRowPadLeft', 28, 19], ['kRowPadRight', 24, 16],
  ['kRowGap', 20, 12], ['kSegmentHeight', 48, 32], ['kSegmentInset', 4, 3], ['kSegmentMinWidth', 104, 64],
  ['kSegmentPad', 18, 12], ['kSliderHeight', 48, 32], ['kSliderMaxWidth', 420, 160], ['kSliderThumbWidth', 4, 3],
  ['kValueWidth', 118, 64]]) {
  assert.match(style, new RegExp(`constexpr int ${name} = pick\\(${big}, ${small}\\);`), name);
}
// Roundings follow the global radius: the mockup's value at the maximum radius.
assert.match(style, /int baseline = at_maximum - \(tile_radius::kMaximum - tile_radius::kMinimum\);/);

console.log('Settings Display page: translated texts, mockup sizes and rotation steps');
