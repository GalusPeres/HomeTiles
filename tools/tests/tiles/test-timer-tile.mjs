// Timer tile (types/timer): the stable type ID and its registry entry, the
// policies it joins, the loop wiring, the central translations it shows in
// every language, the icon picked from a timer's name (timer_icons.h with
// each language's word list, host-compiled), and the Web Admin preview of
// the tile without a timer.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {extractDeliveredFunction, readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const read = (file) => readRepoFile(file).replace(/\r\n?/g, '\n');

// --- Stable ID, registry, policies ------------------------------------------
const tileType = read('src/types/tile_type.h');
assert.match(tileType, /TILE_VACUUM = 29,\n(?:\s*\/\/.*\n)*\s*TILE_TIMER = 30\n\};/, 'TILE_TIMER is appended as 30');
const registry = read('src/types/types_registry.cpp');
assert.match(registry, /\{\s*TILE_TIMER,\s*"Timer",\s*"timer_tile",\s*"timer_tile",\s*"timer_tile",\s*nullptr,\s*"loadTimerFields",\s*"saveTimerFields",\s*"resetTimerFields",\s*0x2A2A2A,\s*false,\s*render_timer_wrapper,\s*apply_timer_wrapper,\s*append_timer_fields_wrapper,\s*append_timer_styles,\s*append_timer_scripts\s*\}/);
assert.match(registry, /case TILE_TIMER: return tr\.timer_default_name;/);
assert.match(registry, /if \(entry\.type == TILE_TIMER && !voice_timers::supported\(\)\) continue;/,
             'the type list offers the Timer tile on speaker boards only');
const policy = read('src/types/tile_type_policy.h');
assert.match(policy, /tileTypeHasFixedIconColorOnly\(int type\) \{[^}]*type == TILE_TIMER;/);
for (const name of ['entityTileStoresSensorEntity', 'tileTypeUsesCachedEntityState', 'tileTypeStoresPopupMode',
                    'tileTypeHasDynamicMqttRoute', 'tileTypeSubscribesDynamicState', 'tileTypeAllowedInScreensaver']) {
  const body = policy.match(new RegExp(`${name}\\([^)]*\\) \\{[^}]*\\}`))[0];
  assert.doesNotMatch(body, /TILE_TIMER/, `${name}: the Timer tile has no entity and no screensaver place`);
}
const handler = read('src/types/timer/web_handler.cpp');
assert.match(handler, /tile\.sensor_entity = "";/, 'an earlier type\'s entity goes');
assert.match(read('src/web/admin/tiles/icon-colors.js'), /const ICON_COLOR_FIXED_TYPES = \[[^\]]*'30'\];/);
assert.ok(JSON.parse(read('src/web/admin/bundle.json')).sources.includes('src/types/timer/admin.js'));

