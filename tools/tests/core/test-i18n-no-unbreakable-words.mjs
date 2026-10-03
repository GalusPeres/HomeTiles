// A translation must never hold a word too long to wrap. The French Web Admin
// tile hint still had the old type list "(capteur/météo/scène/.../texte)",
// one 80-character word without a space that ran out of its box (V2,
// 2026-10-03); the other languages had dropped the list long ago.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const i18n = readRepoFile('src/core/i18n/i18n.cpp').replace(/\r\n?/g, '\n');
const table = head => {
  const start = i18n.indexOf(head);
  assert.ok(start >= 0, head);
  return i18n.slice(start, i18n.indexOf('\nstatic const ', start + head.length));
};
// The longest real words are compounds such as "Home-Assistant-Bridge" (21).
const kMaxWord = 30;
for (const code of ['De', 'En', 'Fr', 'Pl']) {
  const source = table(`static const Strings kStrings${code} = {`) +
    table(`static const LocaleProfile kLocale${code} = {`);
  const literals = [...source.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  assert.ok(literals.length > 300, `${code}: strings found`);
  const long = literals
    .flatMap(text => text.split(/\s+|\\n|<br>/))
    .filter(word => !/^https?:/.test(word) && [...word].length > kMaxWord);
  assert.deepEqual(long, [], `${code}: words longer than ${kMaxWord} characters cannot wrap`);
}
const french = table('static const Strings kStringsFr = {');
assert.ok(french.includes('Choisis le type et ajuste ses réglages.'), 'French hint matches the other languages');

console.log('Translations: every word is short enough to wrap');
