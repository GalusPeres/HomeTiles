// A dragged tile stays exactly under the pointer where it was taken (user
// 2026-10-02: centering the drag image on the grabbed half cell moved it by up
// to a quarter tile). Grid tiles and the parked Settings tile alike.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const source = extractDeliveredFunction('getDragGrabOffset');
const offset = (rect, x, y) => vm.runInNewContext(`${source}; getDragGrabOffset(rect, x, y)`, {rect, x, y});
const rect = {left: 100, top: 50, width: 120, height: 90};
assert.deepEqual({...offset(rect, 130, 70)}, {x: 30, y: 20}, 'the point the pointer took');
assert.deepEqual({...offset(rect, 90, 40)}, {x: 0, y: 0}, 'clamped to the tile');
assert.deepEqual({...offset(rect, 400, 400)}, {x: 119, y: 89}, 'clamped to the tile');

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const drag = fs.readFileSync(path.join(root, 'src/web/admin/tiles/drag-resize.js'), 'utf8').replace(/\r\n?/g, '\n');
const fn = name => {
  const at = drag.indexOf(`  function ${name}(`);
  assert.ok(at >= 0, name);
  return drag.slice(at, drag.indexOf('\n  }\n', at));
};
const grid = fn('enableTileDrag');
assert.ok(grid.includes('getDragGrabOffset(tile.getBoundingClientRect(), e.clientX, e.clientY)'));
assert.ok(grid.includes('setDragImage(dragPreview, grabOffset.x, grabOffset.y)'));
const parked = fn('enableSettingsHiddenSlot');
assert.ok(parked.includes('getDragGrabOffset(rect, event.clientX, event.clientY)') &&
  parked.includes('setDragImage(dragPreview, grabOffset.x, grabOffset.y)'));
// The parked tile anchors the drop on the half it was taken by, like a grid tile.
assert.ok(parked.includes('const grabCellCol = spanW > 0.5 && grabOffset.x >= rect.width / 2 ? 0.5 : 0;'));
assert.ok(!grid.includes('getDragAnchorOffset') && !parked.includes('offsetWidth / 2'));
// Both drags keep the grab point for the drop spot below.
assert.ok(grid.includes('dropOffset: getDragLayoutOffset(tab, layout, e.clientX, e.clientY) || grabOffset,') &&
  parked.includes('dropOffset: grabOffset,'));

// The drop spot is the half cell nearest to the drag image's top-left corner
// (user 2026-10-02: snapping the pointer's half cell put it up to a half cell
// off the drag image). Grid: pad 6, cells 120 x 100, gap 10 -> half cells of
// 65 x 55 px; 6 x 4 cells.
const cellSource = extractDeliveredFunction('getGridCellFromPointer');
const cellAt = (clientX, clientY, dropOffset, layout = {span_w: 1, span_h: 1}) => vm.runInNewContext(
  `${cellSource}; getGridCellFromPointer('folder0', clientX, clientY)`, {
    clientX, clientY, GRID_COLS: 6, GRID_ROWS: 4, placeCols: () => 6, placeRows: () => 4,
    dragSource: {tab: 'folder0', dropOffset, layout},
    getTileGridMetrics: () => ({rect: {left: 100, top: 50}, padLeft: 6, padTop: 6,
      cellW: 120, cellH: 100, gapX: 10, gapY: 10}),
    getDragSourceLayout: () => layout,
    firstAllowedGridRow: () => 0,
    getRawGridCellFromPointer: () => { throw new Error('pointer cell used'); },
    clampHalf: (v) => v
  });
// The image's corner at (100 + 6 + 140, 50 + 6 + 30): 140/65 = 2.15 -> col 1,
// 30/55 = 0.55 -> row 0.5; the pointer far inside the tile does not matter.
assert.deepEqual({...cellAt(246 + 100, 86 + 80, {x: 100, y: 80})}, {col: 1, row: 0.5});
assert.deepEqual({...cellAt(246 + 5, 86 + 5, {x: 5, y: 5})}, {col: 1, row: 0.5});
// Past the edges the spot stays inside the grid with the whole tile.
assert.deepEqual({...cellAt(90, 40, {x: 60, y: 60})}, {col: 0, row: 0});
assert.deepEqual({...cellAt(2000, 2000, {x: 0, y: 0}, {span_w: 2, span_h: 1.5})}, {col: 4, row: 2.5});

console.log('Drag: the drag image stays under the grab point, the drop spot under the image');
