// Every time zone of the catalog works everywhere: a stored code survives the
// load, it has a POSIX rule for the clock, and every language names it in the
// catalog's order. An unknown code silently falls back to Berlin, and a list one
// label short shows a null name. Alaska was missing (issue #145).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const i18n = readRepoFile('src/core/i18n/i18n.cpp').replace(/\r\n/g, '\n');
const header = readRepoFile('src/core/i18n/i18n.h').replace(/\r\n/g, '\n');
const config = readRepoFile('src/core/config/config_manager.cpp').replace(/\r\n/g, '\n');
const ui = readRepoFile('src/ui/ui_manager.cpp').replace(/\r\n/g, '\n');

const table = i18n.slice(i18n.indexOf('static const TimezoneOptionInfo kTimezoneOptions'));
const codes = [...table.slice(0, table.indexOf('};')).matchAll(/\{\d, "([a-z_]+)"\}/g)].map(match => match[1]);
assert.ok(codes.includes('anchorage'), 'Alaska is in the catalog');
assert.equal(codes[2], 'berlin', 'the Settings fallback index stays Berlin (tab_settings.cpp)');

const count = Number(header.match(/kTimezoneOptionCount = (\d+);/)?.[1]);
assert.equal(codes.length, count, 'catalog size');
assert.equal(Number(header.match(/timezone_labels\[(\d+)\];/)?.[1]), count, 'label array size');

const known = config.slice(config.indexOf('kKnownTimezones[] = {'));
const specs = ui.slice(ui.indexOf('static const char* timezone_spec_for_code'));
const specBody = specs.slice(0, specs.indexOf('\n}\n'));
for (const code of codes) {
  assert.ok(known.slice(0, known.indexOf('};')).includes(`{"${code}"}`), `${code} survives the configuration's load`);
  assert.ok(specBody.includes(`"${code}") == 0) return`) || code === 'berlin', `${code} has a clock rule`);
}

// Alaska: UTC-9, UTC-8 in summer, on the US dates like the other US zones.
assert.ok(specBody.includes('"anchorage") == 0) return "AKST9AKDT,M3.2.0/02,M11.1.0/02";'), 'the Alaska rule');

// Every language lists the zones in catalog order: each list starts with UTC,
// and the entry after Honolulu is Anchorage, before Los Angeles.
const lists = [...i18n.matchAll(/"UTC\+0 - UTC"[^}]*\}/g)].map(match => [...match[0].matchAll(/"([^"]*)"/g)].map(item => item[1]));
assert.equal(lists.length, 8, 'eight languages');
const at = codes.indexOf('anchorage');
for (const labels of lists) {
  assert.equal(labels.length, count, `${labels[0]}: one name per zone`);
  assert.match(labels[at - 1], /^UTC-10 - /, 'Honolulu before it');
  assert.match(labels[at], /^UTC-9 \/ UTC-8 - (Anchorage|Анкоридж)$/, 'its name with both offsets');
  assert.match(labels[at + 1], /^UTC-8 \/ UTC-7 - /, 'Los Angeles after it');
}

console.log(`Time zone catalog: ${count} zones with a stored code, a clock rule and a name in all ${lists.length} languages`);
