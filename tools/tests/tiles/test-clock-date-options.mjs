// Clock tile options (user 2026-10-12, issue #13): the weekday (none, short,
// long), more date formats (21/08/2026, 21.08., 21. August, 21. August 2026,
// month names by language, Polish in the genitive) and rolling digits, kept
// with the date format in one value (Tile::sensor_gauge_max). The panel's
// clock_tile::format_date_line and the preview's getClockPreviewDate give the
// same text for every format, weekday and language.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// The language tables as the panel has them (i18n.cpp), for a small i18n.h
// stand-in on the host (web_scripts.cpp CLOCK_I18N comes from the same).
// Every registered language, its LocaleProfile read field by field (i18n.h
// declares the order).
const i18n = read('src/core/i18n/i18n.cpp').replace(/^\s*\/\/[^\n]*$/gm, '');
const localeFields = [.../struct LocaleProfile \{([\s\S]*?)\n\};/.exec(read('src/core/i18n/i18n.h'))[1]
  .replace(/\/\/[^\n]*/g, '').matchAll(/^\s*(?:const char\*|uint8_t)\s+(\w+)(?:\[\d+\])?;/gm)].map(m => m[1]);
// The top-level entries of the initializer that opens at `at` (strings and
// nested braces kept whole).
const entries = (text, at) => {
  const out = [];
  let depth = 0;
  let start = at + 1;
  for (let i = at; i < text.length; i++) {
    const c = text[i];
    if (c === '"') {
      for (i++; text[i] !== '"'; i++) if (text[i] === '\\') i++;
      continue;
    }
    if (c === '{') depth++;
    if (c === '}' && --depth === 0) {
      out.push(text.slice(start, i).trim());
      break;
    }
    if (c === ',' && depth === 1) {
      out.push(text.slice(start, i).trim());
      start = i + 1;
    }
  }
  return out.filter(Boolean);
};
const strings = entry => [...entry.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => JSON.parse(`"${m[1]}"`));
const table = (code) => {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${code} = {`);
  const values = entries(i18n, i18n.indexOf('{', start));
  const field = name => values[localeFields.indexOf(name)];
  return {months: strings(field('month_names_in_date')), weekdays: strings(field('weekday_names')),
    weekdaysShort: strings(field('weather_weekdays_short')), pattern: strings(field('long_date_pattern'))[0],
    patternYear: strings(field('long_date_year_pattern'))[0]};
};
const LANGS = [...i18n.matchAll(/\{&kStrings\w+, &kLocale(\w+)\}/g)].map(m => m[1].toLowerCase());
const TABLES = Object.fromEntries(LANGS.map(lang => [lang, table(lang[0].toUpperCase() + lang.slice(1))]));
const cstr = v => JSON.stringify(v);
const shim = `#pragma once
#include <stdint.h>
#include <string.h>
namespace i18n {
struct LocaleProfile {
  const char* weekday_names[7];
  const char* weather_weekdays_short[7];
  const char* month_names_in_date[12];
  const char* long_date_pattern;
  const char* long_date_year_pattern;
  uint8_t default_time_format;
  uint8_t default_date_format;
};
inline const LocaleProfile& locale(const char* code) {
${LANGS.map(lang => `  static const LocaleProfile k_${lang} = {{${TABLES[lang].weekdays.map(cstr).join(', ')}}, {${TABLES[lang].weekdaysShort.map(cstr).join(', ')}}, {${TABLES[lang].months.map(cstr).join(', ')}}, ${cstr(TABLES[lang].pattern)}, ${cstr(TABLES[lang].patternYear)}, 1, 1};`).join('\n')}
${LANGS.map(lang => `  if (code && strcmp(code, ${cstr(lang)}) == 0) return k_${lang};`).join('\n')}
  return k_en;
}
}  // namespace i18n
`;

// Panel: every case for the 21st of August 2026 (a Friday) and the 10th of
// October 2026 (a Saturday), in every language.
const cases = [];
for (const lang of LANGS) {
  for (const [wday, day, month] of [[5, 21, 7], [6, 10, 9]]) {
    for (let format = 1; format <= 7; ++format) {
      for (const weekday of [0, 1, 2]) {
        for (const show of [1, 0]) cases.push({lang, wday, day, month, format, weekday, show});
      }
    }
  }
}
const harness = String.raw`
#include "src/types/clock/clock_format.h"
#include <cstdio>
#include <initializer_list>
struct Case { const char* lang; int wday, day, month, format, weekday, show; };
static const Case kCases[] = {${cases.map(c => `{"${c.lang}", ${c.wday}, ${c.day}, ${c.month}, ${c.format}, ${c.weekday}, ${c.show}}`).join(', ')}};
int main() {
  for (const Case& c : kCases) {
    char out[96];
    clock_tile::format_date_line(out, sizeof(out), c.wday, c.day, c.month, 2026, c.show != 0, c.format, c.weekday, c.lang);
    std::printf("%s|\n", out);
  }
  // Packing: old values 0-3 stay the format alone; unknown bits drop.
  for (int raw : {0, 1, 3, 7, 0x106, 0x207, 0x405, 0x301, 0x7FF, -5, 9, 0x806, 0x1003, 0x1801, 0x2006, 0x4000, 0x6003,
                  0x54000, 0x64000, 0x1C4000, 0x8003}) {
    const clock_tile::TileDateOptions o = clock_tile::unpack_date_options(raw);
    std::printf("pack %d %d %d %d %d %d %d %d %d\n", raw, o.date_format, o.weekday, o.roll ? 1 : 0, o.align, o.style,
                o.dial, o.hands, static_cast<int>(clock_tile::normalize_date_options(raw)));
  }
  // The month formats' list labels: patterns in the locale's order.
  for (const char* lang : {${LANGS.map(cstr).join(', ')}}) {
    for (int format : {6, 7}) {
      char pattern[32];
      clock_tile::format_date_pattern(pattern, sizeof(pattern), static_cast<uint8_t>(format), lang);
      std::printf("pattern %s %d %s|\n", lang, format, pattern);
    }
  }
  return 0;
}
`;
const output = compileAndRun({label: 'Clock date options', harness, files: {'src/core/i18n/i18n.h': shim}});
if (output !== null) {
  const lines = output.split('\n').filter(Boolean);
  const texts = lines.slice(0, cases.length).map(l => l.replace(/\|\r?$/, ''));
  const at = (lang, day, format, weekday, show = 1) =>
    texts[cases.findIndex(c => c.lang === lang && c.day === day && c.format === format && c.weekday === weekday && c.show === show)];
  assert.equal(at('de', 21, 1, 0), '21.08.2026');
  assert.equal(at('de', 21, 4, 0), '21/08/2026', 'issue #13: DD/MM/YYYY');
  assert.equal(at('de', 21, 5, 0), '21.08.');
  assert.equal(at('de', 21, 6, 0), '21. August');
  assert.equal(at('en', 21, 7, 0), '21 August 2026', 'issue #13: DD MMMM YYYY');
  assert.equal(at('fr', 10, 7, 0), '10 octobre 2026');
  assert.equal(at('pl', 21, 6, 0), '21 sierpnia', 'Polish month in the genitive');
  assert.equal(at('de', 10, 6, 2), 'Samstag, 10. Oktober', 'design 1 "Mit Wochentag"');
  assert.equal(at('de', 10, 1, 1), 'Sa, 10.10.2026');
  assert.equal(at('de', 10, 1, 2, 0), 'Samstag', 'the weekday alone');
  assert.equal(at('de', 10, 1, 0, 0), '', 'nothing without both');
  const packs = Object.fromEntries(lines.slice(cases.length).filter(l => l.startsWith('pack '))
    .map(l => l.trim().split(' ')).map(([, raw, ...v]) => [raw, v.map(Number)]));
  // The list shows patterns like its other choices, never a fixed date
  // (user 2026-10-14).
  const patterns = Object.fromEntries(lines.filter(l => l.startsWith('pattern '))
    .map(l => l.replace(/\|\r?$/, '').match(/^pattern (\w+) (\d) (.*)$/)).map(m => [m[1] + m[2], m[3]]));
  assert.deepEqual(patterns, {de6: 'DD. MMMM', de7: 'DD. MMMM YYYY', en6: 'DD MMMM', en7: 'DD MMMM YYYY',
    fr6: 'DD MMMM', fr7: 'DD MMMM YYYY', pl6: 'DD MMMM', pl7: 'DD MMMM YYYY', cs6: 'DD. MMMM', cs7: 'DD. MMMM YYYY',
    es6: 'DD de MMMM', es7: 'DD de MMMM de YYYY', sk6: 'DD. MMMM', sk7: 'DD. MMMM YYYY'});
  // The year in the language's own order (user 2026-10-18: Spanish "de").
  assert.equal(at('es', 21, 7, 0), '21 de agosto de 2026');
  assert.equal(at('es', 21, 6, 2), 'Viernes, 21 de agosto');
  assert.equal(at('cs', 21, 7, 0), '21. srpna 2026', 'Czech month in the genitive');
  assert.equal(at('sk', 21, 7, 0), '21. augusta 2026', 'Slovak month in the genitive');
  assert.deepEqual(packs['3'], [3, 0, 0, 0, 0, 0, 0, 3], 'an old tile: the format alone, centered, digital');
  assert.deepEqual(packs[String(0x106)], [6, 1, 0, 0, 0, 0, 0, 0x106]);
  assert.deepEqual(packs[String(0x405)], [5, 0, 1, 0, 0, 0, 0, 0x405], 'rolling digits');
  assert.deepEqual(packs[String(0x301)], [1, 0, 0, 0, 0, 0, 0, 1], 'an unknown weekday drops');
  assert.deepEqual(packs['-5'], [0, 0, 0, 0, 0, 0, 0, 0]);
  assert.deepEqual(packs['9'], [0, 0, 0, 0, 0, 0, 0, 0], 'an unknown format falls back to Auto');
  assert.deepEqual(packs[String(0x806)], [6, 0, 0, 1, 0, 0, 0, 0x806], 'left');
  assert.deepEqual(packs[String(0x1003)], [3, 0, 0, 2, 0, 0, 0, 0x1003], 'right');
  assert.deepEqual(packs[String(0x1801)], [1, 0, 0, 0, 0, 0, 0, 1], 'an unknown alignment drops');
  // Clock styles (user 2026-10-12): analog, the wall clock with its dial and
  // hands colors; unknown values drop to digital, slate and gold.
  assert.deepEqual(packs[String(0x2006)], [6, 0, 0, 0, 1, 0, 0, 0x2006], 'analog');
  assert.deepEqual(packs[String(0x4000)], [0, 0, 0, 0, 2, 0, 0, 0x4000], 'wall clock');
  // The big date (3) was removed (user 2026-10-12): such tiles are digital.
  assert.deepEqual(packs[String(0x6003)], [3, 0, 0, 0, 0, 0, 0, 3], 'the removed big date shows the digital clock');
  assert.deepEqual(packs[String(0x8003)], [3, 0, 0, 0, 0, 0, 0, 3], 'an unknown style drops');
  assert.deepEqual(packs[String(0x54000)], [0, 0, 0, 0, 2, 5, 0, 0x54000], 'terracotta dial');
  assert.deepEqual(packs[String(0x64000)], [0, 0, 0, 0, 2, 0, 0, 0x4000], 'an unknown dial drops');
  assert.deepEqual(packs[String(0x1C4000)], [0, 0, 0, 0, 2, 4, 3, 0x1C4000], 'navy dial, white hands');

  const helpers = ['normalizeClockDateFormat', 'resolveClockDateFormat', 'getClockPreviewLanguage', 'getClockPreviewDate',
    'clockDateOptions', 'packClockDateOptions'].map(extractDeliveredFunction).join('\n');
  for (const lang of LANGS) {
    const texts18 = TABLES[lang];
    assert.equal(texts18.months.length, 12, `${lang} months`);
    assert.equal(texts18.weekdays.length, 7, `${lang} weekdays`);
    assert.ok(texts18.pattern, `${lang} pattern`);
    for (const c of cases.filter(k => k.lang === lang)) {
      const ctx = {CLOCK_I18N: texts18, document: {getElementById: () => null, documentElement: {lang}},
        Date: class extends Date { constructor() { super(2026, c.month, c.day, 12, 0, 0); } }};
      vm.createContext(ctx);
      vm.runInContext(helpers, ctx);
      const preview = vm.runInContext(`getClockPreviewDate(${c.format}, ${c.weekday}, ${c.show === 1})`, ctx);
      assert.equal(preview, texts[cases.indexOf(c)], `${lang} ${c.day}.${c.month + 1} format ${c.format} weekday ${c.weekday} date ${c.show}`);
    }
    const ctx = {document: {getElementById: () => null, documentElement: {lang}}};
    vm.createContext(ctx);
    vm.runInContext(helpers, ctx);
    assert.equal(vm.runInContext('packClockDateOptions(6, 2, true)', ctx), 6 | (2 << 8) | 1024);
    assert.equal(vm.runInContext('packClockDateOptions(6, 2, true, 2)', ctx), 6 | (2 << 8) | 1024 | (2 << 11));
    assert.equal(vm.runInContext('JSON.stringify(clockDateOptions(3))', ctx), JSON.stringify({format: 3, weekday: 0, roll: false, align: 0, style: 0, dial: 0, hands: 0}));
    // The preview reads and writes the styles like the panel.
    for (const [raw, packed] of Object.entries(packs)) {
      const [format, weekday, roll, align, style, dial, hands, normalized] = packed;
      assert.equal(vm.runInContext(`JSON.stringify(clockDateOptions(${raw}))`, ctx),
        JSON.stringify({format, weekday, roll: roll === 1, align, style, dial, hands}), `options ${raw}`);
      assert.equal(vm.runInContext(`packClockDateOptions(${format}, ${weekday}, ${roll === 1}, ${align}, ${style}, ${dial}, ${hands})`, ctx),
        normalized, `pack ${raw}`);
    }
  }
}

// One value from the editor to the panel: the Web Admin sends it packed as
// clock_date_format (also what import, export and drafts carry), the server
// keeps it valid, the renderer unpacks it.
const admin = read('src/types/clock/admin.js');
assert.match(admin, /formData\.append\('clock_date_format', String\(packClockDateOptions\(/);
assert.match(admin, /const dateOptions = clockDateOptions\(/);
assert.match(read('src/types/clock/web_handler.cpp'), /clock_tile::normalize_date_options\(server\.arg\("clock_date_format"\)\.toInt\(\)\)/);
const renderer = read('src/types/clock/renderer.cpp');
assert.match(renderer, /clock_tile::unpack_date_options\(tile\.sensor_gauge_max\)/);
assert.match(renderer, /widget_config\.roll = date_options\.roll;/);
// Rolling: LVGL animations on the cell strips, only without text shadow,
// the first text without a roll.
assert.match(renderer, /if \(config\.roll && !config\.text_shadow\) \{\s*time_label = create_clock_roll_line\(/);
assert.match(renderer, /lv_anim_set_exec_cb\(&anim, clock_roll_y\);[\s\S]*?lv_anim_set_completed_cb\(&anim, clock_roll_done\);/);
assert.match(renderer, /update_clock_labels\(data\);\s*\/\/ The first text appears at once; later minutes roll\.\s*data->roll_started = true;/);
// Web fields: the roll checkbox, the new formats by example, the weekday.
const html = read('src/types/clock/web_html.cpp');
for (const id of ['_clock_roll', '_clock_weekday']) assert.ok(html.includes(id + '"'), id);
assert.match(html, /<option value="4">DD\/MM\/YYYY<\/option>/);
assert.match(html, /clock_tile::format_date_pattern\(pattern, sizeof\(pattern\), clock_tile::DATE_FORMAT_D_MONTH, language\);/);
assert.match(html, /clock_tile::format_date_pattern\(pattern, sizeof\(pattern\), clock_tile::DATE_FORMAT_D_MONTH_YEAR, language\);/);
assert.doesNotMatch(html, /21, 7, 2026/, 'No fixed example date in the list');
// One list for the clock tile and the screensaver clock (user 2026-10-14:
// the screensaver only offered DD.MM.YYYY, MM/DD/YYYY and YYYY/MM/DD).
assert.match(html, /_clock_date_format">\)html";\s*append_clock_date_format_options\(html, language\);/);
const adminHtml = read('src/web/server/render/web_admin_html.cpp');
assert.match(adminHtml, /<select id="screensaverDateFormat">\)html";[\s\S]{0,120}append_clock_date_format_options\(html, configManager\.getConfig\(\)\.language\);/);
assert.doesNotMatch(adminHtml, /<option value="3">YYYY\/MM\/DD<\/option><\/select>/, 'No own short list for the screensaver');
assert.match(read('src/types/clock/web_scripts.cpp'), /const CLOCK_I18N = Object\.freeze\(/);
// The editor refreshes on the new fields.
const editor = read('src/web/admin/tiles/editor.js');
assert.ok(editor.includes("target.id === (prefix + '_clock_weekday')") && editor.includes("target.id === (prefix + '_clock_roll')"));
// The preview's date digits are fixed-width like the panel's.
for (const file of ['src/web/admin/tiles/grid-preview.js', 'src/web/admin/tiles/live-preview.js']) {
  assert.match(read(file), /clockDigitsHtml\(getClockPreviewDate\(/, file);
}
assert.match(admin, /setClockDigits\(dateEl, getClockPreviewDate\(dateFormat, weekday, \(flags & 2\) !== 0\)\);/);
// Alignment (user 2026-10-12): left, centered, right on the panel and in
// the preview; without a title or icon the lines use the whole card height.
assert.match(renderer, /widget_config\.time_alignment = clock_tile::tile_alignment\(date_options\.align\);/);
assert.match(renderer, /if \(!compact && !has_header\) lv_obj_set_style_pad_ver\(card, 0, 0\);/);
assert.match(renderer, /date_options\.align == 1 +\? LV_ALIGN_LEFT_MID/);
assert.ok(html.includes('_clock_align"'), 'alignment select');
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('.tile.clock:not(.clock-has-header):not(.clock-compact) { padding-top:0; padding-bottom:0; }'));
assert.ok(css.includes('.tile.clock:not(.clock-compact).clock-align-left { align-items:flex-start; }'));
assert.ok(css.includes('.tile.clock.clock-compact.clock-align-right { justify-content:flex-end; }'));
assert.ok(editor.includes("target.id === (prefix + '_clock_align')"));
console.log('Clock date options: weekday, formats, rolling digits and alignment, panel and preview alike');
