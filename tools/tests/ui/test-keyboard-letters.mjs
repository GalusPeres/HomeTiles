// The panel keyboard takes its letters from the language's data
// (LocaleProfile keyboard_layout and keyboard_letters, made into the accent
// page and the forms of a held letter by keyboard_letters.h), so a language
// pack needs no keyboard code (user 2026-10-18: Czech and Slovak type on
// QWERTZ with č ř š ž ě ů ...). For every registered language: its own
// letters lead the accent page and the held forms, every key and capital is
// in the panel fonts, and nothing the former keyboard offered is lost.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const i18n = readRepoFile('src/core/i18n/i18n.cpp').replace(/\r\n?/g, '\n');

// keyboard_layout and keyboard_letters close every LocaleProfile table, only
// the Energy period labels follow them.
const codes = [...i18n.matchAll(/\{&kStrings(\w+), &kLocale\w+\}/g)].map(match => match[1]);
assert.ok(codes.length >= 7, 'every built-in language is registered');
const languages = codes.map(code => {
  const start = i18n.indexOf(`static const LocaleProfile kLocale${code} = {`);
  assert.ok(start >= 0, code);
  const body = i18n.slice(start, i18n.indexOf('static const ', start + 10));
  const tail = /\s(\d+),(?:\s*\/\/[^\n]*)?\s*("(?:[^"\\]|\\.)*")\s*,?\s*(?:\{[^{}]*\}\s*)?\};/.exec(body);
  assert.ok(tail, `${code}: keyboard_layout and keyboard_letters close the table`);
  return {code, layout: Number(tail[1]), literal: tail[2], letters: JSON.parse(tail[2])};
});
const of = code => languages.find(language => language.code === code);

// The panel text fonts' ranges with their Cyrillic fallbacks (the keys
// draw in popup_layout::font28(), 28 to 48 px by layout).
function fontRanges(name) {
  const source = readRepoFile(`src/fonts/${name}.c`);
  const opts = /Opts:.*$/m.exec(source)[0];
  const ranges = [...opts.matchAll(/(?:-r|--range) (\S+)/g)].flatMap(match => match[1].split(',')).map(part => {
    const [first, last] = part.split('-').map(value => parseInt(value, 16));
    return [first, last ?? first];
  });
  const fallback = /\.fallback = &(\w+),/.exec(source);
  return fallback ? [...ranges, ...fontRanges(fallback[1])] : ranges;
}
const fontList = [16, 20, 24, 28, 32, 40, 48].map(size => fontRanges(`ui_font_${size}`));
const inFont = cp => fontList.every(ranges => ranges.some(([first, last]) => cp >= first && cp <= last));

// The Russian JCUKEN rows of the keyboard (settings_keyboard.cpp).
const keyboardSource = readRepoFile('src/ui/tabs/settings/settings_keyboard.cpp');
const jcuken = [1, 2, 3].map(row => {
  const match = new RegExp(`kJcuken${row}\\[\\] = \\{([^}]*)\\}`).exec(keyboardSource);
  assert.ok(match, `kJcuken${row}`);
  return [...match[1].matchAll(/"([^"]*)"/g)].map(m => m[1]);
});

