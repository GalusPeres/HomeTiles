// The Settings Audio page on panels with a speaker: a fifth category before
// System (the category values stay fixed, System is still 3), the Speaker
// group with Volume and, where the microphone answered, the Microphone group
// with the privacy switch and the sensitivity. Texts come from the central
// translations in every language; the model hands the speaker's own values
// through; the host preview renders the page (--page=audio) when LVGL is set up.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

const header = readRepoFile('src/core/i18n/i18n.h');
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const rawScreen = readRepoFile('src/ui/tabs/settings/settings_screen.cpp');
const screen = maskCpp(rawScreen);
const modelHeader = readRepoFile('src/ui/tabs/settings/settings_model.h');
const rawModel = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
const model = maskCpp(rawModel);
const style = readRepoFile('src/ui/tabs/settings/settings_style.h');
const preview = readRepoFile('tools/settings-preview.mjs');

const between = (source, from, to) => {
  const start = source.indexOf(from);
  const end = source.indexOf(to, start);
  assert.ok(start >= 0 && end > start, `${from} .. ${to}`);
  return source.slice(start, end);
};

// --- Translations ---------------------------------------------------------------------
const keys = ['audio_label', 'settings_speaker', 'settings_volume', 'settings_microphone',
  'settings_mic_sensitivity', 'settings_mic_off'];
const expected = {
  kStringsEn: ['Audio', 'Speaker', 'Volume', 'Microphone', 'Sensitivity', 'Microphone off'],
  kStringsDe: ['Audio', 'Lautsprecher', 'Lautstärke', 'Mikrofon', 'Empfindlichkeit', 'Mikrofon aus'],
  kStringsFr: ['Audio', 'Haut-parleur', 'Volume', 'Microphone', 'Sensibilité', 'Microphone coupé'],
  kStringsPl: ['Dźwięk', 'Głośnik', 'Głośność', 'Mikrofon', 'Czułość', 'Mikrofon wyłączony'],
};
const struct = header.slice(header.indexOf('struct Strings {'), header.indexOf('\n};', header.indexOf('struct Strings {')));
const fields = [...struct.replace(/\/\/[^\n]*/g, '').matchAll(/const char\*\s+(\w+);/g)].map(match => match[1]);
const first = fields.indexOf(keys[0]);
assert.ok(first >= 0, 'the Audio strings are in the Strings struct');
assert.deepEqual(fields.slice(first, first + keys.length), keys, 'the Audio strings sit together');
const tables = [...new Set([...i18n.matchAll(/\{&(kStrings\w+), &kLocale\w+\}/g)].map(match => match[1]))];
assert.deepEqual([...tables].sort(), Object.keys(expected).sort(), 'every registered language is checked');
for (const table of tables) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  const values = [...i18n.slice(start, i18n.indexOf('};', start)).matchAll(/"((?:\\.|[^"\\])*)"/g)]
    .map(match => match[1]);
  assert.equal(values.length, fields.length, `${table} has one value per field`);
  assert.deepEqual(values.slice(first, first + keys.length), expected[table], `${table} Audio strings`);
}

// --- Categories -----------------------------------------------------------------------------
// The values stay fixed; Audio is appended and shown before System only where
// the panel has a speaker.
assert.match(screen, /enum class Category : uint8_t \{ Display = 0, Wifi, Localization, System, Audio \};/);
assert.match(screen, /constexpr uint8_t kCategoryCount = 5;/);
assert.match(style, /constexpr uint32_t kAudioColor = 0x[0-9A-F]{6};/);
const order = between(screen, 'void build_order() {', 'bool category_shown(');
assert.match(order, /Category::Display, Category::Wifi, Category::Localization, Category::Audio,\s*Category::System/);
assert.match(order, /if \(category == Category::Audio && !settings_model::audio_available\(\)\) continue;/);
// Taps and show_category() only reach shown categories; System stays index 3.
assert.match(screen, /if \(raw >= kCategoryCount \|\| !category_shown\(static_cast<uint8_t>\(raw\)\)\) return;/);
assert.match(screen, /if \(index < kCategoryCount && category_shown\(index\) && !setup_screen::shown\(\)\)/);
assert.match(model, /settings_screen::show_category\(3\);/);
// Every layout places the shown categories by their slot.
const frame = between(screen, 'void build_frame() {', 'void select_category(Category category) {');
assert.match(frame, /build_order\(\);\s*if \(!category_shown\(static_cast<uint8_t>\(g_category\)\)\) g_category = Category::Display;/);
assert.doesNotMatch(frame, /\(kCategoryCount - 1\)|kCategoryCount \* d|static_cast<float>\(kCategoryCount\)/,
  'the frame lays out g_shown categories, not the enum size');
