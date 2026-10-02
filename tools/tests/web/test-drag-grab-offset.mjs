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

console.log('Drag: the drag image stays under the grab point');
