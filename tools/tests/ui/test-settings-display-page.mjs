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
const rawScreen = readRepoFile('src/ui/tabs/settings/settings_screen.cpp');
const screen = maskCpp(rawScreen);
const parts = readRepoFile('src/ui/tabs/settings/settings_parts.cpp');
const style = readRepoFile('src/ui/tabs/settings/settings_style.h');
const model = maskCpp(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'));

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
const first = fields.indexOf(keys[0]);
assert.deepEqual(fields.slice(first, first + keys.length), keys, 'the Settings strings sit together in the Strings struct');
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.deepEqual([...tables].sort(), Object.keys(expected).sort(), 'every registered language is checked');
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  assert.deepEqual(values.slice(first, first + keys.length), expected[table], `${table} Settings strings`);
}

// --- Display page -----------------------------------------------------------------------
const between = (source, from, to) => {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start);
  assert.ok(start >= 0 && end > start, `${from} .. ${to}`);
  return source.slice(start, end);
};
const page = between(screen, 'void build_display_page(lv_obj_t* page) {', 'void clear_locale_refs() {');
for (const key of ['settings_screen', 'settings_brightness', 'settings_sleep', 'settings_rotation',
  'settings_screensaver', 'settings_starts_after']) {
  assert.match(page, new RegExp(`s\\.${key}\\b`), `the page shows ${key}`);
}
const rawPage = between(rawScreen, 'void build_display_page(lv_obj_t* page) {', '// ---------- Localization page ----------');
assert.doesNotMatch(rawPage, /"[A-Z][a-z]+/, 'no display text in the page code');
assert.match(page, /section\(page, s\.settings_screen, true\)/);
assert.match(page, /section\(page, s\.settings_screensaver, false\)/);
// Quarter turns on panels that support them, else normal and flipped.
assert.match(page, /if \(settings_model::quarter_turns\(\)\) \{[\s\S]*?kQuarterTurns, 4,[\s\S]*?\} else \{[\s\S]*?kFlip, 2,/);
// The slider track is the control fill computed for the "From icon" card,
// like the Light popup's brightness track (popup_shell.cpp header_fill).
const sliderRow = between(screen, 'lv_obj_t* display_slider_row(', 'void build_display_page(');
assert.match(sliderRow, /settings_style::kDisplayColor,\s*tone\.control, palette\.card, on_change\)/);
assert.match(style, /const uint32_t circle_card = hue \? from_icon_card\(card, color\) : card;/);
assert.match(style, /tone_color::g_from_icon_card\(color & 0xFFFFFF, pressed, 0\)/);
// The slider rows share one length: the longest name x 1.3 (mockup applyProfile).
const width = between(screen, 'int display_slider_width(int page_width) {', 'lv_obj_t* display_slider_row(');
assert.match(width, /name = name \* 13 \/ 10;/);
assert.match(width, /3 \* settings_style::kRowGap -\s*settings_style::kValueWidth - name/);
// Steps read like the mockup: "30 s", "5 min", "1 h", Never.
const step = between(rawScreen, 'void format_step(', 'void category_line(');
assert.match(step, /seconds < 60[\s\S]*?"%u s"[\s\S]*?seconds < 3600[\s\S]*?"%u min"[\s\S]*?"%u h"/);
// Rows have the mockup's fixed height, so LVGL centers their content.
assert.match(parts, /lv_obj_set_size\(r\.row, LV_PCT\(100\), kRowHeight\);/);
// Text sits on the browser's baselines (Inter: ascender 1984, line 2478 per 2048).
assert.match(parts, /constexpr float kInterAscent = 1984\.0f \/ 2048\.0f;/);
assert.match(parts, /constexpr float kInterLine = \(1984\.0f \+ 494\.0f\) \/ 2048\.0f;/);
for (const call of [/browser_line\(label, kSmallFontPx, first \? 0 : kSectionTop, kSectionBottom\)/,
  /browser_line\(r\.title, kRowFontPx\)/, /browser_line\(r\.sub, kSmallFontPx, kSubTop\)/]) {
  assert.match(parts, call);
}
// The firmware saves through the model: the screensaver brightness starts at
// the device floor.
assert.match(model, /values\.saver_brightness_min = Device::kConfiguredBrightnessPercentMin;/);

// --- Category lines -----------------------------------------------------------------------
const lines = between(screen, 'void category_line(', 'void on_lines_timer(');
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
// The bar's circles are the half-height tiles' (tile_icon_disc inset and diameter).
const disc = readRepoFile('src/tiles/runtime/tile_icon_disc.h');
assert.match(disc, /inline int inset\(\) \{ return tile_layout::scale_480\(4\); \}/);
assert.match(disc, /inline int diameter\(\) \{ return row_height\(\) - inset\(\) \* 2; \}/);
assert.match(style, /inline int tile_inset\(\) \{ return popup_layout::scale480\(4\); \}/);
assert.match(style, /inline int half_tile_disc\(\) \{ return \(GRID_CELL_H - GRID_GAP\) \/ 2 - 2 \* tile_inset\(\); \}/);

console.log('Settings Display page: translated texts, mockup sizes and rotation steps');
