// v0.8.0: French was compiled but only selectable in beta builds while its
// device fit pass ran (2026-10-03). Release builds now offer it too: the
// language table lists it without a build flag.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const i18n = readRepoFile('src/core/i18n/i18n.cpp').replace(/\r\n?/g, '\n');
const table = i18n.match(/\{&kStringsDe, &kLocaleDe\},([\s\S]*?)\};/);
assert.ok(table, 'language table');
assert.match(table[1], /^\s*\{&kStringsFr, &kLocaleFr\},/m);
assert.doesNotMatch(table[1], /#if/, 'no build flag around a language');
assert.doesNotMatch(i18n, /HOMETILES_ENABLE_FRENCH/);
console.log('i18n: French is selectable in release builds');
