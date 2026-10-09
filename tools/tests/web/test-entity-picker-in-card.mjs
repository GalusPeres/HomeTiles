// The entity picker's list is at least 340 px wide. A narrower field in the
// right column of the settings card used to open it from the field's left
// edge, so it stood out of the card on the right (screensaver tab, user
// 2026-10-09). It now opens from the field's right edge whenever it would
// leave the card; the select lists there open the same way (CSS).
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const rect = (left, right, top = 100, bottom = 140) => ({left, right, top, bottom, width: right - left});

function place(fieldRect, cardRect, viewportW = 1730) {
  const pop = {classList: {add() {}, remove() {}}, style: {}, offsetHeight: 300};
  const field = {
    getBoundingClientRect: () => fieldRect,
    closest: selector => (selector === '.tile-settings' && cardRect ? {getBoundingClientRect: () => cardRect} : null),
  };
  const context = vm.createContext({
    Math,
    window: {innerWidth: viewportW, innerHeight: 900},
    entityPickerOpen: {input: {}},
    entityPickerPopover: pop,
    entityPickerControl: () => ({querySelector: () => field}),
  });
  vm.runInContext(extractDeliveredFunction('placeEntityPickerPopover'), context);
  context.placeEntityPickerPopover();
  return {left: parseFloat(pop.style.left), width: parseFloat(pop.style.width)};
}

// The reported case: a 243 px field at the card's right edge.
const card = rect(964, 1447);
let placed = place(rect(1181, 1424), card);
assert.equal(placed.width, 340);
assert.equal(placed.left + placed.width, 1424, 'the list ends at the field\'s right edge');
assert.ok(placed.left >= card.left, 'and starts inside the card');

// A field wide enough or with room to its right keeps opening from its left.
placed = place(rect(990, 1424), card);
assert.deepEqual(placed, {left: 990, width: 434});
placed = place(rect(980, 1100), card);
assert.equal(placed.left, 980);

// Outside a settings card nothing changes but the viewport bound.
placed = place(rect(1500, 1600), null);
assert.equal(placed.left, 1730 - 340 - 8);

const css = readRepoFile('src/web/assets/admin.css');
assert.match(css, /@supports \(appearance: base-select\) \{\s*\.screensaver-fixed-type select::picker\(select\) \{\s*position-area:block-end span-inline-start;\s*position-try-fallbacks:block-start span-inline-start;\s*\}\s*\}/,
  'the screensaver card\'s select lists open from the field\'s right edge');

console.log('Entity picker and select lists stay inside the settings card');
