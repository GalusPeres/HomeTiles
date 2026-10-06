// The Settings Localization page of the approved mockup
// (build/design-mockups/settings/settings-menu.html, pageLocalization):
// Language, Time zone, Time format, Date format and Keyboard as dropdown rows
// without colons, no Save button and no Preview group; a pick saves at once
// like the former Save button. The option list opens below its row when it
// fits, else above, else on the larger side and scrolls in itself.
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

const between = (source, from, to) => {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start);
  assert.ok(start >= 0 && end > start, `${from} .. ${to}`);
  return source.slice(start, end);
};

// --- Translations (no colons; Web Admin keeps its labels with colons) ---------------------
const keys = ['settings_language', 'settings_time_zone', 'settings_time_format', 'settings_date_format',
  'settings_keyboard'];
const expected = {
  kStringsEn: ['Language', 'Time zone', 'Time format', 'Date format', 'Keyboard'],
  kStringsDe: ['Sprache', 'Zeitzone', 'Zeitformat', 'Datumsformat', 'Tastatur'],
  kStringsFr: ['Langue', 'Fuseau horaire', "Format de l'heure", 'Format de date', 'Clavier'],
  kStringsPl: ['Język', 'Strefa czasowa', 'Format czasu', 'Format daty', 'Klawiatura'],
};
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
const first = fields.indexOf(keys[0]);
assert.deepEqual(fields.slice(first, first + keys.length), keys, 'the Localization strings sit together');
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.deepEqual([...tables].sort(), Object.keys(expected).sort(), 'every registered language is checked');
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  const labels = values.slice(first, first + keys.length);
  assert.deepEqual(labels, expected[table], `${table} Localization labels`);
  for (const label of labels) assert.doesNotMatch(label, /:/, `${table}: ${label} has no colon`);
}

// --- Page -----------------------------------------------------------------------------------
const page = between(screen, 'void build_localization_page(lv_obj_t* page) {', 'void build_page() {');
const titles = between(screen, 'const char* locale_title(LocaleList list) {', 'const char* locale_icon(');
for (const key of keys) assert.match(titles, new RegExp(`s\\.${key}\\b`), `the page shows ${key}`);
const rawPage = between(rawScreen, 'void build_localization_page(lv_obj_t* page) {', 'void build_page() {');
assert.doesNotMatch(rawPage, /"[A-Z][a-z]+/, 'no display text in the page code');
// One group, a row per list; the value right, or under the label on narrow cards.
assert.match(page, /settings_parts::group\(page, palette\)/);
assert.match(page, /for \(uint8_t i = 0; i < settings_model::kLocaleListCount; \+\+i\)/);
assert.match(page, /const bool stacked = g_card_width < settings_style::kStackedCardWidth;/);
assert.match(style, /constexpr int kStackedCardWidth = 520;/);
assert.match(page, /stacked \? row\.sub : settings_parts::trailing_text\(row\.row, locale_value\(list\), value_width\)/);
assert.match(page, /settings_parts::make_tap\(row\.row, palette\.button, on_locale_row_clicked/);
assert.doesNotMatch(page, /save|preview/i, 'no Save button and no Preview group');
// The category opens its page in the card, not the old popup.
const click = between(screen, 'void on_category_clicked(lv_event_t* e) {', 'void build_category_tile(');
assert.match(click, /category != Category::Display && category != Category::Localization/);
assert.match(between(screen, 'void build_page() {', 'void clear_refs() {'),
  /if \(g_category == Category::Localization\) build_localization_page\(g_page\);/);
// The open list: the selected option in the Localization circle tone, labels aligned.
const open = between(screen, 'void open_locale_list(uint8_t index) {', 'void on_locale_row_clicked(');
assert.match(open, /spec\.selected_color = settings_style::tone\(g_built_card, settings_style::kLocalizationColor\)\.disc;/);
assert.match(open, /spec\.text_x = settings_style::kRowPadLeft \+ settings_parts::icon_width\(\) \+ settings_style::kRowGap;/);
assert.match(open, /lv_obj_get_coords\(g_page, &spec\.bounds\);/);
// A tap on another dropdown row switches lists (mockup click handler).
const closed = between(screen, 'void on_options_closed(uint8_t tag, const lv_point_t* tapped) {', 'void on_option_picked(');
assert.match(closed, /open_locale_list\(i\);/);
// Leaving the page or Settings closes the list.
assert.match(between(screen, 'void clear_locale_refs() {', 'const char* locale_title('), /settings_parts::close_options\(\);/);
assert.match(between(screen, 'void did_hide() {', 'void refresh_lines() {'), /clear_locale_refs\(\);/);

// --- Option list (mockup .ddl and placeDropdown) ---------------------------------------------
for (const [name, big, small] of [['kOptionHeight', 56, 37], ['kOptionInset', 8, 5]]) {
  assert.match(style, new RegExp(`constexpr int ${name} = pick\\(${big}, ${small}\\);`), name);
}
assert.match(style, /constexpr int kOptionsVisible = 6;/);
const list = between(parts, 'void open_options(const OptionList& spec) {', 'void close_options() {');
assert.match(list, /const bool down = below >= height \|\| below >= above;/);
assert.match(list, /if \(height > room\) height = room;/);
assert.match(list, /settings_style::apply_radius\(list, kGroupRadius\);/);
assert.match(list, /ui_surface_style::apply_global_tile_border\(list\);/);
assert.match(list, /settings_style::apply_radius\(option, kGroupRadius - kOptionInset\);/);
assert.match(list, /lv_obj_scroll_to_y\(list, top > 0 \? top : 0, LV_ANIM_OFF\);/);
// Picks and taps close the list after the touch event, never inside it.
assert.match(between(parts, 'void option_clicked_cb(lv_event_t* e) {', 'void overlay_clicked_cb('), /lv_async_call\(options_async_cb, nullptr\);/);
assert.match(between(parts, 'void overlay_clicked_cb(lv_event_t*) {', '}  // namespace'), /lv_async_call\(options_async_cb, nullptr\);/);

// --- Model: the same values and side effects as the former Save button -------------------------
const save = between(model, 'void locale_selected_changed(LocaleList list, uint8_t index) {', 'void open_category_popup(');
assert.match(save, /strncpy\(cfg\.language, i18n::language_code_at\(index\)/);
assert.match(save, /strncpy\(cfg\.timezone, selected_timezone_code\(index\)/);
assert.match(save, /cfg\.global_time_format = clock_tile::normalize_time_format\(index\);/);
assert.match(save, /cfg\.global_date_format = clock_tile::normalize_date_format\(index\);/);
assert.match(save, /cfg\.keyboard_layout = index;/);
assert.match(save, /if \(!configManager\.save\(cfg\)\)/);
assert.match(save, /if \(list == LocaleList::Language\) settings_refresh_language\(\);/);
assert.match(save, /if \(list == LocaleList::TimeZone\) uiManager\.scheduleNtpSync\(0\);/);
assert.match(save, /tiles_request_reload_all\(\);/);
const options = between(model, 'const char* locale_option(LocaleList list, uint8_t index) {', 'uint8_t locale_selected(');
assert.match(options, /i18n::language_native_name_at\(index\)/);
assert.match(options, /i18n::locale\(configManager\.getConfig\(\)\.language\)\.timezone_labels\[index\]/);
assert.match(options, /s\.format_auto_language, s\.format_24_hour, s\.format_12_hour/);

console.log('Settings Localization page: dropdown rows without colons, option list, saved at once');