// --- Loop wiring and lifecycle -----------------------------------------------
const ui = read('src/ui/timers/timer_ui.cpp');
assert.match(ui, /refresh_popup\(false\);\n\s+timer_tile::refresh\(\);/, 'the tiles count down on the timer tick');
const renderer = read('src/types/timer/renderer.cpp');
assert.match(renderer, /lv_obj_add_event_cb\(card, forget_view, LV_EVENT_DELETE, view\);/, 'a view goes with its card');
assert.match(renderer, /delete view;/);
assert.match(renderer, /lv_obj_add_event_cb\(card, on_card, LV_EVENT_CLICKED, nullptr\);/);
assert.match(renderer, /void on_card\(lv_event_t\*\) \{ timer_ui::open_popup\(\); \}/);
// From 2x2: the next timer large with a progress bar, the others as rows
// with their own +1 min, sorted by the time left, "+N" when they overflow.
assert.match(renderer, /view->big = view->wide && tile\.span_h >= 2;/);
assert.match(renderer, /void on_row_add\(lv_event_t\* event\) \{[^}]*requestAdd\(row->id, 60\)/);
assert.match(renderer, /lv_obj_set_width\(view\.bar_fill, w\)/);
assert.match(renderer, /others\[other_count\+\+\] = other;/, 'rows follow the list order (soonest first)');
assert.match(read('src/ui/timers/voice_timers.cpp'), /voice_timer::sortSoonest\(&list\);\n\s*s->list = list;/);
assert.match(read('src/ui/timers/voice_timers.cpp'), /bool nextTimer\([^)]*\) \{ return timerAt\(0, out, left_s\); \}/);
// Boards without voice timers (a tile from a backup) offer no presets.
assert.match(renderer, /set_hidden\(preset, has \|\| !voice_timers::supported\(\)\)/);
assert.match(renderer, /voice_timers::supported\(\) \? tr\(\)\.timer_tap_start : tr\(\)\.timer_none/);
// Unnamed timers show their length, never the tile's title.
assert.match(renderer, /timer_ui::timer_label\(timer, text, sizeof\(text\)\)/);
assert.doesNotMatch(renderer, /return view\.idle_title\.c_str\(\);/);
assert.match(read('src/ui/timers/timer_ui.cpp'), /snprintf\(out, size, tr\(\)\.timer_unnamed, length\);/);
// Voice names arrive in lower case: the first letter is capitalized.
assert.match(read('src/ui/timers/timer_ui.cpp'), /if \(out\[0\] >= 'a' && out\[0\] <= 'z'\) out\[0\] = /);
assert.match(read('src/ui/timers/timer_ui.cpp'), /first\.name\[0\] \? TimerName\(first\)\.text : tr\(\)\.timer_done_title/);
assert.match(renderer, /tile_surface::scope\(view->card\)/, 'later updates draw on the card\'s surface');
// Visible text only from the central translations: literals hold no words.
for (const call of renderer.matchAll(/(?:set_text|lv_label_set_text|button_text)\([^,]+,\s*"([^"]*)"/g)) {
  assert.doesNotMatch(call[1], /[A-Za-z]/, `literal display text in timer/renderer.cpp: "${call[1]}"`);
}
for (const field of ['timer_default_name', 'timer_paused_name', 'timer_of', 'timer_add_minute', 'timer_minutes',
                     'timer_none', 'timer_tap_start', 'timer_icon_words']) {
  assert.match(renderer, new RegExp(`tr\\(\\)\\.${field}`), `the tile shows ${field}`);
}
const scripts = read('src/types/timer/web_scripts.cpp');
for (const key of ['defaultName', 'tapStart', 'none', 'minutes']) {
  assert.match(scripts, new RegExp(`append_i18n\\("${key}", tr\\.`), `TIMER_I18N.${key} comes from Strings`);
}
assert.match(read('src/types/timer/web_html.cpp'), /tr\.timer_tile_note/);
assert.match(read('tools/generate-mdi-bar-icon-fonts.mjs'), /'pause', 'play', 'close',/, 'the button icons are in the bar fonts');
for (const size of [15, 18, 22, 26, 34]) {
  const font = read(`src/fonts/mdi_bar_icons_${size}.c`);
  for (const cp of ['U+F03E4', 'U+F040A', 'U+F0156']) assert.ok(font.includes(cp), `mdi_bar_icons_${size} has ${cp}`);
}

// --- Translations in every language ------------------------------------------
const header = read('src/core/i18n/i18n.h');
const fields = [...header.slice(header.indexOf('struct Strings {'), header.indexOf('const char* code;'))
  .matchAll(/const char\* (\w+);/g)].map((m) => m[1]);
