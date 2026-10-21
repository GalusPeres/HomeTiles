// Every language may only use characters the panel fonts carry (Latin-1,
// Latin Extended-A, general punctuation, the euro sign; ui_font_*.c), or the
// ones English uses at the same place (Web-only texts such as "Cold → Warm").
// The Czech time zones came with the minus sign U+2212 (2026-10-18), which
// the panel would have drawn as nothing; a language pack must pass the same.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

// Comments become spaces (strings stay), so quotes in comments do not count.
function stripComments(source) {
  let out = '';
  for (let i = 0; i < source.length;) {
    const c = source[i];
    if (c === '"' || c === "'") {
      let j = i + 1;
      while (j < source.length && source[j] !== c) j += source[j] === '\\' ? 2 : 1;
      out += source.slice(i, j + 1);
      i = j + 1;
    } else if (source.startsWith('//', i)) {
      const end = source.indexOf('\n', i);
      out += ' '.repeat((end < 0 ? source.length : end) - i);
      i = end < 0 ? source.length : end;
    } else if (source.startsWith('/*', i)) {
      const end = source.indexOf('*/', i) + 2;
      out += source.slice(i, end).replace(/[^\n]/g, ' ');
      i = end;
    } else {
      out += c;
      i++;
    }
  }
  return out;
}

// A C string literal's text: \xHH bytes and \uXXXX as UTF-8, simple escapes.
function decode(literal) {
  const bytes = [];
  const utf8 = text => bytes.push(...Buffer.from(text, 'utf8'));
  for (let i = 0; i < literal.length; i++) {
    const c = literal[i];
    if (c !== '\\') {
      utf8(String.fromCodePoint(literal.codePointAt(i)));
      if (literal.codePointAt(i) > 0xFFFF) i++;
      continue;
    }
    const next = literal[++i];
    if (next === 'x') {
      bytes.push(parseInt(literal.slice(i + 1, i + 3), 16));
      i += 2;
    } else if (next === 'u') {
      utf8(String.fromCodePoint(parseInt(literal.slice(i + 1, i + 5), 16)));
      i += 4;
    } else {
      utf8({n: '\n', t: '\t', r: '\r'}[next] ?? next);
    }
  }
  return Buffer.from(bytes).toString('utf8');
}

const source = stripComments(readRepoFile('src/core/i18n/i18n.cpp').replace(/\r\n?/g, '\n'));
const literals = (type, name) => {
  const start = source.indexOf(`static const ${type} ${name} = {`);
  assert.ok(start >= 0, name);
  // To the brace that closes the initializer (it may end on a text's line).
  let end = source.indexOf('{', start);
  for (let depth = 0; ; end++) {
    if (source[end] === '"') {
      for (end++; source[end] !== '"'; end++) if (source[end] === '\\') end++;
    } else if (source[end] === '{') {
      depth++;
    } else if (source[end] === '}' && --depth === 0) {
      break;
    }
  }
  const body = source.slice(start, end);
  return [...body.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(match => decode(match[1]));
};

const opts = /Opts:.*$/m.exec(readRepoFile('src/fonts/ui_font_20.c'))[0];
const ranges = [...opts.matchAll(/-r (0x[0-9A-Fa-f]+)(?:-(0x[0-9A-Fa-f]+))?/g)]
  .map(match => [parseInt(match[1], 16), parseInt(match[2] || match[1], 16)]);
assert.ok(ranges.some(([first, last]) => first <= 0x0100 && last >= 0x017F), 'the fonts carry Latin Extended-A');
const inFont = cp => cp === 0x0A || ranges.some(([first, last]) => cp >= first && cp <= last);

const codes = [...source.matchAll(/\{&kStrings(\w+), &kLocale\w+\}/g)].map(match => match[1]);
assert.ok(codes.includes('En') && codes.length >= 7, 'every built-in language is registered');
const problems = [];
for (const type of ['Strings', 'LocaleProfile']) {
  const prefix = type === 'Strings' ? 'kStrings' : 'kLocale';
  const english = literals(type, `${prefix}En`);
  for (const code of codes.filter(code => code !== 'En')) {
    const texts = literals(type, `${prefix}${code}`);
    assert.equal(texts.length, english.length, `${code} ${type}: as many texts as English`);
    texts.forEach((text, index) => {
      for (const ch of text) {
        if (!inFont(ch.codePointAt(0)) && !english[index].includes(ch)) {
          problems.push(`${code} ${type} text ${index}: U+${ch.codePointAt(0).toString(16).toUpperCase()} in ${JSON.stringify(text)}`);
        }
      }
    });
  }
}
assert.deepEqual(problems, [], 'characters the panel fonts lack');
console.log(`Panel glyphs: ${codes.length} languages use only characters of the panel fonts`);