const harness = `#include <cstdio>
#include "src/ui/tabs/settings/keyboard_letters.h"
using namespace keyboard_letters;
static void list(const char* tag, const char* code, const uint32_t* cps, uint8_t count) {
  std::printf("%s %s", tag, code);
  for (uint8_t i = 0; i < count; ++i) std::printf(" %X", static_cast<unsigned>(cps[i]));
  std::printf("\\n");
}
static void language(const char* code, const char* own) {
  uint32_t page[kPageCapacity];
  const uint8_t count = accent_page(own, page, kPageCapacity);
  list("page", code, page, count);
  uint32_t capitals[kPageCapacity];
  for (uint8_t i = 0; i < count; ++i) {
    capitals[i] = upper(page[i]);
    char text[8];
    const size_t bytes = encode(page[i], text, sizeof(text));
    const char* p = text;
    if (!bytes || next(p) != page[i] || *p) std::printf("roundtrip %s %X\\n", code, static_cast<unsigned>(page[i]));
  }
  list("upper", code, capitals, count);
  uint8_t rows[3];
  row_counts(count, rows);
  std::printf("rows %s %u %u %u\\n", code, rows[0], rows[1], rows[2]);
  for (char letter = 'a'; letter <= 'z'; ++letter) {
    uint32_t forms[kVariantCapacity];
    const uint8_t found = variants(own, letter, forms, kVariantCapacity);
    char tag[8];
    std::snprintf(tag, sizeof(tag), "hold%c", letter);
    list(tag, code, forms, found);
  }
}
int main() {
${languages.map(language => `  language("${language.code}", ${language.literal});`).join('\n')}
  // Held Cyrillic е and ь, and the JCUKEN letters' capitals and back.
${languages.map(language => `  {
    uint32_t forms[kVariantCapacity];
    uint8_t found = variants(${language.literal}, 0x435, forms, kVariantCapacity);
    list("holdcyr435", "${language.code}", forms, found);
    found = variants(${language.literal}, 0x44C, forms, kVariantCapacity);
    list("holdcyr44C", "${language.code}", forms, found);
  }`).join('\n')}
  {
    const char* letters = ${JSON.stringify(jcuken.flat().join('') + 'ёъ')};
    uint32_t capitals[40];
    uint8_t count = 0;
    for (const char* p = letters; *p && count < 40;) {
      const uint32_t cp = next(p);
      capitals[count++] = upper(cp);
      if (lower(upper(cp)) != cp) std::printf("caseback %X\\n", static_cast<unsigned>(cp));
    }
    list("jcukenupper", "Ru", capitals, count);
  }
  // Every shared letter has its plain letter.
  for (const char* p = kCommon; *p;) {
    const uint32_t cp = next(p);
    if (!plain(cp)) std::printf("noplain %X\\n", static_cast<unsigned>(cp));
  }
  for (const char* p = kFurther; *p;) {
    const uint32_t cp = next(p);
    if (!plain(cp)) std::printf("noplain %X\\n", static_cast<unsigned>(cp));
  }
  return 0;
}
`;
const output = compileAndRun({label: 'keyboard letters', harness});
if (output !== null) {
  const lines = output.trim().split('\n');
  assert.deepEqual(lines.filter(line => /^(roundtrip|noplain|caseback) /.test(line)), [], 'UTF-8, plain letters, case');
  const read = (tag, code) => {
    const line = lines.find(l => l === `${tag} ${code}` || l.startsWith(`${tag} ${code} `));
    assert.ok(line, `${tag} ${code}`);
    return line.split(' ').slice(2).map(hex => String.fromCodePoint(parseInt(hex, 16)));
  };
  for (const {code, layout, letters} of languages) {
    assert.ok(layout >= 0 && layout <= 3, `${code}: QWERTY, QWERTZ, AZERTY or JCUKEN`);
    const own = [...letters];
    for (const ch of own) {
      const cp = ch.codePointAt(0);
      assert.ok(cp >= 0x80 && inFont(cp), `${code}: ${ch} is not ASCII and in the panel fonts`);
      assert.equal(ch, ch.toLowerCase(), `${code}: ${ch} in lower case`);
    }
    assert.equal(new Set(own).size, own.length, `${code}: no letter twice`);
    const page = read('page', code);
    assert.ok(page.length <= 27, `${code}: the accent page fits its rows`);
    assert.deepEqual(page.slice(0, own.length), own, `${code}: its own letters lead the accent page`);
    const capitals = read('upper', code);
    capitals.forEach((ch, i) => {
      assert.ok(inFont(ch.codePointAt(0)), `${code}: the capital of ${page[i]} is in the panel fonts`);
      // Unicode's own capital where it is one character (ß stays ß).
      const unicode = page[i].toUpperCase();
      assert.equal(ch, [...unicode].length === 1 ? unicode : page[i], `${code}: capital of ${page[i]}`);
    });
    // A held letter offers the language's own forms first.
    for (const letter of 'abcdefghijklmnopqrstuvwxyz') {
      const forms = read(`hold${letter}`, code);
      assert.ok(forms.length <= 7, `${code}: at most seven forms of ${letter}`);
      const mine = own.filter(ch => ch.normalize('NFD')[0] === letter);
      assert.deepEqual(forms.slice(0, mine.length), mine, `${code}: own forms of ${letter} first`);
    }
    // Nothing the former keyboard could type is lost, in any language.
    const held = new Set([...'abcdefghijklmnopqrstuvwxyz'].flatMap(letter => read(`hold${letter}`, code)));
    for (const ch of 'äöüßąćęłńóśźżàâæçéèêëîïôœùûÿ') {
      assert.ok(held.has(ch) || page.includes(ch), `${code}: ${ch} can still be typed`);
    }
  }
  // The languages' own keyboards (user 2026-10-18).
  assert.equal(of('Cs').layout, 1, 'Czech types on QWERTZ');
  assert.equal(of('Sk').layout, 1, 'Slovak types on QWERTZ');
  assert.equal(of('De').layout, 1, 'German types on QWERTZ');
  assert.equal(of('Fr').layout, 2, 'French types on AZERTY');
  for (const code of ['En', 'Pl', 'Es']) assert.equal(of(code).layout, 0, `${code} types on QWERTY`);
  // Russian types on JCUKEN (2026-10-29, PR #124): eleven, eleven and nine
  // keys like the common Russian phone keyboards, ё and ъ held on е and ь,
  // so all 33 letters can be typed, and every capital is in the fonts.
  assert.equal(of('Ru').layout, 3, 'Russian types on JCUKEN');
  assert.deepEqual(jcuken.map(row => row.length), [11, 11, 9], 'JCUKEN rows');
  assert.deepEqual(read('holdcyr435', 'Ru').slice(0, 1), ['ё'], 'ё on a held е');
  assert.deepEqual(read('holdcyr44C', 'Ru').slice(0, 1), ['ъ'], 'ъ on a held ь');
  const alphabet = [...'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'];
  const typed = new Set([...jcuken.flat(), ...read('holdcyr435', 'Ru'), ...read('holdcyr44C', 'Ru')]);
  assert.deepEqual(alphabet.filter(ch => !typed.has(ch)), [], 'every Russian letter can be typed');
  const capitals = read('jcukenupper', 'Ru');
  assert.equal(capitals.join(''), (jcuken.flat().join('') + 'ёъ').toUpperCase(), 'JCUKEN capitals');
  for (const ch of [...alphabet, ...capitals]) assert.ok(inFont(ch.codePointAt(0)), `${ch} is in the panel fonts`);
  // The other languages keep their Latin letters on a held е or ь.
  for (const {code} of languages.filter(language => language.code !== 'Ru')) {
    assert.deepEqual(read('holdcyr435', code), [], `${code}: nothing on a held е`);
  }
  assert.deepEqual(read('holdc', 'Cs').slice(0, 1), ['č']);
  assert.deepEqual(read('holdn', 'Es').slice(0, 1), ['ñ']);
  assert.ok(read('page', 'Es').includes('¿') && read('page', 'Es').includes('¡'), 'Spanish ¿ and ¡ on the accent page');
  // The former French page stays as it was (its own letters, then ä ö ß ñ á í ó).
  assert.equal(read('page', 'Fr').join(''), 'àâæéèêëîïôçœùûüÿ«»’€äößñáíó');
  // The page keeps the letter rows' shape: every letter placed, no row over
  // its keys, the third row full first (2026-10-18: 10/10/6 left a gap
  // beside Backspace).
  const rows = code => lines.find(line => line.startsWith(`rows ${code} `)).split(' ').slice(2).map(Number);
  for (const {code} of languages) {
    const [one, two, three] = rows(code);
    assert.equal(one + two + three, read('page', code).length, `${code}: every letter has a key`);
    assert.ok(one <= 10 && two <= 10 && three <= 7, `${code}: the rows fit their keys`);
  }
  assert.deepEqual(rows('En'), [10, 9, 7], 'the former European rows');
  assert.deepEqual(rows('De'), [10, 9, 7]);
  assert.deepEqual(rows('Fr'), [10, 10, 7], 'the former French rows');
  // English and German keep the former European page.
  assert.equal(read('page', 'En').join(''), 'äöüßéèêàâçëîïôûùñœæáíóúÿøå');
  assert.equal(read('page', 'De').join(''), read('page', 'En').join(''));
  console.log(`Keyboard letters: ${languages.length} languages, own letters first, all in the panel fonts`);
}