assert.deepEqual(fields.slice(11), ['timer_tap_start', 'timer_of', 'timer_unnamed', 'timer_icon_words', 'timer_tile_note']);
const i18n = read('src/core/i18n/i18n.cpp');
const tables = new Map([...i18n.matchAll(/static const Strings kStrings(\w+) = \{\n((?:\s+".*",\n){16})/g)]
  .map(([, lang, block]) => [lang, [...block.matchAll(/^\s+"(.*)",$/gm)].map((m) => JSON.parse(`"${m[1]}"`))]));
assert.deepEqual([...tables.keys()].sort(), ['Cs', 'De', 'En', 'Es', 'Fr', 'Pl', 'Ru', 'Sk']);
const ICONS = new Set(read('src/tiles/icons/mdi_icons.cpp').match(/\{"[a-z0-9-]+", 0x/g).map((m) => m.slice(2, -5)));
for (const [lang, values] of tables) {
  const [tap, of, unnamed, words, note] = values.slice(11);
  assert.ok(tap.length && note.length, `${lang} has the hint and the note`);
  assert.equal((of.match(/%s/g) || []).length, 2, `${lang} timer_of keeps two %s`);
  assert.equal((unnamed.match(/%s/g) || []).length, 1, `${lang} timer_unnamed keeps one %s`);
  for (const group of words.split('; ')) {
    const [icon, list] = group.split('=');
    assert.ok(ICONS.has(icon), `${lang}: ${icon} is an MDI icon`);
    for (const word of list.split(', ')) {
      assert.ok(word.length > 0 && word === word.toLowerCase(), `${lang}: "${word}" is a lower-case word`);
    }
  }
}

// --- Icons from the name (host program) --------------------------------------
function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}
const compiler = findCompiler();
if (compiler) {
  const words = (lang) => JSON.stringify(tables.get(lang)[14]);
  // [language, name, icon or "" for no match]; English is the second chance.
  const cases = [
    ['En', 'Pasta', 'pot-steam'], ['En', 'eggs', 'egg'], ['En', 'Steak', 'grill'],
    ['En', 'Laundry', 'washing-machine'], ['En', 'dishwasher', 'dishwasher'], ['En', 'pizza oven', 'pizza'],
    ['En', 'Unicorn', ''], ['En', '', ''], ['En', 'Tea', 'tea'],
    ['De', 'Nudeln', 'pot-steam'], ['De', 'Eier', 'egg'], ['De', 'Ei', 'egg'], ['De', 'Eis', ''],
    ['De', 'Wäsche', 'washing-machine'], ['De', 'Spülmaschine', 'dishwasher'], ['De', 'Bad', 'shower'],
    ['De', 'Badminton', ''], ['De', 'pasta', 'pot-steam'], ['De', 'laundry', 'washing-machine'],
    ['Fr', 'thé', 'tea'], ['Fr', 'Œufs', ''], ['Fr', 'oeufs', 'egg'], ['Fr', 'linge', 'washing-machine'],
    ['Es', 'té', 'tea'], ['Es', 'Huevos', 'egg'], ['Es', 'pan', 'bread-slice'], ['Es', 'pantalla', ''],
    ['Pl', 'jajka', 'egg'], ['Pl', 'lekcja', 'book-open-variant'], ['Pl', 'leki', 'pill'],
    ['Cs', 'vejce', 'egg'], ['Cs', 'prádlo', 'washing-machine'],
    ['Sk', 'vajcia', 'egg'], ['Sk', 'polievka', 'pot-steam'], ['Sk', 'polievanie', 'sprout'],
    ['Ru', 'Паста', 'pot-steam'], ['Ru', 'яйца', 'egg'], ['Ru', 'ЯИЧНИЦА', 'egg'], ['Ru', 'Чай', 'tea'],
    ['Ru', 'Ёлка', ''], ['Ru', 'бегемот', ''], ['Ru', 'бег', 'run'], ['Ru', 'Рис', 'pot-steam'], ['Ru', 'рисование', ''],
  ];
  const lines = cases.map(([lang, name, icon]) =>
    `  check(${words(lang)}, ${JSON.stringify(name)}, ${JSON.stringify(icon)});`).join('\n');
  const source = String.raw`
#include "src/types/timer/timer_icons.h"
#include <cstdio>
#include <cstring>
static int failures = 0;
static const char* kEnglish = ${words('En')};
static void check(const char* words, const char* name, const char* want) {
  char icon[timer_icons::kMaxIconChars + 1] = "";
  const bool found = timer_icons::match_any(name, words, kEnglish, icon, sizeof(icon));
  if (found != (want[0] != 0) || (found && strcmp(icon, want) != 0)) {
    printf("'%s': got %s, want %s\n", name, found ? icon : "(none)", want[0] ? want : "(none)");
    ++failures;
  }
}
int main() {
${lines}
  char tiny[4];
  if (timer_icons::match("Pasta", "pot-steam=pasta", tiny, sizeof(tiny))) { puts("icon longer than the buffer"); ++failures; }
  if (timer_icons::match("Pasta", nullptr, tiny, sizeof(tiny))) ++failures;
  return failures ? 1 : 0;
}
`;
  const out = path.join(repoRoot, 'build/tests/timer-tile');
  fs.mkdirSync(out, {recursive: true});
  const cpp = path.join(out, 'timer_icons.cpp');
  const exe = path.join(out, process.platform === 'win32' ? 'timer_icons.exe' : 'timer_icons');
  fs.writeFileSync(cpp, source);
  const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', repoRoot, cpp, '-o', exe],
                          {encoding: 'utf8'});
  assert.equal(build.status, 0, `timer_icons did not compile:\n${build.stdout}${build.stderr}`);
  const run = spawnSync(exe, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, `timer icons:\n${run.stdout}${run.stderr}`);
} else {
  console.log('SKIP: timer icon program needs a host C++ compiler (the other checks ran)');
}