for (const layout of [/left \+ slot \* \(d \+ gap\)/, /body_y \+ slot \* \(tile_h \+ GRID_GAP\)/,
  /body_x \+ slot \* \(tile_w \+ GRID_GAP\)/]) {
  assert.match(frame, layout);
}
// Upright, five names may take the narrower margin; four keep the old one.
assert.match(frame, /const int narrow = g_shown > 4 \? settings_style::tile_inset\(\) : 2 \* settings_style::tile_inset\(\);/);

// --- Category line ---------------------------------------------------------------------------
const lines = between(rawScreen, 'void category_line(', 'void on_lines_timer(');
assert.match(lines, /case Category::Audio: \{[\s\S]*?audio\.mic_available && !audio\.mic_on[\s\S]*?s\.settings_mic_off[\s\S]*?"%d %%"/);

// --- Audio page -------------------------------------------------------------------------------
const page = between(screen, 'void build_audio_page(lv_obj_t* page) {', 'void clear_locale_refs() {');
const rawPage = between(rawScreen, '// ---------- Audio page ----------', '// ---------- Localization page ----------');
assert.doesNotMatch(rawPage, /"[A-Z][a-z]+/, 'no display text in the page code');
assert.match(page, /section\(page, s\.settings_speaker, true\)/);
assert.match(page, /s\.settings_volume, width, 0, 100,\s*values\.volume, on_volume/);
assert.match(page, /if \(!values\.mic_available\) return;\s*settings_parts::section\(page, s\.settings_microphone, false\)/);
assert.match(page, /settings_parts::toggle\(on\.row, values\.mic_on, settings_style::kAudioColor, palette\.group, true, on_mic_toggle,/);
assert.match(page, /s\.settings_mic_sensitivity, width, 0, values\.mic_gain_max,\s*values\.mic_gain, on_mic_gain/);
// The switch rebuilds the page after its own touch, never inside it.
assert.match(rawPage, /void on_mic_toggle\(lv_event_t\*\) \{\s*settings_model::mic_selected\(!settings_model::audio_values\(\)\.mic_on\);\s*lv_async_call\(audio_rebuild_async, nullptr\);/);
// Sensitivity reads in dB with the half steps (33, 34.5 ...).
assert.match(rawPage, /"%u\.%u dB"[\s\S]*?"%u dB"/);
const build = between(screen, 'void build_page() {', 'void clear_refs() {');
assert.match(build, /clear_audio_refs\(\);/);
assert.match(build, /if \(g_category == Category::Audio\) build_audio_page\(g_page\);/);

// --- Model ------------------------------------------------------------------------------------
for (const declaration of ['bool audio_available();', 'AudioValues audio_values();',
  'unsigned mic_gain_tenths_db(int step);', 'void volume_changed(int percent, bool final);',
  'void mic_selected(bool on);', 'void mic_gain_changed(int step, bool final);']) {
  assert.ok(modelHeader.includes(declaration), declaration);
}
assert.match(model, /bool audio_available\(\) \{ return speaker::supported\(\); \}/);
assert.match(model, /values\.mic_available = speaker::micCapability\(\);/);
assert.match(model, /values\.mic_on = speaker::micEnabled\(\);/);
assert.match(model, /values\.mic_gain_max = speaker::kMaxMicGainStep;/);
// Volume and sensitivity are taken (and stored) once the finger lifts.
assert.match(model, /void volume_changed\(int percent, bool final\) \{\s*if \(final\) speaker::setVolume\(/);
assert.match(model, /void mic_gain_changed\(int step, bool final\) \{\s*if \(final\) speaker::setMicGainStep\(/);
// The panel itself may switch the microphone on (unlike plain MQTT).
assert.match(rawModel, /void mic_selected\(bool on\) \{ speaker::setMicEnabled\(on, "panel"\); \}/);

// --- Host preview ---------------------------------------------------------------------------
assert.match(preview, /audio: 4,/);
assert.match(preview, /bool audio_available\(\) \{ return \$\{audio \? 'true' : 'false'\}; \}/);
for (const icon of ['volume-high', 'volume-off', 'microphone', 'microphone-off', 'microphone-settings']) {
  assert.ok(preview.includes(`'${icon}'`), `the preview has the ${icon} icon`);
}
// Renders the real page where LVGL and a host compiler are set up.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const run = spawnSync(process.execPath, [path.join(root, 'tools/settings-preview.mjs'), 'p1024', '--page=audio',
  '--no-mockup'], {encoding: 'utf8', timeout: 900000});
const output = `${run.stdout}${run.stderr}`;
if (/SKIP:/.test(output)) {
  console.log('Settings Audio page: source checks passed; render skipped (no LVGL host)');
} else {
  assert.equal(run.status, 0, output);
  assert.match(output, /p1024-audio\.png/);
  console.log('Settings Audio page: translated texts, category order, model and host render');
}
