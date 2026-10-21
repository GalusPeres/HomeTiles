// The language tables are positional: one entry missing or misplaced shifts
// every later text of that language. b270 put the Polish "gear in the head"
// 30 entries too late, so Polish showed the swipe note as "Swipe edge",
// "Lewo" as the note, "Prawo" as "Left" and so on up to the camera status
// (found 2026-10-11). Every table has one entry per field, the same printf
// placeholders as English, and known texts at known fields.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Comments become spaces (strings stay), so commas and braces can be counted.
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

const header = stripComments(read('src/core/i18n/i18n.h'));
const body = header.slice(header.indexOf('struct Strings {'));
const struct = body.slice(body.indexOf('{') + 1, body.indexOf('\n};'));
const fields = [...struct.matchAll(/^\s*(?:const char\*|[\w:<>]+(?:\s*\*)?)\s+(\w+)(?:\[[^\]]*\])*;/gm)].map(m => m[1]);

const source = stripComments(read('src/core/i18n/i18n.cpp'));
function entries(table) {
  const start = source.indexOf(`static const Strings ${table} = {`);
  assert.ok(start >= 0, table);
  let i = source.indexOf('{', start) + 1;
  let depth = 1;
  let item = '';
  const out = [];
  while (depth > 0) {
    const c = source[i];
    if (c === '"') {
      let j = i + 1;
      while (source[j] !== '"') j += source[j] === '\\' ? 2 : 1;
      item += source.slice(i, j + 1);
      i = j + 1;
      continue;
    }
    if (c === '{') depth++;
    if (c === '}') depth--;
    if ((c === ',' && depth === 1) || depth === 0) {
      if (item.trim()) out.push(item.trim());
      item = '';
    } else {
      item += c;
    }
    i++;
  }
  return out;
}
// Every language table, also the ones added later (cs, sk, es, ...).
const tables = Object.fromEntries([...source.matchAll(/static const Strings kStrings(\w+) = \{/g)]
  .map(([, name]) => [name.toLowerCase(), entries(`kStrings${name}`)]));
assert.ok(['de', 'en', 'fr', 'pl'].every(language => tables[language]), 'the first four languages exist');
for (const [language, table] of Object.entries(tables)) {
  assert.equal(table.length, fields.length, `${language}: one entry per field`);
}

// The same printf placeholders as English, field by field.
const specs = text => (text.match(/%[-+ #0]*\d*(?:\.\d+)?(?:l|ll|h|z)?[sdufxXc]/g) || []).join(' ');
fields.forEach((field, index) => {
  for (const language of Object.keys(tables).filter(language => language !== 'en')) {
    assert.equal(specs(tables[language][index]), specs(tables.en[index]), `${language}.${field} placeholders`);
  }
});

// Known texts at known fields, around the former Polish shift and at the end.
const expected = {
  settings_head_gear: ['"Zahnrad oben im Kopf"', '"Gear in the head"', null, '"Zębatka w nagłówku"'],
  head_bar_heading: ['"Kopfleiste"', '"Head bar"', `"Barre d'en-tête"`, '"Pasek nagłówka"'],
  head_bar_gear: ['"Zahnrad für Settings"', '"Gear for Settings"', '"Engrenage pour les réglages"', '"Zębatka do ustawień"'],
  head_bar_clock: ['"Uhr"', '"Clock"', '"Horloge"', '"Zegar"'],
  head_bar_overview: ['"Seitenübersicht"', '"Page overview"', '"Aperçu des pages"', '"Przegląd stron"'],
  head_bar_home_title: ['"Home-Titel"', '"Home title"', `"Titre de l'accueil"`, '"Tytuł strony głównej"'],
  head_bar_home_auto_hint: ['"Name in der Sprache des Panels"', `"Name in the panel's language"`, '"Nom dans la langue du panneau"',
    '"Nazwa w języku panelu"'],
  edge_left: ['"Links"', '"Left"', null, '"Lewo"'],
  edge_bottom: ['"Unten"', '"Bottom"', null, '"Dół"'],
  factory_pin_label: [null, '"Factory PIN"', null, '"PIN fabryczny"'],
  local_camera_status_error: [null, '"Camera error"', null, '"Błąd kamery"'],
  local_camera_stream_mode: [null, '"Mode"', null, '"Tryb"'],
  swipe_gestures_heading: ['"Wischen vom Rand"', '"Edge swipes"', '"Balayages depuis le bord"', '"Przesunięcia od krawędzi"'],
  screensaver_no_sd: ['"Keine microSD-Karte eingesteckt."', '"No microSD card inserted."', '"Aucune carte microSD insérée."', '"Brak karty microSD."'],
  tile_fill: ['"Ausfüllen"', '"Fill"', '"Remplir"', '"Wypełnij"'],
};
for (const [field, values] of Object.entries(expected)) {
  const index = fields.indexOf(field);
  assert.ok(index >= 0, `Strings.${field}`);
  ['de', 'en', 'fr', 'pl'].forEach((language, i) => {
    if (values[i] !== null) assert.equal(tables[language][index], values[i], `${language}.${field}`);
  });
}
console.log(`i18n tables: ${fields.length} fields aligned in ${Object.keys(tables).length} languages (${Object.keys(tables).join(', ')})`);
