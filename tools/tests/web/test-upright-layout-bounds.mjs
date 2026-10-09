// An upright layout has more rows than the stored grid (1280 x 800 panels:
// 4 x 6.5 against 7 x 5). The editor bounded loaded tiles, drafts and form
// fields by the stored grid: the 8-inch export (2026-10-09) showed its tiles
// on rows 5 to 6.5 pushed up to 4.5 and halved, and a save could have written
// that back. Every bound is the shown grid now (placeCols/placeRows, the
// server's tilePlaceCols/tilePlaceRows); the classic layout keeps its grid.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const helpers = [
  'clampHalf', 'headBarLayout', 'placeCols', 'placeRows', 'isCompactSensorType', 'isEditableValueType',
  'supportsHalfSize',
  'normalizeLayoutForTileType', 'constrainLayoutToTab', 'normalizeTileLayout', 'normalizeSnapshotLayout',
  'emptyOccupancy', 'markOccupied',
].map(extractDeliveredFunction).join('\n');

function editor({headBar, shownCols, shownRows}) {
  const context = vm.createContext({});
  vm.runInContext(`
    const GRID_COLS = 7, GRID_ROWS = 5, MEDIA_TILE_TYPE = 15, MEDIA_TILE_MIN_SPAN = 2, MEDIA_TILE_MAX_SPAN = 3;
    const HEAD_BAR = ${headBar}, GRID_SHOWN_COLS = ${shownCols}, GRID_SHOWN_ROWS = ${shownRows};
    let currentTileTab = 'folder0';
    function firstAllowedGridRow() { return 0; }
    ${helpers}
    this.api = {normalizeTileLayout, normalizeSnapshotLayout, emptyOccupancy, markOccupied};
  `, context);
  return context.api;
}

// The 8-inch export's upright places (Hochkant 4 x 6.5).
const upright = editor({headBar: true, shownCols: 4, shownRows: 6.5});
const places = [
  {name: 'Schreibtisch', type: 20, col: 0, row: 5, span_w: 1.5, span_h: 0.5},
  {name: 'TV', type: 2, col: 0.5, row: 5.5, span_w: 1, span_h: 1},
  {name: 'FM4', type: 2, col: 1.5, row: 5.5, span_w: 1, span_h: 1},
  {name: 'DeepHouse', type: 2, col: 2.5, row: 5.5, span_w: 1, span_h: 1},
  {name: 'Spotify', type: 15, col: 2, row: 3, span_w: 2, span_h: 2},
];
for (const tile of places) {
  const layout = upright.normalizeTileLayout(tile, 0, 'folder0');
  assert.deepEqual({...layout}, {col: tile.col, row: tile.row, span_w: tile.span_w, span_h: tile.span_h},
    `${tile.name} keeps its upright place`);
  // The draft before a save (1-based like the form fields).
  const draft = upright.normalizeSnapshotLayout(
    {type: tile.type, col: tile.col + 1, row: tile.row + 1, span_w: tile.span_w, span_h: tile.span_h}, 0, 'folder0');
  assert.deepEqual({...draft}, {col: tile.col, row: tile.row, span_w: tile.span_w, span_h: tile.span_h},
    `${tile.name} survives the draft`);
}
// Still bounded by the shown grid.
const below = upright.normalizeTileLayout({type: 2, col: 3.5, row: 6.5, span_w: 1, span_h: 1}, 0, 'folder0');
assert.ok(below.col + below.span_w <= 4 && below.row + below.span_h <= 6.5, 'nothing leaves the upright grid');

// Occupancy covers the upright rows (13 half rows) and keeps the stored width.
const occupied = upright.emptyOccupancy('folder0');
assert.equal(occupied.length, 13);
assert.equal(occupied[0].length, 14);
upright.markOccupied(occupied, {col: 2.5, row: 5.5, span_w: 1, span_h: 1});
assert.equal(occupied[12][6], true, 'the last half row is marked');

// The classic layout keeps the stored grid exactly.
const classic = editor({headBar: false, shownCols: 7, shownRows: 5});
const kept = classic.normalizeTileLayout({type: 2, col: 6, row: 4, span_w: 1, span_h: 1}, 0, 'folder0');
assert.deepEqual({...kept}, {col: 6, row: 4, span_w: 1, span_h: 1});
const clamped = classic.normalizeTileLayout({type: 2, col: 1, row: 5.5, span_w: 1, span_h: 1}, 0, 'folder0');
assert.deepEqual({...clamped}, {col: 1, row: 4.5, span_w: 1, span_h: 0.5},
  'classic rows end at 5 (a half-size type keeps the last half row, as before)');
assert.equal(classic.emptyOccupancy('folder0').length, 10);

console.log('Upright layout bounds: loaded tiles, drafts and occupancy use the shown grid');
