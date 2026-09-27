// Paste must not replace the fixed Settings and Back tiles. Reported: with a
// folder's Back tile selected, Paste turned it into the copied Climate tile at
// column 1 / row 1.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const calls = [];
const context = vm.createContext({
  currentTileIndex: 0, currentTileTab: 'folder_1', tileClipboard: {type: '17'},
  getCurrentTileType: () => context.selectedType,
  getTileTypeMeta: type => ({7: {locked: true}, 8: {locked: true}}[type] || {}),
  t: key => key,
  showNotification: (text, ok) => calls.push(['notify', text, ok]),
  applyTileFormData: () => calls.push(['apply']),
  updateTilePreview: () => calls.push(['preview']),
  updateDraft: () => calls.push(['draft']),
  scheduleAutoSave: () => calls.push(['save'])
});
vm.runInContext(['isLockedTileType', 'pasteTile'].map(extractDeliveredFunction).join('\n'), context);

for (const fixed of ['7', '8']) {
  calls.length = 0;
  context.selectedType = fixed;
  context.pasteTile('folder_1');
  assert.deepEqual(calls, [['notify', 'tileCannotReplace', false]], `type ${fixed} stays`);
}
calls.length = 0;
context.selectedType = '1';
context.pasteTile('folder_1');
assert.deepEqual(calls.map(call => call[0]), ['apply', 'preview', 'draft', 'save', 'notify']);

const i18n = readRepoFile('src/core/i18n/i18n.cpp');
for (const text of ['Diese Kachel kann nicht ersetzt werden', 'This tile cannot be replaced',
  'Cette tuile ne peut pas être remplacée']) {
  assert.ok(i18n.includes(`"${text}"`), text);
}
assert.match(readRepoFile('src/web/server/render/web_admin_scripts.cpp'),
  /appendJsEntry\("tileCannotReplace", tr\.js_tile_cannot_replace\);/);
console.log('PASS paste keeps the fixed Settings and Back tiles');