// --- Web Admin preview ---------------------------------------------------------
const escapeHtml = (value) => String(value).replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
const preview = new Function('escapeHtml', 'isExplicitlyDisabledValue', 'panelVar', 'TIMER_I18N', `
  const TIMER_PRESET_MINUTES = [1, 5, 10, 15];
  ${['timerPreviewText', 'timerPreviewIcon', 'timerPreviewHtml'].map(extractDeliveredFunction).join('\n')}
  return {timerPreviewIcon, timerPreviewHtml};`)(
  escapeHtml, (value) => ['none', '-'].includes(String(value)), (name, fallback) => name === '--timer-hint-span' ? '1.5' : fallback,
  {defaultName: 'Minuteur', tapStart: 'Toucher pour démarrer', none: 'Aucun minuteur en cours', minutes: '%d min'});
assert.equal(preview.timerPreviewIcon('', ''), 'timer-outline');
assert.equal(preview.timerPreviewIcon('none', ''), '');
assert.equal(preview.timerPreviewIcon('pot-steam', 'pot-steam'), 'pot-steam');
const small = preview.timerPreviewHtml('', 1, 1);
assert.match(small, /tile-timer-value">--:--<\/div><div class="tile-timer-title">Minuteur<\/div><\/div>$/);
assert.doesNotMatch(small, /Toucher/, 'a 1x1 tile has no room for the hint');
assert.match(preview.timerPreviewHtml('Küche', 1, 2), /Küche<\/div><div class="tile-timer-hint">Toucher pour démarrer/);
assert.match(preview.timerPreviewHtml('<b>', 1, 1), /&#60;b&#62;/, 'the title is escaped');
const wide = preview.timerPreviewHtml('Kitchen', 2, 1);
assert.match(wide, /tile-timer-head"><div class="tile-timer-title">Kitchen<\/div><div class="tile-timer-hint">Aucun minuteur en cours/);
assert.equal((wide.match(/tile-timer-preset"/g) || []).length, 4);
assert.match(wide, />15 min<\/span><\/div>$/);
const live = read('src/web/admin/tiles/live-preview.js');
const grid = read('src/web/admin/tiles/grid-preview.js');
for (const [name, source] of [['live', live], ['grid', grid]]) {
  assert.match(source, /if \(previewKind === 'timer_tile'\) iconName = timerPreviewIcon\(rawIcon, iconName\);/, `${name} preview icon`);
  assert.match(source, /&& previewKind !== 'timer_tile'\) \{\n\s+html \+= '<div class="tile-title"/, `${name} preview keeps the title at the bottom`);
  assert.match(source, /if \(previewKind === 'timer_tile'\) html \+= timerPreviewHtml\(/, `${name} preview body`);
}
assert.match(read('src/web/server/render/web_admin_styles.cpp'), /emit_scaled\("timer-disc-mid", timer_tile::kPadTop \+ timer\.center_y\);/);
console.log('Timer tile ok');
