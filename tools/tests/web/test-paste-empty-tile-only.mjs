// Paste fills an empty tile only, and only where the copied size fits without
// covering other tiles. Reported: Paste turned a folder's Back tile into the
// copied Climate tile, and pasting a 2x1 tile into a 1x0.5 gap overlapped its
// neighbours until the overlap fix moved it to column 1 / row 1.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const calls = [];
// Folder grid: Back at 0/0, a 2x1 tile at 2/0, an empty 1x0.5 gap at 1/0 and
// an empty 1x1 slot at 4/1.
const tiles = [
  {type: 8, col: 0, row: 0, span_w: 1, span_h: 1},
  {type: 17, col: 2, row: 0, span_w: 2, span_h: 1},
  {type: 0, col: 1, row: 0, span_w: 1, span_h: 0.5},
  {type: 0, col: 4, row: 1, span_w: 1, span_h: 1},
  {type: 4, col: 0, row: 2, span_w: 1, span_h: 1, folder_empty: false},
  {type: 7, col: 1, row: 2, span_w: 1, span_h: 1}
];
const context = vm.createContext({
  GRID_COLS: 7, GRID_ROWS: 5,
  currentTileIndex: -1, currentTileTab: 'folder_1', tileClipboard: null,
  getTilesData: () => tiles,
  getCurrentTileType: () => String(tiles[context.currentTileIndex].type),
  getTileElementLayout: () => null,
  getTileLayoutFromData: (tab, index) => {
    const {col, row, span_w, span_h} = tiles[index];
    return {col, row, span_w, span_h};
  },
  firstAllowedGridRow: () => 0,
  isCompactSensorType: type => [1, 14, 20].includes(Number(type)),
  isEditableValueType: type => [21, 22, 23].includes(Number(type)),
  document: {getElementById: () => ({value: '0'})},
  t: key => key,
  showNotification: (text, ok) => calls.push(['notify', text, ok]),
  applyTileFormData: () => calls.push(['apply']),
  updateTilePreview: () => calls.push(['preview']),
  updateDraft: () => calls.push(['draft']),
  scheduleAutoSave: () => calls.push(['save'])
});
vm.runInContext(['rectsOverlap', 'canPlaceGridLayout', 'canPlaceTileLayout', 'supportsHalfSize',
  'supportedTileLayout', 'pasteTile'].map(extractDeliveredFunction).join('\n'), context);

const paste = (index, clipboard) => {
  calls.length = 0;
  context.currentTileIndex = index;
  context.tileClipboard = clipboard;
  context.pasteTile('folder_1');
  return calls.map(call => call.slice(0, 2).join(':'));
};
const climate2x1 = {type: '17', span_w: '2', span_h: '1'};
const sensorHalf = {type: '1', span_w: '1', span_h: '0.5'};

for (const index of [0, 1, 4, 5]) {
  assert.deepEqual(paste(index, climate2x1), ['notify:pasteEmptyOnly'],
    `tile ${index} (type ${tiles[index].type}) is not replaced`);
}
assert.deepEqual(paste(2, climate2x1), ['notify:pasteNoSpace'],
  'a 2x1 tile does not go into the 1x0.5 gap between Back and the 2x1 tile');
assert.deepEqual(paste(2, {type: '10', span_w: '1', span_h: '0.5'}), ['notify:pasteNoSpace'],
  'Text has no half-height size');
assert.deepEqual(paste(2, sensorHalf), ['apply', 'preview', 'draft', 'save', 'notify:tilePasted']);
assert.deepEqual(paste(3, climate2x1), ['apply', 'preview', 'draft', 'save', 'notify:tilePasted'],
  'a 2x1 tile fits into the free space at 4/1');

const i18n = readRepoFile('src/core/i18n/i18n.cpp');
for (const text of ['Nur auf eine leere Kachel einfügen', 'Hier ist kein Platz für diese Kachel',
  'Paste only onto an empty tile', 'Not enough space for this tile here',
  'Coller uniquement sur une tuile vide', 'Pas assez de place pour cette tuile ici']) {
  assert.ok(i18n.includes(`"${text}"`), text);
}
const scripts = readRepoFile('src/web/server/render/web_admin_scripts.cpp');
assert.match(scripts, /appendJsEntry\("pasteEmptyOnly", tr\.js_paste_empty_only\);/);
assert.match(scripts, /appendJsEntry\("pasteNoSpace", tr\.js_paste_no_space\);/);
console.log('PASS paste fills only empty tiles where the copied size fits');
